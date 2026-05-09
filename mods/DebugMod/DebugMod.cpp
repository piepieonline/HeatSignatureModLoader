#include "ModInterface.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "PropertyNames.h"

HS_EXPORT_MOD_API_VERSION()

static const HS_ModApi* g_api = nullptr;
static uintptr_t        g_moduleBase = 0;

static bool   g_isDrawing = false;
static RValue g_nameVal;

static void Log(const char* prefix, const char* fmt, ...)
{
    if (!g_api) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_api->Log(prefix, buf);
}

static inline uint32_t* GetActiveContext()
{
    uint32_t* ctx = (uint32_t*)0x45ED614;

    while (ctx)
    {
        if (ctx[9]) // validity flag
            return ctx;

        ctx = (uint32_t*)ctx[3];
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// SEH-isolated helpers — no C++ objects with destructors allowed in scope
// ---------------------------------------------------------------------------

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
        Log("DebugMod", "draw_text x=%.1f y=%.1f str=\"%s\"",
            x, y, text ? text : "(null)");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("DebugMod", "draw_text failed to read args");
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
            Log("DebugMod", "arg %d: (raw: %08X)",
                i, *(uint32_t*)&args[i].val);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("DebugMod", "draw_text failed to read args");
    }
}

static void LogRValue_SEH(const char* label, RValue* rv)
{
    if (!rv)
    {
        Log("DebugMod", "  %s @NULL", label);
        return;
    }

    __try
    {
        int type = rv->type & 0xFF;

        switch (type)
        {
        case 0: // REAL
        {
            Log(
                "DebugMod",
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

            Log(
                "DebugMod",
                "  %s @0x%08X type=STRING ptr=0x%08X val=\"%s\"",
                label,
                (uint32_t)(uintptr_t)rv,
                (uint32_t)(uintptr_t)strObj,
                str ? str : "(null)");
            break;
        }
        case 2: // ARRAY
        {
            Log(
                "DebugMod",
                "  %s @0x%08X type=ARRAY raw=0x%08X",
                label,
                (uint32_t)(uintptr_t)rv,
                (uint32_t)(uintptr_t)rv->ptr);
            break;
        }
        default:
        {
            Log(
                "DebugMod",
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
        Log(
            "DebugMod",
            "  %s @0x%08X <exception reading RValue>",
            label,
            (uint32_t)(uintptr_t)rv);
    }
}

static void LogGMLArgs_SEH(const char* fnName, uintptr_t* self, int argc, RValue** argv, RValue* result)
{
    __try
    {
        Log("DebugMod", "%s argc=%d", fnName, argc);
        Log("DebugMod", "  self  id=0x%08X", (uint32_t)(uintptr_t)self);
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
        Log("DebugMod", "%s: exception reading args", fnName);
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

static void LogDrawText(int argc, uintptr_t** argv)
{
    Log("DebugMod", "draw_text argv=%p argc=%d", (void*)argv, argc);
    LogDrawTextArgs_SEH(argv);
}

static void LogArgsAddress(int argc, uintptr_t** argv)
{
    LogArgsAddress_SEH(argc, argv);
}

static void LogGMLCall(const char* fnName, uintptr_t* self, int argc, RValue** argv, RValue* result = nullptr)
{
    LogGMLArgs_SEH(fnName, self, argc, argv, result);
}

static void LogRValue(const char* label, RValue* rv)
{
    LogRValue_SEH(label, rv);
}

static void PollDword(const char* label, uintptr_t rva, DWORD intervalMs)
{
    struct Args { std::string label; uintptr_t rva; DWORD intervalMs; };
    auto* args = new Args{ label, rva, intervalMs };

    HANDLE thread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        auto* a = static_cast<Args*>(param);
        while (true)
        {
            Sleep(a->intervalMs);
            if (g_moduleBase == 0)
            {
                Log(a->label.c_str(), "moduleBase not ready");
                continue;
            }
            DWORD value = 0;
            if (ReadDword_SEH(g_moduleBase + a->rva, &value))
                Log(a->label.c_str(), "0x%08X (%u)", value, value);
            else
                Log(a->label.c_str(), "access violation reading 0x%p + 0x%X",
                    (void*)g_moduleBase, (unsigned)a->rva);
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

// ---------------------------------------------------------------------------
// Hook callbacks
// ---------------------------------------------------------------------------

static void OnAcceptMissionPost(const char* hookName, uintptr_t* self, uintptr_t* /*other*/, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, argc, argv, returnValue);
    /*
    if (argc < 1 || !argv || !argv[0]) return;

    int instance_handle = g_api->ResolveInstance((uint32_t*)argv[0]);
    Log(hookName, "instance=0x%08X", instance_handle);

    for (const char* name : kPropertyNames)
    {
        RValue out{};
        g_api->GetVarByName(instance_handle, name, 0x80000000, &out);

        if (GetTypeName(out.type) == std::string("UNKNOWN"))
            continue;

        char label[128];
        sprintf_s(label, "%s:", name);
        LogRValue(label, &out);
    }
    */
}

static void OnPlayAsCharacterPost(const char* hookName, uintptr_t* self, uintptr_t* /*other*/, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, argc, argv, returnValue);

    /*
    for (const auto& [name, id] : ModLoader::VariableMap)
    {
        RValue out{};
        GetVar(instance_handle, id, 0x80000000, &out);

        if (GetTypeName(out.type) == std::string("UNKNOWN"))
            continue;

        char label[128];
        sprintf_s(label, "%s (%u):", name.c_str(), id);
        ModLoader::LogRValue(label, &out);
    }
    */
}

static void OnPlayerIsDailyChallengerPost(const char* /*hookName*/, uintptr_t* self, uintptr_t* other, RValue* /*returnValue*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    // LogGMLCall(hookName, self, argc, argv, returnValue);

    if (g_isDrawing)
    {
        RValue argX{}, argY{};
        argX.real = 100.0; argX.type = 0;
        argY.real = 200.0; argY.type = 0;

        RValue* argPtrs[3] = {
            &argX,
            &argY,
            &g_nameVal,
        };

        RValue resBuf{};
        g_api->CallScript("draw_text", self, other, &resBuf, 3, argPtrs);
    }
}

static void OnGenerateGunPost(const char* hookName, uintptr_t* self, uintptr_t* /*other*/, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, argc, argv, returnValue);

    int instance_handle = g_api->ResolveInstance((uint32_t*)argv[0]);
    Log("DebugMod", "instance=0x%08X", instance_handle);

    // RValue traits{};
    // GetVar(instance_handle, 664, 0x80000000, &traits);
    // ModLoader::LogRValue("Weapon Traits", &traits);

    /*
    for (const auto& [name, id] : ModLoader::VariableMap)
    {
        RValue out{};
        GetVar(instance_handle, id, 0x80000000, &out);

        if (GetTypeName(out.type) == std::string("UNKNOWN"))
            continue;

        char label[128];
        sprintf_s(label, "%s (%u):", name.c_str(), id);
        ModLoader::LogRValue(label, &out);
    }
    */

    /*
    struct IterState
    {
        uint8_t data[8];   // matches stack iterator buffer (v28)
    };

    using fn_CA8590 = int(__cdecl*)(IterState*, void*, void*, int);
    using fn_C9AC30 = uint8_t(__cdecl*)(IterState*, void*);
    using fn_CC62D0 = int(__cdecl*)(RValue*, int, void*);
    using fn_GetKey = int(__cdecl*)(IterState*, void*);

    fn_CA8590  sub_addr_CA8590 = (fn_CA8590)(HookBase::moduleBase + 0xCA8590);
    fn_C9AC30  sub_addr_C9AC30 = (fn_C9AC30)(HookBase::moduleBase + 0xC9AC30);
    fn_CC62D0  sub_addr_CC62D0 = (fn_CC62D0)(HookBase::moduleBase + 0xCC62D0);
    // fn_GetKey  sub_addr_GetKey = (fn_GetKey)0xGETKEYADDR; // replace

    IterState it;

    void* container = &instance_handle;

    // qword_44DEFB0 is a POINTER stored at that address
    void* ctx = *(void**)(HookBase::moduleBase + 0x44DEFB0);

    if (ctx && sub_addr_CA8590(&it, container, other, 0) > 0)
    {
        do
        {
            int key;

            // Original:
            // v12 ? v12 + 10608 : virtual_get(container, 663)

            int v12 = *(int*)((uint8_t*)container + 4);

            if (v12)
            {
                key = v12 + 10608;
            }
            else
            {
                using fn_virtual_get = int(__thiscall*)(void*, int);

                auto vtbl = *(void***)container;

                key =
                    ((fn_virtual_get)vtbl[1])(container, 663);
            }

            RValue value{};

            sub_addr_CC62D0(&value, key, ctx);

            ModLoader::LogRValue("Trait:", &value);

        } while (sub_addr_C9AC30(&it, container));
    }
    */

    // for (int i = 0; i < traits.arr->length; i++)
    //	ModLoader::LogRValue("Weapon Traits", &(traits.arr->data[i]));
}

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    g_api = api;
    g_moduleBase = (uintptr_t)GetModuleHandleA("Heat_Signature.exe");

    api->SubscribeHookPost("gml_Script_AcceptMission",           &OnAcceptMissionPost,           nullptr);
    api->SubscribeHookPost("gml_Script_PlayAsCharacter",         &OnPlayAsCharacterPost,         nullptr);
    api->SubscribeHookPost("gml_Script_PlayerIsDailyChallenger", &OnPlayerIsDailyChallengerPost, nullptr);
    api->SubscribeHookPost("gml_Script_GenerateGun",             &OnGenerateGunPost,             nullptr);

    Log("DebugMod", "Initialized (moduleBase=0x%p)", (void*)g_moduleBase);

    // PollDword("dword_10C3CEC", 0x10C3CEC, 5000);
}
