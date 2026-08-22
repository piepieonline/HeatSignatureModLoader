#include "FastLoadCharacters.h"
#include "Log.h"
#include "GmArgs.h"
#include "HS/HS_Character.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

// Skip characters that don't need to be loaded to speed up re-entering the station

static const HS_ModApi* g_api = nullptr;

static bool   g_inLoadCharacters = false;
static bool   g_inLoadFromFile   = false;
static double g_loadMode         = 0.0;
static int    g_total            = 0;
static int    g_skipped          = 0;
static bool   g_loggedUnreadable = false;
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

// Pointers into g_cache stay valid: unordered_map never relocates its elements
static std::unordered_map<std::string, CharacterFile> g_cache;
static const CharacterFile* g_pending = nullptr;

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

static std::filesystem::path g_charactersDir;
static bool g_dirScanned = false;
static std::unordered_map<std::wstring, std::pair<std::uintmax_t, std::int64_t>> g_dirEntries;

// One enumeration per load beats three path lookups per file, which is what this
// costs under Proton on a folder holding hundreds of characters
static void ScanCharactersDirectory(const std::wstring& anyFile)
{
    g_dirScanned = true;
    g_dirEntries.clear();
    g_charactersDir.clear();

    const std::filesystem::path resolved = ResolveCharacterPath(anyFile);
    if (resolved.empty()) return;
    const std::filesystem::path dir = resolved.parent_path();

    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return;

    for (; it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec) return;
        const std::uintmax_t size = it->file_size(ec);
        if (ec) { ec.clear(); continue; }
        const auto writeTime = it->last_write_time(ec);
        if (ec) { ec.clear(); continue; }
        g_dirEntries.emplace(it->path().filename().wstring(),
                             std::make_pair(size, writeTime.time_since_epoch().count()));
    }

    if (!g_dirEntries.empty()) g_charactersDir = dir;
}

static const CharacterFile* GetCharacterFile(const char* utf8Path)
{
    const std::wstring wide = WidePath(utf8Path);
    if (wide.empty()) return nullptr;

    if (!g_dirScanned) ScanCharactersDirectory(wide);

    std::filesystem::path path;
    std::uintmax_t size  = 0;
    std::int64_t   mtime = 0;

    const std::wstring leaf = std::filesystem::path(wide).filename().wstring();
    auto entry = g_dirEntries.find(leaf);
    if (!g_charactersDir.empty() && entry != g_dirEntries.end())
    {
        path  = g_charactersDir / leaf;
        size  = entry->second.first;
        mtime = entry->second.second;
    }
    else
    {
        path = ResolveCharacterPath(wide);
        if (path.empty()) return nullptr;

        std::error_code ec;
        size = std::filesystem::file_size(path, ec);
        if (ec) return nullptr;
        const auto writeTime = std::filesystem::last_write_time(path, ec);
        if (ec) return nullptr;
        mtime = writeTime.time_since_epoch().count();
    }

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

// The engine frees an RValue when this holds, so overwriting one would leak
static bool OwnsMemory(const RValue* value)
{
    return ((value->type + 0xFFFFFFu) & 0xFFFFFCu) == 0;
}

static bool IsCharacterObject(CInstance* self, CInstance* other, double objectIndex)
{
    static double s_index   = -1.0;
    static bool   s_checked = false;
    if (!s_checked)
    {
        s_checked = true;
        GmArgs args;
        args.AddReal(objectIndex);
        RValue name{};
        g_api->CallEngineScript("object_get_name", self, other, &name,
                                args.Count(), args.Build());
        const char* text = (name.type == 1 && name.str) ? name.str->text : nullptr;
        if (text && std::strcmp(text, "oPlayer") == 0) s_index = objectIndex;
        Log("Character object index %g is '%s'%s", objectIndex, text ? text : "?",
            text && s_index < 0.0 ? " - not oPlayer, leaving instance creation alone" : "");
    }
    return s_index >= 0.0 && objectIndex == s_index;
}

static bool IsString(const RValue* value, const char* text)
{
    return value && value->type == 1 && value->str && value->str->text &&
           std::strcmp(value->str->text, text) == 0;
}

// The loop logs "Found <path> Captured" and "<path> CharacterFileNames" once each
// per file; the category is always the last argument
static void OnDebugLogPre(const char* /*hookName*/, CInstance* /*self*/, CInstance* /*other*/, RValue* /*result*/, int argc, RValue** argv, void* /*userData*/)
{
    if (!g_inLoadCharacters || g_inLoadFromFile) return;
    if (argc < 2 || !argv) return;

    const RValue* category = argv[argc - 1];
    if (!IsString(category, "CharacterFileNames") &&
        !(argc == 3 && IsString(category, "Captured") && IsString(argv[0], "Found")))
        return;

    g_api->RequestBypass();
}

static void OnLoadCharactersPre(const char* /*hookName*/, CInstance* /*self*/, CInstance* /*other*/, RValue* /*result*/, int argc, RValue** argv, void* /*userData*/)
{
    g_inLoadCharacters = argc >= 1 && argv && argv[0] && argv[0]->type == 0;
    g_loadMode = g_inLoadCharacters ? argv[0]->real : 0.0;
    g_inLoadFromFile = false;
    g_pending        = nullptr;
    g_total          = 0;
    g_skipped        = 0;
    g_dirScanned     = false;
    g_start          = std::chrono::steady_clock::now();

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
        Log("LoadCharacters: skipped %d of %d character files in %lld ms",
            g_skipped, g_total, static_cast<long long>(elapsed));
    }
    g_inLoadCharacters = false;
    g_inLoadFromFile   = false;
    g_pending          = nullptr;
}

