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
 * window manager sets. `fullscreen_flag` and `maximized` are raylib flags: they change at the raylib call, so they
 * show intent, not what the window manager did. */
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

typedef struct DisplayReconciler {
    WindowMode target;
    PixelSize windowed_size;
    int wait_frames; /* frames left before a mismatch is sent again */
    int attempts;    /* commands sent for the current mismatch */
    bool place_pending; /* a windowed target still owes one centring; a window the user moved later stays put */
    bool settled;
} DisplayReconciler;

#define DISPLAY_MAX_ATTEMPTS 6

typedef struct DisplayStepResult {
    DisplayReconciler next;
    DisplayEvent event;
    DisplayCommand command;
} DisplayStepResult;

DisplayReconciler DisplayReconcilerInit(WindowMode target, PixelSize windowed_size);
DisplayReconciler DisplaySetTarget(DisplayReconciler r, WindowMode target);
/* Compares the target with this frame's facts and returns at most one command that moves the window toward it.
 * The window manager can apply a command frames later or ignore it, so an unsettled mismatch is sent again after
 * 1, 2, 4, ... frames, at most DISPLAY_MAX_ATTEMPTS times. A maximized window keeps the size the window manager
 * gave it. The adapter must keep raylib's screen size equal to the framebuffer size once the step settles,
 * because the world labels project with the screen size and the HUD with the framebuffer size. */
DisplayStepResult DisplayStep(DisplayReconciler r, WindowFacts facts);

#endif
