#pragma once
#include <string>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

class ModConfig
{
public:
    explicit ModConfig(const std::string& filePath);

    // Typed accessors. Each returns the stored value coerced to the requested
    // type, falling back to defaultValue (and persisting it) if the key is
    // missing. Strings returned by ReadString are owned by the caller — no
    // aliasing, no scratch buffer, safe across threads.
    std::string ReadString(const char* key, const char* defaultValue);
    bool        ReadBool  (const char* key, bool        defaultValue);
    int64_t     ReadInt   (const char* key, int64_t     defaultValue);
    double      ReadDouble(const char* key, double      defaultValue);

    void        Write(const char* key, const char* value);
    std::string GetJson();
    void        SetJson(const char* json);
    void        Save();

private:
    void Load();
    void SetValue_Locked(const char* key, const char* value); // type-infers and sets m_data[key]; caller must hold m_mutex
    void Save_Locked();                                       // must be called with m_mutex already held

    std::string        m_filePath;
    nlohmann::json     m_data;
    std::mutex         m_mutex;
};
