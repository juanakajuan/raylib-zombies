#include <stdio.h>
#include <string.h>

#include "input.h"
#include "legacy_routing.h"

#define CONTROLS_BEGIN "<!-- controls:begin -->\n"
#define CONTROLS_END "<!-- controls:end -->\n"

static int g_failures;

static void Expect(bool ok, const char *what)
{
    if (ok) return;
    g_failures++;
    fprintf(stderr, "FAIL %s\n", what);
}

static KeySnapshot Keys(const int *pressed, int pressed_count, const int *down, int down_count)
{
    KeySnapshot k = {0};
    for (int i = 0; i < down_count; i++) k.code[down[i]] |= INPUT_DOWN;
    for (int i = 0; i < pressed_count; i++) k.code[pressed[i]] |= INPUT_DOWN | INPUT_PRESSED;
    return k;
}

#define PRESS(...) Keys((const int[]){__VA_ARGS__}, (int)(sizeof((const int[]){__VA_ARGS__}) / sizeof(int)), NULL, 0)

static bool AnyPulse(const FrameInput *in)
{
#define PULSE_ANY(name, type, merge) \
    if (in->name != 0) return true;
    FRAME_PULSES(PULSE_ANY)
#undef PULSE_ANY
    return false;
}

static bool PulseIsSet(const FrameInput *in, Pulse pulse)
{
    switch (pulse) {
#define PULSE_CASE(name, type, merge) \
    case PULSE_##name: \
        return in->name != 0;
        FRAME_PULSES(PULSE_CASE)
#undef PULSE_CASE
    case PULSE_COUNT:
        return false;
    }
    return false;
}

/* e6379c6: Esc while playing opens the pause menu; it never quits. */
static bool EscPausesInsteadOfQuitting(RouteFn route)
{
    KeySnapshot k = PRESS(KEY_ESCAPE);
    Routed r = route(OVERLAY_NONE, false, &k, 1.0f);
    return !r.quit && r.overlay == OVERLAY_PAUSE;
}

/* 26fc5d3: Enter on the frame F1 opens the debug menu runs no debug command. */
static bool EnterOnTheOpeningFrameRunsNoDebugCommand(RouteFn route)
{
    KeySnapshot k = PRESS(KEY_F1, KEY_ENTER);
    Routed r = route(OVERLAY_NONE, true, &k, 1.0f);
    return r.overlay == OVERLAY_DEBUG && !(r.menu_active && r.menu.apply);
}

/* 26fc5d3: Alt+Enter in the debug menu switches the window mode and runs no debug command. */
static bool AltEnterInTheDebugMenuOnlyTogglesTheDisplay(RouteFn route)
{
    KeySnapshot k = Keys((const int[]){KEY_ENTER}, 1, (const int[]){KEY_LEFT_ALT}, 1);
    Routed r = route(OVERLAY_DEBUG, true, &k, 1.0f);
    return r.toggle_display && !(r.menu_active && r.menu.apply);
}

/* 15c50eb: a key that does something while playing never quits from the pause menu. */
static bool NoGameplayKeyQuitsFromPause(RouteFn route)
{
    for (int code = 1; code < INPUT_CODE_COUNT; code++) {
        KeySnapshot k = PRESS(code);
        Routed play = route(OVERLAY_NONE, false, &k, 1.0f);
        if (!AnyPulse(&play.sim)) continue;
        Routed paused = route(OVERLAY_PAUSE, false, &k, 1.0f);
        if (paused.quit) return false;
    }
    return true;
}

/* Live on trunk: Esc and Q in one frame of play opened the pause menu and quit. */
static bool EscAndQInOneFrameDoNotQuit(RouteFn route)
{
    KeySnapshot k = PRESS(KEY_ESCAPE, KEY_Q);
    Routed r = route(OVERLAY_NONE, false, &k, 1.0f);
    return r.overlay == OVERLAY_PAUSE && !r.quit;
}

