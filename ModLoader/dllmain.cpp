#include <windows.h>
#include <cstdio>
#include <mutex>
#include <string>

#include "ModLoader.h";

HMODULE g_hModLoaderModule = nullptr;

DWORD WINAPI HookThread(LPVOID lpParam) {
    ModLoader::Instance();
    return 0;
}

// DllMain: load real DLL and init logging
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hModLoaderModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)HookThread, nullptr, 0, nullptr);

        break;

    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}