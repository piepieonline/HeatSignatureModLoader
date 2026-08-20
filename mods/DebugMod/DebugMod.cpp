#include "ModInterface.h"
#include "GmArgs.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>

#include "PropertyNames.h"

#include <imgui.h>
#include <vector>
#include <fstream>
#include <unordered_set>
#include <HS/HS_Character.h>

HS_EXPORT_MOD_API_VERSION()

static const HS_ModApi* g_api = nullptr;
static uintptr_t        g_moduleBase = 0;

static bool   g_isDrawing = false;
static RValue g_nameVal;

static bool g_isChallengeCharacter = false;

// Item handles snapshotted from the player's Inventory at PlayAsCharacter time.
// OnInteractWithPre uses this to allow "Teleport to you" only for items the
// player genuinely owns, blocking cross-character/global pickups.
static std::unordered_set<int> g_knownInventoryIds;

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
    uint32_t* ctx = g_moduleBase + (uint32_t*)0x45ED614;

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

static void LogGMLArgs_SEH(const char* fnName, CInstance* self, int argc, RValue** argv, RValue* result)
{
    __try
    {
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

static void LogGMLCall(const char* fnName, CInstance* self, CInstance* other, int argc, RValue** argv, RValue* result = nullptr)
{
    Log("DebugMod", "%s argc=%d", fnName, argc);
    if (self)
    {
        std::string objectTypeName = self->GetObjectName(g_api);
        Log("DebugMod", "  self  addr=0x%08X  id=%d type=%s (%d)", (uint32_t)(uintptr_t)self, self->id, objectTypeName.c_str(), self->type);
    }
    if (other)
    {
        std::string objectTypeName = other->GetObjectName(g_api);
        Log("DebugMod", "  other  addr=0x%08X  id=%d type=%s (%d)", (uint32_t)(uintptr_t)other, other->id, objectTypeName.c_str(), other->type);
    }
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

static void DumpVars(int instance)
{
    for (const char* name : kPropertyNames)
    {
        RValue out{};
        g_api->GetVarByName(instance, name, 0x80000000, &out);

        if (GetTypeName(out.type) == std::string("UNKNOWN"))
            continue;

        char label[128];
        sprintf_s(label, "%s:", name);
        LogRValue(label, &out);
    }
}

static void OnAcceptMissionPost(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);

    int instance_handle = g_api->ResolveInstance((uint32_t*)argv[0]);
    CInstance* playerCInstance = g_api->ResolveCInstance(100140);
    std::string objectTypeName = playerCInstance->GetObjectName(g_api);
    Log("DebugMod", "  missionCInstance  addr=0x%08X  id=%d type=%s (%d)", (uint32_t)(uintptr_t)playerCInstance, playerCInstance->id, objectTypeName.c_str(), playerCInstance->type);

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

static void OnPlayAsCharacterPost(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
    HS::HS_Character character(argv[0]->real, g_api);
    // DumpVars(g_api->ResolveInstance((uint32_t*)argv[0]));
    // DumpVars(44);

    // auto a1 = g_api->ResolveInstance((uint32_t*)argv[0]);
    // auto a2 = GetActiveContext();

    /*
    RValue def1{};
    g_api->GetVar(100016, 26, 0x80000000, &def1);
    LogRValue("Defector 26", &def1);

    g_api->GetVar(100016, 27, 0x80000000, &def1);
    LogRValue("Defector 27", &def1);

    g_api->GetVar(100016, 28, 0x80000000, &def1);
    LogRValue("Defector 28", &def1);
    */

    int instance_handle = g_api->ResolveInstance((uint32_t*)argv[0]);

    // Log("DebugMod", "cinstance=0x%08X", reinterpret_cast<int(__cdecl*)(int)>(g_moduleBase + 0xCA7F30)(instance_handle));
    // Log("DebugMod", "cinstance=0x%08X", reinterpret_cast<int(__cdecl*)(int)>(g_moduleBase + 0xD25380)(instance_handle));
    CInstance* playerCInstance = g_api->ResolveCInstance(instance_handle);
    std::string objectTypeName = playerCInstance->GetObjectName(g_api);
    Log("DebugMod", "  playerCInstance  addr=0x%08X  id=%d type=%s (%d)", (uint32_t)(uintptr_t)playerCInstance, playerCInstance->id, objectTypeName.c_str(), playerCInstance->type);

    g_knownInventoryIds.clear();
    for (int i = 0; i < 256; ++i)
    {
        RValue slot{};
        if (!g_api->GetVarByName(instance_handle, "Inventory", i, &slot))
            break;
        if ((slot.type & 0xFF) != 0) // not REAL — past end / empty
            continue;
        int handle = static_cast<int>(slot.real);
        if (handle > 0)
            g_knownInventoryIds.insert(handle);
    }
    Log("DebugMod", "Snapshotted %zu inventory items for player %d",
        g_knownInventoryIds.size(), instance_handle);

    RValue challengeStation = character.ChallengeStation;
    LogRValue("ChallengeStation", &challengeStation);

    // If the challenge station is a real variable ID, it's a challenge mission
    g_isChallengeCharacter = character.ChallengeStation >= 100000;


    RValue DyingTimer = character.DyingTimer;
    LogRValue("DyingTimer", &DyingTimer);
    // {
        /*
        GmArgs args;
        args.AddReal(argv[0]->real);
        args.AddReal(32);
        auto bArgs = args.Build();

        RValue res{};
        auto oGalaxy = g_api->ResolveCInstance(100016);
        g_api->CallScript("gml_Script_AddCharacterTrait", playerCInstance, playerCInstance, &res, 2, bArgs);

        GmArgs emptyArgs;
        auto bEmptyArgs = emptyArgs.Build();
        g_api->CallScript("gml_Script_SortCharacterTraits", playerCInstance, playerCInstance, &res, 0, bEmptyArgs);
        */

        // character.SecondsUntilGlitchGrabberReady = 1000;
    // }

    /*
    for (int i = 0; i < 400; i++)
    {
        GmArgs oNArgs;
        oNArgs.AddReal(i);
        auto oNArgsBuilt = oNArgs.Build();
        RValue oname{};
        g_api->CallEngineScript("object_get_name", self, other, &oname, 1, oNArgsBuilt);
        LogRValue("Object name", &oname);
    }
    */

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

static void OnPlayerIsDailyChallengerPost(const char* /*hookName*/, CInstance* self, CInstance* other, RValue* /*returnValue*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    // LogGMLCall(hookName, self, other, argc, argv, returnValue);

    if (g_isDrawing)
    {
        /*
        RValue argX{}, argY{};
        argX.real = 100.0; argX.type = 0;
        argY.real = 200.0; argY.type = 0;

        RValue str{};
        str.type = 1;
        g_api->SetString(&str, "hello");

        RValue* argPtrs[3] = {
            &argX,
            &argY,
            &str,
        };

        g_api->CallEngineScript("draw_text", self, other, &resBuf, 3, argPtrs);
        */

        GmArgs drawTextArgs;
        drawTextArgs.AddReal(100);
        drawTextArgs.AddReal(200);
        drawTextArgs.AddStr(g_api, "test string");

        auto argv = drawTextArgs.Build();

        RValue resBuf{};
        g_api->CallEngineScript("draw_text", self, other, &resBuf, drawTextArgs.Count(), argv);

        GmArgs a;

        a.AddReal(100);
        a.AddReal(200);
        a.AddReal(300);
        a.AddReal(400);
        a.AddReal(1); // outline

        RValue result{};

        auto argvRect = a.Build();

        g_api->CallEngineScript(
            "draw_rectangle",
            self,
            other,
            &result,
            a.Count(),
            argvRect
        );
    }
}

static void gml_Script_CameraPanToXY(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_LoadGalaxy(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_DrawInventoryList(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_DescriptionOfTrait(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_AddTraitIfNotPresent(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_ObjectiveIsInPlayersInventory(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_KillEnemy(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

static void gml_Script_AddCharacterTrait(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
    HS::HS_Character character(argv[0]->real, g_api);
    std::string name = character.Name;

    Log("DebugMod", "Adding trait to %s", name.c_str());

    const uintptr_t mgrPtr =
        *reinterpret_cast<uintptr_t*>(g_moduleBase + 0x453D610);
    if (!mgrPtr) return;

    // vtbl[1] is a __thiscall property-getter that returns a pointer to the
    // requested RValue slot regardless of whether the inner CInstance is
    // bound. See gml_Script_ConfigureCharacterTrait decomp lines 53/62/77/87.
    const uintptr_t vtbl = *reinterpret_cast<uintptr_t*>(mgrPtr);
    using GetPropFn = void* (__thiscall*)(uintptr_t self, int propId);
    auto getProp = reinterpret_cast<GetPropFn>(
        *reinterpret_cast<uintptr_t*>(vtbl + 4));

    void* namesSlot = getProp(mgrPtr, 65); // CharacterTraitNames

    // sub_CBBEA0(slot, intKey) — engine's array-element accessor, used
    // throughout ConfigureCharacterTrait (lines 112/125/138/151).
    using ArrayLookupFn = RValue* (__cdecl*)(void* slot, int index);
    auto arrayLookup = reinterpret_cast<ArrayLookupFn>(
        g_moduleBase + 0xCBBEA0);

    const int traitNum = static_cast<int>(argv[1]->real);
    RValue* nameRV = arrayLookup(namesSlot, traitNum);

    const char* traitNameStr =
        (nameRV && nameRV->str && nameRV->str->text)
        ? nameRV->str->text
        : "(unknown)";

    Log("DebugMod", "AddCharacterTrait: %d -> %s", traitNum, traitNameStr);
}

static void gml_Script_SetRes(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    // argv[0]->real = 1920.0;
    // argv[1]->real = 1080.0;
    LogGMLCall(hookName, self, other, argc, argv, returnValue);
}

// Pre-hook for gml_Script_InteractWith. Blocks the call when the target is
// farther from `self` than self.Radius — i.e. the "Teleport to you" beam.
// Mirrors the same `distance > radius` test the inventory UI uses to flip the
// button label from "Take" to "Teleport to you". When the player is close
// enough to physically grab the item (Take), the call passes through unchanged.
static void OnInteractWithPre(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    if (!g_isChallengeCharacter)
    {
        return;
    }

    LogGMLCall(hookName, self, other, argc, argv, returnValue);

        int targetHandle = g_api->ResolveInstance((uint32_t*)argv[0]);

        /*
        if (targetHandle <= 0) return;

        RValue px{}, py{}, pr{}, tx{}, ty{};
        if (!g_api->GetVarByName((int)self->id, "x",      0x80000000, &px)) return;
        if (!g_api->GetVarByName((int)self->id, "y",      0x80000000, &py)) return;
        if (!g_api->GetVarByName((int)self->id, "Radius", 0x80000000, &pr)) return;
        if (!g_api->GetVarByName(targetHandle,  "x",      0x80000000, &tx)) return;
        if (!g_api->GetVarByName(targetHandle,  "y",      0x80000000, &ty)) return;

        // Only act when all five vars are REAL (0). Anything else means we're
        // not in a normal player-vs-handle pickup and we shouldn't interfere.
        if ((px.type & 0xFF) != 0 || (py.type & 0xFF) != 0 ||
            (pr.type & 0xFF) != 0 || (tx.type & 0xFF) != 0 || (ty.type & 0xFF) != 0)
            return;

        const double dx = tx.real - px.real;
        const double dy = ty.real - py.real;
        const double r  = pr.real;

        if (dx * dx + dy * dy > r * r)
        {
            Log("DebugMod", "InteractWith blocked: dist^2=%.1f > radius^2=%.1f (target=%d)",
                dx*dx + dy*dy, r*r, targetHandle);
            g_api->RequestBypass();
        }
        */
        auto interactiveItem = g_api->ResolveCInstance(argv[0]->real);

        if (self->type == 137 && interactiveItem->type == 102)
        {
            if (g_knownInventoryIds.find(interactiveItem->id) == g_knownInventoryIds.end())
            {
                Log("DebugMod", "InteractWith blocked: item %d not in player's starting inventory (snapshot size=%zu)", interactiveItem->id, g_knownInventoryIds.size());
                g_api->RequestBypass();
            }
            else
            {
                Log("DebugMod", "InteractWith allowed: item %d is in player's starting inventory", interactiveItem->id);
            }
        }
}

static void OnGenerateGunPost(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, other, argc, argv, returnValue);

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



static void DumpTypeProperties(int id)
{
    CInstance* inst = g_api->ResolveCInstance(id);
    if (!inst)
    {
        Log("DebugMod", "DumpTypeProperties: instance %d not resolvable", id);
        return;
    }

    std::string typeName = inst->GetObjectName(g_api);
    if (typeName.empty())
    {
        Log("DebugMod", "DumpTypeProperties: instance %d has no object name (type=%u)", id, inst->type);
        return;
    }

    CreateDirectoryA("D:\\temp", nullptr);
    CreateDirectoryA("D:\\temp\\hs_types", nullptr);

    char path[MAX_PATH];
    sprintf_s(path, "D:\\temp\\hs_types\\HS_%s.h", typeName.c_str());

    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
    {
        Log("DebugMod", "DumpTypeProperties: %s already exists, skipping", path);
        return;
    }

    std::ofstream out(path);
    if (!out)
    {
        Log("DebugMod", "DumpTypeProperties: failed to open %s", path);
        return;
    }

    out << "#pragma once\n"
        << "#include \"Instance.h\"\n\n"
        << "namespace HS {\n\n"
        << "class HS_" << typeName << " : public Instance\n"
        << "{\n"
        << "public:\n"
        << "    using Instance::Instance;\n\n";

    int kept = 0;
    for (const char* name : kPropertyNames)
    {
        RValue v{};
        g_api->GetVarByName(id, name, 0x80000000, &v);

        int type = v.type & 0xFF;
        const char* propType = nullptr;
        switch (type)
        {
        case 0: // REAL
        case 5: // INT
        case 6: // BOOL
            propType = "RealProperty   ";
            break;
        case 1: // STRING
            propType = "StringProperty ";
            break;
        case 2: // ARRAY
        case 3: // PTR
            propType = "RValueProperty ";
            break;
        default:
            continue; // skip UNKNOWN / UNDEFINED
        }

        out << "    " << propType << " " << name
            << "{ this, \"" << name << "\" };\n";
        ++kept;
    }

    out << "\n};\n\n"
        << "} // namespace HS\n";

    Log("DebugMod", "DumpTypeProperties: wrote %s (%d properties)", path, kept);
}

static void OnImGuiDraw(void* /*userData*/)
{
    ImGui::Begin("DebugMod");
    static int s_dumpId = 100016;
    ImGui::InputInt("##DumpTypePropertiesId", &s_dumpId);
    ImGui::SameLine();
    if (ImGui::Button("DumpTypeProperties"))
    {
        DumpTypeProperties(s_dumpId);
    }
    if (ImGui::Button("DumpAllVars"))
    {
        for(int i = 100000; i < 105000; i++)
            DumpTypeProperties(i);
    }
    static int s_getVarInstance = 100016;
    static int s_getVarPropId   = 0;
    ImGui::InputInt("Instance##GetVarInstance", &s_getVarInstance);
    ImGui::InputInt("PropertyId##GetVarPropId", &s_getVarPropId);
    if (ImGui::Button("GetVar"))
    {
        RValue out{};
        g_api->GetVar(s_getVarInstance, s_getVarPropId, 0x80000000, &out);
        char label[64];
        sprintf_s(label, "instance=%d propId=%d:", s_getVarInstance, s_getVarPropId);
        LogRValue(label, &out);
    }
    if (ImGui::Button("Move Camera"))
    {
        RValue result{};
        // g_api->CallScript("gml_Script_EndManualZoom", 0, 0, &result, 0, nullptr);

        /*
        RValue argZoom{}, argPrioirity{};
        argZoom.real = 100.0; argZoom.type = 0;
        argPrioirity.real = 200.0; argPrioirity.type = 0;

        RValue* argPtrs[2] = {
            &argZoom,
            & argPrioirity
        };


        g_api->CallScript("gml_Script_ZoomToViewSize", 0, 0, &result, 0, argPtrs);
        */

        g_isDrawing = true;
        /*
        int count = *(int*)(g_moduleBase + 0x4545368);
        for (int i = 0; i < count; i++)
        {
            char* entry = *((char**)(g_moduleBase + 0x4545364) + i * 80);

            if (!strcmp(entry, "@@GlobalScope@@"))
            {
                // found it
                Log("DebugMod", "Found global scope");
            }
        }
        */

        /*
        RValue argX{}, argY{}, argUkn{};
        argX.real = -443467.687500; argX.type = 0;
        argY.real = -10078.003906; argY.type = 0;
        argUkn.real = 400000; argUkn.type = 0;

        RValue* argPtrs[3] = {
            &argX,
            &argY,
            &argUkn
        };

        g_api->CallScript("gml_Script_CameraPanToXY", GetActiveContext(), GetActiveContext(), &result, 0, argPtrs);
        */

        /*
        RValue x{};
        x.real = 0;
        g_api->SetVarByName(44, "CenterX", 0, &x);
        */
    }

    ImGui::End();
}

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    g_api = api;
    g_moduleBase = (uintptr_t)GetModuleHandleA("Heat_Signature.exe");

    void* alloc = nullptr; void* freeFn = nullptr; void* ud = nullptr;
    api->GetImGuiAllocators(&alloc, &freeFn, &ud);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)alloc, (ImGuiMemFreeFunc)freeFn, ud);
    ImGui::SetCurrentContext((ImGuiContext*)api->GetImGuiContext());

    api->SubscribeHookPost("gml_Script_AcceptMission",           &OnAcceptMissionPost,           nullptr);
    api->SubscribeHookPost("gml_Script_PlayAsCharacter",         &OnPlayAsCharacterPost,         nullptr);
    api->SubscribeHookPost("gml_Script_PlayerIsDailyChallenger", &OnPlayerIsDailyChallengerPost, nullptr);
    // api->SubscribeHookPost("gml_Script_GenerateGun",             &OnGenerateGunPost,             nullptr);
    api->SubscribeHook("gml_Script_SetRes",             &gml_Script_SetRes,             nullptr);
    api->SubscribeHookPost("gml_Script_AddCharacterTrait",             &gml_Script_AddCharacterTrait,             nullptr);
    api->SubscribeHookPost("gml_Script_AddTraitIfNotPresent",             &gml_Script_AddTraitIfNotPresent,             nullptr);
    api->SubscribeHookPost("gml_Script_KillEnemy",             &gml_Script_KillEnemy,             nullptr);
    // api->SubscribeHookPost("gml_Script_ObjectiveIsInPlayersInventory",             &gml_Script_ObjectiveIsInPlayersInventory,             nullptr);
    api->SubscribeHook    ("gml_Script_InteractWith",                     &OnInteractWithPre,                           nullptr);
    // api->SubscribeHookPost("gml_Script_DescriptionOfTrait",             &gml_Script_DescriptionOfTrait,             nullptr); // Takes trait name, returns trait desc (RValue strings)
    // api->SubscribeHook("gml_Script_AnnotateCharacter",             &gml_Script_DrawInventoryList,             nullptr);
    // api->SubscribeHookPost("gml_Script_CameraPanToXY",             &gml_Script_CameraPanToXY,             nullptr);
    api->RegisterImGuiDraw(&OnImGuiDraw, nullptr);

    Log("DebugMod", "Initialized (moduleBase=0x%p)", (void*)g_moduleBase);

    // PollDword("dword_10C3CEC", 0x10C3CEC, 5000);
}
