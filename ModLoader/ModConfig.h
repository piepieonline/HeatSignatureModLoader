#pragma once
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

class ModConfig
{
public:
    explicit ModConfig(const std::string& filePath);

    // All Read/GetJson calls return owned std::strings — no aliasing,
    // no per-handle scratch buffer, safe across threads.
    std::string Read(const char* key, const char* defaultValue);
    std::string Read(const char* key, bool        defaultValue);
    std::string Read(const char* key, int64_t     defaultValue);
    std::string Read(const char* key, double      defaultValue);
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
