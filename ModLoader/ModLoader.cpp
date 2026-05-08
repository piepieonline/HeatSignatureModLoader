#include "ModLoader.h"
#include "ModInterface.h"
#include "ImGuiHook.h"
#include "GameWindow.h"
#include "ModConfig.h"

#include <Windows.h>
#include <atomic>
#include <iostream>
#include <fstream>
#include <string>

#include "MinHook.h"

static const char* Config_Read   (void* h, const char* key, const char* def) { return static_cast<ModConfig*>(h)->Read(key, def); }
static void        Config_Write  (void* h, const char* key, const char* val) { static_cast<ModConfig*>(h)->Write(key, val); }
static const char* Config_GetJson(void* h)                                   { return static_cast<ModConfig*>(h)->GetJson(); }
static void        Config_SetJson(void* h, const char* json)                 { static_cast<ModConfig*>(h)->SetJson(json); }
static void        Config_Save   (void* h)                                   { static_cast<ModConfig*>(h)->Save(); }

std::map<std::string, HookBase*> ModLoader::HookMap{};

bool ModLoader::isDrawing = false;
RValue ModLoader::nameVal;

// ---------------------------------------------------------------------------
// SEH-isolated helpers — no C++ objects with destructors allowed in scope
// ---------------------------------------------------------------------------

static const char* TimeScaleWalk_SEH(uintptr_t moduleBase, double* outResult)
{
    const uintptr_t kBaseRVA = 0x0453D610;
    const uint32_t kDerefOffsets[] = { 0x60, 0x10, 0x3C4 };
    const uint32_t kFinalOffset = 0x1B0;

    __try
    {
        uint32_t ptr = *reinterpret_cast<uint32_t*>(moduleBase + kBaseRVA);
        if (!ptr)
            return "base pointer null";

        int step = 0;
        for (uint32_t off : kDerefOffsets)
        {
            ptr = *reinterpret_cast<uint32_t*>(ptr + off);
            if (!ptr)
            {
                static const char* kStepReasons[] = {
                    "deref +0x60 null", "deref +0x10 null", "deref +0x3C4 null"
                };
                return kStepReasons[step];
            }
            ++step;
        }
        *outResult = *reinterpret_cast<double*>(ptr + kFinalOffset);
        return nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "access violation walking pointer chain";
    }
}

static void LogDrawTextArgs_SEH(uintptr_t** argv)
{
    struct RValue { double val; uint32_t type; uint32_t pad; };
    RValue* args = reinterpret_cast<RValue*>(argv);
    __try
    {
        double x         = args[0].val;
        double y         = args[1].val;
        uint32_t strPtr  = *reinterpret_cast<uint32_t*>(&args[2].val);
        const char* text = strPtr ? *reinterpret_cast<const char**>(strPtr) : nullptr;
        ModLoader::Log("ModLoader Hook", "draw_text x=%.1f y=%.1f str=\"%s\"",
            x, y, text ? text : "(null)");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ModLoader::Log("ModLoader Hook", "draw_text failed to read args");
    }
}

