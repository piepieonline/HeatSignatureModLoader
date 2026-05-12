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
		uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv);
	static RValue* CallEngineScript(const char* builtinName,
		uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv);
	static void EnsureEngineScriptMap();
	static std::unordered_map<std::string, uint32_t> EngineScriptMap;
	static int GetVarId(const char* name);
	static void EnsureVariableMap();
	static std::unordered_map<std::string, uint32_t> VariableMap;
	static double GetTimeScale();

	static std::map<std::string, HookBase*> HookMap;
	std::vector<HookBase*> hooks
	{
		/*
		// Example engine function hook
		// Note that game functions can be hooked the same way, but care must be taken to not break subscribers
		new Hook<GMLScript_t>(
			"draw_text",
			0xCD8350,
			+[](uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv) -> RValue*
			{
				auto hook = ModLoader::HookMap["draw_text"];
				// ModLoader::LogDrawText(argc, argv); // fires every frame
				RValue* ret = result;
				if (!hook->NotifyPreSubscribers(self, other, result, argc, argv))
					ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				hook->NotifyPostSubscribers(self, other, ret, argc, argv);
				return ret;
			},
			true
		)
		*/
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
