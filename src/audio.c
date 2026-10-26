#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 22050
#define VOICES 4
#define MAX_VARIANTS 3
#define PEAK 0.85f
#define TWO_PI (2.0f * PI)

#define SOUND_ID_ROWS(id, ...) SOUND_FIRE_##id, SOUND_RELOAD_##id,
typedef enum SoundId {
    WEAPON_LIST(SOUND_ID_ROWS)
    SOUND_HIT,
    SOUND_KILL,
    SOUND_HEADSHOT_KILL,
    SOUND_GROWL,
    SOUND_ZOMBIE_ATTACK,
    SOUND_PLAYER_DAMAGE,
    SOUND_ROUND_START,
    SOUND_ROUND_END,
    SOUND_PURCHASE,
    SOUND_DENIED,
    SOUND_COUNT,
} SoundId;

typedef struct Synth {
    uint32_t noise_state;
    float low_pass;
} Synth;

typedef float (*SynthFn)(Synth *s, float t);

/* The files play in turn. When any file is missing the synthesized sound plays instead. */
typedef struct SoundDef {
    float seconds;
    float volume;
    SynthFn synth;
    const char *files[MAX_VARIANTS];
} SoundDef;

typedef struct VoiceBank {
    Sound voices[MAX_VARIANTS][VOICES];
    int variants;
    int next_variant;
    int next_voice[MAX_VARIANTS];
} VoiceBank;

static VoiceBank g_bank[SOUND_COUNT];
static bool g_ready = false;

static float Noise(Synth *s)
{
    s->noise_state = s->noise_state * 1664525u + 1013904223u;
    return (float)(s->noise_state >> 8) / 8388608.0f - 1.0f;
}

/* One-pole low pass over white noise. A smaller cutoff weight gives a duller, heavier burst. */
static float DullNoise(Synth *s, float weight)
{
    s->low_pass += weight * (Noise(s) - s->low_pass);
    return s->low_pass;
}

static float Decay(float t, float rate)
{
    return expf(-rate * t);
}

static float Sine(float freq, float t)
{
    return sinf(TWO_PI * freq * t);
}

static float Saw(float freq, float t)
{
    float phase = freq * t;
    return 2.0f * (phase - floorf(phase)) - 1.0f;
}

static float Square(float freq, float t)
{
    return Saw(freq, t) < 0.0f ? -1.0f : 1.0f;
}

/* A thump whose pitch falls from start_freq toward end_freq. */
static float Thump(float t, float start_freq, float end_freq, float rate)
{
    float freq = end_freq + (start_freq - end_freq) * Decay(t, 30.0f);
    return Sine(freq, t) * Decay(t, rate);
}

static float Click(Synth *s, float t, float at, float rate)
{
    return t < at ? 0.0f : Noise(s) * Decay(t - at, rate);
}

static float FirePistol(Synth *s, float t)
{
    return 0.9f * Noise(s) * Decay(t, 28.0f) + 0.8f * Thump(t, 220.0f, 70.0f, 18.0f);
}

static float FireSmg(Synth *s, float t)
{
    return 0.8f * Noise(s) * Decay(t, 45.0f) + 0.5f * Thump(t, 300.0f, 110.0f, 35.0f);
}

static float FireShotgun(Synth *s, float t)
{
    return 1.0f * DullNoise(s, 0.45f) * Decay(t, 9.0f) + 0.9f * Thump(t, 160.0f, 45.0f, 8.0f);
}

static float Reload(Synth *s, float t)
{
    float slide = t > 0.45f && t < 0.6f ? DullNoise(s, 0.3f) * 0.5f : 0.0f;
    return Click(s, t, 0.0f, 90.0f) + slide + Click(s, t, 0.6f, 70.0f) * 1.2f + Click(s, t, 0.75f, 110.0f);
}

static float Hit(Synth *s, float t)
{
    return 0.8f * Thump(t, 260.0f, 90.0f, 40.0f) + 0.4f * Noise(s) * Decay(t, 90.0f);
}

static float Kill(Synth *s, float t)
{
    return 0.9f * Thump(t, 200.0f, 55.0f, 14.0f) + 0.6f * DullNoise(s, 0.25f) * Decay(t, 14.0f);
}

static float HeadshotKill(Synth *s, float t)
{
    return Kill(s, t) * 0.8f + 0.5f * Sine(1900.0f, t) * Decay(t, 22.0f) + 0.4f * Noise(s) * Decay(t, 60.0f);
}

static float Growl(Synth *s, float t)
{
    float freq = 62.0f + 12.0f * Sine(5.0f, t) + 14.0f * t;
    float rattle = 0.55f + 0.45f * Sine(23.0f, t);
    float envelope = sinf(PI * t / 0.9f);
    return (0.8f * Saw(freq, t) + 0.5f * DullNoise(s, 0.2f)) * rattle * envelope;
}