/* Live on trunk: the pause menu read the arrow keys on the frame it opened. */
static bool PauseMenuIgnoresKeysOnTheFrameItOpens(RouteFn route)
{
    KeySnapshot k = PRESS(KEY_ESCAPE, KEY_DOWN);
    Routed r = route(OVERLAY_NONE, false, &k, 1.0f);
    return r.overlay == OVERLAY_PAUSE && !(r.menu_active && r.menu.dy != 0);
}

typedef struct Regression {
    const char *name;
    LegacyEra broken_in;
    bool (*holds)(RouteFn route);
} Regression;

static const Regression REGRESSIONS[] = {
    {"e6379c6 Esc pauses instead of quitting", ERA_PRE_E6379C6, EscPausesInsteadOfQuitting},
    {"26fc5d3 Enter on the opening frame runs no command", ERA_PRE_26FC5D3, EnterOnTheOpeningFrameRunsNoDebugCommand},
    {"26fc5d3 Alt+Enter in debug only toggles display", ERA_PRE_26FC5D3, AltEnterInTheDebugMenuOnlyTogglesTheDisplay},
    {"15c50eb no gameplay key quits from pause", ERA_PRE_15C50EB, NoGameplayKeyQuitsFromPause},
    {"Esc+Q in one frame pauses and does not quit", ERA_TRUNK, EscAndQInOneFrameDoNotQuit},
    {"pause menu ignores keys on its opening frame", ERA_TRUNK, PauseMenuIgnoresKeysOnTheFrameItOpens},
};

static const char *const ERA_NAMES[ERA_COUNT] = {"pre-e6379c6", "pre-26fc5d3", "pre-15c50eb", "trunk@d4a857c"};

static void RegressionsFailOnLegacyAndHoldNow(void)
{
    printf("%-52s %-14s %-7s %s\n", "regression", "legacy era", "legacy", "table");
    for (size_t i = 0; i < sizeof REGRESSIONS / sizeof REGRESSIONS[0]; i++) {
        const Regression *t = &REGRESSIONS[i];
        bool legacy = t->holds(LegacyRouter(t->broken_in));
        bool table = t->holds(RouteInput);
        printf("%-52s %-14s %-7s %s\n", t->name, ERA_NAMES[t->broken_in], legacy ? "PASS" : "FAIL", table ? "PASS" : "FAIL");
        Expect(!legacy, "the regression test catches the legacy bug");
        Expect(table, "the binding table holds the fix");
        if (t->broken_in != ERA_TRUNK) Expect(t->holds(LegacyRouter(ERA_TRUNK)), "trunk holds the historical fix");
    }
}

static void EveryPulseHasAKey(void)
{
    for (int p = 0; p < PULSE_COUNT; p++) {
        bool reached = false;
        for (int code = 1; code < INPUT_CODE_COUNT && !reached; code++) {
            KeySnapshot k = PRESS(code);
            Routed r = RouteInput(OVERLAY_NONE, false, &k, 1.0f);
            reached = PulseIsSet(&r.sim, (Pulse)p);
        }
        char what[64];
        snprintf(what, sizeof what, "pulse %d has a key", p);
        Expect(reached, what);
    }
}

static void EverySlotHasAKey(void)
{
    for (int slot = 1; slot <= MAX_SLOTS; slot++) {
        bool reached = false;
        for (int code = 1; code < INPUT_CODE_COUNT && !reached; code++) {
            KeySnapshot k = PRESS(code);
            reached = RouteInput(OVERLAY_NONE, false, &k, 1.0f).sim.select_slot == slot;
        }
        Expect(reached, "every weapon slot has a select key");
    }
}

