#pragma once
#include "ModInterface.h"

void ForceFocus_Init(SE_LogFn log, SE_GetGameWindowFn getWindow);
void ForceFocus_OnImGuiDraw(void* userData);
