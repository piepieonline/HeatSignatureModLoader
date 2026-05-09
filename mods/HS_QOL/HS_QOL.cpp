#include "ModInterface.h"
#include "Log.h"
#include "ForceFocus.h"
#include "FixCursorLock.h"
#include "LoudGunPrefix.h"
#include <string>

HS_EXPORT_MOD_API_VERSION()

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    Log_Init(api->Log);

    ModSettings settings(api->config);
    bool forceFocus     = settings.ReadBool("force_focus",     false);
    bool fixCursorLock  = settings.ReadBool("fix_cursor_lock", true);
    bool loudGunPrefix  = settings.ReadBool("loud_gun_prefix", true);

    if (forceFocus)
    {
        ForceFocus_Init(api->GetGameWindow);
        api->RegisterImGuiDraw(ForceFocus_OnImGuiDraw, nullptr);
    }
    if (fixCursorLock) FixCursorLock_Register(api);
    if (loudGunPrefix) LoudGunPrefix_Register(api);

    Log("Initialized");
}
