#include "ModInterface.h"
#include "ForceFocus.h"
#include "FixCursorLock.h"
#include <string>

extern "C" __declspec(dllexport)
void ModInit(const SE_ModApi* api)
{
    ModSettings settings(api->config);
    bool forceFocus    = std::string(settings.Read("force_focus",    "false")) != "false";
    bool fixCursorLock = std::string(settings.Read("fix_cursor_lock","true")) != "false";

    if (forceFocus)
    {
        ForceFocus_Init(api->Log, api->GetGameWindow);
        api->RegisterImGuiDraw(ForceFocus_OnImGuiDraw, nullptr);
    }
    if (fixCursorLock) FixCursorLock_Register(api);

    api->Log("HS_QOL", "Initialized");
}