static float ZombieAttack(Synth *s, float t)
{
    float freq = 110.0f + 220.0f * t;
    float envelope = sinf(PI * t / 0.4f);
    return (0.6f * Saw(freq, t) + 0.7f * DullNoise(s, 0.5f)) * envelope;
}

static float PlayerDamage(Synth *s, float t)
{
    return 0.9f * Thump(t, 140.0f, 50.0f, 9.0f) + 0.5f * DullNoise(s, 0.35f) * Decay(t, 12.0f);
}

static float Arpeggio(float t, const float *freqs, int count, float step)
{
    int note = (int)(t / step);
    if (note >= count) note = count - 1;
    float local = t - (float)note * step;
    float tone = Sine(freqs[note], t) + 0.4f * Sine(freqs[note] * 2.0f, t);
    return tone * Decay(local, 3.5f) * fminf(1.0f, local * 200.0f);
}

static float RoundStart(Synth *s, float t)
{
    static const float NOTES[] = {392.0f, 523.25f, 659.25f, 784.0f};
    (void)s;
    return Arpeggio(t, NOTES, 4, 0.18f) * Decay(t, 1.2f);
}

static float RoundEnd(Synth *s, float t)
{
    static const float NOTES[] = {523.25f, 392.0f, 311.13f, 261.63f};
    (void)s;
    return Arpeggio(t, NOTES, 4, 0.22f) * Decay(t, 1.0f);
}

static float Purchase(Synth *s, float t)
{
    (void)s;
    float second = t > 0.09f ? Sine(2093.0f, t) * Decay(t - 0.09f, 9.0f) : 0.0f;
    return Sine(1396.9f, t) * Decay(t, 40.0f) * fminf(1.0f, t * 400.0f) + second;
}

static float Denied(Synth *s, float t)
{
    (void)s;
    return 0.7f * Square(110.0f, t) * (t < 0.28f ? 1.0f : 0.0f) * fminf(1.0f, t * 200.0f) * Decay(t, 2.0f);
}

#define SOUND_DEF_ROWS(id, name, damage, fire_interval, automatic, mag_size, reserve_max, reload_time, pellets, \
                       spread, wall_cost, view_length, panel, fire_sound, reload_sound) \
    [SOUND_FIRE_##id] = fire_sound, [SOUND_RELOAD_##id] = reload_sound,
static const SoundDef SOUNDS[SOUND_COUNT] = {
    WEAPON_LIST(SOUND_DEF_ROWS)
    [SOUND_HIT] = {0.12f, 0.5f, Hit},
    [SOUND_KILL] = {0.35f, 0.7f, Kill},
    [SOUND_HEADSHOT_KILL] = {0.35f, 0.8f, HeadshotKill},
    [SOUND_GROWL] = {0.9f, 0.5f, Growl, {"growl_1.wav", "growl_2.wav", "growl_3.wav"}},
    [SOUND_ZOMBIE_ATTACK] = {0.4f, 0.7f, ZombieAttack, {"attack_1.wav", "attack_2.wav", "attack_3.wav"}},
    [SOUND_PLAYER_DAMAGE] = {0.45f, 0.9f, PlayerDamage},
    [SOUND_ROUND_START] = {1.2f, 0.7f, RoundStart},
    [SOUND_ROUND_END] = {1.4f, 0.7f, RoundEnd},
    [SOUND_PURCHASE] = {0.5f, 0.6f, Purchase},
    [SOUND_DENIED] = {0.3f, 0.3f, Denied},
};

/* Samples are scaled so the loudest one reaches PEAK, so each synth function needs no gain tuning. */
static Wave Synthesize(const SoundDef *def, uint32_t seed)
{
    int frames = (int)(def->seconds * (float)SAMPLE_RATE);
    float *samples = malloc((size_t)frames * sizeof *samples);
    short *pcm = RL_MALLOC((size_t)frames * sizeof *pcm);
    Synth synth = {.noise_state = seed};
    float loudest = 1e-6f;
    for (int i = 0; i < frames; i++) {
        samples[i] = def->synth(&synth, (float)i / (float)SAMPLE_RATE);
        loudest = fmaxf(loudest, fabsf(samples[i]));
    }
    for (int i = 0; i < frames; i++) pcm[i] = (short)(samples[i] / loudest * PEAK * 32767.0f);
    free(samples);
    return (Wave){.frameCount = (unsigned int)frames, .sampleRate = SAMPLE_RATE, .sampleSize = 16, .channels = 1, .data = pcm};
}

static int LoadFileWaves(const SoundDef *def, Wave *waves)
{
    int count = 0;
    while (count < MAX_VARIANTS && def->files[count] != NULL) {
        waves[count] = LoadWave(TextFormat("%sassets/%s", GetApplicationDirectory(), def->files[count]));
        if (!IsWaveValid(waves[count])) break;
        count++;
    }
    int wanted = 0;
    while (wanted < MAX_VARIANTS && def->files[wanted] != NULL) wanted++;
    if (count == wanted) return count;
    for (int i = 0; i < count; i++) UnloadWave(waves[i]);
    return 0;
}

static void LoadBank(SoundId id)
{
    const SoundDef *def = &SOUNDS[id];
    VoiceBank *bank = &g_bank[id];
    Wave waves[MAX_VARIANTS];
    bank->variants = LoadFileWaves(def, waves);
    if (bank->variants == 0) {
        waves[0] = Synthesize(def, 0x9E3779B9u + (uint32_t)id);
        bank->variants = 1;
    }
    for (int i = 0; i < bank->variants; i++) {
        bank->voices[i][0] = LoadSoundFromWave(waves[i]);
        UnloadWave(waves[i]);
        SetSoundVolume(bank->voices[i][0], def->volume);
        for (int v = 1; v < VOICES; v++) bank->voices[i][v] = LoadSoundAlias(bank->voices[i][0]);
    }
}

bool AudioMuted(int argc, char **argv)
{
    const char *env = getenv("ZOMBIES_MUTE");
    if (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0) return true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mute") == 0) return true;
    }
    return false;
}

