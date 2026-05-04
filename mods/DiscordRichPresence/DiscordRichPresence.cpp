#include "ModInterface.h"
#include <discord_rpc.h>
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

static constexpr const char* DISCORD_APP_ID = "";

static SE_LogFn g_log = nullptr;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_log("DiscordRichPresence", buf);
}

// ── Game state ────────────────────────────────────────────────────────────────

enum class GameState { Station, OnMission, MissionPaused };

static std::atomic<GameState> g_state{ GameState::Station };
static std::atomic<int64_t>   g_missionStart{ 0 };
static std::atomic<bool>      g_discordReady{ false };
static std::atomic<bool>      g_running{ true };
static std::thread            g_callbackThread;

// ── Presence update ──────────────────────────────────────────────────────────

static void UpdatePresence()
{
    if (!g_discordReady) return;

    DiscordRichPresence p{};
    // p.largeImageKey = "heatsig_logo";

    switch (g_state.load())
    {
    case GameState::Station:
        p.details = "In the station";
        p.state   = "Planning the next heist";
        p.startTimestamp = 0;
        break;

    case GameState::OnMission:
        p.details        = "On a mission";
        p.state          = "Infiltrating";
        p.startTimestamp = g_missionStart.load();
        break;

    case GameState::MissionPaused:
        p.details        = "On a mission";
        p.state          = "Paused";
        p.startTimestamp = g_missionStart.load();
        break;
    }

    Discord_UpdatePresence(&p);
}

// ── Discord event handlers ───────────────────────────────────────────────────

static void OnReady(const DiscordUser* user)
{
    g_discordReady = true;
    Log("Connected as %s#%s", user->username, user->discriminator);
    UpdatePresence();
}

static void OnDisconnected(int errorCode, const char* message)
{
    g_discordReady = false;
    Log("Disconnected (%d): %s", errorCode, message);
}

static void OnErrored(int errorCode, const char* message)
{
    Log("Error (%d): %s", errorCode, message);
}

// ── Hook callbacks ───────────────────────────────────────────────────────────

static void OnAcceptMission(const char* /*hookName*/, void* /*userData*/)
{
    g_missionStart = static_cast<int64_t>(std::time(nullptr));
    g_state        = GameState::OnMission;
    UpdatePresence();
}

static void OnCompleteMission(const char* /*hookName*/, void* /*userData*/)
{
    g_state = GameState::Station;
    UpdatePresence();
}

static void OnCancelMission(const char* /*hookName*/, void* /*userData*/)
{
    g_state = GameState::Station;
    UpdatePresence();
}

static void OnPauseMission(const char* /*hookName*/, void* /*userData*/)
{
    // Only treat as paused if we're currently on a mission
    if (g_state == GameState::OnMission)
    {
        g_state = GameState::MissionPaused;
        UpdatePresence();
    }
}

static void OnSetTimeScale(const char* /*hookName*/, void* /*userData*/)
{
    // Resuming from pause restores OnMission state
    if (g_state == GameState::MissionPaused)
    {
        g_state = GameState::OnMission;
        UpdatePresence();
    }
}

// ── Background thread: poll Discord callbacks ────────────────────────────────

static void CallbackThread()
{
    while (g_running)
    {
        Discord_RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// ── ModInit ──────────────────────────────────────────────────────────────────

extern "C" __declspec(dllexport)
void ModInit(const SE_ModApi* api)
{
    g_log = api->Log;
    Log("Initializing");

    DiscordEventHandlers handlers{};
    handlers.ready        = OnReady;
    handlers.disconnected = OnDisconnected;
    handlers.errored      = OnErrored;

    Discord_Initialize(DISCORD_APP_ID, &handlers, 0, nullptr);

    api->SubscribeHook("gml_Script_AcceptMission",   OnAcceptMission,   nullptr);
    api->SubscribeHook("gml_Script_CompleteMission", OnCompleteMission, nullptr);
    api->SubscribeHook("gml_Script_CancelMission",   OnCancelMission,   nullptr);
    api->SubscribeHook("gml_Script_PauseMission",    OnPauseMission,    nullptr);
    api->SubscribeHook("gml_Script_SetTimeScale",    OnSetTimeScale,    nullptr);

    g_callbackThread = std::thread(CallbackThread);
    g_callbackThread.detach();

    Log("Ready");
}
