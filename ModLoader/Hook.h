#pragma once

#include <string>
#include <tuple>
#include <type_traits>
#include <mutex>
#include <vector>
#include <utility>
#include <cassert>

#include <MinHook.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include "ModInterface.h"

// Set by RequestBypass (via SE_ModApi) inside a pre-hook callback.
// NotifyPreSubscribers resets it before each notification pass and returns
// its final value so callers know whether to skip the original function.
extern thread_local bool g_hookBypassRequested;

struct HookReference
{
    uintptr_t offset;
    void* originalFunction;
    void* hookFunction;

    template <typename Fn>
    Fn GetOriginal() const {
        return reinterpret_cast<Fn>(originalFunction);
    }
};

struct HookBase {
    static uintptr_t moduleBase;
    static size_t moduleSize;

    HookReference reference;
    std::string hookName;
    const char* pattern;
    const char* mask;
    bool alwaysLoad;

    // Subscriptions are added during single-threaded LoadMods and only read afterwards, so no lock.
    std::vector<std::pair<SE_HookCallback, void*>> preSubscribers;
    std::vector<std::pair<SE_HookPostCallback, void*>> postSubscribers;

    // Returns true if any subscriber requested a bypass (skip original call).
    bool NotifyPreSubscribers(uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv)
    {
        g_hookBypassRequested = false;
        for (auto& sub : preSubscribers)
            sub.first(hookName.c_str(), self, other, result, argc, argv, sub.second);
        return g_hookBypassRequested;
    }

    void NotifyPostSubscribers(uintptr_t* self, uintptr_t* other, RValue* returnValue, int argc, RValue** argv)
    {
        for (auto& sub : postSubscribers)
            sub.first(hookName.c_str(), self, other, returnValue, argc, argv, sub.second);
    }

    virtual ~HookBase() = default;
    virtual void CreateHook() = 0;
};

// Primary template (undefined)
template <typename>
struct function_traits;

// Specialization for function types
template <typename R, typename... Args>
struct function_traits<R(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    using fn_type = R(Args...);
};

class MinHookManager {
public:
    static void Initialize() {
        static std::once_flag flag;
        std::call_once(flag, []() {
            MH_Initialize();
        });
    }
};

template <typename TReturn, typename... TArgs>
class Hook : public HookBase {
public:
    Hook(const char* _hookName, uintptr_t _offset, void* _hookFunction, bool _alwaysLoad = false)
    {
        hookName = _hookName;
        alwaysLoad = _alwaysLoad;

        reference.offset = _offset;
        reference.hookFunction = _hookFunction;
        reference.originalFunction = nullptr;
    };

    Hook(const char* _hookName, const char* _pattern, const char* _mask, void* _hookFunction, bool _alwaysLoad = false)
    {
        hookName = _hookName;
        alwaysLoad = _alwaysLoad;
        pattern = _pattern;
        mask = _mask;

        reference.offset = 0;
        reference.hookFunction = _hookFunction;
        reference.originalFunction = nullptr;
    };

    uintptr_t PatternScan(uintptr_t base, size_t size, const char* pattern, const char* mask)
    {
        size_t patternLen = strlen(mask);

        for (size_t i = 0; i < size - patternLen; i++) {
            bool found = true;
            for (size_t j = 0; j < patternLen; j++) {
                if (mask[j] == 'x' && pattern[j] != *(char*)(base + i + j)) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return i;
            }
        }
        return 0;
    }

    void GetModuleCodeRegion(const char* moduleName, uintptr_t* o_base, size_t* o_size)
    {
        HMODULE hModule = GetModuleHandleA(moduleName);
        *o_base = (uintptr_t)hModule;
        *o_size = 0;

        MODULEINFO mi{};
        if (GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi)))
            *o_size = mi.SizeOfImage;
    }

    void CreateHook()
    {
        if (moduleBase == 0)
        {
            GetModuleCodeRegion("Heat_Signature.exe", &moduleBase, &moduleSize);
            ModLoader::Log("ModLoader", "Module base found at 0x%p", moduleBase);
        }

        if (reference.offset == 0 && pattern)
        {
            reference.offset = PatternScan(moduleBase, moduleSize, pattern, mask);
        }

        if (!reference.offset)
            return;

        void* targetAddr = (void*)(moduleBase + reference.offset);

        // MinHookManager::Initialize();

        if (MH_CreateHook(targetAddr, reference.hookFunction, &reference.originalFunction) != MH_OK) {
            ModLoader::Log("ModLoader", "Failed to install %s", hookName.c_str());
            return;
        }

        if (MH_EnableHook(targetAddr) != MH_OK)
        {
            ModLoader::Log("ModLoader", "Failed to enable %s", hookName.c_str());
            return;
        }

        ModLoader::Log("ModLoader", "%s installed and enabled at 0x%p (0x%p)", hookName.c_str(), targetAddr, reference.offset);
    };
};
