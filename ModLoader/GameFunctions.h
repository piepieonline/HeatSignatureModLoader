#pragma once

#include <windows.h>

#include "Hook.h"

// Finds the YYC function table (gml_Script_*) and the engine built-in dispatcher
// by scanning the loaded image, then fills ModLoader::HookMap with lazy hooks.
// Requires HookBase::moduleBase and MH_Initialize.
void RegisterGameFunctions();

// gml_Script_* names resolve to their pre-registered hook; any other name is
// treated as an engine built-in and gets a hook created on first use.
HookBase* FindOrCreateHook(const char* name);
