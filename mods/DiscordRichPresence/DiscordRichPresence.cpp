#include "ModInterface.h"
#include "HS/HS_Character.h"
#include <discord_rpc.h>
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

static constexpr const char* DISCORD_APP_ID = "1500846665467166720";

static SE_LogFn g_log = nullptr;
static const SE_ModApi* g_api = nullptr;

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

enum class GameState { Station, OnMission, DailyChallenge };

static std::atomic<GameState> g_state{ GameState::Station };
static std::atomic<int64_t>   g_missionStart{ 0 };
static std::atomic<int64_t>   g_dailyStart{ 0 };
static std::atomic<int>       g_dailyCount{ 0 };
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
        p.state   = "Chilling at the bar";
        p.startTimestamp = 0;
        break;

    case GameState::OnMission:
        p.details        = "On a mission";
        p.state          = "";
        p.startTimestamp = g_missionStart.load();
        break;

    case GameState::DailyChallenge:
    {
        static char dailyBuf[32];
        p.details = "Daily Challenge";
        if (g_dailyCount > 0)
        {
            snprintf(dailyBuf, sizeof(dailyBuf), "On Mission %d/3", g_dailyCount.load());
            p.state = dailyBuf;
        }
        else
        {
            p.state = "";
        }
        p.startTimestamp = g_dailyStart.load();
        break;
    }
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

static void OnAcceptMission(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    if (g_state == GameState::DailyChallenge)
    {
        g_dailyCount++;
        UpdatePresence();
    }
    else
    {
        g_missionStart = static_cast<int64_t>(std::time(nullptr));
        g_state = GameState::OnMission;
        UpdatePresence();
    }
}

static void OnCompleteMission(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    if (g_state != GameState::DailyChallenge)
    {
        g_state = GameState::Station;
        UpdatePresence();
    }
}

static void OnCancelMission(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    if (g_state != GameState::DailyChallenge)
    {
        g_state = GameState::Station;
        UpdatePresence();
    }
}

static void OnPlayAsCharacter(const char* /*hookName*/, uintptr_t* /*self*/, uintptr_t* /*other*/, RValue* /*result*/, int argc, RValue** argv, void* /*userData*/)
{
    if (argc < 1 || !argv || !argv[0]) return;

    auto character = HS::ResolveInstanceAs<HS::HS_Character>((uint32_t*)argv[0], g_api);
    if (!character.valid()) return;

    bool isDailyChallenger = character.DailyChallenge > 0.0;

    Log("PlayerIsDailyChallenger => %s", isDailyChallenger ? "true" : "false");

    if (isDailyChallenger)
    {
        g_state = GameState::DailyChallenge;
        g_dailyStart = static_cast<int64_t>(std::time(nullptr));
    }
    else
    {
        g_state = GameState::Station;
    }

    UpdatePresence();
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
    g_api = api;
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
    api->SubscribeHookPost("gml_Script_PlayAsCharacter", OnPlayAsCharacter, nullptr);

    g_callbackThread = std::thread(CallbackThread);
    g_callbackThread.detach();

    Log("Ready");
}
