#include "ModConfig.h"
#include <fstream>
#include <cstring>

ModConfig::ModConfig(const std::string& filePath)
    : m_filePath(filePath), m_data(nlohmann::json::object())
{
    Load();
}

void ModConfig::Load()
{
    std::ifstream f(m_filePath);
    if (!f.is_open()) return;
    try   { m_data = nlohmann::json::parse(f); }
    catch (...) { m_data = nlohmann::json::object(); }
}

// TODO: defaultValue should be typed
// TODO: If defaultValue is returned, it should be added and saved
const char* ModConfig::Read(const char* key, const char* defaultValue)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        m_readBuf = defaultValue ? defaultValue : "";
        return m_readBuf.c_str();
    }
    const auto& v = *it;
    if      (v.is_string())         m_readBuf = v.get<std::string>();
    else if (v.is_boolean())        m_readBuf = v.get<bool>() ? "true" : "false";
    else if (v.is_number_integer()) m_readBuf = std::to_string(v.get<int64_t>());
    else if (v.is_number_float())   m_readBuf = std::to_string(v.get<double>());
    else                            m_readBuf = v.dump();
    return m_readBuf.c_str();
}

void ModConfig::Write(const char* key, const char* value)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::string s = value ? value : "";

    if (_stricmp(s.c_str(), "true") == 0)  { m_data[key] = true;  Save_Locked(); return; }
    if (_stricmp(s.c_str(), "false") == 0) { m_data[key] = false; Save_Locked(); return; }

    {
        char* end = nullptr;
        int64_t i = std::strtoll(s.c_str(), &end, 10);
        if (end && *end == '\0' && end != s.c_str()) { m_data[key] = i; Save_Locked(); return; }
    }
    {
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end && *end == '\0' && end != s.c_str()) { m_data[key] = d; Save_Locked(); return; }
    }

    m_data[key] = s;
    Save_Locked();
}

const char* ModConfig::GetJson()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_jsonBuf = m_data.dump(4);
    return m_jsonBuf.c_str();
}

void ModConfig::SetJson(const char* json)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    try { m_data = nlohmann::json::parse(json); } catch (...) {}
    Save_Locked();
}

void ModConfig::Save_Locked()
{
    std::ofstream f(m_filePath);
    if (f.is_open()) f << m_data.dump(4);
}

void ModConfig::Save()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    Save_Locked();
}
