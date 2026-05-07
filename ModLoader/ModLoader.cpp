#include "ModLoader.h"
#include "ModInterface.h"
#include "ImGuiHook.h"
#include "GameWindow.h"

#include <Windows.h>
#include <atomic>
#include <iostream>
#include <fstream>
#include <string>

#include "MinHook.h"

std::map<std::string, HookBase*> ModLoader::HookMap{};

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

struct DailyStatusSEHResult { double value; const char* failReason; };

static DailyStatusSEHResult DailyStatusQuery_SEH(uintptr_t moduleBase)
{
    __try
    {
        using fn_CBC420 = int(__cdecl*)(void*);
        using fn_C99410 = char(__cdecl*)(int, int, int, int*);

        auto CBC420 = (fn_CBC420)(moduleBase + 0xCBC420);
        auto C99410 = (fn_C99410)(moduleBase + 0xC99410);

        if (!CBC420 || !C99410)
            return { 0.0, "function pointers invalid" };

        uint32_t instance_table_base = *(uint32_t*)(moduleBase + 0x0453D610);
        if (!instance_table_base)
            return { 0.0, "instance table base null" };

        int instance_id = CBC420((void*)instance_table_base);
        if (!instance_id)
            return { 0.0, "instance_id resolved to 0" };

        struct GMVariant { double value; uint32_t type; uint32_t pad1; uint32_t pad2; } out{};
        if (!C99410(instance_id, 634, 0x80000000, (int*)&out))
            return { 0.0, "C99410 query failed (var 634)" };

        switch (out.type & 0xFFFFFF)
        {
        case 0x0: case 0xD: return { out.value, nullptr };
        case 0x3: case 0x7: case 0xA: return { (double)*(int*)&out, nullptr };
        default:
            ModLoader::Log("ModLoader", "GetDailyStatus: unexpected GM variant type 0x%X", out.type & 0xFFFFFF);
            return { 0.0, "unexpected GM variant type" };
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return { 0.0, "access violation" };
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

static void LogGMLArgs_SEH(const char* fnName, int self, int argc, uintptr_t** argv, uintptr_t* result)
{
    __try
    {
        ModLoader::Log("Hook", "%s argc=%d", fnName, argc);
        ModLoader::Log("Hook", "  self  id=0x%08X", (uint32_t)self);
        for (int i = 0; i < argc; i++)
        {
            char label[16];
            sprintf_s(label, "arg[%d]", i);
            LogRValue_SEH(label, argv ? reinterpret_cast<RValue*>(argv[i]) : nullptr);
        }
        LogRValue_SEH("result", reinterpret_cast<RValue*>(result));
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

// Enumerates character instances of objType and logs their name via the same
// call chain AnnotateCharacter uses: sub_1C47D0 prepares var634 as an argument,
// then sub_CA7F30 calls the compiled name function (handle at moduleBase+0x10C3CCC).
static void LogCharacterNames_SEH(uint32_t moduleBase, int objType, int other)
{
    using fn_CA8590_t = int  (__cdecl*)(void*, uint32_t*, int*, int);
    using fn_C9AC30_t = uint8_t(__cdecl*)(void*, uint32_t*);
    using fn_C9AC60_t = void (__cdecl*)(void*);
    // Evaluates a variant (at *argPtr) into the GML argument stack; returns opaque arg index
    using fn_1C47D0_t = int  (__cdecl*)(uint32_t, int, void*, int, uint32_t*);
    // Calls a compiled GML function: (self, other, result, argc, funcHandle, argStackIdx*)
    using fn_CA7F30_t = uint32_t*(__cdecl*)(uint32_t, int, void*, int, int, int);

    auto CA8590    = (fn_CA8590_t)(moduleBase + 0xCA8590);
    auto C9AC30    = (fn_C9AC30_t)(moduleBase + 0xC9AC30);
    auto C9AC60    = (fn_C9AC60_t)(moduleBase + 0xC9AC60);
    auto sub_1C47D0 = (fn_1C47D0_t)(moduleBase + 0x1C47D0);
    auto CA7F30    = (fn_CA7F30_t)(moduleBase + 0xCA7F30);

    // Name function handle compiled by GML into the binary's data segment
    int nameHandle = *(int*)(moduleBase + 0x10C3CCC);

    uint8_t  iterState[32] = {}; // decomp shows 8 bytes but adjacent slot also used; 32 is safe
    int      ctx[5]        = { other, 0, 0, 0, 0 }; // v368: [0]=other
    uint32_t instance      = 0;

    __try
    {
        if (CA8590(iterState, &instance, ctx, objType) <= 0)
            return;

        do
        {
            // varTable is at instance+4 (same pattern used throughout AnnotateCharacter)
            uint32_t varTable = *(uint32_t*)(instance + 4);
            if (!varTable)
                continue;

            // var634 variant is at varTable + 634*16 = varTable + 10144
            uint32_t var634addr = varTable + 634 * 16;
            uint32_t argRef     = var634addr;
            uint32_t argBuf[8]  = {}; // v222/v226 in decomp are 8-12 bytes; 32 bytes is safe
            int argIdx = sub_1C47D0(instance, other, argBuf, 1, &argRef);

            uint32_t  resultBuf[8] = {};
            uint32_t* nameVar = CA7F30(instance, other, resultBuf, 1, nameHandle, (int)&argIdx);

            // Variant layout: [0..3]=value/ptr, [4..7]=hi, [8..11]=extra, [12..15]=type
            if (nameVar && (nameVar[3] & 0xFF) == 1)
            {
                void* strObj = (void*)nameVar[0];
                if (strObj)
                {
                    const char* name = *(const char**)strObj;
                    if (name)
                        ModLoader::Log("PlayAsCharacter", "Character: %s", name);
                }
            }
        }
        while (C9AC30(iterState, &instance));

        C9AC60(iterState);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ModLoader::Log("PlayAsCharacter", "Exception reading character names (instance=%08X)", instance);
    }
}

// ---------------------------------------------------------------------------

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
    static const char* s_lastFailure = nullptr;
    static DWORD s_lastFailTime = 0;

    return 0;

    auto fail = [&](const char* reason) -> double
        {
            DWORD now = GetTickCount();
            if (!s_lastFailure || strcmp(s_lastFailure, reason) != 0 || (now - s_lastFailTime) >= 5000)
            {
                ModLoader::Log("ModLoader", "GetDailyStatus returning 0.0 (%s)", reason);
                s_lastFailure = reason;
                s_lastFailTime = now;
            }
            return 0.0;
        };

    if (!HookBase::moduleBase)
        return fail("module base not found");

    DailyStatusSEHResult r = DailyStatusQuery_SEH(HookBase::moduleBase);
    if (r.failReason)
        return fail(r.failReason);
    return r.value;
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

void ModLoader::LogGMLCall(const char* fnName, int self, int argc, uintptr_t** argv, uintptr_t* result)
{
    LogGMLArgs_SEH(fnName, self, argc, argv, result);
}

void ModLoader::LogRValue(const char* label, RValue* rv)
{
    LogRValue_SEH(label, rv);
}

void ModLoader::LogCharacterNames(uint32_t moduleBase, int objType, int other)
{
    LogCharacterNames_SEH(moduleBase, objType, other);
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
            &FindGameWindow
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
