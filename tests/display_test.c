/* Replays the display-mode bug history against a fake window manager that applies requests late.
 *
 * The fake raylib copies the raylib 5.5 rcore_desktop_glfw.c behavior that matters here:
 *  - ToggleFullscreen flips the fullscreen flag at the call and asks GLFW for fullscreen on the window's current
 *    monitor at the stored screen size. When that size differs from the monitor's mode, GLFW switches the video
 *    mode, and GetMonitorWidth reads the new mode after.
 *  - SetWindowSize writes the screen size (GetScreenWidth) at the call, then asks the window manager.
 *  - The resize callback always updates the render size (GetRenderWidth), but updates the screen size only while
 *    the fullscreen flag is off.
 *  - GetWorldToScreen (the world labels) projects with the screen size.
 * The fake window manager applies each request `delay` frames later and can ignore resize and move requests.
 * The KDE rules come from the fix commit messages, not from a measurement of KDE. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define MAX_REQUESTS 64
#define MONITORS 2
#define ACTION_FRAMES 30
#define MAX_ACTIONS 8

typedef enum RequestKind { REQ_FULLSCREEN, REQ_WINDOWED, REQ_RESIZE, REQ_MOVE, REQ_MAXIMIZE } RequestKind;

typedef struct Request {
    RequestKind kind;
    int due;
    int monitor;
    PixelRect rect;
} Request;

typedef struct WmRules {
    int delay;              /* frames from a request until the window manager applies it */
    int ignore_after_leave; /* resize and move are ignored this many frames after fullscreen ends */
    bool restore_maximized; /* a window maximized before fullscreen comes back maximized */
    int open_monitor;
} WmRules;

static const PixelRect MONITOR_RECTS[MONITORS] = {{0, 0, 1920, 1080}, {1920, 0, 2560, 1440}};
static const int PANEL_HEIGHT = 40;

static struct {
    WmRules rules;
    int frame;
    Request queue[MAX_REQUESTS];
    int queued;
    bool fullscreen;
    int fullscreen_monitor;
    bool maximized;
    bool was_maximized;
    int left_fullscreen_frame;
    PixelRect rect;
    PixelSize modes[MONITORS];
} wm;

static struct {
    bool fullscreen;
    bool maximized;
    PixelSize screen;
    PixelSize render;
    int x, y, prev_x, prev_y;
} rl;

static void Ask(RequestKind kind, int monitor, PixelRect rect)
{
    if (wm.queued == MAX_REQUESTS) abort();
    wm.queue[wm.queued++] = (Request){kind, wm.frame + wm.rules.delay, monitor, rect};
}

static int MonitorAt(int x, int y)
{
    for (int i = 0; i < MONITORS; i++) {
        PixelRect m = MONITOR_RECTS[i];
        if (x >= m.x && x < m.x + m.width && y >= m.y && y < m.y + m.height) return i;
    }
    return 0;
}

typedef struct Vector2 {
    float x, y;
} Vector2;

static int GetCurrentMonitor(void) { return MonitorAt(rl.x + rl.screen.width / 2, rl.y + rl.screen.height / 2); }
static bool IsWindowFullscreen(void) { return rl.fullscreen; }
static bool IsWindowMaximized(void) { return rl.maximized; }
static int GetMonitorWidth(int m) { return wm.modes[m].width; }
static int GetMonitorHeight(int m) { return wm.modes[m].height; }
static int GetRenderWidth(void) { return rl.render.width; }
static int GetRenderHeight(void) { return rl.render.height; }
static Vector2 GetWindowPosition(void) { return (Vector2){(float)rl.x, (float)rl.y}; }
static Vector2 GetMonitorPosition(int m) { return (Vector2){(float)MONITOR_RECTS[m].x, (float)MONITOR_RECTS[m].y}; }

