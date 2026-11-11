#include "ModLoader.h"
#include "GameFunctions.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <MinHook.h>

#include "GameAddresses.h"
#include "UniversalHooks.h"

namespace {

struct ImageRange
{
    uintptr_t start = 0;
    uintptr_t end = 0;

    bool Contains(uintptr_t address, size_t length = 1) const
    {
        return address >= start && address < end && length <= end - address;
    }
};

struct ImageLayout
{
    ImageRange image;
    ImageRange code;
    std::vector<ImageRange> initializedData;
};

ImageLayout ReadImageLayout(uintptr_t base)
{
    ImageLayout layout;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    layout.image = { base, base + nt->OptionalHeader.SizeOfImage };

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        uintptr_t start = base + section->VirtualAddress;
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            ImageRange range{ start, start + section->Misc.VirtualSize };
            if (range.end - range.start > layout.code.end - layout.code.start)
                layout.code = range;
        }
        else if (section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
        {
            size_t size = (std::min)(static_cast<size_t>(section->Misc.VirtualSize),
                                     static_cast<size_t>(section->SizeOfRawData));
            layout.initializedData.push_back({ start, start + size });
        }
    }
    return layout;
}

uintptr_t FindPattern(const ImageRange& range, const uint8_t* pattern, const char* mask)
{
    const size_t length = std::strlen(mask);
    for (uintptr_t at = range.start; at + length <= range.end; ++at)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(at);
        size_t i = 0;
        while (i < length && (mask[i] == '?' || bytes[i] == pattern[i]))
            ++i;
        if (i == length)
            return at;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// gml_Script_* functions
// ---------------------------------------------------------------------------

struct YYCFunctionEntry
{
    const char* name;
    uintptr_t function;
};
static_assert(sizeof(YYCFunctionEntry) == 8, "YYC function table is laid out for 32-bit builds");

struct YYCFunctionTable
{
    const YYCFunctionEntry* entries = nullptr;
    size_t count = 0;
};

bool IsYYCFunctionEntry(const ImageLayout& layout, uintptr_t at)
{
    const auto* entry = reinterpret_cast<const YYCFunctionEntry*>(at);
    return layout.image.Contains(reinterpret_cast<uintptr_t>(entry->name), 5)
        && std::memcmp(entry->name, "gml_", 4) == 0
        && layout.code.Contains(entry->function);
}

// The table is the longest run of {"gml_...", code pointer} pairs in initialized data.
YYCFunctionTable FindYYCFunctionTable(const ImageLayout& layout)
{
    constexpr size_t kStride = sizeof(YYCFunctionEntry);
    YYCFunctionTable best;

    for (const ImageRange& range : layout.initializedData)
    {
        uintptr_t at = (range.start + 3) & ~uintptr_t(3);
        while (at + kStride <= range.end)
        {
            size_t run = 0;
            while (at + (run + 1) * kStride <= range.end && IsYYCFunctionEntry(layout, at + run * kStride))
                ++run;

            if (run > best.count)
                best = { reinterpret_cast<const YYCFunctionEntry*>(at), run };

            at += run ? run * kStride : 4;
        }
    }
    return best;
}

void RegisterScriptHooks(const ImageLayout& layout)
{
    const YYCFunctionTable table = FindYYCFunctionTable(layout);
    if (!table.count)
    {
        ModLoader::Log("ModLoader", "YYC function table not found; gml_Script_ functions cannot be hooked or called");
        return;
    }

    size_t registered = 0;
    size_t shared = 0;
    size_t dropped = 0;
    for (size_t i = 0; i < table.count; ++i)
    {
        const YYCFunctionEntry& entry = table.entries[i];
        if (std::strncmp(entry.name, "gml_Script_", 11) != 0)
            continue;

        if (ModLoader::HookMap.count(entry.name))
        {
            ++shared;
            continue;
        }

        HookBase* hook = CreateUniversalScriptHook(entry.name, entry.function - HookBase::moduleBase);
        if (!hook)
        {
            ModLoader::Log("ModLoader", "Out of universal hook slots, dropping '%s'", entry.name);
            ++dropped;
            continue;
        }

        ModLoader::HookMap[entry.name] = hook;
        ++registered;
    }

    ModLoader::Log("ModLoader",
        "YYC function table at 0x%p (%zu entries): registered %zu gml_Script_ hooks (%zu shared with hardcoded hooks, %zu dropped)",
        table.entries, table.count, registered, shared, dropped);
}

// ---------------------------------------------------------------------------
// Engine built-ins
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct BuiltinEntry
{
    char     name[0x40];
    uint32_t function;
    uint32_t argCount;
    uint8_t  flags;
    uint8_t  pad[3];
    int32_t  unk; // initialized to -1
};
#pragma pack(pop)
static_assert(sizeof(BuiltinEntry) == 0x50, "BuiltinEntry size mismatch");

using BuiltinDispatcher_t = RValue*(__cdecl*)(CInstance* self, CInstance* other, RValue* result, int argc, uint32_t index, RValue** argv);

uintptr_t s_dispatcher = 0;
BuiltinDispatcher_t s_dispatcherOriginal = nullptr;
BuiltinEntry* const* s_registry = nullptr;
const uint32_t* s_registryCount = nullptr;

class BuiltinHook;

std::mutex s_builtinMutex;
std::vector<BuiltinHook*> s_pendingBuiltins;
std::unordered_map<std::string, uint32_t> s_builtinIndices;
std::unique_ptr<std::atomic<HookBase*>[]> s_builtinHookStorage;
uint32_t s_builtinHookCount = 0;
std::atomic<std::atomic<HookBase*>*> s_builtinHooksByIndex{ nullptr };

class BuiltinHook : public HookBase
{
public:
    explicit BuiltinHook(const char* name)
    {
        hookName = name;
        pattern = nullptr;
        mask = nullptr;
        alwaysLoad = false;
        reference = {};
    }

    std::atomic<int32_t> index{ -1 };

    void CreateHook() override;
    RValue* Invoke(CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv) override;
};

// Requires s_builtinMutex and s_builtinHookStorage.
bool BindBuiltinLocked(BuiltinHook* hook)
{
    auto it = s_builtinIndices.find(hook->hookName);
    if (it == s_builtinIndices.end())
    {
        ModLoader::Log("ModLoader", "Unknown built-in '%s'", hook->hookName.c_str());
        return false;
    }
    hook->index.store(static_cast<int32_t>(it->second), std::memory_order_release);
    s_builtinHookStorage[it->second].store(hook, std::memory_order_release);
    return true;
}

// Only called once GML is running: the engine fills the registry during startup,
// possibly while ModInit is still executing on the load thread.
std::atomic<HookBase*>* ResolveBuiltinsLocked()
{
    if (auto* table = s_builtinHooksByIndex.load(std::memory_order_acquire))
        return table;

    if (!s_registry || !*s_registry)
        return nullptr;

    const uint32_t count = *s_registryCount;
    if (count == 0 || count > 50000)
    {
        ModLoader::Log("ModLoader", "Built-in registry has suspicious count %u", count);
        return nullptr;
    }

    const BuiltinEntry* entries = *s_registry;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (entries[i].name[0] != '\0')
            s_builtinIndices.emplace(std::string(entries[i].name, strnlen(entries[i].name, sizeof(entries[i].name))), i);
    }

    s_builtinHookStorage = std::make_unique<std::atomic<HookBase*>[]>(count);
    s_builtinHookCount = count;

    for (BuiltinHook* hook : s_pendingBuiltins)
        BindBuiltinLocked(hook);
    s_pendingBuiltins.clear();

    s_builtinHooksByIndex.store(s_builtinHookStorage.get(), std::memory_order_release);
    ModLoader::Log("ModLoader", "Built-in registry resolved: %zu named entries", s_builtinIndices.size());
    return s_builtinHookStorage.get();
}

