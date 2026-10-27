/* Copies of the key routing in src/main.c before the binding table, rewritten to read a KeySnapshot. ERA_TRUNK is
   main.c at d4a857c. Each other era undoes one fix, as its commit diff shows. */
#include "legacy_routing.h"

#include "raymath.h"

static LegacyEra g_era;

static bool Down(const KeySnapshot *k, int code)
{
    return (k->code[code] & INPUT_DOWN) != 0;
}

static bool Pressed(const KeySnapshot *k, int code)
{
    return (k->code[code] & INPUT_PRESSED) != 0;
}

static bool MenuKeyPressed(const KeySnapshot *k, int code)
{
    return (k->code[code] & (INPUT_PRESSED | INPUT_REPEAT)) != 0;
}

static FrameInput ReadInput(const KeySnapshot *k, float scale)
{
    FrameInput in = {0};
    in.look = (Vector2){k->mouse_delta.x * scale, -k->mouse_delta.y * scale};
    in.move.x = (float)(Down(k, KEY_D) - Down(k, KEY_A));
    in.move.y = (float)(Down(k, KEY_W) - Down(k, KEY_S));
    if (Vector2Length(in.move) > 1.0f) in.move = Vector2Normalize(in.move);
    in.fire_held = Down(k, INPUT_MOUSE_LEFT);
    in.fire_pressed = Pressed(k, INPUT_MOUSE_LEFT);
    in.reload_pressed = Pressed(k, KEY_R);
    in.interact_pressed = Pressed(k, KEY_E);
    if (g_era == ERA_PRE_15C50EB) {
        in.cycle_steps = Pressed(k, KEY_Q); /* swap_pressed: with two slots a swap is one cycle step */
    } else {
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (Pressed(k, KEY_ONE + i)) in.select_slot = i + 1;
        }
        in.cycle_steps = Pressed(k, INPUT_WHEEL_DOWN) - Pressed(k, INPUT_WHEEL_UP);
    }
    in.sprint_held = Down(k, KEY_LEFT_SHIFT);
    in.crouch_held = Down(k, KEY_LEFT_CONTROL) || Down(k, KEY_C);
    in.jump_pressed = Pressed(k, KEY_SPACE);
    in.restart_pressed = Pressed(k, KEY_R);
    return in;
}

static bool DisplayTogglePressed(const KeySnapshot *k)
{
    bool alt = Down(k, KEY_LEFT_ALT) || Down(k, KEY_RIGHT_ALT);
    return Pressed(k, KEY_F11) || (alt && Pressed(k, KEY_ENTER));
}

static Routed LegacyRoute(Overlay overlay, bool debug, const KeySnapshot *k, float look_scale)
{
    Routed r = {0};
    r.toggle_display = DisplayTogglePressed(k);
    if (g_era == ERA_PRE_E6379C6) {
        /* raylib's default exit key made WindowShouldClose() true on Esc. There were no overlays. */
        r.quit = Pressed(k, KEY_ESCAPE);
        r.sim = ReadInput(k, look_scale);
        return r;
    }
    Overlay previous = overlay;
    if (Pressed(k, KEY_ESCAPE)) overlay = overlay == OVERLAY_NONE ? OVERLAY_PAUSE : OVERLAY_NONE;
    if (debug && Pressed(k, KEY_F1) && overlay != OVERLAY_PAUSE) overlay = overlay == OVERLAY_NONE ? OVERLAY_DEBUG : OVERLAY_NONE;
    r.overlay = overlay;
    r.overlay_changed = overlay != previous;
    r.quit = overlay == OVERLAY_PAUSE && Pressed(k, KEY_Q);
    if (overlay == OVERLAY_PAUSE) {
        r.menu_active = true;
        r.menu.dy = MenuKeyPressed(k, KEY_DOWN) - MenuKeyPressed(k, KEY_UP);
        r.menu.dx = MenuKeyPressed(k, KEY_RIGHT) - MenuKeyPressed(k, KEY_LEFT);
    }
    bool debug_update = g_era == ERA_PRE_26FC5D3 ? overlay == OVERLAY_DEBUG : overlay == OVERLAY_DEBUG && previous == OVERLAY_DEBUG;
    if (debug_update) {
        r.menu_active = true;
        r.menu.dy = MenuKeyPressed(k, KEY_DOWN) - MenuKeyPressed(k, KEY_UP);
        r.menu.apply = g_era == ERA_PRE_26FC5D3 ? Pressed(k, KEY_ENTER) : Pressed(k, KEY_ENTER) && !DisplayTogglePressed(k);
    }
    if (overlay == OVERLAY_NONE) r.sim = ReadInput(k, look_scale);
    return r;
}

RouteFn LegacyRouter(LegacyEra era)
{
    g_era = era;
    return LegacyRoute;
}
