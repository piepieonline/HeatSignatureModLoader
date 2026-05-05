#pragma once

#include "ModInterface.h"

namespace ImGuiHook
{
    // Hooks Direct3DCreate9 from d3d9.dll. Subsequent vtable hooks are
    // installed lazily the first time the game creates an IDirect3D9 / device.
    bool Install();

    // Mod-facing draw registration. Safe to call from any thread.
    void RegisterDraw(SE_ImGuiDrawFn callback, void* userData);

    // Returns the shared ImGuiContext* (created lazily on first EndScene).
    // May return nullptr if EndScene hasn't fired yet — mods should call this
    // from their first registered draw callback rather than from ModInit if
    // they want a guaranteed-non-null context.
    void* GetContext();

    // Outputs the allocator pair ImGui is using inside ModLoader so mods
    // can mirror them via ImGui::SetAllocatorFunctions.
    void GetAllocators(void** allocFn, void** freeFn, void** userData);
}
