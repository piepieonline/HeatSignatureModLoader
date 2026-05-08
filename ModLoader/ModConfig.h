#pragma once
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

class ModConfig
{
public:
    explicit ModConfig(const std::string& filePath);

    // Returned const char* is valid until the next call of the same method on this object.
    const char* Read(const char* key, const char* defaultValue);
    const char* Read(const char* key, bool        defaultValue);
    const char* Read(const char* key, int64_t     defaultValue);
    const char* Read(const char* key, double      defaultValue);
    void        Write(const char* key, const char* value);
    const char* GetJson();
    void        SetJson(const char* json);
    void        Save();

private:
    void Load();
    void SetValue_Locked(const char* key, const char* value); // type-infers and sets m_data[key]; caller must hold m_mutex
    void Save_Locked();                                       // must be called with m_mutex already held

    std::string        m_filePath;
    nlohmann::json     m_data;
    std::mutex         m_mutex;
    std::string        m_readBuf;
    std::string        m_jsonBuf;
};
