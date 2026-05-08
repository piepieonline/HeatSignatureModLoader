#pragma once

#include <windows.h>

#include <lua.hpp>
#include "LuaBridge/LuaBridge.h"

#include "Hook.h"
#include "GameWindow.h"

struct YYString
{
	const char* text;
	uint32_t refcount;
	uint32_t length;
};

struct RValue
{
	union
	{
		double real;
		int i32;
		void* ptr;
		YYString* str;
	};

	uint32_t unk08;
	uint32_t type;
};

static const char* GetTypeName(int type)
{
	switch (type)
	{
	case 0: return "REAL";
	case 1: return "STRING";
	case 2: return "ARRAY";
	case 3: return "PTR";
	case 5: return "INT";
	case 6: return "BOOL";
	case 7: return "UNDEFINED";
	default: return "UNKNOWN";
	}
}

static inline uint32_t* GetActiveContext()
{
	uint32_t* ctx = (uint32_t*)0x45ED614;

	while (ctx)
	{
		if (ctx[9]) // validity flag
			return ctx;

		ctx = (uint32_t*)ctx[3];
	}

	return nullptr;
}

// ---------------------------------------------------------------------------

class ModLoader
{
public:
	static ModLoader& Instance();
	static void Log(const char* prefix, const char* format, ...);
	static void SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData);
	static void SubscribeHookPost(const char* hookName, SE_HookPostCallback callback, void* userData);
	static double GetTimeScale();
	static double GetDailyStatus();
	static void PollDword(const char* label, uintptr_t rva, DWORD intervalMs);
	static void LogGMLCall(const char* fnName, int self, int argc, uintptr_t** argv, uintptr_t* result = nullptr);
	static void LogRValue(const char* label, RValue* rv);
	static void LogDrawText(int argc, uintptr_t** argv);
	static void LogArgsAddress(int argc, uintptr_t** argv);

	static bool isDrawing;
	static RValue nameVal;


	using GMLScript_t = uintptr_t * (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);

	static std::map<std::string, HookBase*> HookMap;
	std::vector<HookBase*> hooks
	{
		new Hook<GMLScript_t>(
			"gml_Script_AcceptMission",
			0x566200,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_AcceptMission"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_GenerateMissions",
			0x5C9EB0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_GenerateMissions"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_CompleteMission",
			0x570840,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_CompleteMission"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_CancelMission",
			0x56DC30,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_CancelMission"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_SetTimeScale",
			0x976800,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_SetTimeScale"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				// ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);  // fires every frame
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_PauseMission",
			0x6056B0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_PauseMission"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_PauseFor",
			0x9771A0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_PauseFor"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_PlayAsCharacter",
			0x000A0080,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_PlayAsCharacter"];
				hook->NotifyPreSubscribers();

				auto ret =
					hook->reference.GetOriginal<GMLScript_t>()(
						self, other, result, argc, argv);

				ModLoader::LogGMLCall(
					hook->hookName.c_str(),
					self, argc, argv, result);

				hook->NotifyPostSubscribers(result);

				using challengerFn_t = GMLScript_t;
				auto challengerFn =
					(challengerFn_t)(HookBase::moduleBase + 0x1CE8A0);

				uintptr_t challengerResult[4] = {};

				challengerFn(
					self,
					other,
					challengerResult,
					1,
					argv
				);

				int type = *(int*)(challengerResult + 3);

				bool isChallenger = false;

				if ((type & 0xFF) == 0)
				{
					isChallenger =
						(*(double*)challengerResult) != 0.0;
				}

				ModLoader::Log(
					"PlayAsCharacter",
					"PlayerIsDailyChallenger => %s",
					isChallenger ? "true" : "false");

				using sub_CBC420_t = int(__cdecl*)(uint32_t*);
				using sub_C99410_t = int(__cdecl*)(
					int, int, int, RValue*
				);
				using SetVar_t = int(__cdecl*)(
					int, int, int, RValue*
				);

				auto SetVar =
					(SetVar_t)(HookBase::moduleBase + 0xC996F0);

				auto GetVar =
					(sub_C99410_t)(HookBase::moduleBase + 0xC99410);

				auto ResolveInstance =
					(sub_CBC420_t)(HookBase::moduleBase + 0xCBC420);

				int instance_handle =
					ResolveInstance((uint32_t*)argv[0]);

				ModLoader::Log(
					"PlayerIsDailyChallenger",
					"instance=0x%08X",
					instance_handle);

				for (int i = 0; i <= 1000; i++)
				{
					RValue out{};
					GetVar(instance_handle, i, 0x80000000, &out);

					char label[64];
					if (i == 634)
						sprintf_s(label, "Var[634] (Daily Challenger Flag)");
					else
						sprintf_s(label, "Var[%d]", i);

					ModLoader::LogRValue(label, &out);
				}

				ModLoader::isDrawing = true;

				using SetString_t = int(__cdecl*)(RValue*, char*);
				auto SetString = (SetString_t)(HookBase::moduleBase + 0xCAB130);

				RValue nameTest;

				GetVar(instance_handle, 673, 0x80000000, &nameTest);
				SetString(&ModLoader::nameVal, (char*)"Hello");

				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_PlayerIsDailyChallenger",
			0x1CE8A0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_PlayerIsDailyChallenger"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				// ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				
				if(ModLoader::isDrawing)
				{
					using callGML_t = uint32_t* (__cdecl*)(int, int, void*, int, int, int);
					auto callGML = (callGML_t)(HookBase::moduleBase + 0xCA7F30);
					int drawTextHandle = *(int*)(HookBase::moduleBase + 0x10C3CEC);

					RValue argX{}, argY{};
					argX.real = 100.0; argX.type = 0;
					argY.real = 200.0; argY.type = 0;

					uintptr_t* argPtrs[3] = {
						reinterpret_cast<uintptr_t*>(&argX),
						reinterpret_cast<uintptr_t*>(&argY),
						reinterpret_cast<uintptr_t*>(&ModLoader::nameVal),
					};

					uintptr_t resBuf[4] = {};
					callGML(self, other, resBuf, 3, drawTextHandle, (int)argPtrs);
				}

				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_SetRoomSprite",
			0x00209390,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_SetRoomSprite"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_ShowTutorialTip",
			0x009FD8E0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_ShowTutorialTip"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_GetNameFromPersonaCache",
			0x00913880,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_GetNameFromPersonaCache"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_SelectThisCharacter",
			0xCB420,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_SelectThisCharacter"];
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"draw_text",
			0xCD8350,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["draw_text"];
				// ModLoader::LogDrawText(argc, argv); // fires every frame
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				hook->NotifyPostSubscribers(result);
				return ret;
			},
			true
		)
	};

	ModLoader();
	ModLoader(const ModLoader&) = delete;
	ModLoader& operator=(const ModLoader&) = delete;

	void CreateHooks();
	void LoadMods();
};
