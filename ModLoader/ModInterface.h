#pragma once

extern "C" {

typedef void (*SE_LogFn)(const char* prefix, const char* message);

// Fires after a hooked function is invoked by the game. userData is the pointer
// the mod passed to SubscribeHook. Callbacks run on the game thread that
// invoked the hook, so keep work short or hand off to the mod's own thread.
typedef void (*SE_HookCallback)(const char* hookName, void* userData);

typedef void (*SE_SubscribeHookFn)(const char* hookName,
                                   SE_HookCallback callback,
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

struct SE_ModApi
{
    SE_LogFn                 Log;
    SE_SubscribeHookFn       SubscribeHook;
    SE_GetTimeScaleFn        GetTimeScale;
    SE_RegisterImGuiDrawFn   RegisterImGuiDraw;
    SE_GetImGuiContextFn     GetImGuiContext;
    SE_GetImGuiAllocatorsFn  GetImGuiAllocators;
};

// Each mod DLL must export:
//     extern "C" __declspec(dllexport) void ModInit(const SE_ModApi* api);
typedef void (*SE_ModInitFn)(const SE_ModApi* api);

}
