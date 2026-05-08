#pragma once
#include <windows.h>
#include <string>

#include "GameMaker.h"

extern "C" {

typedef void (*SE_LogFn)(const char* prefix, const char* message);

// Fires before a hooked function is invoked. Callbacks run on the game thread.
typedef void (*SE_HookCallback)(const char* hookName, uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv, void* userData);

// Fires after a hooked function returns. returnValue is the GML RValue* the
// function returned. Callbacks run on the game thread.
typedef void (*SE_HookPostCallback)(const char* hookName, uintptr_t* self, uintptr_t* other, RValue* returnValue, int argc, RValue** argv, void* userData);

typedef void (*SE_SubscribeHookFn)(const char* hookName,
                                   SE_HookCallback callback,
                                   void* userData);

typedef void (*SE_SubscribeHookPostFn)(const char* hookName,
                                       SE_HookPostCallback callback,
                                       void* userData);

// Returns 1.0 if the time-manager instance isn't available yet.
typedef double (*SE_GetTimeScaleFn)();

// Drawn from inside the host's per-frame ImGui::NewFrame / ::Render block,
// on the game's render thread. Keep work short.
typedef void (*SE_ImGuiDrawFn)(void* userData);
typedef void (*SE_RegisterImGuiDrawFn)(SE_ImGuiDrawFn callback, void* userData);

// Returns the host's ImGuiContext*. Mods must call ImGui::SetCurrentContext
// with this value once before issuing any ImGui calls so they share the
// host's globals (which would otherwise be per-DLL).
typedef void* (*SE_GetImGuiContextFn)();

// Outputs the host's ImGui allocator pair so mods can call
// ImGui::SetAllocatorFunctions and allocate from the same heap.
typedef void (*SE_GetImGuiAllocatorsFn)(void** allocFn, void** freeFn, void** userData);

// Returns the game's main window handle — largest visible, unowned,
// non-console window in the current process. May return nullptr before the
// game window is created.
typedef HWND (*SE_GetGameWindowFn)();

// Call from inside a SE_HookCallback to skip the hooked game function for
// this invocation. Has no effect when called outside a pre-hook callback.
typedef void (*SE_RequestBypassFn)();

// Invoke a `gml_Script_*` function by name. Routes through the installed hook
// so other mods' subscribers fire. Lazily installs the hook on first use.
// Returns `result` unchanged if the script name is not in the offset table or
// the hook cannot be installed.
typedef RValue* (*SE_CallScriptFn)(const char* scriptName,
                                   uintptr_t* self, uintptr_t* other,
                                   RValue* result, int argc, RValue** argv);

struct SE_ModConfig {
    void*        handle;
    const char* (*Read)   (void* handle, const char* key, const char* defaultValue);
    void        (*Write)  (void* handle, const char* key, const char* value);
    const char* (*GetJson)(void* handle);
    void        (*SetJson)(void* handle, const char* json);
    void        (*Save)   (void* handle);
};

struct SE_ModApi
{
    SE_LogFn                 Log;
    SE_SubscribeHookFn       SubscribeHook;
    SE_SubscribeHookPostFn   SubscribeHookPost;
    SE_GetTimeScaleFn        GetTimeScale;
    SE_RegisterImGuiDrawFn   RegisterImGuiDraw;
    SE_GetImGuiContextFn     GetImGuiContext;
    SE_GetImGuiAllocatorsFn  GetImGuiAllocators;
    SE_GetGameWindowFn       GetGameWindow;
    SE_RequestBypassFn       RequestBypass;
    SE_CallScriptFn          CallScript;
    SE_ModConfig             config;
};

// Each mod DLL must export:
//     extern "C" __declspec(dllexport) void ModInit(const SE_ModApi* api);
typedef void (*SE_ModInitFn)(const SE_ModApi* api);

} // extern "C"

struct ModSettings
{
    ModSettings() = default;
    explicit ModSettings(const SE_ModConfig& cfg) : m_cfg(cfg) {}

    const char* Read(const char* key, const char* defaultVal = "") const
        { return m_cfg.Read(m_cfg.handle, key, defaultVal); }
    void Write(const char* key, const char* value)
        { m_cfg.Write(m_cfg.handle, key, value); }
    std::string GetJson() const
        { return std::string(m_cfg.GetJson(m_cfg.handle)); }
    void SetJson(const std::string& json)
        { m_cfg.SetJson(m_cfg.handle, json.c_str()); }
    void SetJson(const char* json)
        { m_cfg.SetJson(m_cfg.handle, json); }
    void Save()
        { m_cfg.Save(m_cfg.handle); }

private:
    SE_ModConfig m_cfg{};
};