static void LogArgsAddress_SEH(int argc, uintptr_t** argv)
{
    struct RValue { double val; uint32_t type; uint32_t pad; };
    RValue* args = reinterpret_cast<RValue*>(argv);
    __try
    {
        for (int i = 0; i < argc; i++)
        {
            ModLoader::Log("ModLoader Hook", "arg %d: (raw: %08X)",
                i, *(uint32_t*)&args[i].val);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ModLoader::Log("ModLoader Hook", "draw_text failed to read args");
    }
}

static void LogRValue_SEH(const char* label, RValue* rv)
{
    if (!rv)
    {
        ModLoader::Log("Hook", "  %s @NULL", label);
        return;
    }

    __try
    {
        int type = rv->type & 0xFF;

        switch (type)
        {
        case 0: // REAL
        {
            ModLoader::Log(
                "Hook",
                "  %s @0x%08X type=REAL val=%f",
                label,
                (uint32_t)(uintptr_t)rv,
                rv->real);
            break;
        }

        case 1: // STRING
        {
            void* strObj = rv->ptr;

            const char* str =
                strObj
                ? *(const char**)strObj
                : nullptr;

            ModLoader::Log(
                "Hook",
                "  %s @0x%08X type=STRING ptr=0x%08X val=\"%s\"",
                label,
                (uint32_t)(uintptr_t)rv,
                (uint32_t)(uintptr_t)strObj,
                str ? str : "(null)");
            break;
        }
        case 2: // ARRAY
        {
            ModLoader::Log(
                "Hook",
                "  %s @0x%08X type=ARRAY raw=0x%08X",
                label,
                (uint32_t)(uintptr_t)rv,
                (uint32_t)(uintptr_t)rv->ptr);
            break;
        }
        default:
        {
            ModLoader::Log(
                "Hook",
                "  %s @0x%08X type=%s(%d) raw=0x%08X",
                label,
                (uint32_t)(uintptr_t)rv,
                GetTypeName(type),
                type,
                (uint32_t)(uintptr_t)rv->ptr);
            break;
        }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ModLoader::Log(
            "Hook",
            "  %s @0x%08X <exception reading RValue>",
            label,
            (uint32_t)(uintptr_t)rv);
    }
}

static void LogGMLArgs_SEH(const char* fnName, uintptr_t* self, int argc, RValue** argv, RValue* result)
{
    __try
    {
        ModLoader::Log("Hook", "%s argc=%d", fnName, argc);
        ModLoader::Log("Hook", "  self  id=0x%08X", (uint32_t)(uintptr_t)self);
        for (int i = 0; i < argc; i++)
        {
            char label[16];
            sprintf_s(label, "arg[%d]", i);
            LogRValue_SEH(label, argv ? argv[i] : nullptr);
        }
        LogRValue_SEH("result", result);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ModLoader::Log("Hook", "%s: exception reading args", fnName);
    }
}

static BOOL ReadDword_SEH(uintptr_t addr, DWORD* outValue)
{
    __try
    {
        *outValue = *reinterpret_cast<DWORD*>(addr);
        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

double ModLoader::GetTimeScale()
{
    static const char* s_lastFailure = "init";
    auto fail = [](const char* reason) -> double {
        if (s_lastFailure == nullptr || strcmp(s_lastFailure, reason) != 0)
        {
            ModLoader::Log("ModLoader", "TimeScale returning 1.0 (%s)", reason);
            s_lastFailure = reason;
        }
        return 1.0;
    };

    if (HookBase::moduleBase == 0)
        return fail("module base not found");

    double result = 1.0;
    const char* failReason = TimeScaleWalk_SEH(HookBase::moduleBase, &result);

    if (failReason)
        return fail(failReason);

    if (s_lastFailure != nullptr)
    {
        ModLoader::Log("ModLoader", "TimeScale recovered (was: %s) -> %f", s_lastFailure, result);
        s_lastFailure = nullptr;
    }
    return result;
}

double ModLoader::GetDailyStatus()
{
    return 0;
}

void ModLoader::LogDrawText(int argc, uintptr_t** argv)
{
    Log("ModLoader Hook", "draw_text argv=%p argc=%d", (void*)argv, argc);
    LogDrawTextArgs_SEH(argv);
}

void ModLoader::LogArgsAddress(int argc, uintptr_t** argv)
{
    LogArgsAddress_SEH(argc, argv);
}

void ModLoader::LogGMLCall(const char* fnName, uintptr_t* self, int argc, RValue** argv, RValue* result)
{
    LogGMLArgs_SEH(fnName, self, argc, argv, result);
}

void ModLoader::LogRValue(const char* label, RValue* rv)
{
    LogRValue_SEH(label, rv);
}

void ModLoader::PollDword(const char* label, uintptr_t rva, DWORD intervalMs)
{
    struct Args { std::string label; uintptr_t rva; DWORD intervalMs; };
    auto* args = new Args{ label, rva, intervalMs };

    HANDLE thread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        auto* a = static_cast<Args*>(param);
        while (true)
        {
            Sleep(a->intervalMs);
            if (HookBase::moduleBase == 0)
            {
                ModLoader::Log(a->label.c_str(), "moduleBase not ready");
                continue;
            }
            DWORD value = 0;
            if (ReadDword_SEH(HookBase::moduleBase + a->rva, &value))
                ModLoader::Log(a->label.c_str(), "0x%08X (%u)", value, value);
            else
                ModLoader::Log(a->label.c_str(), "access violation reading 0x%p + 0x%X",
                    (void*)HookBase::moduleBase, (unsigned)a->rva);
        }
        return 0;
    }, args, 0, nullptr);

    if (thread) CloseHandle(thread);
}

using MissionAccept_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using MissionComplete_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using GenerateMissions_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
using SetTimeScale_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
using PauseMission_t = uintptr_t* (__cdecl*)(uintptr_t* a1, int a2, uintptr_t* a3);
using PauseFor_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t** a5);
using SetSlowMotionEffectStrength_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t* a5);

