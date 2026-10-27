#include "input.h"

#include <stdio.h>

#include "raymath.h"

#define PULSE_SET_PULSE_OR(field, amount) field = true
#define PULSE_SET_PULSE_SUM(field, amount) field += amount
#define PULSE_SET_PULSE_LAST(field, amount) field = amount

void FrameInputPulse(FrameInput *in, Pulse pulse, int amount)
{
    switch (pulse) {
#define PULSE_SET(name, type, merge) \
    case PULSE_##name: \
        PULSE_SET_##merge(in->name, amount); \
        return;
        FRAME_PULSES(PULSE_SET)
#undef PULSE_SET
    case PULSE_COUNT:
        return;
    }
}

#define PULSE_MERGE_PULSE_OR(pending, fresh) pending = pending || fresh
#define PULSE_MERGE_PULSE_SUM(pending, fresh) pending += fresh
#define PULSE_MERGE_PULSE_LAST(pending, fresh) \
    if (fresh != 0) pending = fresh

void MergeInput(FrameInput *pending, const FrameInput *fresh)
{
    pending->move = fresh->move;
    pending->fire_held = fresh->fire_held;
    pending->sprint_held = fresh->sprint_held;
    pending->crouch_held = fresh->crouch_held;
    pending->look = Vector2Add(pending->look, fresh->look);
#define PULSE_MERGE(name, type, merge) PULSE_MERGE_##merge(pending->name, fresh->name);
    FRAME_PULSES(PULSE_MERGE)
#undef PULSE_MERGE
}

void ClearOneShots(FrameInput *in)
{
    in->look = Vector2Zero();
#define PULSE_CLEAR(name, type, merge) in->name = 0;
    FRAME_PULSES(PULSE_CLEAR)
#undef PULSE_CLEAR
}

/* Row order is footer and README order. */
const Binding BINDINGS[] = {
    {.code = KEY_F11, .contexts = CONTEXT_ANY, .kind = ACTION_COMMAND, .command = COMMAND_TOGGLE_DISPLAY,
     .hint = {CONTEXT_ANY, "F11 or Alt+Enter", "fullscreen"}},
    {.code = KEY_ENTER, .alt = true, .contexts = CONTEXT_ANY, .kind = ACTION_COMMAND, .command = COMMAND_TOGGLE_DISPLAY},

    {.code = KEY_ESCAPE, .contexts = CONTEXT_PLAY, .kind = ACTION_COMMAND, .command = COMMAND_OPEN_PAUSE,
     .hint = {CONTEXT_PLAY, "Esc", "pause and free the mouse"}},
    {.code = KEY_F1, .contexts = CONTEXT_PLAY, .debug_only = true, .kind = ACTION_COMMAND, .command = COMMAND_OPEN_DEBUG,
     .hint = {CONTEXT_PLAY, "F1", "debug menu (with `--debug`)"}},
    {.code = KEY_ESCAPE, .contexts = CONTEXT_PAUSE | CONTEXT_DEBUG, .kind = ACTION_COMMAND, .command = COMMAND_CLOSE_OVERLAY,
     .hint = {CONTEXT_PAUSE, "Esc", "resume"}},
    {.code = KEY_F1, .contexts = CONTEXT_DEBUG, .kind = ACTION_COMMAND, .command = COMMAND_CLOSE_OVERLAY,
     .hint = {CONTEXT_DEBUG, "F1 or Esc", "close"}},
    {.code = KEY_Q, .contexts = CONTEXT_PAUSE, .kind = ACTION_COMMAND, .command = COMMAND_QUIT,
     .hint = {CONTEXT_PAUSE, "Q", "quit"}},

    {.code = KEY_UP, .contexts = CONTEXT_PAUSE | CONTEXT_DEBUG, .kind = ACTION_COMMAND, .command = COMMAND_MENU_UP,
     .hint = {CONTEXT_PAUSE | CONTEXT_DEBUG, "Up/Down", "select"}},
    {.code = KEY_DOWN, .contexts = CONTEXT_PAUSE | CONTEXT_DEBUG, .kind = ACTION_COMMAND, .command = COMMAND_MENU_DOWN},
    {.code = KEY_LEFT, .contexts = CONTEXT_PAUSE, .kind = ACTION_COMMAND, .command = COMMAND_MENU_LEFT,
     .hint = {CONTEXT_PAUSE, "Left/Right or mouse", "adjust"}},
    {.code = KEY_RIGHT, .contexts = CONTEXT_PAUSE, .kind = ACTION_COMMAND, .command = COMMAND_MENU_RIGHT},
    {.code = KEY_ENTER, .contexts = CONTEXT_DEBUG, .kind = ACTION_COMMAND, .command = COMMAND_MENU_APPLY,
     .hint = {CONTEXT_DEBUG, "Enter or click", "apply"}},

    {.code = KEY_W, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_FORWARD, .hint = {CONTEXT_PLAY, "W A S D", "move"}},
    {.code = KEY_S, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_BACK},
    {.code = KEY_A, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_LEFT},
    {.code = KEY_D, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_RIGHT},
    {.code = INPUT_MOUSE_LEFT, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_FIRE,
     .hint = {CONTEXT_PLAY, "Left mouse", "fire"}},
    {.code = INPUT_MOUSE_LEFT, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_fire_pressed},
    {.code = KEY_R, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_reload_pressed,
     .hint = {CONTEXT_PLAY, "R", "reload (restart on the game over screen)"}},
    {.code = KEY_R, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_restart_pressed},
    {.code = KEY_E, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_interact_pressed,
     .hint = {CONTEXT_PLAY, "E", "buy or interact"}},
    {.code = KEY_ONE, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_select_slot, .amount = 1,
     .hint = {CONTEXT_PLAY, "1, 2 or mouse wheel", "select weapon"}},
    {.code = KEY_TWO, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_select_slot, .amount = 2},
    {.code = INPUT_WHEEL_DOWN, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_cycle_steps, .amount = 1},
    {.code = INPUT_WHEEL_UP, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_cycle_steps, .amount = -1},
    {.code = KEY_LEFT_SHIFT, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_SPRINT, .hint = {CONTEXT_PLAY, "Shift", "sprint"}},
    {.code = KEY_SPACE, .contexts = CONTEXT_PLAY, .kind = ACTION_PULSE, .pulse = PULSE_jump_pressed, .hint = {CONTEXT_PLAY, "Space", "jump"}},
    {.code = KEY_LEFT_CONTROL, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_CROUCH,
     .hint = {CONTEXT_PLAY, "Ctrl / C", "crouch (hold)"}},
    {.code = KEY_C, .contexts = CONTEXT_PLAY, .kind = ACTION_HOLD, .hold = HOLD_CROUCH},
};
const int BINDING_COUNT = (int)(sizeof BINDINGS / sizeof BINDINGS[0]);

