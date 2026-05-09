#include "FixCursorLock.h"
#include <windows.h>
#include "Log.h"

static HS_GetGameWindowFn g_getGameWindow = nullptr;
static HS_RequestBypassFn g_requestBypass = nullptr;

static void OnUpdateCursorPosition(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    HWND hwnd = g_getGameWindow();
    if (!hwnd || GetForegroundWindow() != hwnd)
    {
        g_requestBypass();
    }
}

void FixCursorLock_Register(const HS_ModApi* api)
{
    g_getGameWindow = api->GetGameWindow;
    g_requestBypass = api->RequestBypass;
    api->SubscribeHook("gml_Script_CaptureCursor", OnUpdateCursorPosition, nullptr);
}