static void PulsesSurviveMergeAndClear(void)
{
    for (int p = 0; p < PULSE_COUNT; p++) {
        FrameInput fresh = {0};
        FrameInput pending = {0};
        FrameInputPulse(&fresh, (Pulse)p, 1);
        MergeInput(&pending, &(FrameInput){0});
        MergeInput(&pending, &fresh);
        MergeInput(&pending, &(FrameInput){0});
        Expect(PulseIsSet(&pending, (Pulse)p), "MergeInput keeps a pulse until a step clears it");
        ClearOneShots(&pending);
        Expect(!AnyPulse(&pending), "ClearOneShots clears every pulse");
    }
}

static void RoutingRules(void)
{
    KeySnapshot k = PRESS(KEY_F1);
    Expect(RouteInput(OVERLAY_NONE, false, &k, 1.0f).overlay == OVERLAY_NONE, "F1 does nothing without --debug");
    Expect(RouteInput(OVERLAY_DEBUG, true, &k, 1.0f).overlay == OVERLAY_NONE, "F1 closes the debug menu");

    k = PRESS(KEY_ESCAPE, KEY_F1);
    Expect(RouteInput(OVERLAY_NONE, true, &k, 1.0f).overlay == OVERLAY_PAUSE, "Esc and F1 in one frame open only the pause menu");

    k = PRESS(KEY_ESCAPE, KEY_Q);
    Routed r = RouteInput(OVERLAY_PAUSE, false, &k, 1.0f);
    Expect(r.overlay == OVERLAY_NONE && !r.quit, "Esc and Q in one paused frame resume and do not quit");

    k = PRESS(KEY_ESCAPE, KEY_F11);
    r = RouteInput(OVERLAY_NONE, false, &k, 1.0f);
    Expect(r.overlay == OVERLAY_PAUSE && r.toggle_display, "a display toggle survives the frame that opens an overlay");

    k = PRESS(KEY_ESCAPE, KEY_R, KEY_E, KEY_SPACE);
    r = RouteInput(OVERLAY_NONE, false, &k, 1.0f);
    Expect(!AnyPulse(&r.sim), "the frame that opens the pause menu carries no gameplay pulse");

    k = Keys((const int[]){KEY_ENTER}, 1, (const int[]){KEY_RIGHT_ALT, KEY_W}, 2);
    r = RouteInput(OVERLAY_NONE, false, &k, 1.0f);
    Expect(r.toggle_display && r.sim.move.y == 1.0f, "Alt+Enter toggles the display while W still moves");

    k = Keys(NULL, 0, (const int[]){KEY_W, KEY_D, KEY_LEFT_SHIFT, KEY_C}, 4);
    r = RouteInput(OVERLAY_NONE, false, &k, 1.0f);
    Expect(r.sim.move.x > 0.7f && r.sim.move.x < 0.71f && r.sim.move.y > 0.7f, "diagonal move has length 1");
    Expect(r.sim.sprint_held && r.sim.crouch_held, "Shift and C are held");
    r = RouteInput(OVERLAY_PAUSE, false, &k, 1.0f);
    Expect(r.sim.move.y == 0.0f && !r.sim.sprint_held, "held keys do not reach the sim while paused");

    k = PRESS(KEY_R);
    r = RouteInput(OVERLAY_NONE, false, &k, 1.0f);
    Expect(r.sim.reload_pressed && r.sim.restart_pressed, "R reloads and restarts");

    k = PRESS(INPUT_WHEEL_DOWN);
    Expect(RouteInput(OVERLAY_NONE, false, &k, 1.0f).sim.cycle_steps == 1, "wheel down selects the next weapon");
    k = PRESS(INPUT_WHEEL_UP);
    Expect(RouteInput(OVERLAY_NONE, false, &k, 1.0f).sim.cycle_steps == -1, "wheel up selects the previous weapon");

    k = (KeySnapshot){.mouse_delta = {10.0f, 4.0f}};
    r = RouteInput(OVERLAY_NONE, false, &k, 0.5f);
    Expect(r.sim.look.x == 5.0f && r.sim.look.y == -2.0f, "mouse delta scales into look, y inverted");

    k = (KeySnapshot){0};
    k.code[KEY_DOWN] = INPUT_DOWN | INPUT_REPEAT;
    Expect(RouteInput(OVERLAY_PAUSE, false, &k, 1.0f).menu.dy == 1, "a held arrow repeats in a menu");
}