static void ToggleFullscreen(void)
{
    if (!rl.fullscreen) {
        rl.prev_x = rl.x;
        rl.prev_y = rl.y;
        rl.fullscreen = true;
        Ask(REQ_FULLSCREEN, GetCurrentMonitor(), (PixelRect){0, 0, rl.screen.width, rl.screen.height});
    } else {
        rl.fullscreen = false;
        Ask(REQ_WINDOWED, 0, (PixelRect){rl.prev_x, rl.prev_y, rl.screen.width, rl.screen.height});
        rl.x = rl.prev_x;
        rl.y = rl.prev_y;
    }
}

static void SetWindowMonitor(int m)
{
    if (rl.fullscreen) Ask(REQ_FULLSCREEN, m, (PixelRect){0, 0, wm.modes[m].width, wm.modes[m].height});
}

static void SetWindowSize(int w, int h)
{
    rl.screen = (PixelSize){w, h};
    Ask(REQ_RESIZE, 0, (PixelRect){0, 0, w, h});
}

static void SetWindowPosition(int x, int y) { Ask(REQ_MOVE, 0, (PixelRect){x, y, 0, 0}); }

#include "display_apply.h"

/* The glfwPollEvents at the end of each frame. */
static void Callbacks(PixelRect before, bool maximized_before)
{
    if (wm.rect.width != before.width || wm.rect.height != before.height) {
        rl.render = (PixelSize){wm.rect.width, wm.rect.height};
        if (!rl.fullscreen) rl.screen = rl.render;
    }
    if (wm.rect.x != before.x || wm.rect.y != before.y) {
        rl.x = wm.rect.x;
        rl.y = wm.rect.y;
    }
    if (wm.maximized != maximized_before) rl.maximized = wm.maximized;
}

static PixelRect MaximizedRect(void)
{
    PixelRect m = MONITOR_RECTS[MonitorAt(wm.rect.x, wm.rect.y)];
    return (PixelRect){m.x, m.y, m.width, m.height - PANEL_HEIGHT};
}

static void WmApply(const Request *q)
{
    bool ignoring = wm.maximized || wm.fullscreen || wm.frame - wm.left_fullscreen_frame < wm.rules.ignore_after_leave;
    switch (q->kind) {
    case REQ_FULLSCREEN:
        if (!wm.fullscreen) wm.was_maximized = wm.maximized;
        wm.fullscreen = true;
        wm.maximized = false;
        wm.fullscreen_monitor = q->monitor;
        wm.modes[q->monitor] = (PixelSize){q->rect.width, q->rect.height};
        wm.rect = MONITOR_RECTS[q->monitor];
        break;
    case REQ_WINDOWED:
        if (!wm.fullscreen) break;
        wm.fullscreen = false;
        wm.left_fullscreen_frame = wm.frame;
        if (wm.rules.restore_maximized && wm.was_maximized) {
            wm.maximized = true;
            wm.rect = MaximizedRect();
        } else {
            wm.rect = q->rect;
        }
        break;
    case REQ_RESIZE:
        if (!ignoring) wm.rect.width = q->rect.width, wm.rect.height = q->rect.height;
        break;
    case REQ_MOVE:
        if (!ignoring) wm.rect.x = q->rect.x, wm.rect.y = q->rect.y;
        break;
    case REQ_MAXIMIZE:
        if (wm.fullscreen) break;
        wm.maximized = true;
        wm.rect = MaximizedRect();
        break;
    }
}

static void WmPoll(void)
{
    PixelRect before = wm.rect;
    bool maximized_before = wm.maximized;
    int kept = 0;
    for (int i = 0; i < wm.queued; i++) {
        if (wm.queue[i].due <= wm.frame) WmApply(&wm.queue[i]);
        else wm.queue[kept++] = wm.queue[i];
    }
    wm.queued = kept;
    Callbacks(before, maximized_before);
}

static void OpenWindow(WmRules rules)
{
    memset(&wm, 0, sizeof wm);
    memset(&rl, 0, sizeof rl);
    for (int i = 0; i < MONITORS; i++) wm.modes[i] = (PixelSize){MONITOR_RECTS[i].width, MONITOR_RECTS[i].height};
    wm.rules = rules;
    wm.left_fullscreen_frame = -1000;
    PixelRect m = MONITOR_RECTS[rules.open_monitor];
    wm.rect = (PixelRect){m.x + 100, m.y + 100, WINDOW_WIDTH, WINDOW_HEIGHT};
    rl.screen = rl.render = (PixelSize){WINDOW_WIDTH, WINDOW_HEIGHT};
    rl.x = wm.rect.x;
    rl.y = wm.rect.y;
}

