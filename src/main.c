#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "audio.h"
#include "game.h"
#include "input.h"
#include "render.h"
#include "settings.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define TARGET_FPS 144
#define MOUSE_SENSITIVITY 0.0022f
#define SIM_DT (1.0f / 240.0f)
#define MAX_FRAME_TIME 0.1f
#define LOOK_SETTLE_FRAMES 2
#define WINDOWED_LAYOUT_DELAY_FRAMES 3

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

static void AdvanceSim(GameState *g, SimClock *clock, const FrameInput *fresh, float frame_dt)
{
    MergeInput(&clock->pending, fresh);
    clock->accumulator += fminf(frame_dt, MAX_FRAME_TIME);
    while (clock->accumulator >= SIM_DT) {
        GameUpdate(g, &clock->pending, SIM_DT);
        for (int i = 0; i < g->event_count; i++) AudioPlayEvent(&g->events[i]);
        ClearOneShots(&clock->pending);
        clock->accumulator -= SIM_DT;
    }
}

/* GLFW lists the primary monitor first. */
static int windowed_layout_frames;

static void ApplyWindowedLayout(void)
{
    /* The window manager ignores a resize of a maximized window, so it stays maximized. */
    if (IsWindowMaximized()) return;
    Vector2 monitor = GetMonitorPosition(0);
    SetWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    SetWindowPosition((int)monitor.x + (GetMonitorWidth(0) - WINDOW_WIDTH) / 2,
                      (int)monitor.y + (GetMonitorHeight(0) - WINDOW_HEIGHT) / 2);
}

static void SetDisplayMode(DisplayMode mode)
{
    int monitor_width = GetMonitorWidth(0);
    int monitor_height = GetMonitorHeight(0);
    switch (mode) {
    case DISPLAY_FULLSCREEN:
        windowed_layout_frames = 0;
        if (IsWindowFullscreen()) return;
        SetWindowSize(monitor_width, monitor_height);
        ToggleFullscreen();
        SetWindowMonitor(0);
        return;
    case DISPLAY_WINDOWED:
        if (IsWindowFullscreen()) {
            ToggleFullscreen();
            windowed_layout_frames = WINDOWED_LAYOUT_DELAY_FRAMES;
        } else {
            ApplyWindowedLayout();
        }
        return;
    }
}

/* The compositor ignores a resize or move sent in the same frame that leaves fullscreen, so the layout waits. */
static void UpdateWindowedLayout(void)
{
    if (windowed_layout_frames > 0 && --windowed_layout_frames == 0) ApplyWindowedLayout();
}

static KeySnapshot ReadKeys(void)
{
    KeySnapshot keys = {.mouse_delta = GetMouseDelta()};
    for (int code = 1; code < INPUT_KEYBOARD_COUNT; code++) {
        keys.code[code] = (unsigned char)(IsKeyDown(code) * INPUT_DOWN | IsKeyPressed(code) * INPUT_PRESSED |
                                          IsKeyPressedRepeat(code) * INPUT_REPEAT);
    }
    keys.code[INPUT_MOUSE_LEFT] =
        (unsigned char)(IsMouseButtonDown(MOUSE_BUTTON_LEFT) * INPUT_DOWN | IsMouseButtonPressed(MOUSE_BUTTON_LEFT) * INPUT_PRESSED);
    float wheel = GetMouseWheelMove();
    keys.code[INPUT_WHEEL_UP] = (unsigned char)((wheel > 0.0f) * INPUT_PRESSED);
    keys.code[INPUT_WHEEL_DOWN] = (unsigned char)((wheel < 0.0f) * INPUT_PRESSED);
    return keys;
}

static MenuPointer ReadPointer(void)
{
    Vector2 delta = GetMouseDelta();
    return (MenuPointer){
        .pos = UiMousePosition(),
        .moved = delta.x != 0.0f || delta.y != 0.0f,
        .pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
        .down = IsMouseButtonDown(MOUSE_BUTTON_LEFT),
    };
}

/* Returns true when a setting changed. A window mode row effect sets *toggle_display instead. */
static bool ApplyPauseEffect(Settings *settings, MenuEffect effect, bool *toggle_display)
{
    bool display = effect.row == PAUSE_ROW_DISPLAY;
    switch (effect.kind) {
    case MENU_NONE:
        return false;
    case MENU_ACTIVATE:
        *toggle_display = *toggle_display || display;
        return false;
    case MENU_ADJUST:
        if (display) {
            *toggle_display = true;
            return false;
        }
        SettingsAdjust(settings, (SettingId)effect.row, effect.direction);
        return true;
    case MENU_SET_RATIO:
        SettingsSetRatio(settings, (SettingId)effect.row, effect.ratio);
        return true;
    }
    return false;
}

static MenuEffect UpdatePauseMenu(MenuState *menu, MenuIntent intent, const MenuPointer *pointer)
{
    Rectangle rows[PAUSE_ROW_COUNT];
    Rectangle bars[PAUSE_ROW_COUNT];
    for (int i = 0; i < PAUSE_ROW_COUNT; i++) {
        rows[i] = PauseRowBounds(i);
        bars[i] = i == PAUSE_ROW_DISPLAY ? (Rectangle){0} : PauseBarBounds(i);
    }
    return MenuUpdate(menu, &(MenuRows){PAUSE_ROW_COUNT, rows, bars}, intent, pointer);
}

