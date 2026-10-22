#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "game.h"
#include "render.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define TARGET_FPS 144
#define MOUSE_SENSITIVITY 0.0022f
#define SIM_DT (1.0f / 240.0f)
#define MAX_FRAME_TIME 0.1f
#define LOOK_SETTLE_FRAMES 2

#define SMOKE_SEED 20240607u
#define SMOKE_FRAME_DT (1.0f / 60.0f)
#define SMOKE_FRAMES 600
#define SMOKE_POINTS 3000
#define SMOKE_SHOTS 3
#define SMOKE_MAX_TURN 0.07f

typedef enum DisplayMode {
    DISPLAY_WINDOWED,
    DISPLAY_FULLSCREEN,
} DisplayMode;

typedef struct SimClock {
    FrameInput pending;
    float accumulator;
} SimClock;

static FrameInput ReadInput(void)
{
    FrameInput in = {0};
    Vector2 mouse = GetMouseDelta();
    in.look = (Vector2){mouse.x * MOUSE_SENSITIVITY, -mouse.y * MOUSE_SENSITIVITY};
    in.move.x = (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
    in.move.y = (float)(IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
    if (Vector2Length(in.move) > 1.0f) in.move = Vector2Normalize(in.move);
    in.fire_held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.fire_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.reload_pressed = IsKeyPressed(KEY_R);
    in.interact_pressed = IsKeyPressed(KEY_E);
    in.swap_pressed = IsKeyPressed(KEY_Q);
    in.sprint_held = IsKeyDown(KEY_LEFT_SHIFT);
    in.restart_pressed = IsKeyPressed(KEY_R);
    return in;
}

/* Frames can outpace the sim step, so one-shot events and look deltas queue until a step consumes them. */
static void MergeInput(FrameInput *pending, const FrameInput *fresh)
{
    pending->move = fresh->move;
    pending->fire_held = fresh->fire_held;
    pending->sprint_held = fresh->sprint_held;
    pending->look = Vector2Add(pending->look, fresh->look);
    pending->fire_pressed = pending->fire_pressed || fresh->fire_pressed;
    pending->reload_pressed = pending->reload_pressed || fresh->reload_pressed;
    pending->interact_pressed = pending->interact_pressed || fresh->interact_pressed;
    pending->swap_pressed = pending->swap_pressed || fresh->swap_pressed;
    pending->restart_pressed = pending->restart_pressed || fresh->restart_pressed;
}

static void ClearOneShots(FrameInput *in)
{
    in->look = Vector2Zero();
    in->fire_pressed = false;
    in->reload_pressed = false;
    in->interact_pressed = false;
    in->swap_pressed = false;
    in->restart_pressed = false;
}

static void AdvanceSim(GameState *g, SimClock *clock, const FrameInput *fresh, float frame_dt)
{
    MergeInput(&clock->pending, fresh);
    clock->accumulator += fminf(frame_dt, MAX_FRAME_TIME);
    while (clock->accumulator >= SIM_DT) {
        GameUpdate(g, &clock->pending, SIM_DT);
        ClearOneShots(&clock->pending);
        clock->accumulator -= SIM_DT;
    }
}

/* GLFW lists the primary monitor first. */
static void SetDisplayMode(DisplayMode mode)
{
    Vector2 monitor = GetMonitorPosition(0);
    int monitor_width = GetMonitorWidth(0);
    int monitor_height = GetMonitorHeight(0);
    switch (mode) {
    case DISPLAY_FULLSCREEN:
        if (IsWindowFullscreen()) return;
        SetWindowPosition((int)monitor.x, (int)monitor.y);
        SetWindowSize(monitor_width, monitor_height);
        ToggleFullscreen();
        return;
    case DISPLAY_WINDOWED:
        if (IsWindowFullscreen()) ToggleFullscreen();
        SetWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
        SetWindowPosition((int)monitor.x + (monitor_width - WINDOW_WIDTH) / 2,
                          (int)monitor.y + (monitor_height - WINDOW_HEIGHT) / 2);
        return;
    }
}

static bool DisplayTogglePressed(void)
{
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    return IsKeyPressed(KEY_F11) || (alt && IsKeyPressed(KEY_ENTER));
}

/* Pause lives here, not in GamePhase: a paused loop just stops stepping the sim. */
static int RunGame(void)
{
    GameState game;
    SimClock clock = {0};
    bool paused = false;
    bool quit = false;
    int discard_look_frames = LOOK_SETTLE_FRAMES;
    GameInit(&game, (uint32_t)time(NULL));
    SetExitKey(KEY_NULL);
    DisableCursor();

    while (!quit && !WindowShouldClose()) {
        if (DisplayTogglePressed()) {
            SetDisplayMode(IsWindowFullscreen() ? DISPLAY_WINDOWED : DISPLAY_FULLSCREEN);
            discard_look_frames = LOOK_SETTLE_FRAMES;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            paused = !paused;
            clock = (SimClock){0};
            if (paused) {
                EnableCursor();
            } else {
                DisableCursor();
                discard_look_frames = LOOK_SETTLE_FRAMES;
            }
        }
        quit = paused && IsKeyPressed(KEY_Q);
        if (!paused) {
            FrameInput in = ReadInput();
            if (discard_look_frames > 0) {
                in.look = Vector2Zero();
                discard_look_frames--;
            }
            AdvanceSim(&game, &clock, &in, GetFrameTime());
        }
        BeginDrawing();
        GameDraw(&game);
        if (paused) DrawPauseOverlay();
        EndDrawing();
    }
    return 0;
}

/* TakeScreenshot scales the read size by the display DPI, but the framebuffer is only as large as the window. */
static void SaveScreenshot(const char *path)
{
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    Image image = {
        .data = rlReadScreenPixels(width, height),
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    ExportImage(image, path);
    UnloadImage(image);
}

typedef struct SmokeBot {
    int frames_at_prompt;
    int shots_taken;
} SmokeBot;

static float WrapAngle(float angle)
{
    return angle - 2.0f * PI * floorf((angle + PI) / (2.0f * PI));
}

static void SmokeAim(const Player *p, Vector3 target, FrameInput *in, bool *on_target)
{
    Vector3 eye = PlayerEye(p);
    Vector3 to = Vector3Subtract(target, eye);
    float yaw_error = WrapAngle(atan2f(to.x, -to.z) - p->yaw);
    float pitch_error = atan2f(to.y, hypotf(to.x, to.z)) - p->pitch;
    in->look.x = Clamp(yaw_error, -SMOKE_MAX_TURN, SMOKE_MAX_TURN);
    in->look.y = Clamp(pitch_error, -SMOKE_MAX_TURN, SMOKE_MAX_TURN);
    *on_target = fabsf(yaw_error) < 0.04f && fabsf(pitch_error) < 0.04f;
}

static const Zombie *SmokeNearestZombie(const GameState *g)
{
    const Zombie *nearest = NULL;
    float best = INFINITY;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        const Zombie *z = &g->zombies[i];
        if (z->state != ZOMBIE_CHASE && z->state != ZOMBIE_ATTACK) continue;
        float distance = hypotf(z->pos.x - g->player.pos.x, z->pos.z - g->player.pos.z);
        if (distance < best) {
            best = distance;
            nearest = z;
        }
    }
    return nearest;
}

static int SmokeWeaponItem(WeaponId id)
{
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (LEVEL_INTERACTABLES[i].kind == INTERACT_WALL_BUY && LEVEL_INTERACTABLES[i].payload == (int)id) return i;
    }
    return -1;
}

static FrameInput SmokeInput(const GameState *g, SmokeBot *bot, int frame)
{
    FrameInput in = {0};
    const Player *p = &g->player;
    float t = (float)frame * SMOKE_FRAME_DT;
    bool on_target = false;

    if (t < 1.0f) {
        in.look = (Vector2){0.006f, 0.004f * sinf(t * 5.0f)};
        return in;
    }

    if (p->slot_count < 2) {
        int item = SmokeWeaponItem(WEAPON_SMG);
        Vector3 panel = BoxCenter(LEVEL_INTERACTABLES[item].box);
        SmokeAim(p, panel, &in, &on_target);
        in.move.y = GameNearestInteractable(g) == item ? 0.0f : 1.0f;
        if (GameNearestInteractable(g) == item && ++bot->frames_at_prompt > 40) in.interact_pressed = true;
        return in;
    }

    const Zombie *target = SmokeNearestZombie(g);
    if (target == NULL) {
        in.look.x = 0.01f;
        return in;
    }
    Vector3 aim = {target->pos.x, 1.0f, target->pos.z};
    SmokeAim(p, aim, &in, &on_target);
    in.fire_held = on_target;
    in.reload_pressed = p->slots[p->active].mag < 4;
    float distance = hypotf(target->pos.x - p->pos.x, target->pos.z - p->pos.z);
    in.move.y = distance > 8.0f ? 1.0f : (distance < 3.5f ? -1.0f : 0.0f);
    in.move.x = 0.5f * sinf(t * 2.0f);
    return in;
}

static bool SmokeWantsShot(const GameState *g, const SmokeBot *bot, int frame)
{
    switch (bot->shots_taken) {
    case 0:
        return frame >= 45;
    case 1:
        return bot->frames_at_prompt >= 30 || frame >= 400;
    case 2:
        return (frame >= 250 && GameAliveZombies(g) >= 2 && g->player.muzzle_timer > 0.0f) || frame >= SMOKE_FRAMES - 2;
    default:
        return false;
    }
}

static int RunSmoke(void)
{
    GameState game;
    SimClock clock = {0};
    SmokeBot bot = {0};
    GameInit(&game, SMOKE_SEED);
    game.player.points = SMOKE_POINTS;
    game.phase_timer = INTERMISSION_TIME - 1.0f;

    for (int frame = 0; frame < SMOKE_FRAMES && !WindowShouldClose(); frame++) {
        FrameInput in = SmokeInput(&game, &bot, frame);
        AdvanceSim(&game, &clock, &in, SMOKE_FRAME_DT);
        BeginDrawing();
        GameDraw(&game);
        if (SmokeWantsShot(&game, &bot, frame)) {
            rlDrawRenderBatchActive();
            SaveScreenshot(TextFormat("smoke_%d.png", ++bot.shots_taken));
        }
        EndDrawing();
    }

    printf("smoke: round=%d phase=%d kills=%d points=%d health=%.0f weapons=%d alive=%d\n", game.round_number, (int)game.phase,
           game.kills, game.player.points, (double)game.player.health, game.player.slot_count, GameAliveZombies(&game));
    bool wrote_all = true;
    for (int i = 1; i <= SMOKE_SHOTS; i++) wrote_all = wrote_all && FileExists(TextFormat("smoke_%d.png", i));
    if (!wrote_all) fprintf(stderr, "smoke: expected %d screenshots, found %d\n", SMOKE_SHOTS, bot.shots_taken);
    return wrote_all ? 0 : 1;
}

int main(int argc, char **argv)
{
    bool smoke = false;
    DisplayMode mode = DISPLAY_WINDOWED;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--smoke") == 0) {
            smoke = true;
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            mode = DISPLAY_FULLSCREEN;
        } else if (strcmp(argv[i], "--windowed") == 0) {
            mode = DISPLAY_WINDOWED;
        } else {
            fprintf(stderr, "usage: %s [--fullscreen | --windowed] [--smoke]\n", argv[0]);
            return 2;
        }
    }
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Zombies");
    SetDisplayMode(smoke ? DISPLAY_WINDOWED : mode);
    SetTargetFPS(TARGET_FPS);
    int status = smoke ? RunSmoke() : RunGame();
    CloseWindow();
    return status;
}
