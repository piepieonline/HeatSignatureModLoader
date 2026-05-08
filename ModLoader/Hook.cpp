#include "./Hook.h"

uintptr_t HookBase::moduleBase = 0;
size_t HookBase::moduleSize = 0;
thread_local bool g_hookBypassRequested = false;