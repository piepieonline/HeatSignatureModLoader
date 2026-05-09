#pragma once
#include <string>
#include "../ModInterface.h"

namespace HS {

class Instance
{
public:
    Instance(int handle, const HS_ModApi* api) : m_handle(handle), m_api(api) {}

    int handle() const { return m_handle; }
    bool valid() const { return m_handle != 0 && m_api != nullptr; }
    const HS_ModApi* api() const { return m_api; }

    // Generic by-name access for variables not exposed as a typed member.
    struct NamedProperty
    {
        Instance* owner;
        const char* name;

        RValue get() const
        {
            RValue out{};
            owner->m_api->GetVarByName(owner->m_handle, name, 0x80000000, &out);
            return out;
        }

        void set(RValue& v)
        {
            owner->m_api->SetVarByName(owner->m_handle, name, 0x80000000, &v);
        }

        operator std::string() const
        {
            RValue out{};
            owner->m_api->GetVarByName(owner->m_handle, name, 0x80000000, &out);
            return (out.str && out.str->text) ? out.str->text : std::string();
        }

        operator double() const
        {
            RValue out{};
            owner->m_api->GetVarByName(owner->m_handle, name, 0x80000000, &out);
            return out.real;
        }
    };

    NamedProperty operator[](const char* name) { return { this, name }; }

protected:
    struct StringProperty
    {
        Instance* owner;
        const char* name;

        operator std::string() const
        {
            RValue out{};
            owner->m_api->GetVarByName(owner->m_handle, name, 0x80000000, &out);
            return (out.str && out.str->text) ? out.str->text : std::string();
        }

        StringProperty& operator=(const char* v)
        {
            RValue tmp{};
            owner->m_api->SetString(&tmp, v);
            owner->m_api->SetVarByName(owner->m_handle, name, 0x80000000, &tmp);
            return *this;
        }

        StringProperty& operator=(const std::string& v) { return *this = v.c_str(); }
    };

    struct RealProperty
    {
        Instance* owner;
        const char* name;

        operator double() const
        {
            RValue out{};
            owner->m_api->GetVarByName(owner->m_handle, name, 0x80000000, &out);
            return out.real;
        }

        RealProperty& operator=(double v)
        {
            RValue tmp{};
            tmp.real = v;
            tmp.type = 0;
            owner->m_api->SetVarByName(owner->m_handle, name, 0x80000000, &tmp);
            return *this;
        }
    };

    struct RValueProperty
    {
        Instance* owner;
        const char* name;

        RValue get() const
        {
            RValue out{};
            owner->m_api->GetVarByName(owner->m_handle, name, 0x80000000, &out);
            return out;
        }

        void set(RValue& v)
        {
            owner->m_api->SetVarByName(owner->m_handle, name, 0x80000000, &v);
        }
    };

    int              m_handle;
    const HS_ModApi* m_api;
};

template <typename T>
T ResolveInstanceAs(uint32_t* argHandle, const HS_ModApi* api)
{
    return T(api->ResolveInstance(argHandle), api);
}

} // namespace HS
