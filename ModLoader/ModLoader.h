#pragma once

#include <windows.h>

#include "Hook.h"
#include "GameWindow.h"
#include "ModConfig.h"

#include <memory>
#include <unordered_map>
#include <vector>

class ModLoader
{
public:
	static ModLoader& Instance();
	static void Log(const char* prefix, const char* format, ...);
	static void SubscribeHook(const char* hookName, HS_HookCallback callback, void* userData);
	static void SubscribeHookPost(const char* hookName, HS_HookPostCallback callback, void* userData);
	static RValue* CallScript(const char* scriptName,
		CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv);
	static int GetVarId(const char* name);
	static void EnsureVariableMap();
	static std::unordered_map<std::string, uint32_t> VariableMap;
	static double GetTimeScale();

	static std::map<std::string, HookBase*> HookMap;
	std::vector<HookBase*> hooks
	{
	};

	ModLoader();
	ModLoader(const ModLoader&) = delete;
	ModLoader& operator=(const ModLoader&) = delete;

	void CreateHooks();
	void LoadMods();

private:
	std::vector<std::unique_ptr<HS_ModApi>> m_modApis;
	std::vector<std::unique_ptr<ModConfig>> m_modConfigs;
	std::unique_ptr<ModConfig>              m_loaderConfig;
};
