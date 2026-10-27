#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#include "game.h"

/* Codes past raylib's last keyboard key stand for mouse inputs, so one array holds every input the game reads. */
#define INPUT_KEYBOARD_COUNT 350
#define INPUT_MOUSE_LEFT 400
#define INPUT_WHEEL_UP 401
#define INPUT_WHEEL_DOWN 402
#define INPUT_CODE_COUNT 403

enum { INPUT_DOWN = 1, INPUT_PRESSED = 2, INPUT_REPEAT = 4 };

/* Every input for one render frame. main.c fills it from raylib, tests build it by hand. */
typedef struct KeySnapshot {
    unsigned char code[INPUT_CODE_COUNT]; /* INPUT_DOWN | INPUT_PRESSED | INPUT_REPEAT */
    Vector2 mouse_delta;
} KeySnapshot;

typedef enum PulseMerge {
    PULSE_OR,   /* any press sets it */
    PULSE_SUM,  /* presses add their amount */
    PULSE_LAST, /* the last press stores its amount */
} PulseMerge;

#define FRAME_PULSE_ID(name, type, merge) PULSE_##name,
typedef enum Pulse { FRAME_PULSES(FRAME_PULSE_ID) PULSE_COUNT } Pulse;
#undef FRAME_PULSE_ID

void FrameInputPulse(FrameInput *in, Pulse pulse, int amount);
/* Folds one render frame's input into the input that waits for the next sim step. */
void MergeInput(FrameInput *pending, const FrameInput *fresh);
void ClearOneShots(FrameInput *in);

typedef enum Overlay {
    OVERLAY_NONE,
    OVERLAY_PAUSE,
    OVERLAY_DEBUG,
} Overlay;

/* Bit masks: a binding is live in each context whose bit it sets. */
typedef enum Context {
    CONTEXT_PLAY = 1,
    CONTEXT_PAUSE = 2,
    CONTEXT_DEBUG = 4,
    CONTEXT_ANY = 7,
} Context;

typedef enum Hold {
    HOLD_FORWARD,
    HOLD_BACK,
    HOLD_LEFT,
    HOLD_RIGHT,
    HOLD_FIRE,
    HOLD_SPRINT,
    HOLD_CROUCH,
    HOLD_COUNT,
} Hold;

typedef enum Command {
    COMMAND_OPEN_PAUSE,
    COMMAND_OPEN_DEBUG,
    COMMAND_CLOSE_OVERLAY,
    COMMAND_QUIT,
    COMMAND_TOGGLE_DISPLAY,
    COMMAND_MENU_UP,
    COMMAND_MENU_DOWN,
    COMMAND_MENU_LEFT,
    COMMAND_MENU_RIGHT,
    COMMAND_MENU_APPLY,
} Command;

typedef enum ActionKind {
    ACTION_HOLD,    /* true while the key is down */
    ACTION_PULSE,   /* one FrameInput pulse per press */
    ACTION_COMMAND, /* one loop or menu command per press; consumes the key for this frame */
} ActionKind;

typedef struct ControlHint {
    unsigned contexts; /* footers and README sections that list this row */
    const char *keys;  /* NULL: the row has no hint */
    const char *text;
} ControlHint;

typedef struct Binding {
    int code;
    bool alt; /* chord: Alt must be down. Chords route before single keys. */
    unsigned contexts;
    bool debug_only;
    ActionKind kind;
    union {
        Hold hold;
        Pulse pulse;
        Command command;
    };
    int amount; /* PULSE_SUM and PULSE_LAST value */
    ControlHint hint;
} Binding;

extern const Binding BINDINGS[];
extern const int BINDING_COUNT;

typedef struct MenuIntent {
    int dy; /* +1 moves the selection down */
    int dx; /* +1 adjusts right */
    bool apply;
} MenuIntent;

typedef struct Routed {
    Overlay overlay; /* the overlay after this frame */
    bool overlay_changed;
    bool quit;
    bool toggle_display;
    bool menu_active; /* the menu of `overlay` reads `menu` this frame; false on the frame it opens */
    MenuIntent menu;
    FrameInput sim; /* zero unless overlay is OVERLAY_NONE */
} Routed;

/* Keys route by the overlay open at the start of the frame. Chords route before single keys. A frame that opens or
   closes an overlay carries nothing else except a display toggle. */
Routed RouteInput(Overlay overlay, bool debug, const KeySnapshot *keys, float look_scale);

/* Writes "keys text, keys text" for the hints of one context. Returns the full length, like snprintf. */
int InputFooter(Context context, char *buf, int size);

/* Pointer state for a list menu, in UI units. */
typedef struct MenuPointer {
    Vector2 pos;
    bool moved;
    bool pressed;
    bool down;
} MenuPointer;

typedef struct MenuRows {
    int count;
    const Rectangle *row;
    const Rectangle *bar; /* NULL, or one bar per row; a zero-width bar means the row has none */
} MenuRows;

typedef struct MenuState {
    int selected;
    int dragging; /* row whose bar the held button grabbed, or -1 */
} MenuState;

typedef enum MenuEffectKind {
    MENU_NONE,
    MENU_ADJUST,
    MENU_SET_RATIO,
    MENU_ACTIVATE,
} MenuEffectKind;

typedef struct MenuEffect {
    MenuEffectKind kind;
    int row;
    int direction; /* MENU_ADJUST */
    float ratio;   /* MENU_SET_RATIO */
} MenuEffect;

/* A drag keeps its row until the button is released. A click on a row without a bar activates it. */
MenuEffect MenuUpdate(MenuState *menu, const MenuRows *rows, MenuIntent intent, const MenuPointer *pointer);

#endif
