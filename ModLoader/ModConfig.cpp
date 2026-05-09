#include "ModConfig.h"
#include <fstream>
#include <cstring>
#include <cstdlib>

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

void ModConfig::SetValue_Locked(const char* key, const char* value)
{
    std::string s = value ? value : "";

    if (_stricmp(s.c_str(), "true") == 0)  { m_data[key] = true;  return; }
    if (_stricmp(s.c_str(), "false") == 0) { m_data[key] = false; return; }

    char* end = nullptr;
    int64_t i = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == '\0' && end != s.c_str()) { m_data[key] = i; return; }

    end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == '\0' && end != s.c_str()) { m_data[key] = d; return; }

    m_data[key] = s;
}

std::string ModConfig::ReadString(const char* key, const char* defaultValue)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        SetValue_Locked(key, defaultValue);
        Save_Locked();
        return defaultValue ? std::string(defaultValue) : std::string();
    }
    const auto& v = *it;
    if (v.is_string())         return v.get<std::string>();
    if (v.is_boolean())        return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_float())   return std::to_string(v.get<double>());
    return v.dump();
}

bool ModConfig::ReadBool(const char* key, bool defaultValue)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        m_data[key] = defaultValue;
        Save_Locked();
        return defaultValue;
    }
    const auto& v = *it;
    if (v.is_boolean())        return v.get<bool>();
    if (v.is_number_integer()) return v.get<int64_t>() != 0;
    if (v.is_number_float())   return v.get<double>()  != 0.0;
    if (v.is_string())         return _stricmp(v.get<std::string>().c_str(), "true") == 0;
    return defaultValue;
}

int64_t ModConfig::ReadInt(const char* key, int64_t defaultValue)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        m_data[key] = defaultValue;
        Save_Locked();
        return defaultValue;
    }
    const auto& v = *it;
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number_float())   return static_cast<int64_t>(v.get<double>());
    if (v.is_boolean())        return v.get<bool>() ? 1 : 0;
    if (v.is_string())
    {
        const std::string s = v.get<std::string>();
        char* end = nullptr;
        int64_t parsed = std::strtoll(s.c_str(), &end, 0);
        if (end && *end == '\0' && end != s.c_str()) return parsed;
    }
    return defaultValue;
}

double ModConfig::ReadDouble(const char* key, double defaultValue)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        m_data[key] = defaultValue;
        Save_Locked();
        return defaultValue;
    }
    const auto& v = *it;
    if (v.is_number_float())   return v.get<double>();
    if (v.is_number_integer()) return static_cast<double>(v.get<int64_t>());
    if (v.is_boolean())        return v.get<bool>() ? 1.0 : 0.0;
    if (v.is_string())
    {
        const std::string s = v.get<std::string>();
        char* end = nullptr;
        double parsed = std::strtod(s.c_str(), &end);
        if (end && *end == '\0' && end != s.c_str()) return parsed;
    }
    return defaultValue;
}

void ModConfig::Write(const char* key, const char* value)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    SetValue_Locked(key, value);
    Save_Locked();
}

std::string ModConfig::GetJson()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_data.dump(4);
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
