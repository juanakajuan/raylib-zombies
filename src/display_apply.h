#ifndef DISPLAY_APPLY_H
#define DISPLAY_APPLY_H

/* The raylib side of DisplayStep. src/main.c includes this after raylib.h. tests/display_test.c includes it after a
 * fake raylib with the same names, so the test runs this exact code. GLFW lists the primary monitor first. */

#include "display.h"

static WindowFacts DisplayReadFacts(void)
{
    Vector2 position = GetWindowPosition();
    Vector2 primary = GetMonitorPosition(0);
    return (WindowFacts){
        .fullscreen_flag = IsWindowFullscreen(),
        .maximized = IsWindowMaximized(),
        .window = {(int)position.x, (int)position.y, GetRenderWidth(), GetRenderHeight()},
        .primary = {(int)primary.x, (int)primary.y, GetMonitorWidth(0), GetMonitorHeight(0)},
    };
}

static void DisplayApply(DisplayCommand c)
{
    switch (c.kind) {
    case DISPLAY_ENTER_FULLSCREEN:
        /* ToggleFullscreen asks GLFW for fullscreen at raylib's stored screen size. Without this resize, GLFW
         * switches the monitor video mode to 1280x720. Also, raylib does not update its screen size in fullscreen.
         * Thus only this call keeps the screen size of the world labels equal to the framebuffer. */
        SetWindowSize(c.rect.width, c.rect.height);
        if (!IsWindowFullscreen()) ToggleFullscreen();
        SetWindowMonitor(0);
        return;
    case DISPLAY_LEAVE_FULLSCREEN:
        if (IsWindowFullscreen()) ToggleFullscreen();
        return;
    case DISPLAY_PLACE_WINDOW:
        SetWindowSize(c.rect.width, c.rect.height);
        SetWindowPosition(c.rect.x, c.rect.y);
        return;
    }
}

/* Reads the facts only while the reconciler is not settled: each read costs X server round trips. */
static DisplayStepResult DisplayUpdate(DisplayReconciler *r)
{
    if (r->phase == DISPLAY_SETTLED) return (DisplayStepResult){.next = *r};
    DisplayStepResult step = DisplayStep(*r, DisplayReadFacts());
    *r = step.next;
    if (step.event == DISPLAY_EVENT_SENT) DisplayApply(step.command);
    return step;
}

#endif
