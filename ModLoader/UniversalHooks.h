#pragma once

#include <cstddef>

#include "Hook.h"

// One-detour-per-name dispatch via templated functions: each Nth instantiation
// reads slot[N] -> HookBase* so the original-call and subscriber notifications
// route through the correct hook without needing a captured name.

constexpr size_t kUniversalHookSlots = 1800; // ~1700 entries today; small headroom

extern HookBase* g_universalSlots[kUniversalHookSlots];

void* GetUniversalDetour(size_t index);

// Populates HookMap with HookBase entries for every script in the generated
// table whose name is not already present. Hooks are constructed but their
// MinHook patches are NOT installed here — that is deferred until the first
// SubscribeHook / SubscribeHookPost / CallScript for that name.
void InstallUniversalHookEntries();
