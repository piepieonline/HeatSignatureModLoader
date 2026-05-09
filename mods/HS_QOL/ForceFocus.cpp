#include "ForceFocus.h"
#include "Log.h"
#include <windows.h>

static HS_GetGameWindowFn g_getGameWindow = nullptr;
static int                g_frameCount    = 0;
static bool               g_done          = false;

static constexpr int kMaxRetryFrames = 600;

// SetForegroundWindow is restricted on modern Windows: it silently returns
// FALSE (and just flashes the taskbar) when the caller lacks foreground
// rights. The well-known workaround is to attach the target window's input
// queue to the current foreground thread's queue for the duration of the
// call — that side-steps the restriction without synthesizing fake input.
static bool ForceForeground(HWND hwnd)
{
    if (GetForegroundWindow() == hwnd && !IsIconic(hwnd))
        return true;

    HWND  fore          = GetForegroundWindow();
    DWORD foreThread    = fore ? GetWindowThreadProcessId(fore, nullptr) : 0;
    DWORD targetThread  = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD currentThread = GetCurrentThreadId();

    bool attachedCurrent = false;
    bool attachedTarget  = false;
    if (foreThread && foreThread != currentThread)
        attachedCurrent = AttachThreadInput(currentThread, foreThread, TRUE) != 0;
    if (targetThread && foreThread && targetThread != foreThread)
        attachedTarget = AttachThreadInput(targetThread, foreThread, TRUE) != 0;

    AllowSetForegroundWindow(ASFW_ANY);

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    // Z-order kick: TOPMOST → NOTOPMOST forces the window above the rest of
    // the stack but doesn't leave it pinned. SWP_NOACTIVATE keeps focus
    // changes consolidated to the SetForegroundWindow call below.
    SetWindowPos(hwnd, HWND_TOPMOST,   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    BringWindowToTop(hwnd);
    BOOL sfwOk = SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attachedTarget)  AttachThreadInput(targetThread,  foreThread, FALSE);
    if (attachedCurrent) AttachThreadInput(currentThread, foreThread, FALSE);

    const bool foregrounded = GetForegroundWindow() == hwnd;
    if (!foregrounded)
        Log("ForceFocus: attempt %d: SFW=%d, fore=0x%p target=0x%p (foreThread=%lu targetThread=%lu attachCur=%d attachTgt=%d)",
            g_frameCount, sfwOk ? 1 : 0,
            GetForegroundWindow(), hwnd,
            foreThread, targetThread,
            attachedCurrent ? 1 : 0, attachedTarget ? 1 : 0);
    return foregrounded;
}

void ForceFocus_OnImGuiDraw(void* /*userData*/)
{
    if (g_done) return;
    g_frameCount++;

    HWND hwnd = g_getGameWindow ? g_getGameWindow() : nullptr;
    if (!hwnd)
    {
        if (g_frameCount == 1)
            Log("ForceFocus: GetGameWindow returned null, will retry");
        return;
    }

    if (ForceForeground(hwnd))
    {
        Log("ForceFocus: Foreground secured after %d frame(s) (HWND=0x%p)", g_frameCount, hwnd);
        g_done = true;
        return;
    }

    if (g_frameCount >= kMaxRetryFrames)
    {
        Log("ForceFocus: Gave up forcing foreground after %d frames; current fore=0x%p target=0x%p",
            g_frameCount, GetForegroundWindow(), hwnd);
        g_done = true;
    }
}

void ForceFocus_Init(HS_GetGameWindowFn getWindow)
{
    g_getGameWindow = getWindow;
}
