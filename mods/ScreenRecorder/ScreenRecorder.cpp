#include "Globals.h"

#include <imgui.h>

// Global definitions (declared extern in Globals.h)
ModSettings         g_settings;
HS_LogFn            g_log            = nullptr;
HS_GetTimeScaleFn   g_getTimeScale   = nullptr;
HS_GetGameWindowFn  g_getGameWindow  = nullptr;
std::atomic<bool>   g_recording{false};
std::atomic<bool>   g_recording_enabled{false};
std::atomic<bool>   g_recording_paused{false};
std::atomic<int>    g_unpause_skip_frames{0};
std::thread         g_recordThread;
UINT32              g_video_bit_rate        = 2'000'000;
GUID                g_video_encoding_format = MFVideoFormat_H264;
std::wstring        g_video_output_path     = L"./";

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

    void OnAcceptMission(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
    {
        if (g_recording.load(std::memory_order_acquire)) return;
        if (!g_recording_enabled.load(std::memory_order_acquire)) return;
        Log("AcceptMission -> start recording");
        ToggleRecording();
    }

    void OnCompleteMission(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
    {
        if (!g_recording.load(std::memory_order_acquire)) return;
        Log("CompleteMission -> stop recording");
        ToggleRecording();
    }

    void OnCancelMission(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
    {
        if (!g_recording.load(std::memory_order_acquire)) return;
        Log("CancelMission -> stop recording");
        ToggleRecording();
    }

    void ShowInventoryMenu_Prefix(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
    {
        g_recording_paused.store(true);
    }

    void HideInventoryMenu_Postfix(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*returnValue*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
    {
        // Skip a handful of frames so that any screen frames where the menu is
        // still fading out don't make it into the recording.
        g_unpause_skip_frames.store(2, std::memory_order_release);
        g_recording_paused.store(false, std::memory_order_release);
    }
}

HS_EXPORT_MOD_API_VERSION()

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    g_settings       = ModSettings(api->config);
    g_log            = api->Log;
    g_getTimeScale   = api->GetTimeScale;
    g_getGameWindow  = api->GetGameWindow;

    api->SubscribeHook("gml_Script_AcceptMission",   &OnAcceptMission,   nullptr);
    api->SubscribeHook("gml_Script_CompleteMission", &OnCompleteMission, nullptr);
    api->SubscribeHook("gml_Script_CancelMission",   &OnCancelMission,   nullptr);
    api->SubscribeHook("gml_Script_ShowInventoryMenu",   &ShowInventoryMenu_Prefix,   nullptr);
    api->SubscribeHookPost("gml_Script_CloseInventoryMenu",   &HideInventoryMenu_Postfix,   nullptr);

    g_recording_enabled.store(g_settings.ReadBool("recordByDefault", true));
    g_video_bit_rate = static_cast<UINT32>(g_settings.ReadInt("bitrate", 8000000));
    {
        std::string codec = g_settings.ReadString("codec", "h264");
        if (codec == "h265" || codec == "hevc")
            g_video_encoding_format = MFVideoFormat_H265;
        else
            g_video_encoding_format = MFVideoFormat_H264;
    }
    {
        std::string path = g_settings.ReadString("outputPath", "./");
        int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (len > 0)
        {
            g_video_output_path.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, g_video_output_path.data(), len);
        }
    }

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

    Log("Initialized");
}
