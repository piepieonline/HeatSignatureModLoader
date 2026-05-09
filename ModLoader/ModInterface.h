#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

#include "GameMaker.h"

extern "C" {

// Bump whenever any cross-ABI struct (HS_ModApi, HS_ModConfig, HS_ConfigString,
// or any callback signature reachable through them) changes shape. Mods compiled
// against a different value are rejected by the loader unless the user opts in
// via `allow_version_mismatch` in ModLoader.json.
#define HS_API_VERSION 1u

// Mods must export `ModApiVersion()` returning HS_API_VERSION at the version
// they were built for. The macro below is the canonical one-liner.
typedef uint32_t (*HS_ModApiVersionFn)(void);

#define HS_EXPORT_MOD_API_VERSION() \
    extern "C" __declspec(dllexport) uint32_t ModApiVersion(void) { return HS_API_VERSION; }

typedef void (*HS_LogFn)(const char* prefix, const char* message);

// Fires before a hooked function is invoked. Callbacks run on the game thread.
typedef void (*HS_HookCallback)(const char* hookName, uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv, void* userData);

// Fires after a hooked function returns. returnValue is the GML RValue* the
// function returned. Callbacks run on the game thread.
typedef void (*HS_HookPostCallback)(const char* hookName, uintptr_t* self, uintptr_t* other, RValue* returnValue, int argc, RValue** argv, void* userData);

typedef void (*HS_SubscribeHookFn)(const char* hookName,
                                   HS_HookCallback callback,
                                   void* userData);

typedef void (*HS_SubscribeHookPostFn)(const char* hookName,
                                       HS_HookPostCallback callback,
                                       void* userData);

// Returns 1.0 if the time-manager instance isn't available yet.
typedef double (*HS_GetTimeScaleFn)();

// Drawn from inside the host's per-frame ImGui::NewFrame / ::Render block,
// on the game's render thread. Keep work short.
typedef void (*HS_ImGuiDrawFn)(void* userData);
typedef void (*HS_RegisterImGuiDrawFn)(HS_ImGuiDrawFn callback, void* userData);

// Returns the host's ImGuiContext*. Mods must call ImGui::SetCurrentContext
// with this value once before issuing any ImGui calls so they share the
// host's globals (which would otherwise be per-DLL).
typedef void* (*HS_GetImGuiContextFn)();

// Outputs the host's ImGui allocator pair so mods can call
// ImGui::SetAllocatorFunctions and allocate from the same heap.
typedef void (*HS_GetImGuiAllocatorsFn)(void** allocFn, void** freeFn, void** userData);

// Returns the game's main window handle — largest visible, unowned,
// non-console window in the current process. May return nullptr before the
// game window is created.
typedef HWND (*HS_GetGameWindowFn)();

// Call from inside a HS_HookCallback to skip the hooked game function for
// this invocation. Has no effect when called outside a pre-hook callback.
typedef void (*HS_RequestBypassFn)();

// Invoke a `gml_Script_*` function by name. Routes through the installed hook
// so other mods' subscribers fire. Lazily installs the hook on first use.
// Returns `result` unchanged if the script name is not in the offset table or
// the hook cannot be installed.
typedef RValue* (*HS_CallScriptFn)(const char* scriptName,
                                   uintptr_t* self, uintptr_t* other,
                                   RValue* result, int argc, RValue** argv);

// Engine helpers — the four GameMaker built-ins mods most often need.
// ResolveInstance: turns the `argv[i]` instance reference into an integer handle
// suitable for GetVar/SetVar. GetVar/SetVar use GameMaker variable IDs (e.g.
// 673 = displayName on a weapon). SetString assigns a C string into an RValue.
typedef int (*HS_ResolveInstanceFn)(uint32_t* argHandle);
typedef int (*HS_GetVarFn)(int instance, int varId, int arrayIndex, RValue* out);
typedef int (*HS_SetVarFn)(int instance, int varId, int arrayIndex, RValue* in);
typedef int (*HS_SetStringFn)(RValue* dest, const char* text);

// Variable name -> ID lookup, populated at startup by hooking the engine's
// variable-table initializer. Returns -1 for unknown names (logged once).
typedef int (*HS_GetVarIdFn)(const char* name);

// Convenience wrappers around GetVar/SetVar that resolve the ID from a name.
// No-op (returns 0) if the name is unknown.
typedef int (*HS_GetVarByNameFn)(int instance, const char* name, int arrayIndex, RValue* out);
typedef int (*HS_SetVarByNameFn)(int instance, const char* name, int arrayIndex, RValue* in);

// Owning string returned by HS_ModConfig::ReadString / GetJson.
// `data` is heap-allocated by the host and null-terminated. The caller MUST
// release it by calling HS_ModConfig::FreeString — the C++ wrapper below
// (ModSettings) does this automatically. Multiple in-flight strings from the
// same handle do not alias each other.
struct HS_ConfigString {
    const char* data;
    size_t      length;
};

struct HS_ModConfig {
    void*           handle;
    HS_ConfigString (*ReadString)(void* handle, const char* key, const char* defaultValue);
    int             (*ReadBool)  (void* handle, const char* key, int      defaultValue);  // 0 / 1
    int64_t         (*ReadInt)   (void* handle, const char* key, int64_t  defaultValue);
    double          (*ReadDouble)(void* handle, const char* key, double   defaultValue);
    void            (*Write)     (void* handle, const char* key, const char* value);
    HS_ConfigString (*GetJson)   (void* handle);
    void            (*SetJson)   (void* handle, const char* json);
    void            (*Save)      (void* handle);
    void            (*FreeString)(HS_ConfigString s);
};

struct HS_ModApi
{
    HS_LogFn                 Log;
    HS_SubscribeHookFn       SubscribeHook;
    HS_SubscribeHookPostFn   SubscribeHookPost;
    HS_GetTimeScaleFn        GetTimeScale;
    HS_RegisterImGuiDrawFn   RegisterImGuiDraw;
    HS_GetImGuiContextFn     GetImGuiContext;
    HS_GetImGuiAllocatorsFn  GetImGuiAllocators;
    HS_GetGameWindowFn       GetGameWindow;
    HS_RequestBypassFn       RequestBypass;
    HS_CallScriptFn          CallScript;
    HS_ResolveInstanceFn     ResolveInstance;
    HS_GetVarFn              GetVar;
    HS_SetVarFn              SetVar;
    HS_SetStringFn           SetString;
    HS_GetVarIdFn            GetVarId;
    HS_GetVarByNameFn        GetVarByName;
    HS_SetVarByNameFn        SetVarByName;
    HS_ModConfig             config;
};

// Each mod DLL must export:
//     extern "C" __declspec(dllexport) void ModInit(const HS_ModApi* api);
typedef void (*HS_ModInitFn)(const HS_ModApi* api);

} // extern "C"

