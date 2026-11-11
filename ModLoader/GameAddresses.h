#pragma once

#include <cstddef>
#include <cstdint>

// Everything tied to a specific version
// gml_Script_* functions are found by scanning for the YYC function table.
// TODO: Patterns for everything, though it's unlikely the game will get an update at this point that makes it worth it
namespace GameAddresses
{
    // YYC dispatcher for engine built-ins: RValue* (self, other, result, argc, index, argv).
    // The `ADD EDI, [imm32]` operand is the address of the built-in registry pointer; the count follows it.
    inline constexpr uint8_t kBuiltinDispatcher[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC5,
        0x89, 0x45, 0xFC, 0x8B, 0x45, 0x18, 0x53, 0x8B, 0x5D, 0x14, 0x56, 0x57, 0x8D,
        0x3C, 0x80, 0xC1, 0xE7, 0x04, 0x03, 0x3D, 0x00, 0x00, 0x00, 0x00,
    };
    inline constexpr char kBuiltinDispatcherMask[] = "xxxxxxx????xxxxxxxxxxxxxxxxxxxxxx????";
    inline constexpr size_t kBuiltinDispatcherRegistryOperand = 0x21;
    static_assert(sizeof(kBuiltinDispatcher) == sizeof(kBuiltinDispatcherMask) - 1, "pattern and mask length differ");

    inline constexpr uintptr_t kResolveInstanceRVA  = 0xCBC420;
    inline constexpr uintptr_t kResolveCInstanceRVA = 0xC98DF0;
    inline constexpr uintptr_t kGetVarRVA           = 0xC99410;
    inline constexpr uintptr_t kSetVarRVA           = 0xC996F0;
    inline constexpr uintptr_t kSetStringRVA        = 0xCAB130;

    // Pointer to the variable-table struct filled by sub_C7F050: [4] = entry count, [5] = PropertyDesc* array.
    inline constexpr uintptr_t kVariableTableStructRVA = 0x453D5B8;

    // Time scale = *(*(*(*(base + RVA) + 0x60) + 0x10) + 0x3C4) as double at +0x1B0.
    inline constexpr uintptr_t kTimeScaleBaseRVA = 0x0453D610;
    inline constexpr uint32_t  kTimeScaleDerefOffsets[] = { 0x60, 0x10, 0x3C4 };
    inline constexpr uint32_t  kTimeScaleFinalOffset = 0x1B0;
}
