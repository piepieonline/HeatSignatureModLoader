#include "ModLoader.h"
#include "ModInterface.h"
#include "ImGuiHook.h"
#include "GameWindow.h"
#include "ModConfig.h"
#include "UniversalHooks.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>

#include "MinHook.h"

static SE_ConfigString MakeOwnedString(const std::string& s)
{
    SE_ConfigString out{};
    char* buf = new char[s.size() + 1];
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    out.data   = buf;
    out.length = s.size();
    return out;
}

static SE_ConfigString Config_Read      (void* h, const char* key, const char* def) { return MakeOwnedString(static_cast<ModConfig*>(h)->Read(key, def)); }
static void            Config_Write     (void* h, const char* key, const char* val) { static_cast<ModConfig*>(h)->Write(key, val); }
static SE_ConfigString Config_GetJson   (void* h)                                   { return MakeOwnedString(static_cast<ModConfig*>(h)->GetJson()); }
static void            Config_SetJson   (void* h, const char* json)                 { static_cast<ModConfig*>(h)->SetJson(json); }
static void            Config_Save      (void* h)                                   { static_cast<ModConfig*>(h)->Save(); }
static void            Config_FreeString(SE_ConfigString s)                         { delete[] const_cast<char*>(s.data); }

std::map<std::string, HookBase*> ModLoader::HookMap{};
std::unordered_map<std::string, uint32_t> ModLoader::VariableMap{};

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

ModLoader& ModLoader::Instance()
{
    static ModLoader instance;
    return instance;
}

ModLoader::ModLoader()
{
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

    if (m_loaderConfig->Read("show_console", false) == "true")
    {
        AllocConsole();
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONIN$", "r", stdin);
    }

    Log("ModLoader", "Attached");

    CreateHooks();
    ImGuiHook::Install();

    {
        ImGuiHook::SetVisible(m_loaderConfig->Read("imgui_visible_default", false) == "true");
        std::string toggleKeyStr = m_loaderConfig->Read("imgui_toggle_key", static_cast<int64_t>(VK_F7));
        int toggleKey = static_cast<int>(std::strtol(toggleKeyStr.c_str(), nullptr, 0));

        struct ToggleArgs { int vk; };
        auto* args = new ToggleArgs{ toggleKey };
        HANDLE thread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            auto* a = static_cast<ToggleArgs*>(p);
            bool down = false;
            while (true)
            {
                bool now = (GetAsyncKeyState(a->vk) & 0x8000) != 0;
                if (now && !down)
                    ImGuiHook::ToggleVisible();
                down = now;
                Sleep(50);
            }
            return 0;
        }, args, 0, nullptr);
        if (thread) CloseHandle(thread);
    }

    LoadMods();
}

void ModLoader::Log(const char* prefix, const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    static std::mutex s_logMutex;
    static std::ofstream s_logFile("ModLoader.log", std::ios::app);

    std::lock_guard<std::mutex> lock(s_logMutex);
    std::cout << "[" << prefix << "] " << buffer << std::endl;
    if (s_logFile.is_open())
        s_logFile << "[" << prefix << "] " << buffer << std::endl;
}

// Ensures the MinHook patch is in place. No-op if already installed.
// Returns true if the hook is usable (originalFunction is non-null) afterwards.
static bool EnsureHookInstalled(HookBase* base)
{
    if (base->reference.originalFunction)
        return true;

    static std::mutex s_installMutex;
    std::lock_guard<std::mutex> guard(s_installMutex);

    if (base->reference.originalFunction)
        return true;

    base->CreateHook();
    return base->reference.originalFunction != nullptr;
}

void ModLoader::SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData)
{
    auto it = HookMap.find(hookName);
    if (it == HookMap.end())
    {
        Log("ModLoader", "Subscribe failed : unknown hook '%s'", hookName);
        return;
    }
    if (!EnsureHookInstalled(it->second))
    {
        Log("ModLoader", "Subscribe failed : install of '%s' failed", hookName);
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
    if (!EnsureHookInstalled(it->second))
    {
        Log("ModLoader", "SubscribePost failed: install of '%s' failed", hookName);
        return;
    }
    it->second->postSubscribers.emplace_back(callback, userData);
    Log("ModLoader", "Post-subscriber registered for %s", hookName);
}

RValue* ModLoader::CallScript(const char* scriptName,
    uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv)
{
    auto it = HookMap.find(scriptName);
    if (it == HookMap.end())
    {
        Log("ModLoader", "CallScript: unknown script '%s'", scriptName);
        return result;
    }
    if (!EnsureHookInstalled(it->second))
    {
        Log("ModLoader", "CallScript: install of '%s' failed", scriptName);
        return result;
    }
    auto detour = reinterpret_cast<GMLScript_t>(it->second->reference.hookFunction);
    return detour(self, other, result, argc, argv);
}

// The engine stores a pointer to the variable-table struct at moduleBase +
// 0x453D5B8, populated by sub_C7F050. struct[4] = entry count, struct[5] =
// pointer to the PropertyDesc* array. Read lazily on first lookup.
void ModLoader::EnsureVariableMap()
{
    static std::atomic<bool> s_built{ false };
    if (s_built.load(std::memory_order_acquire)) return;

    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_built.load()) return;

    auto structPtr = *reinterpret_cast<uint32_t**>(HookBase::moduleBase + 0x453D5B8);
    if (!structPtr)
    {
        Log("ModLoader", "VariableMap: struct at moduleBase+0x453D5B8 not yet populated");
        return;
    }

    uintptr_t tableAddr = static_cast<uintptr_t>(structPtr[5]);
    uint32_t  count     = structPtr[4];

    auto map = BuildMap(tableAddr, count);
    if (map.empty())
    {
        Log("ModLoader", "VariableMap: BuildMap returned empty (table=0x%p count=%u)",
            reinterpret_cast<void*>(tableAddr), count);
        return;
    }

    VariableMap = std::move(map);
    s_built.store(true, std::memory_order_release);
    Log("ModLoader", "VariableMap: built %zu entries from 0x%p (count=%u)",
        VariableMap.size(), reinterpret_cast<void*>(tableAddr), count);
}

