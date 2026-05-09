#include "LoudGunPrefix.h"
#include "Log.h"
#include "HS/HS_Weapon.h"
#include <string>

static const SE_ModApi* g_api = nullptr;

static void OnGenerateGunPost(const char* /*hookName*/, uintptr_t* self, uintptr_t* other, RValue* /*returnValue*/, int argc, RValue** argv, void* /*userData*/)
{
    if (argc < 1 || !argv || !argv[0]) return;

    RValue traitArg{};
    g_api->SetString(&traitArg, "Loud");

    RValue* args[2] = { argv[0], &traitArg };
    RValue challengerResult{};
    g_api->CallScript("gml_Script_ItemHasTrait", self, other, &challengerResult, 2, args);

    if (challengerResult.real <= 0.5) return;

    auto weapon = HS::ResolveInstanceAs<HS::HS_Weapon>((uint32_t*)argv[0], g_api);
    if (!weapon.valid()) return;

    std::string current = weapon.Name;
    if (current.empty()) return;

    weapon.Name = "Loud " + current;

    Log("Generating loud gun");
}

void LoudGunPrefix_Register(const SE_ModApi* api)
{
    g_api = api;
    api->SubscribeHookPost("gml_Script_GenerateGun", OnGenerateGunPost, nullptr);
}
