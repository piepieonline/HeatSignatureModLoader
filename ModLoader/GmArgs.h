#pragma once

#include <vector>

#include "ModInterface.h"

// Builds the (argc, RValue** argv) pair expected by HS_CallScriptFn /
// HS_CallEngineScriptFn. Owns the RValues and the pointer array, so the
// returned argv stays valid for the lifetime of the GmArgs instance.
class GmArgs
{
public:
    void AddReal(double v)
    {
        RValue r{};
        r.type = 0;
        r.real = v;
        m_values.push_back(r);
    }

    void AddStr(const HS_ModApi* api, const char* s)
    {
        RValue r{};
        r.type = 1;
        api->SetString(&r, s);
        m_values.push_back(r);
    }

    // Overload for callers that hold the raw engine SetString pointer rather
    // than a full HS_ModApi (the loader itself).
    void AddStr(HS_SetStringFn setString, const char* s)
    {
        RValue r{};
        r.type = 1;
        setString(&r, s);
        m_values.push_back(r);
    }

    void AddRValue(const RValue& v)
    {
        m_values.push_back(v);
    }

    RValue** Build()
    {
        m_ptrs.resize(m_values.size());
        for (size_t i = 0; i < m_values.size(); i++)
            m_ptrs[i] = &m_values[i];
        return m_ptrs.data();
    }

    int Count() const { return static_cast<int>(m_values.size()); }

private:
    std::vector<RValue>  m_values;
    std::vector<RValue*> m_ptrs;
};
