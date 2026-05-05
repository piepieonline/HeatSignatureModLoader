#include "GameWindow.h"

namespace
{
    struct FindWindowCtx
    {
        DWORD pid      = 0;
        HWND  bestHwnd = nullptr;
        LONG  bestArea = 0;
    };

    BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lparam)
    {
        auto* ctx = reinterpret_cast<FindWindowCtx*>(lparam);

        DWORD wndPid = 0;
        GetWindowThreadProcessId(hwnd, &wndPid);
        if (wndPid != ctx->pid)        return TRUE;
        if (!IsWindowVisible(hwnd))    return TRUE;
        if (GetWindow(hwnd, GW_OWNER)) return TRUE;

        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (lstrcmpiA(cls, "ConsoleWindowClass") == 0) return TRUE;

        RECT r{};
        if (!GetClientRect(hwnd, &r)) return TRUE;
        const LONG area = (r.right - r.left) * (r.bottom - r.top);
        if (area <= 0) return TRUE;

        if (area > ctx->bestArea)
        {
            ctx->bestArea = area;
            ctx->bestHwnd = hwnd;
        }
        return TRUE;
    }
}

HWND FindGameWindow()
{
    FindWindowCtx ctx;
    ctx.pid = GetCurrentProcessId();
    EnumWindows(&FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.bestHwnd;
}
