#pragma once

#include <windows.h>

#include <lua.hpp>
#include "LuaBridge/LuaBridge.h"

#include "Hook.h"
#include "GameWindow.h"

struct RValue
{
	union
	{
		double real;
		int i32;
		void* ptr;
		const char* str;
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

static uint32_t GetInstanceIdFromVariant(void* var)
{
	if (!var)
		return 0;

	auto type = *(uint32_t*)((char*)var + 8) & 0xFF;

	// only treat known instance-like types
	if (type != 2)
		return 0;

	void* payload = *(void**)var;

	// in your engine: payload encodes ID, not pointer
	return (uint32_t)payload;
}

static uintptr_t GetInstanceFromVariant(uintptr_t* v)
{
	int type = *(int*)(v + 3);

	switch (type & 0xFF)
	{
	case 0: // double
	case 10:
	case 13:
		return (uintptr_t)(*(double*)v);

	case 1: // string (not expected here)
	case 2: // object / instance
	case 6:
	case 7:
	case 14:
		return *(uintptr_t*)v;

	default:
		return 0;
	}
}

static const char* GetGMString(uintptr_t* variant)
{
	int type = *(int*)(variant + 3);

	if ((type & 0xFF) == 1) // string
	{
		void* strObj = *(void**)variant;
		if (!strObj) return nullptr;

		return *(const char**)strObj; // first field = char*
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// SEH-isolated helpers for gml_Script_SelectThisCharacter
// ---------------------------------------------------------------------------

static int* SelectThisChar_ResolveInstance_SEH(uint32_t* ctx, uint32_t instanceId, bool* outCrashed)
{
	*outCrashed = false;
	__try
	{
		using sub_CB66F0_t = int*(__thiscall*)(uint32_t*, uint32_t);
		auto fn = (sub_CB66F0_t)(HookBase::moduleBase + 0xCB66F0);
		return fn(ctx, instanceId);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*outCrashed = true;
		return nullptr;
	}
}

static uintptr_t SelectThisChar_ReadVarTable_SEH(int* instance)
{
	__try
	{
		return *(uintptr_t*)(instance + 1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

static const char* SelectThisChar_GetName_SEH(uintptr_t* nameVar)
{
	__try
	{
		return GetGMString(nameVar);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

static bool PlayAsCharacter_IsChallenger_SEH(uintptr_t* result, char* outMsg, int outMsgSize)
{
	__try
	{
		int type = *(int*)(result + 3);
		uintptr_t player_instance = GetInstanceFromVariant(result);
		if (!player_instance)
		{
			sprintf_s(outMsg, outMsgSize, "GetInstanceFromVariant returned null (type=%d, raw=%08X)", type & 0xFF, (uint32_t)*result);
			return false;
		}

		uint32_t varTable = *(uint32_t*)(player_instance + 4);
		if (!varTable)
		{
			sprintf_s(outMsg, outMsgSize, "varTable (instance+4) is null (instance=%08X)", (uint32_t)player_instance);
			return false;
		}

		double var634 = *(double*)(varTable + 634 * 16);
		bool isChallenger = var634 != 0.0;
		sprintf_s(outMsg, outMsgSize, "%s (var634=%f, instance=%08X)", isChallenger ? "true" : "false", var634, (uint32_t)player_instance);
		return isChallenger;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		sprintf_s(outMsg, outMsgSize, "exception");
		return false;
	}
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
	static void LogCharacterNames(uint32_t moduleBase, int objType, int other);

	using GMLScript_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);

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
				// ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv);  // fires every frame
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
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
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);

				auto challengerFn =
					(GMLScript_t)(HookBase::moduleBase + 0x1CE8A0);

				uintptr_t challengerResult[4] = {};

				challengerFn(
					self,                  // SAME self as engine used
					other,                 // SAME other
					challengerResult,      // output variant
					1,                     // argc
					argv                   // reuse arg0 (character id)
				);

				int type = *(int*)(challengerResult + 3);

				bool isChallenger = false;

				if ((type & 0xFF) == 0) // REAL
				{
					isChallenger =
						(*(double*)challengerResult) != 0.0;
				}

				ModLoader::Log(
					"PlayAsCharacter",
					"PlayerIsDailyChallenger => %s",
					isChallenger ? "true" : "false");

				using sub_CBC420_t = int(__cdecl*)(uint32_t* a1);
				using sub_C99410_t = int(__cdecl*)(
					int instanceHandle,
					int varId,
					int flags,
					RValue* out
					);

				auto GetVar =
					(sub_C99410_t)(HookBase::moduleBase + 0xC99410);

				auto ResolveInstance =
					(sub_CBC420_t)(HookBase::moduleBase + 0xCBC420);

				int instance_handle = ResolveInstance((uint32_t*)argv[0]);

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


				return ret;

				/*
				if (HookBase::moduleBase && argv && argc >= 1)
				{
					// argv[0] points to the object-type variant; cast the double value to int
					int objType = (int)(*(double*)argv[0]);
					ModLoader::LogCharacterNames((uint32_t)HookBase::moduleBase, objType, other);
				}

				char challengerMsg[128] = {};
				bool isChallenger = PlayAsCharacter_IsChallenger_SEH(result, challengerMsg, sizeof(challengerMsg));
				ModLoader::Log("PlayAsCharacter", "PlayerIsDailyChallenger: %s", challengerMsg);

				return ret;
				*/
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_PlayerIsDailyChallenger",
			0x1CE8A0,
			+[](int self, int other, uintptr_t* result, int argc, uintptr_t** argv) -> uintptr_t*
			{
				auto hook = ModLoader::HookMap["gml_Script_PlayerIsDailyChallenger"];
				// ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv);
				hook->NotifyPreSubscribers();
				auto ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				// ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, result);
				hook->NotifyPostSubscribers(result);
				//ModLoader::Log("ModLoader Hook", "gml_Script_PlayerIsDailyChallenger -> %f", *(double*)result);
				char challengerMsg[128] = {};
				//bool isChallenger = PlayAsCharacter_IsChallenger_SEH(argv[0], challengerMsg, sizeof(challengerMsg));
				//ModLoader::Log("PlayAsCharacter", "PlayerIsDailyChallenger: %s", challengerMsg);
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
			if (!argv || argc < 1)
			{
				ModLoader::Log("ModLoader Hook", "Invalid argv");
				return nullptr;
			}

			// ------------------------------------------------------------
			// Extract instanceId directly (already engine-resolved variant)
			// ------------------------------------------------------------
			uintptr_t* characterVar = argv[0];
			if (!characterVar)
			{
				ModLoader::Log("ModLoader Hook", "characterVar null");
				return nullptr;
			}

			uint32_t instanceId = (uint32_t)(*(double*)characterVar);

			if (!instanceId)
			{
				ModLoader::Log("ModLoader Hook", "instanceId resolved to 0");
				return nullptr;
			}

			ModLoader::Log("ModLoader Hook", "instanceId=%u (0x%X)", instanceId, instanceId);

			// ------------------------------------------------------------
			// Direct variable system access (NO instance dereference)
			// ------------------------------------------------------------
			using fn_C99410 = int(__cdecl*)(uint32_t, int, int, void*);
			auto C99410 = (fn_C99410)(HookBase::moduleBase + 0xC99410);

			if (!C99410)
			{
				ModLoader::Log("ModLoader Hook", "C99410 invalid");
				return nullptr;
			}

			struct GMVariant
			{
				double value;
				uint32_t type;
				uint32_t pad[2];
			} out{};

			// 634 = name (based on your discovery)
			int ok = C99410(instanceId, 634, 0x80000000, &out);

			if (!ok)
			{
				ModLoader::Log("ModLoader Hook", "C99410 failed (var 634)");
				return nullptr;
			}

			// ------------------------------------------------------------
			// Decode result (same logic style as sub_C99410)
			// ------------------------------------------------------------
			const char* name = nullptr;

			switch (out.type & 0xFF)
			{
			case 0:
			case 10:
			case 13:
			{
				ModLoader::Log("Character", "Selected (numeric): %f", out.value);
				break;
			}

			case 1: // string
			{
				// IMPORTANT: strings are NOT in GMVariant
				// They are returned via engine output buffer (result), not "out"
				name = *(const char**)((uintptr_t)result);
				break;
			}

			default:
			{
				ModLoader::Log("Character",
					"Selected: <unknown type 0x%X>",
					out.type & 0xFF);
				break;
			}
			}

			if (name && name[0])
				ModLoader::Log("Character", "Selected: %s", name);
			else
				ModLoader::Log("Character", "<no valid name>");

			// ------------------------------------------------------------
			// continue original function
			// ------------------------------------------------------------
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
