#pragma once

extern "C" {

typedef void (*SE_LogFn)(const char* message);

// Fires after a hooked function is invoked by the game. userData is the pointer
// the mod passed to SubscribeHook. Callbacks run on the game thread that
// invoked the hook, so keep work short or hand off to the mod's own thread.
typedef void (*SE_HookCallback)(const char* hookName, void* userData);

typedef void (*SE_SubscribeHookFn)(const char* hookName,
                                   SE_HookCallback callback,
                                   void* userData);

// Returns 1.0 if the time-manager instance isn't available yet.
typedef double (*SE_GetTimeScaleFn)();

struct SE_ModApi
{
    SE_LogFn           Log;
    SE_SubscribeHookFn SubscribeHook;
    SE_GetTimeScaleFn  GetTimeScale;
};

// Each mod DLL must export:
//     extern "C" __declspec(dllexport) void ModInit(const SE_ModApi* api);
typedef void (*SE_ModInitFn)(const SE_ModApi* api);

}
