#include "ModInterface.h"
#include "Log.h"
#include "ForceFocus.h"
#include "FixCursorLock.h"
#include "LoudGunPrefix.h"
#include <string>

SE_EXPORT_MOD_API_VERSION()

extern "C" __declspec(dllexport)
void ModInit(const SE_ModApi* api)
{
    Log_Init(api->Log);

    ModSettings settings(api->config);
    bool forceFocus     = settings.Read("force_focus",     "false") != "false";
    bool fixCursorLock  = settings.Read("fix_cursor_lock", "true")  != "false";
    bool loudGunPrefix  = settings.Read("loud_gun_prefix", "true")  != "false";

    if (forceFocus)
    {
        ForceFocus_Init(api->GetGameWindow);
        api->RegisterImGuiDraw(ForceFocus_OnImGuiDraw, nullptr);
    }
    if (fixCursorLock) FixCursorLock_Register(api);
    if (loudGunPrefix) LoudGunPrefix_Register(api);

    Log("Initialized");
}