/* A command consumes its key for the frame, so no other binding may share its key, chord flag and context. */
static int KeyConflicts(const Binding *rows, int count, FILE *report)
{
    int conflicts = 0;
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            const Binding *a = &rows[i];
            const Binding *b = &rows[j];
            bool shared = a->code == b->code && a->alt == b->alt && (a->contexts & b->contexts) != 0;
            if (!shared || (a->kind != ACTION_COMMAND && b->kind != ACTION_COMMAND)) continue;
            conflicts++;
            if (report != NULL) fprintf(report, "BINDINGS rows %d and %d share key %d in one context\n", i, j, a->code);
        }
    }
    return conflicts;
}

static void NoTwoBindingsShareAKeyInOneContext(void)
{
    Expect(KeyConflicts(BINDINGS, BINDING_COUNT, stderr) == 0, "no command shares a key with another binding in one context");
    const Binding q_quits_and_swaps[] = {
        {.code = KEY_Q, .contexts = CONTEXT_PAUSE, .kind = ACTION_COMMAND, .command = COMMAND_QUIT},
        {.code = KEY_Q, .contexts = CONTEXT_PLAY | CONTEXT_PAUSE, .kind = ACTION_PULSE, .pulse = PULSE_cycle_steps, .amount = 1},
    };
    Expect(KeyConflicts(q_quits_and_swaps, 2, NULL) == 1, "the conflict check catches Q bound to quit and swap while paused");
}

static void FootersComeFromTheTable(void)
{
    char buf[256];
    InputFooter(CONTEXT_PAUSE, buf, sizeof buf);
    Expect(strcmp(buf, "F11 or Alt+Enter fullscreen, Esc resume, Q quit, Up/Down select, Left/Right or mouse adjust") == 0, buf);
    InputFooter(CONTEXT_DEBUG, buf, sizeof buf);
    Expect(strcmp(buf, "F11 or Alt+Enter fullscreen, F1 or Esc close, Up/Down select, Enter or click apply") == 0, buf);
    char small[8];
    int needed = InputFooter(CONTEXT_DEBUG, small, sizeof small);
    Expect(needed == (int)strlen(buf) && strlen(small) == sizeof small - 1, "InputFooter truncates and returns the full length");
}

static void MenuReducer(void)
{
    const Rectangle rows[3] = {{0, 0, 100, 10}, {0, 10, 100, 10}, {0, 20, 100, 10}};
    const Rectangle bars[3] = {{50, 0, 50, 10}, {50, 10, 50, 10}, {0, 0, 0, 0}};
    MenuRows menu_rows = {3, rows, bars};
    MenuState menu = {.selected = 0, .dragging = -1};
    MenuPointer idle = {.pos = {-1.0f, -1.0f}};

    MenuUpdate(&menu, &menu_rows, (MenuIntent){.dy = -1}, &idle);
    Expect(menu.selected == 2, "Up on the first row wraps to the last");
    MenuEffect e = MenuUpdate(&menu, &menu_rows, (MenuIntent){.dx = 1}, &idle);
    Expect(e.kind == MENU_ADJUST && e.row == 2 && e.direction == 1, "Right adjusts the selected row");

    MenuPointer press = {.pos = {75.0f, 15.0f}, .moved = true, .pressed = true, .down = true};
    e = MenuUpdate(&menu, &menu_rows, (MenuIntent){0}, &press);
    Expect(e.kind == MENU_SET_RATIO && e.row == 1 && e.ratio == 0.5f, "a press on a bar starts a drag");
    MenuPointer drag = {.pos = {200.0f, 25.0f}, .moved = true, .down = true};
    e = MenuUpdate(&menu, &menu_rows, (MenuIntent){0}, &drag);
    Expect(e.kind == MENU_SET_RATIO && e.row == 1 && menu.selected == 1, "a drag keeps its row off the bar");
    e = MenuUpdate(&menu, &menu_rows, (MenuIntent){0}, &(MenuPointer){.pos = {200.0f, 25.0f}});
    Expect(e.kind == MENU_NONE && menu.dragging == -1, "release ends the drag");

    MenuPointer click = {.pos = {10.0f, 25.0f}, .pressed = true, .down = true};
    e = MenuUpdate(&menu, &menu_rows, (MenuIntent){0}, &click);
    Expect(e.kind == MENU_ACTIVATE && e.row == 2, "a click on a row without a bar activates it");

    MenuRows list = {3, rows, NULL};
    e = MenuUpdate(&menu, &list, (MenuIntent){.dy = 1, .apply = true}, &idle);
    Expect(e.kind == MENU_ACTIVATE && e.row == 0, "Enter applies the row the same frame selects");
}

