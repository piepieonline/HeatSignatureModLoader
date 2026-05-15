#include "BleedOutTime.h"
#include "GmArgs.h"
#include "HS/HS_Character.h"

#include <string>

static const HS_ModApi* g_api = nullptr;

// Replaces the "Injuries Suffered" line on the mission/lifetime stat panels
// with a "Bleed Out Time: M:SS" line for the player character. The game's
// post-hook lets us re-issue DrawMissionRating with our substituted text.
static void OnDrawMissionRatingPost(const char* /*hookName*/, CInstance* self, CInstance* other, RValue* /*returnValue*/, int argc, RValue** argv, void* /*userData*/)
{
    if (argc < 2 || !argv || !argv[0] || !argv[1]) return;
    if (!argv[0]->str || !argv[0]->str->text) return;

    const std::string text = argv[0]->str->text;

    // Matches both the mission-end and the lifetime-stats panels.
    const bool isInjuriesLine =
        text.find("Injuries Suffered") != std::string::npos ||
        text.find("Injuries suffered") != std::string::npos;
    if (!isInjuriesLine) return;

    HS::HS_Character character(self->id, g_api);
    if (!character.valid()) return;

    GmArgs timeArgs;
    timeArgs.AddReal(character.BleedOutTime);

    RValue bleedTimeStr{};
    g_api->CallScript(
        "gml_Script_MinutesAndSeconds",
        self, other,
        &bleedTimeStr,
        timeArgs.Count(), timeArgs.Build());

    const char* timeText = (bleedTimeStr.str && bleedTimeStr.str->text)
        ? bleedTimeStr.str->text
        : "?";

    GmArgs out;
    out.AddStr(g_api, (std::string("Bleed Out Time: ") + timeText).c_str());
    out.AddRValue(*argv[1]);

    RValue result{};
    g_api->CallScript(
        "gml_Script_DrawMissionRating",
        self, other,
        &result,
        out.Count(), out.Build());
}

void BleedOutTime_Register(const HS_ModApi* api)
{
    g_api = api;
    api->SubscribeHookPost("gml_Script_DrawMissionRating", OnDrawMissionRatingPost, nullptr);
}