/* The SetDisplayMode that src/main.c had at each historical commit. */
typedef struct LegacyVersion {
    const char *sha;
    bool move_then_toggle; /* 34e62c9: position and size, then ToggleFullscreen, no SetWindowMonitor */
    bool presize;          /* d34415e: SetWindowSize(monitor) before ToggleFullscreen */
    int layout_delay;      /* da9ccd2: frames from leaving fullscreen to the windowed layout */
    bool maximized_guard;  /* c56d38a: no layout while maximized */
    bool start_move;       /* c2830b0: main() moves the hidden window to monitor 0 before it shows */
} LegacyVersion;

typedef enum LegacyMode { LEGACY_WINDOWED, LEGACY_FULLSCREEN } LegacyMode;

static const LegacyVersion *legacy;
static int legacy_layout_frames;

static void LegacyWindowedLayout(void)
{
    if (legacy->maximized_guard && IsWindowMaximized()) return;
    Vector2 monitor = GetMonitorPosition(0);
    SetWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    SetWindowPosition((int)monitor.x + (GetMonitorWidth(0) - WINDOW_WIDTH) / 2,
                      (int)monitor.y + (GetMonitorHeight(0) - WINDOW_HEIGHT) / 2);
}

static void LegacySetDisplayMode(LegacyMode mode)
{
    int monitor_width = GetMonitorWidth(0);
    int monitor_height = GetMonitorHeight(0);
    switch (mode) {
    case LEGACY_FULLSCREEN:
        legacy_layout_frames = 0;
        if (IsWindowFullscreen()) return;
        if (legacy->move_then_toggle) {
            SetWindowPosition((int)GetMonitorPosition(0).x, (int)GetMonitorPosition(0).y);
            SetWindowSize(monitor_width, monitor_height);
            ToggleFullscreen();
            return;
        }
        if (legacy->presize) SetWindowSize(monitor_width, monitor_height);
        ToggleFullscreen();
        SetWindowMonitor(0);
        return;
    case LEGACY_WINDOWED:
        if (IsWindowFullscreen()) {
            ToggleFullscreen();
            legacy_layout_frames = legacy->layout_delay;
            if (legacy->layout_delay == 0) LegacyWindowedLayout();
        } else {
            LegacyWindowedLayout();
        }
        return;
    }
}

static void LegacyUpdateLayout(void)
{
    if (legacy_layout_frames > 0 && --legacy_layout_frames == 0) LegacyWindowedLayout();
}

static const LegacyVersion LEGACY_VERSIONS[] = {
    {"34e62c9", true, false, 0, false, false},
    {"511968a", false, false, 0, false, false},
    {"d34415e", false, true, 0, false, false},
    {"da9ccd2", false, true, 3, false, true},
    {"trunk", false, true, 3, true, true},
};
#define LEGACY_COUNT (int)(sizeof LEGACY_VERSIONS / sizeof LEGACY_VERSIONS[0])
#define VERSION_COUNT (LEGACY_COUNT + 1) /* the last column is DisplayStep */
#define TRUNK_COLUMN (LEGACY_COUNT - 1)

typedef enum Action { NONE, TOGGLE, USER_MAXIMIZE } Action;

typedef struct Scenario {
    const char *name;
    WmRules rules;
    WindowMode start;
    Action script[MAX_ACTIONS]; /* one action every ACTION_FRAMES frames */
    bool expect_pass[VERSION_COUNT];
} Scenario;

typedef struct Outcome {
    bool ok;
    int transient_frames; /* frames where the HUD or the world labels disagreed with the framebuffer */
    char why[128];
} Outcome;

static DisplayReconciler reconciler;
static int settle_events;

