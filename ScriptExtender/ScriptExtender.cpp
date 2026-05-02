#include "ScriptExtender.h";


#include <Windows.h>
#include <iostream>
#include <fstream>

#include "MinHook.h"

std::map<std::string, HookBase*> ScriptExtender::HookMap{};

using MissionAccept_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
MissionAccept_t oMissionAccept = nullptr;

using GenerateMissions_t = uintptr_t* (__cdecl*)(double a1, uintptr_t* a2, int a3, uintptr_t* a4);
void* oGenerateMissions = nullptr;

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
}

void ScriptExtender::Log(const char* format, ...) {
    char buffer[1024]; // TODO: temporary buffer for formatted string

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::cout << buffer << std::endl;

    std::ofstream logFile("ScriptExtender.log", std::ios::app);

    if (logFile.is_open()) {
        logFile << buffer << std::endl;
    }

    logFile.close();
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

    /*
    if (MH_CreateHook(
        reinterpret_cast<LPVOID>(0x00B16200),
        +[](int a1, int a2, uintptr_t* a3, int a4, int a5) -> uintptr_t* {
            ScriptExtender::Instance().Log("hook: mission accepted");

            // Call original function
            if (oMissionAccept)
                return oMissionAccept(a1, a2, a3, a4, a5);

            return nullptr;
        },
        reinterpret_cast<LPVOID*>(&oMissionAccept)) != MH_OK)
    {
        Log("MH_CreateHook failed: AcceptMission");
        return;
    }
    if (MH_EnableHook(reinterpret_cast<LPVOID>(0x00B16200)) != MH_OK)
    {
        Log("MH_EnableHook failed");
        return;
    }
    */

    /*
    if (MH_CreateHook(
        reinterpret_cast<LPVOID>(0x01176200),
        +[](double a1, uintptr_t* a2, int a3, uintptr_t* a4) -> uintptr_t* {
            ScriptExtender::Instance().Log("hook: mission generation");

            // Call original function
            if (oGenerateMissions)
                return oGenerateMissions(a1, a2, a3, a4);

            return nullptr;
        },
        reinterpret_cast<LPVOID*>(&oGenerateMissions)) != MH_OK)
    {
        Log("MH_CreateHook failed: AcceptMission");
        return;
    }

    */
    /*
    if (MH_EnableHook(reinterpret_cast<LPVOID>(0x011D9EB0)) != MH_OK)
    {
        Log("MH_EnableHook failed");
        return;
    }
    */

    Log("Hooks installed");
}