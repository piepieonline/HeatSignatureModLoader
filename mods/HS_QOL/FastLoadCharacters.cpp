#include "FastLoadCharacters.h"
#include "Log.h"
#include "HS/HS_Character.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

// Skip characters that don't need to be loaded to speed up re-entering the station

static const HS_ModApi* g_api = nullptr;

static bool   g_inLoadCharacters = false;
static double g_loadMode         = 0.0;
static int    g_skipped          = 0;
static int    g_total            = 0;
static int    g_rejectedUnreadable = 0;
static int    g_rejectedContents   = 0;
static int    g_rejectedRescue     = 0;
static int    g_rejectedStatus     = 0;
static int    g_rejectedInstance   = 0;
static bool   g_loggedUnreadable   = false;
static std::chrono::steady_clock::time_point g_start;

struct CharacterFile
{
    std::uintmax_t size  = 0;
    std::int64_t   mtime = 0;
    std::string    status;
    std::string    forename;
    std::string    surname;
    bool           hasContents    = false;
    bool           hasRescueAgent = false;
};

static std::unordered_map<std::string, CharacterFile> g_cache;

static const signed char* Base64Table()
{
    static signed char table[256];
    static bool built = false;
    if (!built)
    {
        for (int i = 0; i < 256; i++) table[i] = -1;
        const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++)
            table[static_cast<unsigned char>(alphabet[i])] = static_cast<signed char>(i);
        built = true;
    }
    return table;
}

static bool Base64Decode(const std::string& in, std::string& out)
{
    if (in.empty() || (in.size() % 4) != 0) return false;

    const signed char* table = Base64Table();
    out.clear();
    out.reserve(in.size() / 4 * 3);

    for (size_t i = 0; i < in.size(); i += 4)
    {
        int sextets[4] = { 0, 0, 0, 0 };
        int padding = 0;
        for (int j = 0; j < 4; j++)
        {
            const char c = in[i + j];
            if (c == '=')
            {
                if (i + 4 != in.size() || j < 2) return false;
                padding++;
                continue;
            }
            if (padding) return false;
            const signed char value = table[static_cast<unsigned char>(c)];
            if (value < 0) return false;
            sextets[j] = value;
        }

        const std::uint32_t triple =
            (sextets[0] << 18) | (sextets[1] << 12) | (sextets[2] << 6) | sextets[3];
        out.push_back(static_cast<char>((triple >> 16) & 0xFF));
        if (padding < 2) out.push_back(static_cast<char>((triple >> 8) & 0xFF));
        if (padding < 1) out.push_back(static_cast<char>(triple & 0xFF));
    }
    return true;
}

static void ParseLine(const std::string& text, CharacterFile& out)
{
    if (text.empty()) return;

    if (text.front() == '<')
    {
        // Other sections make the parse mark unique/workshop items in use globally
        if (text != "<Header>" && text != "<Character>" &&
            text != "<Pod>"    && text != "<Validation>")
            out.hasContents = true;
        return;
    }

    const size_t separator = text.find(" = ");
    if (separator == std::string::npos) return;

    const std::string key   = text.substr(0, separator);
    const std::string value = text.substr(separator + 3);

    if      (key == "Status")   out.status   = value;
    else if (key == "Forename") out.forename = value;
    else if (key == "Surname")  out.surname  = value;
    else if (key == "PersonalMissionRescueAgent") out.hasRescueAgent = !value.empty();
}

static std::wstring WidePath(const char* utf8)
{
    if (!utf8) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring wide(static_cast<size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
    return wide;
}

static bool ReadCharacterFile(const std::filesystem::path& path, CharacterFile& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::string line;
    std::string decoded;

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Plain lines carry "<" or " = ", never valid base64, so a decode proves encoding
        ParseLine(Base64Decode(line, decoded) ? decoded : line, out);
    }
    return true;
}

static std::filesystem::path EnvPath(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return std::filesystem::path(buffer, buffer + length);
}

// The game passes the folder relative to GameMaker's sandboxed save area, which
// only the engine's own file functions prepend
static std::filesystem::path ResolveCharacterPath(const std::wstring& given)
{
    std::error_code ec;
    const std::filesystem::path direct(given);
    if (std::filesystem::exists(direct, ec)) return direct;

    static std::filesystem::path s_saveRoot;
    static bool s_resolved = false;
    if (!s_resolved)
    {
        s_resolved = true;
        for (const wchar_t* variable : { L"APPDATA", L"LOCALAPPDATA" })
        {
            const std::filesystem::path root = EnvPath(variable) / L"Heat_Signature";
            if (std::filesystem::exists(root / given, ec)) { s_saveRoot = root; break; }
        }
        Log("Save root for relative character paths: '%s'",
            s_saveRoot.empty() ? "<not found>" : s_saveRoot.string().c_str());
    }

    if (s_saveRoot.empty()) return {};
    return s_saveRoot / given;
}

