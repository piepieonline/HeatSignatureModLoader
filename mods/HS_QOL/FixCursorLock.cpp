#include "FixCursorLock.h"
#include <windows.h>

static SE_GetGameWindowFn g_getGameWindow = nullptr;
static SE_RequestBypassFn g_requestBypass = nullptr;

static void OnUpdateCursorPosition(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    HWND hwnd = g_getGameWindow();
    if (!hwnd || GetForegroundWindow() != hwnd)
        g_requestBypass();
}

void FixCursorLock_Register(const SE_ModApi* api)
{
    g_getGameWindow = api->GetGameWindow;
    g_requestBypass = api->RequestBypass;
    api->SubscribeHook("gml_Script_UpdateCursorPosition", OnUpdateCursorPosition, nullptr);
}
