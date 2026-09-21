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
        int64_t   i64;
        int32_t   i32;
        void*     ptr;
        YYString* str;
        YYArray*  arr;
    };
    uint32_t unk08;
    uint32_t type;
};
static_assert(sizeof(RValue) == 0x10, "RValue size mismatch");

// Matches YoYo's YYRValue.h; the top byte of RValue::type holds flags, not the kind.
enum class RValueKind : uint32_t
{
    Real      = 0,
    String    = 1,
    Array     = 2,
    Pointer   = 3,
    Vec3      = 4,
    Undefined = 5,
    Object    = 6,
    Int32     = 7,
    Vec4      = 8,
    Vec44     = 9,
    Int64     = 10,
    Accessor  = 11,
    Null      = 12,
    Bool      = 13,
    Iterator  = 14,
    Ref       = 15,
    Unset     = 0x00ffffff
};

inline RValueKind GetRValueKind(const RValue& value)
{
    return static_cast<RValueKind>(value.type & 0x00ffffffu);
}

using GMLScript_t = RValue*(__cdecl*)(CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv);

static const char* GetTypeName(int type)
{
    switch (static_cast<RValueKind>(type & 0x00ffffffu))
    {
    case RValueKind::Real:      return "REAL";
    case RValueKind::String:    return "STRING";
    case RValueKind::Array:     return "ARRAY";
    case RValueKind::Pointer:   return "PTR";
    case RValueKind::Vec3:      return "VEC3";
    case RValueKind::Undefined: return "UNDEFINED";
    case RValueKind::Object:    return "OBJECT";
    case RValueKind::Int32:     return "INT32";
    case RValueKind::Vec4:      return "VEC4";
    case RValueKind::Vec44:     return "VEC44";
    case RValueKind::Int64:     return "INT64";
    case RValueKind::Accessor:  return "ACCESSOR";
    case RValueKind::Null:      return "NULL";
    case RValueKind::Bool:      return "BOOL";
    case RValueKind::Iterator:  return "ITERATOR";
    case RValueKind::Ref:       return "REF";
    case RValueKind::Unset:     return "UNSET";
    default: return "UNKNOWN";
    }
}