static Context ContextOf(Overlay overlay)
{
    switch (overlay) {
    case OVERLAY_NONE:
        return CONTEXT_PLAY;
    case OVERLAY_PAUSE:
        return CONTEXT_PAUSE;
    case OVERLAY_DEBUG:
        return CONTEXT_DEBUG;
    }
    return CONTEXT_PLAY;
}

static bool Down(const KeySnapshot *keys, int code)
{
    return (keys->code[code] & INPUT_DOWN) != 0;
}

/* Menu navigation also fires on key repeat, so a held arrow keeps moving. */
static bool Fired(const KeySnapshot *keys, const Binding *b)
{
    bool repeats = b->kind == ACTION_COMMAND && b->command >= COMMAND_MENU_UP && b->command <= COMMAND_MENU_RIGHT;
    return (keys->code[b->code] & (repeats ? INPUT_PRESSED | INPUT_REPEAT : INPUT_PRESSED)) != 0;
}

static void RunCommand(Routed *r, Overlay from, Command command)
{
    switch (command) {
    case COMMAND_OPEN_PAUSE:
        if (r->overlay == from) r->overlay = OVERLAY_PAUSE;
        return;
    case COMMAND_OPEN_DEBUG:
        if (r->overlay == from) r->overlay = OVERLAY_DEBUG;
        return;
    case COMMAND_CLOSE_OVERLAY:
        if (r->overlay == from) r->overlay = OVERLAY_NONE;
        return;
    case COMMAND_QUIT:
        r->quit = true;
        return;
    case COMMAND_TOGGLE_DISPLAY:
        r->toggle_display = true;
        return;
    case COMMAND_MENU_UP:
        r->menu.dy--;
        return;
    case COMMAND_MENU_DOWN:
        r->menu.dy++;
        return;
    case COMMAND_MENU_LEFT:
        r->menu.dx--;
        return;
    case COMMAND_MENU_RIGHT:
        r->menu.dx++;
        return;
    case COMMAND_MENU_APPLY:
        r->menu.apply = true;
        return;
    }
}