void BuiltinHook::CreateHook()
{
    if (!s_dispatcherOriginal)
        return;

    std::lock_guard<std::mutex> lock(s_builtinMutex);
    if (s_builtinHooksByIndex.load(std::memory_order_acquire))
    {
        if (!BindBuiltinLocked(this))
            return;
    }
    else
    {
        s_pendingBuiltins.push_back(this);
    }
    reference.originalFunction = reinterpret_cast<void*>(s_dispatcherOriginal);
}

RValue* BuiltinHook::Invoke(CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv)
{
    int32_t resolved = index.load(std::memory_order_acquire);
    if (resolved < 0)
    {
        std::lock_guard<std::mutex> lock(s_builtinMutex);
        ResolveBuiltinsLocked();
        resolved = index.load(std::memory_order_acquire);
    }
    if (resolved < 0)
    {
        ModLoader::Log("ModLoader", "Built-in '%s' called before the engine registry was ready", hookName.c_str());
        return result;
    }
    return reinterpret_cast<BuiltinDispatcher_t>(s_dispatcher)(self, other, result, argc, static_cast<uint32_t>(resolved), argv);
}

RValue* __cdecl BuiltinDispatcherDetour(CInstance* self, CInstance* other, RValue* result, int argc, uint32_t index, RValue** argv)
{
    std::atomic<HookBase*>* hooks = s_builtinHooksByIndex.load(std::memory_order_acquire);
    if (!hooks)
    {
        std::lock_guard<std::mutex> lock(s_builtinMutex);
        hooks = ResolveBuiltinsLocked();
    }

    HookBase* hook = (hooks && index < s_builtinHookCount) ? hooks[index].load(std::memory_order_acquire) : nullptr;
    if (!hook)
        return s_dispatcherOriginal(self, other, result, argc, index, argv);

    RValue* ret = result;
    if (!hook->NotifyPreSubscribers(self, other, result, argc, argv))
    {
        ret = s_dispatcherOriginal(self, other, result, argc, index, argv);
        hook->NotifyPostSubscribers(self, other, ret, argc, argv);
    }
    return ret;
}

