#include "LoudGunPrefix.h"
#include "Log.h"
#include "HS/HS_Weapon.h"
#include "GmArgs.h"
#include <string>

static const HS_ModApi* g_api = nullptr;

static void OnGenerateGunPost(const char* /*hookName*/, CInstance* self, CInstance* other, RValue* /*returnValue*/, int argc, RValue** argv, void* /*userData*/)
{
    if (argc < 1 || !argv || !argv[0]) return;

    GmArgs args;
    args.AddRValue(*argv[0]);
    args.AddStr(g_api, "Loud");

    RValue challengerResult{};
    g_api->CallScript("gml_Script_ItemHasTrait", self, other, &challengerResult, args.Count(), args.Build());

    if (challengerResult.real <= 0.5) return;

    auto weapon = HS::ResolveInstanceAs<HS::HS_Weapon>((uint32_t*)argv[0], g_api);
    if (!weapon.valid()) return;

    if (weapon.Mysterious || weapon.PersonalMissionItem) 
    {
        Log("Skipping loud gun as it was mysterious or a personal mission item");
        return;
    }

    std::string current = weapon.Name;
    if (current.empty()) return;

    weapon.Name = "Loud " + current;

    Log("Generating loud gun");
}

void LoudGunPrefix_Register(const HS_ModApi* api)
{
    g_api = api;
    api->SubscribeHookPost("gml_Script_GenerateGun", OnGenerateGunPost, nullptr);
}
