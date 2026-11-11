#include "UniversalHooks.h"

#include <array>
#include <utility>

#include "ModLoader.h"

namespace {

HookBase* g_universalSlots[kUniversalHookSlots] = {};
size_t g_nextUniversalSlot = 0;

template <size_t N>
RValue* __cdecl UniversalDetour(CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv)
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

HookBase* CreateUniversalScriptHook(const char* name, uintptr_t offset)
{
    if (g_nextUniversalSlot >= kUniversalHookSlots)
        return nullptr;

    auto* hook = new Hook<GMLScript_t>(
        name,
        offset,
        kDetourTable[g_nextUniversalSlot],
        /*alwaysLoad=*/false);

    g_universalSlots[g_nextUniversalSlot++] = hook;
    return hook;
}
