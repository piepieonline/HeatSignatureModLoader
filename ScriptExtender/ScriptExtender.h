#pragma once

#include <windows.h>

#include <lua.hpp>
#include "LuaBridge/LuaBridge.h"

#include "Hook.h"

class ScriptExtender
{
public:
	static ScriptExtender& Instance();
	static void Log(const char* format, ...);
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
				auto hook = ScriptExtender::HookMap["gml_Script_AcceptMission"];
				ScriptExtender::Instance().Log("hook: gml_Script_AcceptMission");
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
				ScriptExtender::Instance().Log("hook: gml_Script_GenerateMissions (argc=%d)", argc);
				auto hook = ScriptExtender::HookMap["gml_Script_GenerateMissions"];
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
				ScriptExtender::Instance().Log("hook: gml_Script_CompleteMission");
				auto hook = ScriptExtender::HookMap["gml_Script_CompleteMission"];
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
				ScriptExtender::Instance().Log("hook: gml_Script_CancelMission");
				auto hook = ScriptExtender::HookMap["gml_Script_CancelMission"];
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
				/*
				ScriptExtender::Instance().Log("hook: gml_Script_SetTimeScale (self=0x%X, other=0x%X, argc=%d)", self, other, argc);
				for (int i = 0; i < argc && argv; ++i)
				{
					uintptr_t* rv = argv[i];
					if (!rv) { ScriptExtender::Instance().Log("  argv[%d] = <null>", i); continue; }
					int kind = ((int*)rv)[3];
					double asReal = *(double*)rv;
					int asInt = *(int*)rv;
					ScriptExtender::Instance().Log("  argv[%d] kind=%d real=%f int=0x%X ptr=0x%p", i, kind, asReal, asInt, (void*)rv);
				}
				*/

				auto hook = ScriptExtender::HookMap["gml_Script_SetTimeScale"];
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
				ScriptExtender::Instance().Log("hook: gml_Script_PauseMission");
				auto hook = ScriptExtender::HookMap["gml_Script_PauseMission"];
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
				ScriptExtender::Instance().Log("hook: gml_Script_PauseFor");
				auto hook = ScriptExtender::HookMap["gml_Script_PauseFor"];
				hook->NotifySubscribers();
				auto originalFn = hook->reference.GetOriginal<PauseFor_t>();
				return originalFn(a1, a2, a3, a4, a5);
			},
			true
		),
		/*
		new Hook<SetSlowMotionEffectStrength_t>(
			"gml_Script_SetSlowMotionEffectStrength",
			0x2729B0,
			+[](int a1, int a2, uintptr_t* a3, int a4, uintptr_t* a5) -> uintptr_t*
			{
				ScriptExtender::Instance().Log("hook: gml_Script_SetSlowMotionEffectStrength (self=0x%X, other=0x%X, argc=%d)", a1, a2, a4);
				uintptr_t** argv = (uintptr_t**)a5;
				for (int i = 0; i < a4 && argv; ++i)
				{
					uintptr_t* rv = argv[i];
					if (!rv) { ScriptExtender::Instance().Log("  argv[%d] = <null>", i); continue; }
					int kind = ((int*)rv)[3];
					double asReal = *(double*)rv;
					int asInt = *(int*)rv;
					ScriptExtender::Instance().Log("  argv[%d] kind=%d real=%f int=0x%X ptr=0x%p", i, kind, asReal, asInt, (void*)rv);
				}

				auto hook = ScriptExtender::HookMap["gml_Script_SetSlowMotionEffectStrength"];
				auto originalFn = hook->reference.GetOriginal<SetSlowMotionEffectStrength_t>();
				return originalFn(a1, a2, a3, a4, a5);
			},
			true
		),
		*/
	};

	ScriptExtender();
	ScriptExtender(const ScriptExtender&) = delete;
	ScriptExtender& operator=(const ScriptExtender&) = delete;

	void CreateHooks();
	void LoadMods();
};

/**
_DWORD *__cdecl gml_Script_SetSlowMotionEffectStrength(int a1, int a2, _DWORD *a3, int a4, _DWORD *a5)
{
  _DWORD *v5; // edi
  _DWORD v7[2]; // [esp+10h] [ebp-34h] BYREF
  _DWORD v8[3]; // [esp+18h] [ebp-2Ch] BYREF
  int v9; // [esp+24h] [ebp-20h]
  _DWORD v10[2]; // [esp+28h] [ebp-1Ch] BYREF
  int v11; // [esp+30h] [ebp-14h]

  v10[1] = "gml_Script_SetSlowMotionEffectStrength";
  v11 = 0;
  v10[0] = dword_45356E8;
  dword_45356E8 = (int)v10;
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + a3[3])) == 0 )
	sub_1790(a3);
  a3[3] = 0;
  a3[1] = 0;
  *a3 = 0;
  v11 = 0;
  v9 = 5;
  v8[0] = 0;
  sub_C99410(208, 1156, 0x80000000, v8);
  v7[0] = *a5;
  v7[1] = v8;
  v5 = (_DWORD *)sub_CC4120(&dword_452AED8, 2, v7);
  if ( v5 != v8 )
  {
	if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v9)) == 0 )
	  sub_1790(v8);
	sub_1EE0(v8, v5);
  }
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + dword_452AEE4)) == 0 )
	sub_1790(&dword_452AED8);
  dword_452AEE0 = 0;
  dword_452AEE4 = 5;
  dword_452AED8 = 0;
  sub_C996F0(208, 1156, 0x80000000, v8);
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v9)) == 0 )
	sub_1790(v8);
  dword_45356E8 = v10[0];
  return a3;
}
*/