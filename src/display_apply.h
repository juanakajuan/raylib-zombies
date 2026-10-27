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
        if (!IsWindowFullscreen()) {
            /* ToggleFullscreen asks GLFW for fullscreen at raylib's stored screen size. Without this resize GLFW
             * switches the monitor video mode to 1280x720, and the screen size that the world labels use stays
             * 1280x720 on a 1920x1080 framebuffer. */
            SetWindowSize(c.rect.width, c.rect.height);
            ToggleFullscreen();
        }
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

#endif
