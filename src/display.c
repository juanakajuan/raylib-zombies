#include "display.h"

DisplayReconciler DisplayReconcilerInit(WindowMode target, PixelSize windowed_size)
{
    DisplayReconciler r = {.windowed_size = windowed_size};
    return DisplaySetTarget(r, target);
}

DisplayReconciler DisplaySetTarget(DisplayReconciler r, WindowMode target)
{
    r.target = target;
    r.wait_frames = 0;
    r.attempts = 0;
    r.place_pending = target == WINDOW_MODE_WINDOWED;
    r.settled = false;
    return r;
}

static bool SameRect(PixelRect a, PixelRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static PixelRect CentredWindow(PixelRect monitor, PixelSize size)
{
    return (PixelRect){monitor.x + (monitor.width - size.width) / 2, monitor.y + (monitor.height - size.height) / 2,
                       size.width, size.height};
}

static DisplayStepResult Send(DisplayReconciler r, DisplayCommandKind kind, PixelRect rect)
{
    r.wait_frames = 1 << r.attempts;
    r.attempts++;
    r.settled = false;
    return (DisplayStepResult){.next = r, .event = DISPLAY_EVENT_SENT, .command = {kind, rect}};
}

static DisplayStepResult Settle(DisplayReconciler r, DisplayEvent event)
{
    DisplayStepResult result = {.next = r, .event = r.settled ? DISPLAY_EVENT_NONE : event};
    result.next.settled = true;
    result.next.place_pending = false;
    result.next.wait_frames = 0;
    if (event == DISPLAY_EVENT_SETTLED) result.next.attempts = 0;
    return result;
}

DisplayStepResult DisplayStep(DisplayReconciler r, WindowFacts f)
{
    bool want_fullscreen = r.target == WINDOW_MODE_FULLSCREEN;
    PixelRect goal = want_fullscreen ? f.primary : CentredWindow(f.primary, r.windowed_size);
    bool flag_matches = f.fullscreen_flag == want_fullscreen;
    bool placed = want_fullscreen ? SameRect(f.window, goal) : !r.place_pending || f.maximized || SameRect(f.window, goal);
    if (flag_matches && placed) return Settle(r, DISPLAY_EVENT_SETTLED);
    if (r.wait_frames > 0) {
        r.wait_frames--;
        return (DisplayStepResult){.next = r};
    }
    if (!flag_matches) return Send(r, want_fullscreen ? DISPLAY_ENTER_FULLSCREEN : DISPLAY_LEAVE_FULLSCREEN, f.primary);
    if (r.attempts >= DISPLAY_MAX_ATTEMPTS) return Settle(r, DISPLAY_EVENT_GAVE_UP);
    return Send(r, want_fullscreen ? DISPLAY_ENTER_FULLSCREEN : DISPLAY_PLACE_WINDOW, goal);
}
