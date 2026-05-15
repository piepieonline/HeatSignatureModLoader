#include "ModInterface.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>

#include "PropertyNames.h"

#include <imgui.h>
#include <vector>
#include <filesystem>
#include <HS/HS_Character.h>

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

static void LogGMLCall(const char* fnName, CInstance* self, int argc, RValue** argv, RValue* result = nullptr)
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

static void OnAcceptMissionPost(const char* hookName, CInstance* self, CInstance* /*other*/, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
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

static void OnPlayAsCharacterPost(const char* hookName, CInstance* self, CInstance* /*other*/, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, argc, argv, returnValue);

    // DumpVars(g_api->ResolveInstance((uint32_t*)argv[0]));
    // DumpVars(44);

    // auto a1 = g_api->ResolveInstance((uint32_t*)argv[0]);
    // auto a2 = GetActiveContext();

    int instance_handle = g_api->ResolveInstance((uint32_t*)argv[0]);
    Log("DebugMod", "instance=0x%08X", instance_handle);

    RValue BleedOutTime{};
    g_api->GetVar(instance_handle, 704, 0x80000000, &BleedOutTime);
    RValue SecondsUntilBleedOut{};
    g_api->GetVar(instance_handle, 705, 0x80000000, &SecondsUntilBleedOut);

    LogRValue("BleedOutTime", &BleedOutTime);
    LogRValue("SecondsUntilBleedOut", &SecondsUntilBleedOut);

    // Log("DebugMod", "cinstance=0x%08X", reinterpret_cast<int(__cdecl*)(int)>(g_moduleBase + 0xCA7F30)(instance_handle));
    // Log("DebugMod", "cinstance=0x%08X", reinterpret_cast<int(__cdecl*)(int)>(g_moduleBase + 0xD25380)(instance_handle));
    Log("DebugMod", "cinstance=0x%08X", reinterpret_cast<int(__cdecl*)(int)>(g_moduleBase + 0xC98DF0)(instance_handle));

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

struct GmArg
{
    RValue value;

    static GmArg Real(double v)
    {
        GmArg a;
        a.value.type = 0;
        a.value.real = v;
        return a;
    }

    static GmArg Str(const HS_ModApi* api, const char* s)
    {
        GmArg a;
        a.value.type = 1;
        api->SetString(&a.value, s);
        return a;
    }
};

class GmArgs
{
public:
    std::vector<RValue> values;
    std::vector<RValue*> ptrs;

    void AddReal(double v)
    {
        RValue r{};
        r.type = 0;
        r.real = v;
        values.push_back(r);
    }

    void AddStr(const HS_ModApi* api, const char* s)
    {
        RValue r{};
        r.type = 1;
        api->SetString(&r, s);
        values.push_back(r);
    }

    RValue** Build()
    {
        ptrs.resize(values.size());

        for (size_t i = 0; i < values.size(); i++)
            ptrs[i] = &values[i];

        return ptrs.data();
    }

    int Count() const
    {
        return (int)values.size();
    }
};

static void OnPlayerIsDailyChallengerPost(const char* /*hookName*/, CInstance* self, CInstance* other, RValue* /*returnValue*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    // LogGMLCall(hookName, self, argc, argv, returnValue);

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
    LogGMLCall(hookName, self, argc, argv, returnValue);
}

static void gml_Script_LoadGalaxy(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, argc, argv, returnValue);
}

static void gml_Script_DrawInventoryList(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    LogGMLCall(hookName, self, argc, argv, returnValue);
}

static void gml_Script_DrawMissionRating(const char* hookName, CInstance* self, CInstance* other, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
{
    // LogGMLCall(hookName, self, argc, argv, returnValue);

    std::string text = argv[0]->str->text;

    if ((text.find("Injuries Suffered") != std::string::npos) || // On lifetime stats
    (text.find("Injuries suffered") != std::string::npos)) // On mission stats
    {
        // Log("DebugMod", "Found glory score");

        // (HS_Character)
        CInstance* selfC = reinterpret_cast<CInstance*>(self);

        // Log("DebugMod", "Self ID: %d", selfC->id);

        auto character = HS::ResolveInstanceAs<HS::HS_Character>(selfC->id, g_api);
        if (!character.valid()) return;


        /*
        RValue BleedOutTime{};
        g_api->GetVar(selfC->id, 704, 0x80000000, &BleedOutTime);
        a.AddStr(g_api, ("Hello: " + std::to_string(BleedOutTime.real)).c_str());
        */
        RValue bleedTimeArg{};
        bleedTimeArg.type = 0;
        bleedTimeArg.real = character.BleedOutTime;

        RValue* timeArgv[1] = { &bleedTimeArg };

        RValue bleedTimeStr{};
        g_api->CallScript(
            "gml_Script_MinutesAndSeconds",
            self,
            other,
            &bleedTimeStr,
            1,
            timeArgv
        );

        const char* timeText = (bleedTimeStr.str && bleedTimeStr.str->text)
            ? bleedTimeStr.str->text
            : "?";

        GmArgs a;

        a.AddStr(g_api, (std::string("Bleed Out Time: ") + timeText).c_str());
        a.AddReal(argv[1]->real);

        RValue result{};

        auto argvRect = a.Build();

        g_api->CallScript(
            "gml_Script_DrawMissionRating",
            self,
            other,
            &result,
            a.Count(),
            argvRect
        );
    }
}

