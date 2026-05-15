#include "ModInterface.h"
#include "Log.h"
#include "ForceFocus.h"
#include "FixCursorLock.h"
#include "LoudGunPrefix.h"
#include "BleedOutTime.h"
#include "LoadGalaxy.h"

#include <imgui.h>
#include <string>

HS_EXPORT_MOD_API_VERSION()

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    Log_Init(api->Log);

    void* alloc = nullptr; void* freeFn = nullptr; void* ud = nullptr;
    api->GetImGuiAllocators(&alloc, &freeFn, &ud);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)alloc, (ImGuiMemFreeFunc)freeFn, ud);
    ImGui::SetCurrentContext((ImGuiContext*)api->GetImGuiContext());

    ModSettings settings(api->config);
    bool forceFocus     = settings.ReadBool("force_focus",     false);
    bool fixCursorLock  = settings.ReadBool("fix_cursor_lock", true);
    bool loudGunPrefix  = settings.ReadBool("loud_gun_prefix", true);
    bool bleedOutTime   = settings.ReadBool("bleed_out_time",  true);
    bool loadGalaxyMenu = settings.ReadBool("load_galaxy_menu",     false);

    if (forceFocus)
    {
        ForceFocus_Init(api->GetGameWindow);
        api->RegisterImGuiDraw(ForceFocus_OnImGuiDraw, nullptr);
    }
    if (fixCursorLock)  FixCursorLock_Register(api);
    if (loudGunPrefix)  LoudGunPrefix_Register(api);
    if (bleedOutTime)   BleedOutTime_Register(api);
    if (loadGalaxyMenu) LoadGalaxyMenu_Register(api);

    Log("Initialized");
}