static int ControlsMarkdown(char *buf, int size)
{
    static const struct {
        Context context;
        const char *name;
    } SECTIONS[] = {{CONTEXT_PLAY, "Playing"}, {CONTEXT_PAUSE, "Paused"}, {CONTEXT_DEBUG, "Debug menu"}};
    int n = snprintf(buf, (size_t)size, "%s", CONTROLS_BEGIN);
    for (size_t s = 0; s < sizeof SECTIONS / sizeof SECTIONS[0]; s++) {
        n += snprintf(buf + n, (size_t)(size - n), "\n**%s**\n\n| Key | Action |\n| --- | --- |\n", SECTIONS[s].name);
        for (int i = 0; i < BINDING_COUNT; i++) {
            const ControlHint *hint = &BINDINGS[i].hint;
            if (hint->keys != NULL && (hint->contexts & SECTIONS[s].context)) {
                n += snprintf(buf + n, (size_t)(size - n), "| %s | %s |\n", hint->keys, hint->text);
            }
        }
    }
    n += snprintf(buf + n, (size_t)(size - n), "\n%s", CONTROLS_END);
    return n;
}

static void ReadmeControlsMatchTheBindingTable(void)
{
    static char readme[1 << 16];
    static char expected[1 << 13];
    FILE *f = fopen(SOURCE_DIR "/README.md", "rb");
    Expect(f != NULL, "README.md opens");
    if (f == NULL) return;
    size_t len = fread(readme, 1, sizeof readme - 1, f);
    fclose(f);
    readme[len] = '\0';
    ControlsMarkdown(expected, sizeof expected);
    const char *begin = strstr(readme, CONTROLS_BEGIN);
    const char *end = begin == NULL ? NULL : strstr(begin, CONTROLS_END);
    bool same = end != NULL && (size_t)(end + strlen(CONTROLS_END) - begin) == strlen(expected) &&
                strncmp(begin, expected, strlen(expected)) == 0;
    if (!same) fprintf(stderr, "README.md controls are stale. Run input_test --print-controls and paste its output over the block.\n");
    Expect(same, "README controls match the binding table");
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--print-controls") == 0) {
        static char controls[1 << 13];
        ControlsMarkdown(controls, sizeof controls);
        fputs(controls, stdout);
        return 0;
    }
    RegressionsFailOnLegacyAndHoldNow();
    EveryPulseHasAKey();
    EverySlotHasAKey();
    PulsesSurviveMergeAndClear();
    RoutingRules();
    NoTwoBindingsShareAKeyInOneContext();
    FootersComeFromTheTable();
    MenuReducer();
    ReadmeControlsMatchTheBindingTable();
    if (g_failures > 0) {
        fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    printf("input tests passed\n");
    return 0;
}
