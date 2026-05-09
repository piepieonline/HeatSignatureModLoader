#include "Log.h"
#include <cstdarg>
#include <cstdio>

static SE_LogFn g_log = nullptr;

void Log_Init(SE_LogFn log)
{
    g_log = log;
}

void Log(const char* fmt, ...)
{
    if (!g_log) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_log("HS QOL", buf);
}