static void OnGenerateGunPost(const char* hookName, CInstance* self, CInstance* /*other*/, RValue* returnValue, int argc, RValue** argv, void* /*userData*/)
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

static std::vector<std::string> g_galaxyFolders;

static void LoadGalaxy(std::string name)
{
    GmArgs a;

    a.AddStr(g_api, (name + "\\").c_str());

    RValue result{};

    auto argvRect = a.Build();

    g_api->CallScript(
        "gml_Script_LoadGalaxy",
        0,
        0,
        &result,
        a.Count(),
        argvRect
    );
}

static void RefreshGalaxyList()
{
    g_galaxyFolders.clear();

    // %APPDATA%
    const char* appData = std::getenv("APPDATA");
    if (!appData)
        return;

    std::filesystem::path heatSignaturePath = std::filesystem::path(appData) / "Heat_Signature";

    if (!std::filesystem::exists(heatSignaturePath) || !std::filesystem::is_directory(heatSignaturePath))
        return;

    // Iterate all subfolders
    for (const auto& entry : std::filesystem::directory_iterator(heatSignaturePath))
    {
        if (!entry.is_directory())
            continue;

        std::filesystem::path galaxyFile = entry.path() / "Galaxy.txt";

        // Check if Galaxy.txt exists
        if (std::filesystem::exists(galaxyFile) && std::filesystem::is_regular_file(galaxyFile))
        {
            // Store folder name only
            g_galaxyFolders.push_back(entry.path().filename().string());
        }
    }
}

static void OnImGuiDraw(void* /*userData*/)
{
    ImGui::Begin("DebugMod");
    if (ImGui::Button("DumpVars()"))
    {
        /*
        DumpVars(20);
        DumpVars(25);
        DumpVars(26);
        DumpVars(41);
        DumpVars(44);
        */
        // 
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
    
    ImGui::Begin("Load Galaxy");

    if (ImGui::Button("Load Galaxy List"))
    {
        RefreshGalaxyList();
    }

    ImGui::Separator();

    for (const std::string& folderName : g_galaxyFolders)
    {
        if (ImGui::Button(folderName.c_str()))
        {
            LoadGalaxy(folderName);
        }
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
    api->SubscribeHookPost("gml_Script_GenerateGun",             &OnGenerateGunPost,             nullptr);
    api->SubscribeHookPost("gml_Script_LoadGalaxy",             &gml_Script_LoadGalaxy,             nullptr);
    api->SubscribeHookPost("gml_Script_DrawMissionRating",             &gml_Script_DrawMissionRating,             nullptr);
    // api->SubscribeHook("gml_Script_AnnotateCharacter",             &gml_Script_DrawInventoryList,             nullptr);
    // api->SubscribeHookPost("gml_Script_CameraPanToXY",             &gml_Script_CameraPanToXY,             nullptr);
    api->RegisterImGuiDraw(&OnImGuiDraw, nullptr);

    Log("DebugMod", "Initialized (moduleBase=0x%p)", (void*)g_moduleBase);

    // PollDword("dword_10C3CEC", 0x10C3CEC, 5000);
}