struct ModSettings
{
    ModSettings() = default;
    explicit ModSettings(const HS_ModConfig& cfg) : m_cfg(cfg) {}

    std::string ReadString(const char* key, const char* defaultVal = "") const
    {
        HS_ConfigString s = m_cfg.ReadString(m_cfg.handle, key, defaultVal);
        std::string out = s.data ? std::string(s.data, s.length) : std::string();
        m_cfg.FreeString(s);
        return out;
    }
    bool    ReadBool  (const char* key, bool    defaultVal = false) const { return m_cfg.ReadBool  (m_cfg.handle, key, defaultVal ? 1 : 0) != 0; }
    int64_t ReadInt   (const char* key, int64_t defaultVal = 0)     const { return m_cfg.ReadInt   (m_cfg.handle, key, defaultVal); }
    double  ReadDouble(const char* key, double  defaultVal = 0.0)   const { return m_cfg.ReadDouble(m_cfg.handle, key, defaultVal); }
    void Write(const char* key, const char* value)
        { m_cfg.Write(m_cfg.handle, key, value); }
    std::string GetJson() const
    {
        HS_ConfigString s = m_cfg.GetJson(m_cfg.handle);
        std::string out = s.data ? std::string(s.data, s.length) : std::string();
        m_cfg.FreeString(s);
        return out;
    }
    void SetJson(const std::string& json)
        { m_cfg.SetJson(m_cfg.handle, json.c_str()); }
    void SetJson(const char* json)
        { m_cfg.SetJson(m_cfg.handle, json); }
    void Save()
        { m_cfg.Save(m_cfg.handle); }

private:
    HS_ModConfig m_cfg{};
};
