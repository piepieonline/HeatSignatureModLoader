#include "ScriptExtender.h"
#include "ModInterface.h"
#include "ImGuiHook.h"

#include <Windows.h>
#include <atomic>
#include <iostream>
#include <fstream>
#include <string>

#include "MinHook.h"

std::map<std::string, HookBase*> ScriptExtender::HookMap{};

double ScriptExtender::GetTimeScale()
{
    // Pointer chain identified via Cheat Engine — resolves to the live game
    // timescale double once the time-manager instance exists. Walking it is
    // safe to call from any thread (read-only, no engine code runs).
    //
    // Heat_Signature.exe is 32-bit, so each indirection is a 4-byte pointer.
    // Chain (bottom-to-top in CE):
    //   p = *(u32*)(module + 0x0453D610)
    //   p = *(u32*)(p + 0x60)
    //   p = *(u32*)(p + 0x10)
    //   p = *(u32*)(p + 0x3C4)
    //   value = *(double*)(p + 0x1B0)

    // Called every recorder loop iteration — log only on state transitions
    // (last failure reason vs. current) to avoid spamming the console.
    static const char* s_lastFailure = "init"; // != nullptr -> next success logs recovery
    auto fail = [](const char* reason) -> double {
        if (s_lastFailure == nullptr || strcmp(s_lastFailure, reason) != 0)
        {
            ScriptExtender::Log("ModLoader", "TimeScale returning 1.0 (% s)", reason);
            s_lastFailure = reason;
        }
        return 1.0;
    };

    if (HookBase::moduleBase == 0)
        return fail("module base not found");

    constexpr uintptr_t kBaseRVA      = 0x0453D610;
    constexpr uint32_t  kDerefOffsets[] = { 0x60, 0x10, 0x3C4 };
    constexpr uint32_t  kFinalOffset  = 0x1B0;

    double result = 1.0;
    const char* failReason = nullptr;
    __try
    {
        uint32_t ptr = *reinterpret_cast<uint32_t*>(HookBase::moduleBase + kBaseRVA);
        if (!ptr) { failReason = "base pointer null"; }
        else
        {
            int step = 0;
            for (uint32_t off : kDerefOffsets)
            {
                ptr = *reinterpret_cast<uint32_t*>(ptr + off);
                if (!ptr)
                {
                    static const char* kStepReasons[] = {
                        "deref +0x60 null", "deref +0x10 null", "deref +0x3C4 null"
                    };
                    failReason = kStepReasons[step];
                    break;
                }
                ++step;
            }
            if (!failReason)
                result = *reinterpret_cast<double*>(ptr + kFinalOffset);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        failReason = "access violation walking pointer chain";
    }

    if (failReason)
        return fail(failReason);

    if (s_lastFailure != nullptr)
    {
        ScriptExtender::Log("ModLoader", "TimeScale recovered(was: % s) -> % f", s_lastFailure, result);
        s_lastFailure = nullptr;
    }
    return result;
}

using MissionAccept_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using MissionComplete_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using GenerateMissions_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
using SetTimeScale_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
using PauseMission_t = uintptr_t* (__cdecl*)(uintptr_t* a1, int a2, uintptr_t* a3);
using PauseFor_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t** a5);
using SetSlowMotionEffectStrength_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t* a5);

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

    Log("ModLoader", "Attached");

    CreateHooks();
    ImGuiHook::Install();
    LoadMods();
}

void ScriptExtender::Log(const char* prefix, const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::cout << "[" << prefix << "] " << buffer << std::endl;

    std::ofstream logFile("ScriptExtender.log", std::ios::app);
    if (logFile.is_open())
        logFile << "[" << prefix << "] " << buffer << std::endl;
}

void ScriptExtender::SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData)
{
    auto it = HookMap.find(hookName);
    if (it == HookMap.end())
    {
        Log("ModLoader", "Subscribe failed : unknown hook '%s'", hookName);
        return;
    }
    it->second->subscribers.emplace_back(callback, userData);
    Log("ModLoader", "Subscriber registered for %s", hookName);
}

void ScriptExtender::LoadMods()
{
    CreateDirectoryA(".\\mods", nullptr);



    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(".\\mods\\*.dll", &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("ModLoader", "No mods found in .\\mods\\");
        return;
    }

    do {
        std::string path = std::string(".\\mods\\") + fd.cFileName;
        HMODULE hMod = LoadLibraryA(path.c_str());
        if (!hMod)
        {
            Log("ModLoader", "Failed to load %s (error %lu)", fd.cFileName, GetLastError());
            continue;
        }

        auto modInit = reinterpret_cast<SE_ModInitFn>(GetProcAddress(hMod, "ModInit"));
        if (!modInit)
        {
            Log("ModLoader", "%s has no ModInit export, skipping", fd.cFileName);
            continue;
        }

        std::string modName = fd.cFileName;

        if (modName.size() > 4 &&
            _stricmp(modName.c_str() + modName.size() - 4, ".dll") == 0)
        {
            modName.resize(modName.size() - 4);
        }

        SE_ModApi modApi = {
            +[](const char* prefix, const char* msg) { ScriptExtender::Log(prefix, msg); },
            +[](const char* hookName, SE_HookCallback cb, void* userData) {
                ScriptExtender::SubscribeHook(hookName, cb, userData);
            },
            &ScriptExtender::GetTimeScale,
            +[](SE_ImGuiDrawFn cb, void* userData) {
                ImGuiHook::RegisterDraw(cb, userData);
            },
            +[]() -> void* { return ImGuiHook::GetContext(); },
            +[](void** a, void** f, void** ud) { ImGuiHook::GetAllocators(a, f, ud); }
        };

        modInit(&modApi);
        Log("ModLoader", "Loaded % s", fd.cFileName);

    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void ScriptExtender::CreateHooks()
{
    if (MH_Initialize() != MH_OK)
    {
        Log("ModLoader", "MH_Initialize failed");
        return;
    }

    for (auto hook : hooks)
    {
        hook->CreateHook();
        HookMap[hook->hookName] = hook;
    }

    Log("ModLoader", "Hooks installed");
}
