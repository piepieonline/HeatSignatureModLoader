#pragma once

#include <windows.h>

#include <lua.hpp>
#include "LuaBridge/LuaBridge.h"

#include "Hook.h"
#include "GameWindow.h"
#include "ModConfig.h"

#include <memory>
#include <vector>

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

#define STANDARD_GML_HOOK(name, offset, log) \
    new Hook<GMLScript_t>( \
        name, offset, \
        +[](uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv) -> RValue* \
        { \
            auto hook = ModLoader::HookMap[name]; \
            RValue* ret = result; \
            if (!hook->NotifyPreSubscribers(self, other, result, argc, argv)) \
            { \
                ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv); \
                if (log) ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, ret); \
				hook->NotifyPostSubscribers(self, other, ret, argc, argv); \
            } \
            return ret; \
        }, \
        true)

// todo: draw a UI for the modloader itself, and pass a "we want the UI drawn now" flag to mods
// They can still choose to draw if they want to

class ModLoader
{
public:
	static ModLoader& Instance();
	static void Log(const char* prefix, const char* format, ...);
	static void SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData);
	static void SubscribeHookPost(const char* hookName, SE_HookPostCallback callback, void* userData);
	static RValue* CallScript(const char* scriptName,
		uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv);
	static double GetTimeScale();
	static void PollDword(const char* label, uintptr_t rva, DWORD intervalMs);
	static void LogGMLCall(const char* fnName, uintptr_t* self, int argc, RValue** argv, RValue* result = nullptr);
	static void LogRValue(const char* label, RValue* rv);
	static void LogDrawText(int argc, uintptr_t** argv);
	static void LogArgsAddress(int argc, uintptr_t** argv);

	static bool isDrawing;
	static RValue nameVal;


	static std::map<std::string, HookBase*> HookMap;
	std::vector<HookBase*> hooks
	{
		STANDARD_GML_HOOK("gml_Script_AcceptMission",    0x566200, true),
		new Hook<GMLScript_t>(
			"gml_Script_PlayAsCharacter",
			0x000A0080,
			+[](uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv) -> RValue*
			{
				auto hook = ModLoader::HookMap["gml_Script_PlayAsCharacter"];
				RValue* ret = result;
				if (hook->NotifyPreSubscribers(self, other, result, argc, argv))
				{
					hook->NotifyPostSubscribers(self, other, ret, argc, argv);
					return ret;
				}

				ret =
					hook->reference.GetOriginal<GMLScript_t>()(
						self, other, result, argc, argv);

				ModLoader::LogGMLCall(
					hook->hookName.c_str(),
					self, argc, argv, ret);

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

				hook->NotifyPostSubscribers(self, other, ret, argc, argv);
				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_PlayerIsDailyChallenger",
			0x1CE8A0,
			+[](uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv) -> RValue*
			{
				auto hook = ModLoader::HookMap["gml_Script_PlayerIsDailyChallenger"];
				RValue* ret = result;
				if (!hook->NotifyPreSubscribers(self, other, result, argc, argv))
					ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
				// ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, ret);
				hook->NotifyPostSubscribers(self, other, ret, argc, argv);

				if (ModLoader::isDrawing)
				{
					using callGML_t = uint32_t * (__cdecl*)(int, int, void*, int, int, int);
					auto callGML = (callGML_t)(HookBase::moduleBase + 0xCA7F30);
					int drawTextHandle = *(int*)(HookBase::moduleBase + 0x10C3CEC);

					RValue argX{}, argY{};
					argX.real = 100.0; argX.type = 0;
					argY.real = 200.0; argY.type = 0;

					RValue* argPtrs[3] = {
						&argX,
						&argY,
						&ModLoader::nameVal,
					};

					RValue resBuf{};
					callGML((int)(uintptr_t)self, (int)(uintptr_t)other, &resBuf, 3, drawTextHandle, (int)argPtrs);
				}

				return ret;
			},
			true
		),
		new Hook<GMLScript_t>(
			"gml_Script_GenerateGun",
			0x003DF2C0,
			+[](uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv) -> RValue*
			{
				auto hook = ModLoader::HookMap["gml_Script_GenerateGun"];
				RValue* ret = result;
				if (!hook->NotifyPreSubscribers(self, other, result, argc, argv))
				{
					ret = hook->reference.GetOriginal<GMLScript_t>()(self, other, result, argc, argv);
					ModLoader::LogGMLCall(hook->hookName.c_str(), self, argc, argv, ret);

					using sub_CBC420_t = int(__cdecl*)(uint32_t*);
					using sub_C99410_t = int(__cdecl*)(int, int, int, RValue*);
					using SetVar_t = int(__cdecl*)(int, int, int, RValue*);
					using SetString_t = int(__cdecl*)(RValue*, const char*);

					auto SetVar = (SetVar_t)(HookBase::moduleBase + 0xC996F0);
					auto GetVar = (sub_C99410_t)(HookBase::moduleBase + 0xC99410);
					auto ResolveInstance = (sub_CBC420_t)(HookBase::moduleBase + 0xCBC420);
					auto SetString = (SetString_t)(HookBase::moduleBase + 0xCAB130);

					int instance_handle = ResolveInstance((uint32_t*)argv[0]);
					ModLoader::Log("gml_Script_GenerateGun", "instance=0x%08X", instance_handle);

					RValue traits{};
					GetVar(instance_handle, 664, 0x80000000, &traits);
					ModLoader::LogRValue("Weapon Traits", &traits);

					/*
					struct IterState
					{
						uint8_t data[8];   // matches stack iterator buffer (v28)
					};

					using fn_CA8590 = int(__cdecl*)(IterState*, void*, void*, int);
					using fn_C9AC30 = uint8_t(__cdecl*)(IterState*, void*);
					using fn_CC62D0 = int(__cdecl*)(RValue*, int, void*);
					using fn_GetKey = int(__cdecl*)(IterState*, void*);

					fn_CA8590  sub_addr_CA8590 = (fn_CA8590)(HookBase::moduleBase + 0xCA8590);
					fn_C9AC30  sub_addr_C9AC30 = (fn_C9AC30)(HookBase::moduleBase + 0xC9AC30);
					fn_CC62D0  sub_addr_CC62D0 = (fn_CC62D0)(HookBase::moduleBase + 0xCC62D0);
					// fn_GetKey  sub_addr_GetKey = (fn_GetKey)0xGETKEYADDR; // replace

					IterState it;

					void* container = &instance_handle;

					// qword_44DEFB0 is a POINTER stored at that address
					void* ctx = *(void**)(HookBase::moduleBase + 0x44DEFB0);

					if (ctx && sub_addr_CA8590(&it, container, other, 0) > 0)
					{
						do
						{
							int key;

							// Original:
							// v12 ? v12 + 10608 : virtual_get(container, 663)

							int v12 = *(int*)((uint8_t*)container + 4);

							if (v12)
							{
								key = v12 + 10608;
							}
							else
							{
								using fn_virtual_get = int(__thiscall*)(void*, int);

								auto vtbl = *(void***)container;

								key =
									((fn_virtual_get)vtbl[1])(container, 663);
							}

							RValue value{};

							sub_addr_CC62D0(&value, key, ctx);

							ModLoader::LogRValue("Trait:", &value);

						} while (sub_addr_C9AC30(&it, container));
					}
					*/

					// for (int i = 0; i < traits.arr->length; i++)
					//	ModLoader::LogRValue("Weapon Traits", &(traits.arr->data[i]));
				}
				hook->NotifyPostSubscribers(self, other, ret, argc, argv);
				return ret;
			},
			true
		),
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
	};

	ModLoader();
	ModLoader(const ModLoader&) = delete;
	ModLoader& operator=(const ModLoader&) = delete;

	void CreateHooks();
	void LoadMods();

private:
	std::vector<std::unique_ptr<SE_ModApi>> m_modApis;
	std::unique_ptr<ModConfig>              m_loaderConfig;
};

