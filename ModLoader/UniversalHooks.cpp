#include "UniversalHooks.h"

#include <array>
#include <utility>

#include "ModLoader.h"
#include "ScriptFunctions.generated.h"

HookBase* g_universalSlots[kUniversalHookSlots] = {};

namespace {

template <size_t N>
RValue* __cdecl UniversalDetour(uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv)
{
    HookBase* hook = g_universalSlots[N];
    RValue* ret = result;
    if (!hook->NotifyPreSubscribers(self, other, result, argc, argv))
    {
        ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
        hook->NotifyPostSubscribers(self, other, ret, argc, argv);
    }
    return ret;
}

template <size_t... Is>
std::array<void*, sizeof...(Is)> MakeDetourTable(std::index_sequence<Is...>)
{
    return { reinterpret_cast<void*>(&UniversalDetour<Is>)... };
}

const auto kDetourTable =
    MakeDetourTable(std::make_index_sequence<kUniversalHookSlots>{});

} // namespace

void* GetUniversalDetour(size_t index)
{
    return (index < kUniversalHookSlots) ? kDetourTable[index] : nullptr;
}

void InstallUniversalHookEntries()
{
    static_assert(kScriptFunctionCount <= kUniversalHookSlots,
        "kUniversalHookSlots is smaller than kScriptFunctionCount; raise the slot count.");

    size_t slot = 0;
    size_t skipped = 0;

    for (size_t i = 0; i < kScriptFunctionCount; ++i)
    {
        const auto& entry = kScriptFunctions[i];

        if (ModLoader::HookMap.find(entry.name) != ModLoader::HookMap.end())
        {
            ++skipped;
            continue;
        }

        if (slot >= kUniversalHookSlots)
        {
            ModLoader::Log("ModLoader",
                "UniversalHooks: out of slots, dropping '%s'", entry.name);
            continue;
        }

        auto* hook = new Hook<GMLScript_t>(
            entry.name,
            entry.offset,
            kDetourTable[slot],
            /*alwaysLoad=*/false);

        g_universalSlots[slot] = hook;
        ModLoader::HookMap[entry.name] = hook;
        ++slot;
    }

    ModLoader::Log("ModLoader",
        "UniversalHooks: registered %zu lazy entries (%zu shared with hardcoded hooks)",
        slot, skipped);
}