Routed RouteInput(Overlay overlay, bool debug, const KeySnapshot *keys, float look_scale)
{
    Routed r = {.overlay = overlay};
    bool held[HOLD_COUNT] = {0};
    bool consumed[INPUT_CODE_COUNT] = {0};
    bool alt = Down(keys, KEY_LEFT_ALT) || Down(keys, KEY_RIGHT_ALT);
    unsigned context = ContextOf(overlay);
    for (int chords = 1; chords >= 0; chords--) {
        for (int i = 0; i < BINDING_COUNT; i++) {
            const Binding *b = &BINDINGS[i];
            if (b->alt != (chords == 1) || (b->alt && !alt) || !(b->contexts & context) || (b->debug_only && !debug)) continue;
            switch (b->kind) {
            case ACTION_HOLD:
                held[b->hold] = held[b->hold] || Down(keys, b->code);
                break;
            case ACTION_PULSE:
                if (Fired(keys, b) && !consumed[b->code]) FrameInputPulse(&r.sim, b->pulse, b->amount);
                break;
            case ACTION_COMMAND:
                if (!Fired(keys, b) || consumed[b->code]) break;
                consumed[b->code] = true;
                RunCommand(&r, overlay, b->command);
                break;
            }
        }
    }
    r.overlay_changed = r.overlay != overlay;
    if (r.overlay_changed) return (Routed){.overlay = r.overlay, .overlay_changed = true, .toggle_display = r.toggle_display};
    r.menu_active = overlay != OVERLAY_NONE;
    if (r.menu_active) {
        r.sim = (FrameInput){0};
        return r;
    }
    r.sim.move = (Vector2){(float)(held[HOLD_RIGHT] - held[HOLD_LEFT]), (float)(held[HOLD_FORWARD] - held[HOLD_BACK])};
    if (Vector2Length(r.sim.move) > 1.0f) r.sim.move = Vector2Normalize(r.sim.move);
    r.sim.fire_held = held[HOLD_FIRE];
    r.sim.sprint_held = held[HOLD_SPRINT];
    r.sim.crouch_held = held[HOLD_CROUCH];
    r.sim.look = (Vector2){keys->mouse_delta.x * look_scale, -keys->mouse_delta.y * look_scale};
    return r;
}

int InputFooter(Context context, char *buf, int size)
{
    int length = 0;
    if (size > 0) buf[0] = '\0';
    for (int i = 0; i < BINDING_COUNT; i++) {
        const ControlHint *hint = &BINDINGS[i].hint;
        if (hint->keys == NULL || !(hint->contexts & context)) continue;
        int room = length < size ? size - length : 0;
        length += snprintf(room > 0 ? buf + length : NULL, (size_t)room, "%s%s %s", length > 0 ? ", " : "", hint->keys, hint->text);
    }
    return length;
}

MenuEffect MenuUpdate(MenuState *menu, const MenuRows *rows, MenuIntent intent, const MenuPointer *pointer)
{
    MenuEffect effect = {.kind = MENU_NONE, .row = -1};
    menu->selected = ((menu->selected + intent.dy) % rows->count + rows->count) % rows->count;
    if (pointer->moved) {
        for (int i = 0; i < rows->count; i++) {
            if (CheckCollisionPointRec(pointer->pos, rows->row[i])) menu->selected = i;
        }
    }
    if (!pointer->down) menu->dragging = -1;
    if (pointer->pressed) {
        for (int i = 0; i < rows->count; i++) {
            if (!CheckCollisionPointRec(pointer->pos, rows->row[i])) continue;
            menu->selected = i;
            bool has_bar = rows->bar != NULL && rows->bar[i].width > 0.0f;
            if (has_bar && CheckCollisionPointRec(pointer->pos, rows->bar[i])) menu->dragging = i;
            else if (!has_bar) effect = (MenuEffect){.kind = MENU_ACTIVATE, .row = i};
        }
    }
    if (menu->dragging >= 0) {
        Rectangle bar = rows->bar[menu->dragging];
        menu->selected = menu->dragging;
        return (MenuEffect){.kind = MENU_SET_RATIO, .row = menu->dragging, .ratio = (pointer->pos.x - bar.x) / bar.width};
    }
    if (intent.dx != 0) return (MenuEffect){.kind = MENU_ADJUST, .row = menu->selected, .direction = intent.dx};
    if (intent.apply) return (MenuEffect){.kind = MENU_ACTIVATE, .row = menu->selected};
    return effect;
}