ModLoader& ModLoader::Instance()
{
    static ModLoader instance;
    return instance;
}

ModLoader::ModLoader()
{
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);

    std::ofstream logFile("ModLoader.log", std::ios::trunc);
    logFile.close();

    extern HMODULE g_hModLoaderModule;
    char selfPath[MAX_PATH] = {};
    GetModuleFileNameA(g_hModLoaderModule, selfPath, MAX_PATH);
    std::string loaderConfigPath = selfPath;
    auto extPos = loaderConfigPath.rfind('.');
    if (extPos != std::string::npos)
        loaderConfigPath.replace(extPos, std::string::npos, ".json");
    m_loaderConfig = std::make_unique<ModConfig>(loaderConfigPath);

    Log("ModLoader", "Attached");

    CreateHooks();
    ImGuiHook::Install();
    LoadMods();

    // PollDword("dword_10C3CEC", 0x10C3CEC, 5000);
}

void ModLoader::Log(const char* prefix, const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::cout << "[" << prefix << "] " << buffer << std::endl;

    std::ofstream logFile("ModLoader.log", std::ios::app);
    if (logFile.is_open())
        logFile << "[" << prefix << "] " << buffer << std::endl;
}

void ModLoader::SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData)
{
    auto it = HookMap.find(hookName);
    if (it == HookMap.end())
    {
        Log("ModLoader", "Subscribe failed : unknown hook '%s'", hookName);
        return;
    }
    it->second->preSubscribers.emplace_back(callback, userData);
    Log("ModLoader", "Pre-subscriber registered for %s", hookName);
}

void ModLoader::SubscribeHookPost(const char* hookName, SE_HookPostCallback callback, void* userData)
{
    auto it = HookMap.find(hookName);
    if (it == HookMap.end())
    {
        Log("ModLoader", "SubscribePost failed: unknown hook '%s'", hookName);
        return;
    }
    it->second->postSubscribers.emplace_back(callback, userData);
    Log("ModLoader", "Post-subscriber registered for %s", hookName);
}

void ModLoader::LoadMods()
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
        Log("ModLoader", "Loading % s", fd.cFileName);
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

        auto modConfigObj = std::make_unique<ModConfig>(std::string(".\\mods\\") + modName + ".json");
        SE_ModConfig modConfigApi = { modConfigObj.get(), Config_Read, Config_Write, Config_GetJson, Config_SetJson, Config_Save };
        m_modConfigs.push_back(std::move(modConfigObj));

        SE_ModApi modApi = {
            +[](const char* prefix, const char* msg) { ModLoader::Log(prefix, msg); },
            +[](const char* hookName, SE_HookCallback cb, void* userData) {
                ModLoader::SubscribeHook(hookName, cb, userData);
            },
            +[](const char* hookName, SE_HookPostCallback cb, void* userData) {
                ModLoader::SubscribeHookPost(hookName, cb, userData);
            },
            &ModLoader::GetTimeScale,
            &ModLoader::GetDailyStatus,
            +[](SE_ImGuiDrawFn cb, void* userData) {
                ImGuiHook::RegisterDraw(cb, userData);
            },
            +[]() -> void* { return ImGuiHook::GetContext(); },
            +[](void** a, void** f, void** ud) { ImGuiHook::GetAllocators(a, f, ud); },
            &FindGameWindow,
            +[]() { g_hookBypassRequested = true; },
            modConfigApi
        };

        modInit(&modApi);

    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void ModLoader::CreateHooks()
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