void InstallBuiltinDispatcherHook(const ImageLayout& layout)
{
    const uintptr_t dispatcher = FindPattern(layout.code, GameAddresses::kBuiltinDispatcher, GameAddresses::kBuiltinDispatcherMask);
    if (!dispatcher)
    {
        ModLoader::Log("ModLoader", "Built-in dispatcher not found; engine built-ins cannot be hooked or called");
        return;
    }

    const uintptr_t registryGlobal = *reinterpret_cast<const uint32_t*>(dispatcher + GameAddresses::kBuiltinDispatcherRegistryOperand);
    if (!layout.image.Contains(registryGlobal, 8))
    {
        ModLoader::Log("ModLoader", "Built-in registry operand 0x%p is outside the image", reinterpret_cast<void*>(registryGlobal));
        return;
    }

    s_dispatcher = dispatcher;
    s_registry = reinterpret_cast<BuiltinEntry* const*>(registryGlobal);
    s_registryCount = reinterpret_cast<const uint32_t*>(registryGlobal + 4);

    void* original = nullptr;
    if (MH_CreateHook(reinterpret_cast<void*>(dispatcher), reinterpret_cast<void*>(&BuiltinDispatcherDetour), &original) != MH_OK
        || MH_EnableHook(reinterpret_cast<void*>(dispatcher)) != MH_OK)
    {
        ModLoader::Log("ModLoader", "Failed to hook the built-in dispatcher at 0x%p", reinterpret_cast<void*>(dispatcher));
        return;
    }
    s_dispatcherOriginal = reinterpret_cast<BuiltinDispatcher_t>(original);

    ModLoader::Log("ModLoader", "Built-in dispatcher hooked at 0x%p (registry global 0x%p)",
        reinterpret_cast<void*>(dispatcher), reinterpret_cast<void*>(registryGlobal));
}

} // namespace

void RegisterGameFunctions()
{
    const ImageLayout layout = ReadImageLayout(HookBase::moduleBase);
    RegisterScriptHooks(layout);
    InstallBuiltinDispatcherHook(layout);
}

HookBase* FindOrCreateHook(const char* name)
{
    if (!name)
        return nullptr;

    static std::mutex s_hookMapMutex;
    std::lock_guard<std::mutex> lock(s_hookMapMutex);

    auto it = ModLoader::HookMap.find(name);
    if (it != ModLoader::HookMap.end())
        return it->second;

    if (std::strncmp(name, "gml_", 4) == 0)
        return nullptr;

    auto* hook = new BuiltinHook(name);
    ModLoader::HookMap.emplace(name, hook);
    return hook;
}
