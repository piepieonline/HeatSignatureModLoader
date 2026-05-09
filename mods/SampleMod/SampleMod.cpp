#include "ModInterface.h"
#include <imgui.h>

static HS_LogFn g_log = nullptr;
static bool g_showPopup = false;

static void OnAcceptMission(const char*, uintptr_t*, uintptr_t*, RValue*, int, RValue**, void*)
{
    g_showPopup = true;
    g_log("SampleMod", "Mission accepted!");
}

static void OnImGuiDraw(void*)
{
    if (!g_showPopup) return;
    if (ImGui::Begin("SampleMod", &g_showPopup))
    {
        ImGui::Text("Mission accepted!");
        if (ImGui::Button("Dismiss")) g_showPopup = false;
    }
    ImGui::End();
}

HS_EXPORT_MOD_API_VERSION()

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    g_log = api->Log;

    void* alloc = nullptr; void* freeFn = nullptr; void* ud = nullptr;
    api->GetImGuiAllocators(&alloc, &freeFn, &ud);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)alloc, (ImGuiMemFreeFunc)freeFn, ud);
    ImGui::SetCurrentContext((ImGuiContext*)api->GetImGuiContext());

    api->SubscribeHook("gml_Script_AcceptMission", OnAcceptMission, nullptr);
    api->RegisterImGuiDraw(OnImGuiDraw, nullptr);
    g_log("SampleMod", "Loaded");
}