static const CharacterFile* GetCharacterFile(const char* utf8Path)
{
    const std::wstring wide = WidePath(utf8Path);
    if (wide.empty()) return nullptr;
    const std::filesystem::path path = ResolveCharacterPath(wide);
    if (path.empty()) return nullptr;

    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return nullptr;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) return nullptr;
    const std::int64_t mtime = writeTime.time_since_epoch().count();

    const std::string key = utf8Path;
    auto cached = g_cache.find(key);
    if (cached != g_cache.end() && cached->second.size == size && cached->second.mtime == mtime)
        return &cached->second;

    CharacterFile parsed;
    parsed.size  = size;
    parsed.mtime = mtime;
    if (!ReadCharacterFile(path, parsed)) return nullptr;

    CharacterFile& slot = g_cache[key];
    slot = std::move(parsed);
    return &slot;
}

// Mirrors the Status checks LoadCharacters runs once LoadCharacterFromFile returns
static bool GameWillDiscard(const std::string& status, double mode)
{
    // mode >= 1 discards everything not Retired, so a failed read must not decide
    if (status.empty()) return false;
    if (static_cast<int>(mode) >= 1) return status != "Retired";
    if (mode == 0.0 && status == "Retired") return true;
    return status == "Dead" || status == "Lost";
}

static void OnLoadCharactersPre(const char* /*hookName*/, CInstance* /*self*/, CInstance* /*other*/, RValue* /*result*/, int argc, RValue** argv, void* /*userData*/)
{
    g_inLoadCharacters = argc >= 1 && argv && argv[0] && argv[0]->type == 0;
    g_loadMode = g_inLoadCharacters ? argv[0]->real : 0.0;
    g_skipped  = 0;
    g_total    = 0;
    g_rejectedUnreadable = 0;
    g_rejectedContents   = 0;
    g_rejectedRescue     = 0;
    g_rejectedStatus     = 0;
    g_rejectedInstance   = 0;
    g_start    = std::chrono::steady_clock::now();

    if (!g_inLoadCharacters)
        Log("LoadCharacters: mode argument is not a real (argc=%d, type=%u) - not optimising",
            argc, (argc >= 1 && argv && argv[0]) ? argv[0]->type : 0u);
}

static void OnLoadCharactersPost(const char* /*hookName*/, CInstance* /*self*/, CInstance* /*other*/, RValue* /*returnValue*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    if (g_inLoadCharacters && g_total > 0)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_start).count();
        Log("LoadCharacters: skipped %d of %d character files in %lld ms "
            "(mode=%g, kept: unreadable=%d contents=%d rescuer=%d status=%d instance=%d)",
            g_skipped, g_total, static_cast<long long>(elapsed), g_loadMode,
            g_rejectedUnreadable, g_rejectedContents, g_rejectedRescue, g_rejectedStatus, g_rejectedInstance);
    }
    g_inLoadCharacters = false;
}

static void OnLoadCharacterFromFilePre(const char* /*hookName*/, CInstance* self, CInstance* /*other*/, RValue* /*result*/, int argc, RValue** argv, void* /*userData*/)
{
    // Only the station-entry bulk load creates instances it is about to throw away
    if (!g_inLoadCharacters || !self) return;
    if (argc < 1 || !argv || !argv[0] || argv[0]->type != 1) return;
    if (!argv[0]->str || !argv[0]->str->text) return;

    g_total++;

    const CharacterFile* file = GetCharacterFile(argv[0]->str->text);

    if (!file)
    {
        g_rejectedUnreadable++;
        if (!g_loggedUnreadable)
        {
            g_loggedUnreadable = true;
            Log("Could not read character file '%s' - loading it normally", argv[0]->str->text);
        }
        return;
    }
    if (file->hasContents)    { g_rejectedContents++;   return; }
    if (file->hasRescueAgent) { g_rejectedRescue++;     return; }
    if (!GameWillDiscard(file->status, g_loadMode)) { g_rejectedStatus++; return; }

    HS::HS_Character character(self->id, g_api);
    if (!character.valid()) { g_rejectedInstance++; return; }

    character.Status   = file->status;
    character.Forename = file->forename;
    character.Surname  = file->surname;
    character.Name     = file->forename + " " + file->surname;

    g_skipped++;
    g_api->RequestBypass();
}

void FastLoadCharacters_Register(const HS_ModApi* api)
{
    g_api = api;
    api->SubscribeHook    ("gml_Script_LoadCharacters",        OnLoadCharactersPre,        nullptr);
    api->SubscribeHookPost("gml_Script_LoadCharacters",        OnLoadCharactersPost,       nullptr);
    api->SubscribeHook    ("gml_Script_LoadCharacterFromFile", OnLoadCharacterFromFilePre, nullptr);
}