static void StepFrame(void)
{
    DisplayStepResult step = DisplayStep(reconciler, DisplayReadFacts());
    reconciler = step.next;
    if (step.event == DISPLAY_EVENT_SENT) DisplayApply(step.command);
    if (step.event == DISPLAY_EVENT_SETTLED || step.event == DISPLAY_EVENT_GAVE_UP) settle_events++;
}

static bool SameSize(PixelSize a, PixelSize b) { return a.width == b.width && a.height == b.height; }

static const char *SettledProblem(bool reconciled, WindowMode target)
{
    bool want_fullscreen = target == WINDOW_MODE_FULLSCREEN;
    PixelRect m = MONITOR_RECTS[0];
    PixelRect centred = {m.x + (m.width - WINDOW_WIDTH) / 2, m.y + (m.height - WINDOW_HEIGHT) / 2, WINDOW_WIDTH, WINDOW_HEIGHT};
    PixelSize hud = reconciled ? rl.render : rl.screen;
    if (want_fullscreen && (!wm.fullscreen || wm.fullscreen_monitor != 0)) return "not fullscreen on monitor 0";
    if (!want_fullscreen && wm.fullscreen) return "still fullscreen";
    if (!want_fullscreen && !wm.maximized && memcmp(&wm.rect, &centred, sizeof centred) != 0) return "window not 1280x720 centred";
    if (!SameSize(hud, rl.render)) return "HUD size differs from the framebuffer";
    if (!SameSize(rl.screen, rl.render)) return "screen size differs from the framebuffer (world labels off)";
    if (wm.modes[0].width != m.width) return "monitor video mode changed";
    if (reconciled && !reconciler.settled) return "reconciler not settled";
    if (reconciled && settle_events != 1) return "reconciler did not settle exactly once";
    return NULL;
}

static Outcome Run(const Scenario *s, int column)
{
    bool reconciled = column == LEGACY_COUNT;
    legacy = reconciled ? NULL : &LEGACY_VERSIONS[column];
    legacy_layout_frames = 0;
    settle_events = 0;
    OpenWindow(s->rules);
    WindowMode target = s->start;
    if (reconciled) {
        reconciler = DisplayReconcilerInit(target, (PixelSize){WINDOW_WIDTH, WINDOW_HEIGHT});
    } else {
        if (legacy->start_move) SetWindowPosition((int)GetMonitorPosition(0).x, (int)GetMonitorPosition(0).y);
        LegacySetDisplayMode(target == WINDOW_MODE_FULLSCREEN ? LEGACY_FULLSCREEN : LEGACY_WINDOWED);
    }
    Outcome o = {.ok = true};
    for (wm.frame = 0; wm.frame < ACTION_FRAMES * (MAX_ACTIONS + 1); wm.frame++) {
        WmPoll();
        if (!reconciled) LegacyUpdateLayout();
        int slot = wm.frame / ACTION_FRAMES - 1;
        Action a = wm.frame % ACTION_FRAMES == 0 && slot >= 0 ? s->script[slot] : NONE;
        if (a == TOGGLE) {
            target = target == WINDOW_MODE_FULLSCREEN ? WINDOW_MODE_WINDOWED : WINDOW_MODE_FULLSCREEN;
            if (reconciled) reconciler = DisplaySetTarget(reconciler, target);
            else LegacySetDisplayMode(IsWindowFullscreen() ? LEGACY_WINDOWED : LEGACY_FULLSCREEN);
            settle_events = 0;
        }
        if (a == USER_MAXIMIZE) Ask(REQ_MAXIMIZE, 0, (PixelRect){0});
        if (reconciled) StepFrame();
        PixelSize hud = reconciled ? rl.render : rl.screen;
        if (!SameSize(hud, rl.render) || !SameSize(rl.screen, rl.render)) o.transient_frames++;
        if ((wm.frame + 1) % ACTION_FRAMES != 0 || !o.ok) continue;
        const char *problem = SettledProblem(reconciled, target);
        if (problem != NULL) {
            o.ok = false;
            snprintf(o.why, sizeof o.why, "frame %d: %s; framebuffer %dx%d screen %dx%d", wm.frame, problem, wm.rect.width,
                     wm.rect.height, rl.screen.width, rl.screen.height);
        }
    }
    return o;
}

