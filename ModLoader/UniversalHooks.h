#pragma once

#include <cstddef>
#include <cstdint>

#include "Hook.h"

// One-detour-per-name dispatch via templated functions: each Nth instantiation
// reads slot[N] -> HookBase* so the original-call and subscriber notifications
// route through the correct hook without needing a captured name.

constexpr size_t kUniversalHookSlots = 2560; // 2005 gml_Script_ entries today

// Takes the next free detour slot. The MinHook patch is deferred until the first
// SubscribeHook / SubscribeHookPost / CallScript for that name.
// Returns nullptr once every slot is taken.
HookBase* CreateUniversalScriptHook(const char* name, uintptr_t offset);
