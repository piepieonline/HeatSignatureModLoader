#pragma once

#include <windows.h>

#include <lua.hpp>
#include "LuaBridge/LuaBridge.h"

#include "Hook.h"

class ModLoader
{
public:
	static ModLoader& Instance();
	static void Log(const char* prefix, const char* format, ...);
	static void SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData);
	static double GetTimeScale();

private:

	using MissionAccept_t = uintptr_t * (__cdecl*)(int, int, uintptr_t*, int, int);
	using MissionComplete_t = uintptr_t * (__cdecl*)(int, int, uintptr_t*, int, int);
	using MissionCancel_t = uintptr_t * (__cdecl*)(int, int, uintptr_t*);
	using GenerateMissions_t = uintptr_t * (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
	using SetTimeScale_t = uintptr_t * (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
	using PauseMission_t = uintptr_t * (__cdecl*)(uintptr_t* a1, int a2, uintptr_t* a3);
	using PauseFor_t = uintptr_t * (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t** a5);
	using SetSlowMotionEffectStrength_t = uintptr_t * (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t* a5);

	static std::map<std::string, HookBase*> HookMap;
	std::vector<HookBase*> hooks
	{
		new Hook<MissionAccept_t>(
			"gml_Script_AcceptMission",
			0x566200,
			+[](int a1, int a2, uintptr_t* a3, int a4, int a5) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_AcceptMission"];
				ModLoader::Instance().Log("ModLoader Hook", "gml_Script_AcceptMission");
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<MissionAccept_t>();
				return originalFn(a1, a2, a3, a4, a5);
			},
			true
		),
		new Hook<GenerateMissions_t>(
			"gml_Script_GenerateMissions",
			0x5C9EB0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				ModLoader::Instance().Log("ModLoader Hook", "gml_Script_GenerateMissions (argc=%d)", argc);
				auto hook = ModLoader::HookMap["gml_Script_GenerateMissions"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<GenerateMissions_t>();
				return originalFn(self, other, result, argc, argv);
			},
			true
		),
		new Hook<MissionComplete_t>(
			"gml_Script_CompleteMission",
			0x570840,
			+[](int a1, int a2, uintptr_t* a3, int a4, int a5) -> uintptr_t*
			{
				ModLoader::Instance().Log("ModLoader Hook", "gml_Script_CompleteMission");
				auto hook = ModLoader::HookMap["gml_Script_CompleteMission"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<MissionComplete_t>();
				return originalFn(a1, a2, a3, a4, a5);
			},
			true
		),
		new Hook<MissionCancel_t>(
			"gml_Script_CancelMission",
			0x56DC30,
			+[](int a1, int a2, uintptr_t* a3) -> uintptr_t*
			{
				ModLoader::Instance().Log("ModLoader Hook", "gml_Script_CancelMission");
				auto hook = ModLoader::HookMap["gml_Script_CancelMission"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<MissionCancel_t>();
				return originalFn(a1, a2, a3);
			},
			true
		),
		new Hook<SetTimeScale_t>(
			"gml_Script_SetTimeScale",
			0x976800,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_SetTimeScale"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<SetTimeScale_t>();
				return originalFn(self, other, result, argc, argv);
			},
			true
		),
		new Hook<PauseMission_t>(
			"gml_Script_PauseMission",
			0x6056B0,
			+[](uintptr_t* a1, int a2, uintptr_t* a3) -> uintptr_t*
			{
				ModLoader::Instance().Log("ModLoader Hook", "gml_Script_PauseMission");
				auto hook = ModLoader::HookMap["gml_Script_PauseMission"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<PauseMission_t>();
				return originalFn(a1, a2, a3);
			},
			true
		),
		new Hook<PauseFor_t>(
			"gml_Script_PauseFor",
			0x9771A0,
			+[](int a1, int a2, uintptr_t* a3, int a4, uintptr_t** a5) -> uintptr_t*
			{
				ModLoader::Instance().Log("ModLoader Hook", "gml_Script_PauseFor");
				auto hook = ModLoader::HookMap["gml_Script_PauseFor"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<PauseFor_t>();
				return originalFn(a1, a2, a3, a4, a5);
			},
			true
		),
	};

	ModLoader();
	ModLoader(const ModLoader&) = delete;
	ModLoader& operator=(const ModLoader&) = delete;

	void CreateHooks();
	void LoadMods();
};