int main(void)
{
    const WmRules gamescope = {.delay = 1};
    const WmRules kde = {.delay = 1, .ignore_after_leave = 2, .restore_maximized = true};
    const WmRules kde_slow = {.delay = 2, .ignore_after_leave = 5, .restore_maximized = true};
    /* Columns: 34e62c9 511968a d34415e da9ccd2 trunk DisplayStep. Each fix commit is the first that passes its row. */
    const Scenario scenarios[] = {
        {"S1 start fullscreen, window opened on monitor 1 (fix 511968a)", {.delay = 1, .open_monitor = 1},
         WINDOW_MODE_FULLSCREEN, {NONE}, {false, false, true, true, true, true}},
        {"S2 HUD size after entering fullscreen (fix d34415e)", gamescope, WINDOW_MODE_WINDOWED, {TOGGLE},
         {true, false, true, true, true, true}},
        {"S3 F11 out gives 1280x720 centred, WM ignores 2 frames (fix da9ccd2)", kde, WINDOW_MODE_FULLSCREEN, {TOGGLE},
         {false, false, false, true, true, true}},
        {"S4 maximized before fullscreen stays maximized (fix c56d38a)", kde, WINDOW_MODE_WINDOWED,
         {USER_MAXIMIZE, TOGGLE, TOGGLE}, {true, false, true, false, true, true}},
        {"S5 F11 in, F11 out, Alt+Enter in and out, gamescope rules", gamescope, WINDOW_MODE_FULLSCREEN,
         {TOGGLE, TOGGLE, TOGGLE, TOGGLE}, {true, false, true, true, true, true}},
        {"S6 S3 with a slower WM, delay 2, ignores 5 frames", kde_slow, WINDOW_MODE_FULLSCREEN, {TOGGLE, TOGGLE, TOGGLE},
         {false, false, false, false, false, true}},
        {"S7 S4 with a slower WM", kde_slow, WINDOW_MODE_WINDOWED, {USER_MAXIMIZE, TOGGLE, TOGGLE},
         {true, false, true, false, true, true}},
        {"S8 S2 with delay 4", {.delay = 4}, WINDOW_MODE_WINDOWED, {TOGGLE, TOGGLE}, {true, false, true, true, true, true}},
    };
    int failures = 0;
    printf("%-72s", "scenario (cell = settled result / transient frames)");
    for (int v = 0; v < LEGACY_COUNT; v++) printf(" %-9s", LEGACY_VERSIONS[v].sha);
    printf(" %-9s\n", "step");
    for (size_t i = 0; i < sizeof scenarios / sizeof scenarios[0]; i++) {
        const Scenario *s = &scenarios[i];
        Outcome outcomes[VERSION_COUNT];
        printf("%-72s", s->name);
        for (int v = 0; v < VERSION_COUNT; v++) {
            outcomes[v] = Run(s, v);
            char cell[16];
            snprintf(cell, sizeof cell, "%s/%d", outcomes[v].ok ? "pass" : "FAIL", outcomes[v].transient_frames);
            printf(" %-9s", cell);
        }
        printf("\n");
        for (int v = 0; v < VERSION_COUNT; v++) {
            const char *who = v < LEGACY_COUNT ? LEGACY_VERSIONS[v].sha : "step";
            if (!outcomes[v].ok) printf("    %-8s %s\n", who, outcomes[v].why);
            if (outcomes[v].ok != s->expect_pass[v]) {
                failures++;
                fprintf(stderr, "FAIL %s: %s %s, expected %s\n", s->name, who, outcomes[v].ok ? "passes" : "fails",
                        s->expect_pass[v] ? "pass" : "fail");
            }
        }
        const Outcome *step = &outcomes[LEGACY_COUNT], *trunk = &outcomes[TRUNK_COLUMN];
        if (trunk->ok && step->transient_frames > trunk->transient_frames) {
            failures++;
            fprintf(stderr, "FAIL %s: step has %d transient frames, trunk %d\n", s->name, step->transient_frames,
                    trunk->transient_frames);
        }
    }
    return failures == 0 ? 0 : 1;
}