int ModLoader::GetVarId(const char* name)
{
    if (!name) return -1;

    EnsureVariableMap();

    auto it = VariableMap.find(name);
    if (it != VariableMap.end()) return static_cast<int>(it->second);

    static std::mutex s_missMutex;
    static std::unordered_set<std::string> s_logged;
    std::lock_guard<std::mutex> lock(s_missMutex);
    if (s_logged.insert(name).second)
        Log("ModLoader", "GetVarId: unknown variable '%s'", name);
    return -1;
}

void ModLoader::LoadMods()
{
    CreateDirectoryA(".\\mods", nullptr);

    const bool allowVersionMismatch =
        m_loaderConfig->Read("allow_version_mismatch", false) == "true";

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(".\\mods\\*.dll", &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("ModLoader", "No mods found in .\\mods\\");
        return;
    }

    do {
        Log("ModLoader", "Loading %s", fd.cFileName);
        std::string path = std::string(".\\mods\\") + fd.cFileName;
        HMODULE hMod = LoadLibraryA(path.c_str());
        if (!hMod)
        {
            Log("ModLoader", "Failed to load %s (error %lu)", fd.cFileName, GetLastError());
            continue;
        }

        auto modVersionFn = reinterpret_cast<SE_ModApiVersionFn>(GetProcAddress(hMod, "ModApiVersion"));
        uint32_t modVersion = modVersionFn ? modVersionFn() : 0u;
        if (modVersion != SE_API_VERSION)
        {
            if (!allowVersionMismatch)
            {
                Log("ModLoader",
                    "Skipping %s: built for API v%u, loader is v%u "
                    "(set allow_version_mismatch=true to override)",
                    fd.cFileName, modVersion, SE_API_VERSION);
                FreeLibrary(hMod);
                continue;
            }
            Log("ModLoader",
                "WARNING: loading %s with API mismatch (mod v%u, loader v%u)",
                fd.cFileName, modVersion, SE_API_VERSION);
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
        SE_ModConfig modConfigApi = { modConfigObj.get(), Config_Read, Config_Write, Config_GetJson, Config_SetJson, Config_Save, Config_FreeString };

        auto modApi = std::make_unique<SE_ModApi>(SE_ModApi{
            +[](const char* prefix, const char* msg) { ModLoader::Log(prefix, msg); },
            +[](const char* hookName, SE_HookCallback cb, void* userData) {
                ModLoader::SubscribeHook(hookName, cb, userData);
            },
            +[](const char* hookName, SE_HookPostCallback cb, void* userData) {
                ModLoader::SubscribeHookPost(hookName, cb, userData);
            },
            &ModLoader::GetTimeScale,
            +[](SE_ImGuiDrawFn cb, void* userData) {
                ImGuiHook::RegisterDraw(cb, userData);
            },
            +[]() -> void* { return ImGuiHook::GetContext(); },
            +[](void** a, void** f, void** ud) { ImGuiHook::GetAllocators(a, f, ud); },
            &FindGameWindow,
            +[]() { g_hookBypassRequested = true; },
            +[](const char* name, uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv) {
                return ModLoader::CallScript(name, self, other, result, argc, argv);
            },
            reinterpret_cast<SE_ResolveInstanceFn>(HookBase::moduleBase + 0xCBC420),
            reinterpret_cast<SE_GetVarFn>         (HookBase::moduleBase + 0xC99410),
            reinterpret_cast<SE_SetVarFn>         (HookBase::moduleBase + 0xC996F0),
            reinterpret_cast<SE_SetStringFn>      (HookBase::moduleBase + 0xCAB130),
            +[](const char* name) { return ModLoader::GetVarId(name); },
            +[](int instance, const char* name, int arrayIndex, RValue* out) -> int {
                int id = ModLoader::GetVarId(name);
                if (id < 0) { if (out) *out = RValue{}; return 0; }
                using GetVar_t = int(__cdecl*)(int, int, int, RValue*);
                auto fn = reinterpret_cast<GetVar_t>(HookBase::moduleBase + 0xC99410);
                return fn(instance, id, arrayIndex, out);
            },
            +[](int instance, const char* name, int arrayIndex, RValue* in) -> int {
                int id = ModLoader::GetVarId(name);
                if (id < 0) return 0;
                using SetVar_t = int(__cdecl*)(int, int, int, RValue*);
                auto fn = reinterpret_cast<SetVar_t>(HookBase::moduleBase + 0xC996F0);
                return fn(instance, id, arrayIndex, in);
            },
            modConfigApi
        });

        modInit(modApi.get());
        m_modApis.push_back(std::move(modApi));

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

    InstallUniversalHookEntries();

    Log("ModLoader", "Hooks installed");
}