void AudioInit(bool muted)
{
    if (muted) {
        TraceLog(LOG_INFO, "AUDIO: muted, audio device not opened");
        return;
    }
    InitAudioDevice();
    g_ready = IsAudioDeviceReady();
    if (!g_ready) return;
    for (int i = 0; i < SOUND_COUNT; i++) LoadBank((SoundId)i);
}

#define FIRE_SOUND_ROW(id, ...) [WEAPON_##id] = SOUND_FIRE_##id,
#define RELOAD_SOUND_ROW(id, ...) [WEAPON_##id] = SOUND_RELOAD_##id,
static const SoundId FIRE_SOUND[WEAPON_COUNT] = {WEAPON_LIST(FIRE_SOUND_ROW)};
static const SoundId RELOAD_SOUND[WEAPON_COUNT] = {WEAPON_LIST(RELOAD_SOUND_ROW)};

static bool SoundFor(const GameEvent *event, SoundId *out)
{
    switch (event->kind) {
    case EVENT_FIRE:
        *out = FIRE_SOUND[event->arg];
        return true;
    case EVENT_RELOAD:
        *out = RELOAD_SOUND[event->arg];
        return true;
    case EVENT_HIT: *out = SOUND_HIT; return true;
    case EVENT_KILL: *out = SOUND_KILL; return true;
    case EVENT_HEADSHOT_KILL: *out = SOUND_HEADSHOT_KILL; return true;
    case EVENT_ZOMBIE_GROWL: *out = SOUND_GROWL; return true;
    case EVENT_ZOMBIE_ATTACK: *out = SOUND_ZOMBIE_ATTACK; return true;
    case EVENT_PLAYER_DAMAGE: *out = SOUND_PLAYER_DAMAGE; return true;
    case EVENT_ROUND_START: *out = SOUND_ROUND_START; return true;
    case EVENT_ROUND_END: *out = SOUND_ROUND_END; return true;
    case EVENT_PURCHASE: *out = SOUND_PURCHASE; return true;
    case EVENT_PURCHASE_DENIED: *out = SOUND_DENIED; return true;
    }
    return false;
}

void AudioPlayEvent(const GameEvent *event)
{
    SoundId id;
    if (!SoundFor(event, &id)) return;
    const char *file = SOUNDS[id].files[0];
    TraceLog(LOG_DEBUG, "AUDIO: t=%.3f sound=%d %s", GetTime(), (int)id, file != NULL ? file : "synth");
    if (!g_ready) return;
    VoiceBank *bank = &g_bank[id];
    int variant = bank->next_variant;
    PlaySound(bank->voices[variant][bank->next_voice[variant]]);
    bank->next_voice[variant] = (bank->next_voice[variant] + 1) % VOICES;
    bank->next_variant = (variant + 1) % bank->variants;
}

void AudioShutdown(void)
{
    if (!g_ready) return;
    for (int i = 0; i < SOUND_COUNT; i++) {
        for (int variant = 0; variant < g_bank[i].variants; variant++) {
            for (int v = 1; v < VOICES; v++) UnloadSoundAlias(g_bank[i].voices[variant][v]);
            UnloadSound(g_bank[i].voices[variant][0]);
        }
    }
    CloseAudioDevice();
    g_ready = false;
}
