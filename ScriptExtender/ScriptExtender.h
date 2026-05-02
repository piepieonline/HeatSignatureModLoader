#pragma once

#include <windows.h>

#include <lua.hpp>
#include "LuaBridge/LuaBridge.h"

#include "Hook.h"
#include "capture.hpp"

class ScriptExtender
{
public:
	static ScriptExtender& Instance();
	static void Log(const char* format, ...);

private:

	using MissionAccept_t = uintptr_t * (__cdecl*)(int, int, uintptr_t*, int, int);
	using GenerateMissions_t = uintptr_t * (__cdecl*)(double a1, uintptr_t* a2, int a3, uintptr_t* a4);

	/*
	std::vector<LoadedMod*> loadedMods;
	*/
	static std::map<std::string, HookBase*> HookMap;
	std::vector<HookBase*> hooks
	{
		new Hook<MissionAccept_t>(
			"gml_Script_AcceptMission",
			0x566200, // Found at 0xB16200,
			+[](int a1, int a2, uintptr_t* a3, int a4, int a5) -> uintptr_t*
			{
				auto hook = ScriptExtender::HookMap["gml_Script_AcceptMission"];
				ScriptExtender::Instance().Log("hook: gml_Script_AcceptMission");

				CoInitializeEx(0, COINIT_APARTMENTTHREADED);
				MFStartup(MF_VERSION);
				ScriptExtender::Instance().Log("Recording: Started");
				DESKTOPCAPTUREPARAMS dp;
				dp.f = L"capture.mp4";
				dp.EndMS = 3000;
				DesktopCapture(dp);
				ScriptExtender::Instance().Log("Recording: Done");

				auto originalFn = hook->reference.GetOriginal<MissionAccept_t>();
				return originalFn(a1, a2, a3, a4, a5);
			},
			true
		),
		new Hook<uintptr_t*, double, uintptr_t*, int, uintptr_t>(
			"gml_Script_GenerateMissions",
			0x5C9EB0, // Found at 0x00B79EB0
			+[](double a1, uintptr_t* a2, int a3, uintptr_t* a4) -> uintptr_t*
			{
				ScriptExtender::Instance().Log("hook: gml_Script_GenerateMissions");
				auto hook = ScriptExtender::HookMap["gml_Script_GenerateMissions"];
				ScriptExtender::Instance().Log("hook: gml_Script_GenerateMissions");
				auto originalFn = hook->reference.GetOriginal<GenerateMissions_t>();
				return originalFn(a1, a2, a3, a4);
			},
			true
		),
	};

	ScriptExtender();
	ScriptExtender(const ScriptExtender&) = delete;
	ScriptExtender& operator=(const ScriptExtender&) = delete;

	void CreateHooks();
};