// First call of the loop body, and the only one that sees the file path before
// the instance is created
static void OnCharacterNameFromFilenamePre(const char* /*hookName*/, CInstance* /*self*/, CInstance* /*other*/, RValue* /*result*/, int argc, RValue** argv, void* /*userData*/)
{
    g_pending = nullptr;
    if (!g_inLoadCharacters || g_inLoadFromFile) return;
    if (argc < 1 || !argv || !argv[0] || argv[0]->type != 1) return;
    if (!argv[0]->str || !argv[0]->str->text) return;

    g_total++;

    const CharacterFile* file = GetCharacterFile(argv[0]->str->text);
    if (!file)
    {
        if (!g_loggedUnreadable)
        {
            g_loggedUnreadable = true;
            Log("Could not read character file '%s' - loading it normally", argv[0]->str->text);
        }
        return;
    }
    if (file->hasContents || file->hasRescueAgent) return;
    if (!GameWillDiscard(file->status, g_loadMode)) return;

    g_pending = file;
}

// LoadCharacters runs the loop body as `with (CreateInstance(...))`, so returning
// noone skips the load, the status checks and the destroy in one go
static void OnCreateInstancePre(const char* /*hookName*/, CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv, void* /*userData*/)
{
    if (!g_pending) return;
    if (!g_inLoadCharacters || g_inLoadFromFile) return;
    if (argc != 3 || !argv || !argv[2] || argv[2]->type != 0) return;
    if (!result || OwnsMemory(result)) return;
    if (!IsCharacterObject(self, other, argv[2]->real)) return;

    g_pending = nullptr;
    result->real = -4.0; // noone
    result->type = 0;
    g_skipped++;
    g_api->RequestBypass();
}

// Fallback for when the instance was created anyway: skip the parse instead
static void OnLoadCharacterFromFilePre(const char* /*hookName*/, CInstance* self, CInstance* /*other*/, RValue* /*result*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    const CharacterFile* file = g_pending;
    g_pending = nullptr;

    if (file && self)
    {
        HS::HS_Character character(self->id, g_api);
        if (character.valid())
        {
            character.Status   = file->status;
            character.Forename = file->forename;
            character.Surname  = file->surname;
            character.Name     = file->forename + " " + file->surname;
            g_skipped++;
            g_api->RequestBypass();
            return;
        }
    }

    g_inLoadFromFile = true;
}

static void OnLoadCharacterFromFilePost(const char* /*hookName*/, CInstance* /*self*/, CInstance* /*other*/, RValue* /*returnValue*/, int /*argc*/, RValue** /*argv*/, void* /*userData*/)
{
    g_inLoadFromFile = false;
}

void FastLoadCharacters_Register(const HS_ModApi* api)
{
    g_api = api;

    api->SubscribeHook    ("gml_Script_LoadCharacters",            OnLoadCharactersPre,            nullptr);
    api->SubscribeHookPost("gml_Script_LoadCharacters",            OnLoadCharactersPost,           nullptr);
    api->SubscribeHook    ("gml_Script_CharacterNameFromFilename", OnCharacterNameFromFilenamePre, nullptr);
    api->SubscribeHook    ("gml_Script_LoadCharacterFromFile",     OnLoadCharacterFromFilePre,     nullptr);
    api->SubscribeHookPost("gml_Script_LoadCharacterFromFile",     OnLoadCharacterFromFilePost,    nullptr);
    api->SubscribeHook    ("gml_Script_CreateInstance",            OnCreateInstancePre,            nullptr);
    api->SubscribeHook    ("gml_Script_DebugLog",                  OnDebugLogPre,                  nullptr);
}
