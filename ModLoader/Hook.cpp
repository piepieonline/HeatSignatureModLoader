#include "./Hook.h"
#include "./ModLoader.h"

#include <cstdarg>
#include <cstdio>

uintptr_t HookBase::moduleBase = 0;
size_t HookBase::moduleSize = 0;
thread_local bool g_hookBypassRequested = false;

void HookLog(const char* prefix, const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ModLoader::Log(prefix, "%s", buf);
}
