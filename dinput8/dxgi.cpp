#include <windows.h>
#include <cstdio>
#include <cstdarg>

static HMODULE g_real = nullptr;

static HMODULE LoadRealDxgi()
{
    if (g_real) return g_real;

    char sys[MAX_PATH] = { 0 };
    if (!GetSystemDirectoryA(sys, MAX_PATH)) return nullptr;
    strcat_s(sys, "\\dxgi.dll");
    g_real = LoadLibraryA(sys);
    return g_real;
}

static DWORD WINAPI InitThread(LPVOID)
{
    while (!GetModuleHandleA("steam_api.dll")) Sleep(100);
    LoadLibraryA(".\\ScriptExtender.dll");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        LoadRealDxgi();
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        if (g_real) { FreeLibrary(g_real); g_real = nullptr; }
        break;
    }
    return TRUE;
}