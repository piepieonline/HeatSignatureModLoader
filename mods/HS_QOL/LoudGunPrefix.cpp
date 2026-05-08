#include "LoudGunPrefix.h"
#include <string>

static const SE_ModApi* g_api = nullptr;

using ResolveInstance_t = int(__cdecl*)(uint32_t*);
using GetVar_t          = int(__cdecl*)(int, int, int, RValue*);
using SetVar_t          = int(__cdecl*)(int, int, int, RValue*);
using SetString_t       = int(__cdecl*)(RValue*, const char*);

static ResolveInstance_t g_ResolveInstance = nullptr;
static GetVar_t          g_GetVar          = nullptr;
static SetVar_t          g_SetVar          = nullptr;
static SetString_t       g_SetString       = nullptr;

static void OnGenerateGunPost(const char* /*hookName*/, uintptr_t* self, uintptr_t* other, RValue* /*returnValue*/, int argc, RValue** argv, void* /*userData*/)
{
    if (argc < 1 || !argv || !argv[0]) return;

    RValue traitArg{};
    g_SetString(&traitArg, "Loud");

    RValue* args[2] = { argv[0], &traitArg };
    RValue challengerResult{};
    g_api->CallScript("gml_Script_ItemHasTrait", self, other, &challengerResult, 2, args);

    if (challengerResult.real <= 0.5) return;

    int instance_handle = g_ResolveInstance((uint32_t*)argv[0]);

    RValue out{};
    g_GetVar(instance_handle, 673, 0x80000000, &out);
    if (!out.str || !out.str->text) return;

    std::string s = std::string("Loud ") + out.str->text;
    g_SetString(&out, s.c_str());
    g_SetVar(instance_handle, 673, 0x80000000, &out);

    g_api->Log("LoudGunPrefix", "Generating loud gun");
}

void LoudGunPrefix_Register(const SE_ModApi* api)
{
    g_api = api;

    uintptr_t moduleBase = (uintptr_t)GetModuleHandleA("Heat_Signature.exe");
    g_ResolveInstance = (ResolveInstance_t)(moduleBase + 0xCBC420);
    g_GetVar          = (GetVar_t)         (moduleBase + 0xC99410);
    g_SetVar          = (SetVar_t)         (moduleBase + 0xC996F0);
    g_SetString       = (SetString_t)      (moduleBase + 0xCAB130);

    api->SubscribeHookPost("gml_Script_GenerateGun", OnGenerateGunPost, nullptr);
}
