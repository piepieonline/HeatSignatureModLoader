#pragma once
#include <stdint.h>

struct RValue;

struct YYString {
    const char* text;
    uint32_t    refcount;
    uint32_t    length;
};

struct YYArray {
    uint32_t length;
    RValue*  data;
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

using GMLScript_t = RValue*(__cdecl*)(uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv);

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
