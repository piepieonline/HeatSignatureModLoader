#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>

struct RValue;
struct HS_ModApi;

// GameMaker engine instance object. Used as the `self` / `other` pointer
// in script function calls. Only the `id` field is interpreted by mods;
// the preceding bytes are opaque engine state. Field offset is verified
// against decompiled `GetVar` (see docs/decomp/eng_GetVar_C99410.txt).
struct CInstance {
    uint8_t  pad_0x00[0x78];
    uint32_t id;
    uint32_t type;

    // Resolves `this->type` (object_index) to the GML object name via the
    // engine's `object_get_name` built-in. Defined inline in GmArgs.h, which
    // is where the call-script plumbing lives. Returns "" on failure.
    std::string GetObjectName(const HS_ModApi* api) const;
};
static_assert(offsetof(CInstance, id) == 0x78, "CInstance::id offset mismatch");
static_assert(offsetof(CInstance, type) == 0x7C, "CInstance::type offset mismatch");

struct YYString {
    const char* text;
    uint32_t    refcount;
    uint32_t    length;
};

struct YYArray {
    uint32_t length;
};

struct RValue {
    union {
        double    real;
        int       i32;
        void*     ptr;
        YYString* str;
        YYArray*  arr;
    };
    uint32_t unk08;
    uint32_t type;
};

using GMLScript_t = RValue*(__cdecl*)(CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv);

static const char* GetTypeName(int type)
{
    switch (type)
    {
    case 0: return "REAL";
    case 1: return "STRING";
    case 2: return "ARRAY";
    case 3: return "PTR";
    case 5: return "INT";
    case 6: return "BOOL";
    case 7: return "UNDEFINED";
    default: return "UNKNOWN";
    }
}