static MenuEffect UpdateDebugMenu(MenuState *menu, MenuIntent intent, const MenuPointer *pointer)
{
    Rectangle rows[DEBUG_COMMAND_COUNT];
    for (int i = 0; i < DEBUG_COMMAND_COUNT; i++) rows[i] = DebugRowBounds(i);
    return MenuUpdate(menu, &(MenuRows){DEBUG_COMMAND_COUNT, rows, NULL}, intent, pointer);
}

/* Pause lives here, not in GamePhase: a paused loop just stops stepping the sim. */
static int RunGame(bool debug)
{
    GameState game;
    SimClock clock = {0};
    Overlay overlay = OVERLAY_NONE;
    bool quit = false;
    int discard_look_frames = LOOK_SETTLE_FRAMES;
    Settings settings;
    MenuState menu = {.selected = 0, .dragging = -1};
    MenuState debug_menu = {.selected = 0, .dragging = -1};
    bool settings_dirty = false;
    SettingsLoad(&settings);
    SetMasterVolume(settings.volume);
    GameInit(&game, (uint32_t)time(NULL));
    SetExitKey(KEY_NULL);
    DisableCursor();

    while (!quit && !WindowShouldClose()) {
        UpdateWindowedLayout();
        KeySnapshot keys = ReadKeys();
        Routed routed = RouteInput(overlay, debug, &keys, MOUSE_SENSITIVITY * settings.sensitivity);
        bool toggle_display = routed.toggle_display;
        quit = routed.quit;
        if (routed.overlay_changed) {
            overlay = routed.overlay;
            clock = (SimClock){0};
            if (overlay != OVERLAY_NONE) {
                EnableCursor();
            } else {
                if (settings_dirty) SettingsSave(&settings);
                settings_dirty = false;
                DisableCursor();
                discard_look_frames = LOOK_SETTLE_FRAMES;
            }
        }
        MenuPointer pointer = ReadPointer();
        if (routed.menu_active && overlay == OVERLAY_PAUSE &&
            ApplyPauseEffect(&settings, UpdatePauseMenu(&menu, routed.menu, &pointer), &toggle_display)) {
            settings_dirty = true;
            SetMasterVolume(settings.volume);
        }
        if (routed.menu_active && overlay == OVERLAY_DEBUG) {
            MenuEffect effect = UpdateDebugMenu(&debug_menu, routed.menu, &pointer);
            if (effect.kind == MENU_ACTIVATE) GameDebugApply(&game, &DEBUG_COMMANDS[effect.row]);
        }
        if (toggle_display) {
            SetDisplayMode(IsWindowFullscreen() ? DISPLAY_WINDOWED : DISPLAY_FULLSCREEN);
            discard_look_frames = LOOK_SETTLE_FRAMES;
        }
        if (overlay == OVERLAY_NONE) {
            FrameInput in = routed.sim;
            if (discard_look_frames > 0) {
                in.look = Vector2Zero();
                discard_look_frames--;
            }
            AdvanceSim(&game, &clock, &in, GetFrameTime());
        }
        BeginDrawing();
        if (overlay == OVERLAY_NONE) GameDraw(&game, settings.fov);
        else GameDrawBlurred(&game, settings.fov);
        if (overlay == OVERLAY_PAUSE) DrawPauseOverlay(&settings, menu.selected, IsWindowFullscreen());
        if (overlay == OVERLAY_DEBUG) DrawDebugOverlay(&game, debug_menu.selected);
        EndDrawing();
    }
    if (settings_dirty) SettingsSave(&settings);
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
        if (LEVEL_INTERACTABLES[i].kind == INTERACT_WALL_BUY && LEVEL_INTERACTABLES[i].weapon == id) return i;
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
    Settings defaults = SettingsDefault();
    GameInit(&game, SMOKE_SEED);
    game.player.points = SMOKE_POINTS;
    game.phase_timer = INTERMISSION_TIME - 1.0f;

    for (int frame = 0; frame < SMOKE_FRAMES && !WindowShouldClose(); frame++) {
        FrameInput in = SmokeInput(&game, &bot, frame);
        AdvanceSim(&game, &clock, &in, SMOKE_FRAME_DT);
        BeginDrawing();
        GameDraw(&game, defaults.fov);
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
    bool debug = false;
    bool muted = AudioMuted(argc, argv);
    DisplayMode mode = DISPLAY_FULLSCREEN;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--smoke") == 0) {
            smoke = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } else if (strcmp(argv[i], "--mute") == 0) {
            continue;
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            mode = DISPLAY_FULLSCREEN;
        } else if (strcmp(argv[i], "--windowed") == 0) {
            mode = DISPLAY_WINDOWED;
        } else {
            fprintf(stderr, "usage: %s [--fullscreen | --windowed] [--smoke] [--debug] [--mute]\n", argv[0]);
            return 2;
        }
    }
    if (debug) SetTraceLogLevel(LOG_DEBUG);
    /* The window starts hidden so the compositor places it, and picks its workspace, on the primary monitor. */
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Zombies");
    SetWindowPosition((int)GetMonitorPosition(0).x, (int)GetMonitorPosition(0).y);
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetDisplayMode(smoke ? DISPLAY_WINDOWED : mode);
    SetTargetFPS(TARGET_FPS);
    AudioInit(muted);
    int status = smoke ? RunSmoke() : RunGame(debug);
    AudioShutdown();
    CloseWindow();
    return status;
}
