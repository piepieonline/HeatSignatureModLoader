#include "ScriptExtender.h"
#include "ModInterface.h"

#include <Windows.h>
#include <iostream>
#include <fstream>
#include <string>

#include "MinHook.h"

std::map<std::string, HookBase*> ScriptExtender::HookMap{};

using MissionAccept_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using GenerateMissions_t = uintptr_t* (__cdecl*)(double a1, uintptr_t* a2, int a3, uintptr_t* a4);

ScriptExtender& ScriptExtender::Instance()
{
    static ScriptExtender instance;
    return instance;
}

ScriptExtender::ScriptExtender()
{
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);

    std::ofstream logFile("ScriptExtender.log", std::ios::trunc);
    logFile.close();

    Log("Up");

    CreateHooks();
    LoadMods();
}

void ScriptExtender::Log(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::cout << buffer << std::endl;

    std::ofstream logFile("ScriptExtender.log", std::ios::app);
    if (logFile.is_open())
        logFile << buffer << std::endl;
}

void ScriptExtender::LoadMods()
{
    CreateDirectoryA(".\\mods", nullptr);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(".\\mods\\*.dll", &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("[Mods] No mods found in .\\mods\\");
        return;
    }

    do {
        std::string path = std::string(".\\mods\\") + fd.cFileName;
        HMODULE hMod = LoadLibraryA(path.c_str());
        if (!hMod)
        {
            Log("[Mods] Failed to load %s (error %lu)", fd.cFileName, GetLastError());
            continue;
        }

        auto modInit = reinterpret_cast<void(*)(SE_LogFn)>(GetProcAddress(hMod, "ModInit"));
        if (!modInit)
        {
            Log("[Mods] %s has no ModInit export, skipping", fd.cFileName);
            continue;
        }

        modInit([](const char* msg) { ScriptExtender::Log(msg); });
        Log("[Mods] Loaded %s", fd.cFileName);

    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void ScriptExtender::CreateHooks()
{
    if (MH_Initialize() != MH_OK)
    {
        Log("MH_Initialize failed");
        return;
    }

    for (auto hook : hooks)
    {
        hook->CreateHook();
        HookMap[hook->hookName] = hook;
    }

    Log("Hooks installed");
}
