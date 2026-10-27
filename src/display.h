#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>

typedef enum WindowMode {
    WINDOW_MODE_WINDOWED,
    WINDOW_MODE_FULLSCREEN,
} WindowMode;

typedef struct PixelSize {
    int width, height;
} PixelSize;

typedef struct PixelRect {
    int x, y, width, height;
} PixelRect;

/* What the platform reports at the start of a frame. `window` is the framebuffer size and position, which the
 * window manager sets. `fullscreen_flag` is a raylib flag that changes at the ToggleFullscreen call, so it shows
 * intent. `maximized` changes at a raylib call and in the window manager's maximize callback. */
typedef struct WindowFacts {
    bool fullscreen_flag;
    bool maximized;
    PixelRect window;
    PixelRect primary;
} WindowFacts;

typedef enum DisplayCommandKind {
    DISPLAY_ENTER_FULLSCREEN, /* fullscreen on the primary monitor */
    DISPLAY_LEAVE_FULLSCREEN,
    DISPLAY_PLACE_WINDOW, /* resize and move to `rect` */
} DisplayCommandKind;

typedef struct DisplayCommand {
    DisplayCommandKind kind;
    PixelRect rect;
} DisplayCommand;

typedef enum DisplayEvent {
    DISPLAY_EVENT_NONE,
    DISPLAY_EVENT_SENT, /* `command` is valid */
    DISPLAY_EVENT_SETTLED,
    DISPLAY_EVENT_GAVE_UP,
} DisplayEvent;

typedef enum DisplayPhase {
    DISPLAY_CHANGING,
    DISPLAY_SETTLED,
    DISPLAY_GAVE_UP, /* still watches the facts, so a late match settles */
} DisplayPhase;

typedef struct DisplayReconciler {
    WindowMode target;
    PixelSize windowed_size;
    int wait_frames; /* frames left before a mismatch is sent again */
    int attempts;    /* commands sent for the current mismatch */
    bool place_pending; /* a windowed target still owes one centring; a window the user moved later stays put */
    DisplayPhase phase;
} DisplayReconciler;

#define DISPLAY_MAX_ATTEMPTS 6
#define DISPLAY_COMMAND_KIND_COUNT (DISPLAY_PLACE_WINDOW + 1)

typedef struct DisplayStepResult {
    DisplayReconciler next;
    DisplayEvent event;
    DisplayCommand command;
} DisplayStepResult;

DisplayReconciler DisplayReconcilerInit(WindowMode target, PixelSize windowed_size);
DisplayReconciler DisplaySetTarget(DisplayReconciler r, WindowMode target);
/* Compares the target with this frame's facts and returns at most one command that moves the window toward it.
 * The window manager can apply a command frames later or ignore it. Thus the step sends an unsettled mismatch
 * again, with 2, 3, 5, 9 and 17 frames between sends, at most DISPLAY_MAX_ATTEMPTS sends in all. A maximized
 * window keeps the size the window manager gave it. After the step settles, the facts can stop: a window that the
 * user moves or maximizes later stays as it is. The world labels use raylib's screen size, and the HUD uses the
 * framebuffer size. Thus the adapter must make the two sizes equal when the step settles. */
DisplayStepResult DisplayStep(DisplayReconciler r, WindowFacts facts);

#endif
