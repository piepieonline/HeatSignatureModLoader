#include "Globals.h"

#include <imgui.h>

// Global definitions (declared extern in Globals.h)
SE_LogFn            g_log            = nullptr;
SE_GetTimeScaleFn   g_getTimeScale   = nullptr;
SE_GetDailyStatusFn g_getDailyStatus = nullptr;
SE_GetGameWindowFn  g_getGameWindow  = nullptr;
std::atomic<bool>   g_recording{false};
std::atomic<bool>   g_recording_enabled{false};
std::thread         g_recordThread;

void Log(const char* fmt, ...)
{
    if (!g_log) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_log("ScreenRecorder", buf);
}

namespace
{
    void ToggleRecordingEnabled()
    {
        g_recording_enabled.store(!g_recording_enabled.load(std::memory_order_acquire), std::memory_order_release);
    }

    void InputLoop()
    {
        bool f9Down = false;
        while (true)
        {
            const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            if (f9 && !f9Down)
            {

            }
            f9Down = f9;

            Sleep(50);
        }
    }

    void DrawImGui(void* /*userData*/)
    {
        if (!ImGui::Begin("ScreenRecorder")) { ImGui::End(); return; }

        const bool recording = g_recording.load(std::memory_order_acquire);
        const bool recordingEnabled = g_recording_enabled.load(std::memory_order_acquire);
        ImGui::Text("recording: %s", recording ? "yes" : "no");
        if (g_getTimeScale)
            ImGui::Text("timescale: %.3f", g_getTimeScale());

        if (ImGui::Button(recordingEnabled ? "Disable Recording" : "Enable Recording"))
            ToggleRecordingEnabled();

        ImGui::End();
    }

    void OnAcceptMission(const char* /*hookName*/, void* /*userData*/)
    {
        if (g_recording.load(std::memory_order_acquire)) return;
        if (!g_recording_enabled.load(std::memory_order_acquire)) return;
        Log("AcceptMission -> start recording");
        ToggleRecording();
    }

    void OnCompleteMission(const char* /*hookName*/, void* /*userData*/)
    {
        if (!g_recording.load(std::memory_order_acquire)) return;
        Log("CompleteMission -> stop recording");
        ToggleRecording();
    }

    void OnCancelMission(const char* /*hookName*/, void* /*userData*/)
    {
        if (!g_recording.load(std::memory_order_acquire)) return;
        Log("CancelMission -> stop recording");
        ToggleRecording();
    }
}

extern "C" __declspec(dllexport)
void ModInit(const SE_ModApi* api)
{
    g_log            = api->Log;
    g_getTimeScale   = api->GetTimeScale;
    g_getDailyStatus = api->GetDailyStatus;
    g_getGameWindow  = api->GetGameWindow;
    Log("Initialized");

    api->SubscribeHook("gml_Script_AcceptMission",   &OnAcceptMission,   nullptr);
    api->SubscribeHook("gml_Script_CompleteMission", &OnCompleteMission, nullptr);
    api->SubscribeHook("gml_Script_CancelMission",   &OnCancelMission,   nullptr);

    if (api->GetImGuiAllocators && api->GetImGuiContext && api->RegisterImGuiDraw)
    {
        // Mirror the host's allocators *first* — required because
        // ImGui::SetCurrentContext is no-op-safe with a null context but any
        // subsequent allocation (e.g. during draw) must come from the host heap.
        void* allocFn  = nullptr;
        void* freeFn   = nullptr;
        void* userData = nullptr;
        api->GetImGuiAllocators(&allocFn, &freeFn, &userData);
        if (allocFn && freeFn)
        {
            ImGui::SetAllocatorFunctions(
                reinterpret_cast<ImGuiMemAllocFunc>(allocFn),
                reinterpret_cast<ImGuiMemFreeFunc>(freeFn),
                userData);
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->GetImGuiContext()));
        api->RegisterImGuiDraw(&DrawImGui, nullptr);
    }

    std::thread(InputLoop).detach();
}
