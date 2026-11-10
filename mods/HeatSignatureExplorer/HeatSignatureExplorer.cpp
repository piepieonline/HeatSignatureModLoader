#include "ModInterface.h"
#include "GmArgs.h"
#include "PropertyNames.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

HS_EXPORT_MOD_API_VERSION()

static constexpr int    kNoArrayIndex       = 0x80000000;
static constexpr int    kFirstInstanceId    = 100000;
static constexpr int    kAllInstances       = -3;
static constexpr int    kNoInstance         = -4;
static constexpr int    kMaxArrayElements   = 256;
static constexpr int    kMaxInstances       = 20000;
static constexpr double kChangeHighlightSec = 1.0;

static const HS_ModApi* g_api = nullptr;
static bool             g_showWindow = false;
static std::atomic<int> g_playerHandle{ 0 };

struct VarSlot
{
    const char* name;
    int         id;
};

struct WatchedVar
{
    const char* name;
    int         id;
    RValue      value;
    std::string text;
    double      lastChangeTime;
    bool        everChanged;
};

struct InstanceEntry
{
    int         handle;
    std::string objectName;
};

static std::vector<VarSlot>                   g_allVars;
static std::unordered_map<uint32_t, std::string> g_objectNameByIndex;

static int                     g_viewHandle = 0;
static std::string             g_viewObjectName;
static std::vector<WatchedVar> g_viewVars;
static std::vector<int>        g_history;

static int             g_inputHandle = 100016;
static ImGuiTextFilter g_varFilter;
static bool            g_changedOnly = false;

static std::vector<InstanceEntry> g_instances;
static ImGuiTextFilter            g_instanceFilter;

static bool   g_varTableMissing = false;
static int    g_editVarIndex = -1;
static double g_editValue = 0.0;
static bool   g_openEditPopup = false;
static int    g_arrayVarIndex = -1;
static int    g_pendingInspectHandle = 0;
static bool   g_pendingInspectPushHistory = false;

static bool EnsureVarIds()
{
    if (!g_allVars.empty()) return true;

    for (const char* name : kPropertyNames)
    {
        int id = g_api->GetVarId(name);
        if (id >= 0) g_allVars.push_back({ name, id });
    }
    std::sort(g_allVars.begin(), g_allVars.end(), [](const VarSlot& a, const VarSlot& b)
    {
        return _stricmp(a.name, b.name) < 0;
    });
    return !g_allVars.empty();
}

static bool HoldsValue(const RValue& v)
{
    RValueKind kind = GetRValueKind(v);
    return kind != RValueKind::Unset
        && kind != RValueKind::Undefined
        && strcmp(GetTypeName(v.type), "UNKNOWN") != 0;
}

static std::string ObjectNameOf(int handle)
{
    CInstance* inst = g_api->ResolveCInstance(handle);
    if (!inst) return {};

    auto it = g_objectNameByIndex.find(inst->type);
    if (it != g_objectNameByIndex.end()) return it->second;

    std::string name = inst->GetObjectName(g_api);
    g_objectNameByIndex[inst->type] = name;
    return name;
}

static int AsInstanceHandle(const RValue& v)
{
    if (GetRValueKind(v) != RValueKind::Real) return 0;
    if (v.real < kFirstInstanceId || v.real != std::floor(v.real)) return 0;
    int handle = static_cast<int>(v.real);
    return g_api->ResolveCInstance(handle) ? handle : 0;
}

static bool IsEditable(const RValue& v)
{
    RValueKind kind = GetRValueKind(v);
    return kind == RValueKind::Real || kind == RValueKind::Bool;
}

static std::string FormatValue(const RValue& v)
{
    char buf[128];
    switch (GetRValueKind(v))
    {
    case RValueKind::Real:
    case RValueKind::Bool:
        snprintf(buf, sizeof(buf), "%.9g", v.real);
        return buf;
    case RValueKind::Int32:
        snprintf(buf, sizeof(buf), "%d", v.i32);
        return buf;
    case RValueKind::Int64:
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.i64));
        return buf;
    case RValueKind::String:
        return (v.str && v.str->text) ? std::string("\"") + v.str->text + "\"" : "(null string)";
    case RValueKind::Array:
        return "[array]";
    case RValueKind::Pointer:
        snprintf(buf, sizeof(buf), "0x%p", v.ptr);
        return buf;
    default:
        return GetTypeName(v.type);
    }
}

static void Inspect(int handle, bool pushHistory)
{
    g_varTableMissing = !EnsureVarIds();
    if (g_varTableMissing) return;

    if (pushHistory && g_viewHandle != 0 && g_viewHandle != handle)
        g_history.push_back(g_viewHandle);

    g_viewHandle = handle;
    g_inputHandle = handle;
    g_viewObjectName = ObjectNameOf(handle);
    g_viewVars.clear();
    g_editVarIndex = -1;
    g_arrayVarIndex = -1;

    for (const VarSlot& slot : g_allVars)
    {
        RValue v{};
        if (!g_api->GetVar(handle, slot.id, kNoArrayIndex, &v) || !HoldsValue(v))
            continue;
        g_viewVars.push_back({ slot.name, slot.id, v, FormatValue(v), -1e9, false });
    }
}

static void RequestInspect(int handle, bool pushHistory)
{
    g_pendingInspectHandle = handle;
    g_pendingInspectPushHistory = pushHistory;
}

static void ApplyPendingInspect()
{
    if (g_pendingInspectHandle == 0) return;
    int handle = g_pendingInspectHandle;
    g_pendingInspectHandle = 0;
    Inspect(handle, g_pendingInspectPushHistory);
}

static void RefreshViewVars()
{
    double now = ImGui::GetTime();
    for (WatchedVar& var : g_viewVars)
    {
        RValue v{};
        if (!g_api->GetVar(g_viewHandle, var.id, kNoArrayIndex, &v))
            continue;
        std::string text = FormatValue(v);
        var.value = v;
        if (text == var.text) continue;
        var.text = std::move(text);
        var.lastChangeTime = now;
        var.everChanged = true;
    }
}

static void RefreshInstanceList()
{
    g_instances.clear();
    for (int i = 0; i < kMaxInstances; ++i)
    {
        GmArgs args;
        args.AddReal(kAllInstances);
        args.AddReal(i);
        RValue out{};
        g_api->CallEngineScript("instance_find", nullptr, nullptr, &out, args.Count(), args.Build());
        if (GetRValueKind(out) != RValueKind::Real || out.real == kNoInstance)
            break;
        int handle = static_cast<int>(out.real);
        g_instances.push_back({ handle, ObjectNameOf(handle) });
    }
}

static void DrawValueCell(const RValue& v, const std::string& text, int varIndex)
{
    if (int ref = AsInstanceHandle(v))
    {
        std::string label = text + " (" + ObjectNameOf(ref) + ")##ref" + std::to_string(varIndex);
        if (ImGui::SmallButton(label.c_str()))
            RequestInspect(ref, true);
    }
    else if (GetRValueKind(v) == RValueKind::Array)
    {
        std::string label = "[array]##arr" + std::to_string(varIndex);
        if (ImGui::SmallButton(label.c_str()))
            g_arrayVarIndex = varIndex;
    }
    else
    {
        ImGui::TextUnformatted(text.c_str());
    }

    if (IsEditable(v) && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        g_editVarIndex = varIndex;
        g_editValue = v.real;
        g_openEditPopup = true;
    }
}

static void DrawEditPopup()
{
    if (g_openEditPopup)
    {
        ImGui::OpenPopup("EditVar");
        g_openEditPopup = false;
    }
    if (!ImGui::BeginPopup("EditVar")) return;

    if (g_editVarIndex < 0 || g_editVarIndex >= static_cast<int>(g_viewVars.size()))
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    WatchedVar& var = g_viewVars[g_editVarIndex];
    ImGui::Text("%s", var.name);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputDouble("##EditValue", &g_editValue, 0.0, 0.0, "%.9g");
    bool submitted = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::Button("Set") || submitted)
    {
        RValue v{};
        v.type = static_cast<uint32_t>(RValueKind::Real);
        v.real = g_editValue;
        g_api->SetVar(g_viewHandle, var.id, kNoArrayIndex, &v);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void DrawArrayWindow()
{
    if (g_arrayVarIndex < 0 || g_arrayVarIndex >= static_cast<int>(g_viewVars.size())) return;

    const WatchedVar& var = g_viewVars[g_arrayVarIndex];
    bool open = true;
    std::string title = std::string(var.name) + " [" + std::to_string(g_viewHandle) + "]###ExplorerArray";
    ImGui::SetNextWindowSize(ImVec2(360, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &open))
    {
        int varId = var.id;
        if (ImGui::BeginTable("ArrayElements", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();

            for (int i = 0; i < kMaxArrayElements; ++i)
            {
                RValue v{};
                if (!g_api->GetVar(g_viewHandle, varId, i, &v) || !HoldsValue(v))
                    break;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GetTypeName(v.type));
                ImGui::TableNextColumn();
                std::string text = FormatValue(v);
                if (int ref = AsInstanceHandle(v))
                {
                    std::string label = text + " (" + ObjectNameOf(ref) + ")##elem" + std::to_string(i);
                    if (ImGui::SmallButton(label.c_str()))
                    {
                        RequestInspect(ref, true);
                        open = false;
                    }
                }
                else
                {
                    ImGui::TextUnformatted(text.c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    if (!open) g_arrayVarIndex = -1;
}

static void DumpToLog(const std::vector<int>& varIndices)
{
    char line[1024];
    snprintf(line, sizeof(line), "Dump of %d %s (%zu vars)", g_viewHandle, g_viewObjectName.c_str(), varIndices.size());
    g_api->Log("HeatSignatureExplorer", line);
    for (int varIndex : varIndices)
    {
        const WatchedVar& var = g_viewVars[varIndex];
        snprintf(line, sizeof(line), "  %s (%d) %s = %s", var.name, var.id, GetTypeName(var.value.type), var.text.c_str());
        g_api->Log("HeatSignatureExplorer", line);
    }
}

static void DrawInspectorTab()
{
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##Handle", &g_inputHandle, 0, 0);
    bool submitted = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::Button("Inspect") || submitted)
        RequestInspect(g_inputHandle, true);
    ImGui::SameLine();
    int player = g_playerHandle.load();
    ImGui::BeginDisabled(player == 0);
    if (ImGui::Button("Player"))
        RequestInspect(player, true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_history.empty());
    if (ImGui::Button("Back"))
    {
        int previous = g_history.back();
        g_history.pop_back();
        RequestInspect(previous, false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_viewHandle == 0);
    if (ImGui::Button("Rescan"))
        RequestInspect(g_viewHandle, false);
    ImGui::EndDisabled();

    if (g_varTableMissing)
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Variable table not available yet.");
        return;
    }
    if (g_viewHandle == 0)
    {
        ImGui::TextDisabled("Enter an instance handle (>= 100000) or pick one from the Instances tab.");
        return;
    }

    if (!g_api->ResolveCInstance(g_viewHandle))
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Instance %d no longer exists.", g_viewHandle);
        return;
    }

    RefreshViewVars();

    ImGui::Text("%d  %s  (%zu vars)", g_viewHandle, g_viewObjectName.c_str(), g_viewVars.size());
    ImGui::SameLine();
    bool dumpRequested = ImGui::SmallButton("Dump to log");
    g_varFilter.Draw("Filter##Vars", 200);
    ImGui::SameLine();
    ImGui::Checkbox("Changed only", &g_changedOnly);
    ImGui::SameLine();
    ImGui::TextDisabled("(right-click a number to edit)");

    std::vector<int> visible;
    visible.reserve(g_viewVars.size());
    for (int i = 0; i < static_cast<int>(g_viewVars.size()); ++i)
    {
        const WatchedVar& var = g_viewVars[i];
        if (g_changedOnly && !var.everChanged) continue;
        if (!g_varFilter.PassFilter(var.name)) continue;
        visible.push_back(i);
    }
    if (dumpRequested)
        DumpToLog(visible);

    double now = ImGui::GetTime();
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("Vars", 4, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                int varIndex = visible[row];
                const WatchedVar& var = g_viewVars[varIndex];

                ImGui::TableNextRow();
                double sinceChange = now - var.lastChangeTime;
                if (sinceChange < kChangeHighlightSec)
                {
                    float alpha = 0.5f * static_cast<float>(1.0 - sinceChange / kChangeHighlightSec);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, alpha)));
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(var.name);
                ImGui::TableNextColumn();
                ImGui::Text("%d", var.id);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GetTypeName(var.value.type));
                ImGui::TableNextColumn();
                DrawValueCell(var.value, var.text, varIndex);
            }
        }
        ImGui::EndTable();
    }

    DrawEditPopup();
}

static void DrawInstancesTab()
{
    if (ImGui::Button("Refresh"))
        RefreshInstanceList();
    ImGui::SameLine();
    g_instanceFilter.Draw("Filter##Instances", 200);
    ImGui::SameLine();
    ImGui::Text("%zu instances", g_instances.size());

    if (ImGui::BeginTable("Instances", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Object");
        ImGui::TableHeadersRow();

        for (const InstanceEntry& entry : g_instances)
        {
            if (!g_instanceFilter.PassFilter(entry.objectName.c_str())) continue;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string label = std::to_string(entry.handle);
            if (ImGui::Selectable(label.c_str(), entry.handle == g_viewHandle, ImGuiSelectableFlags_SpanAllColumns))
                RequestInspect(entry.handle, true);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.objectName.c_str());
        }
        ImGui::EndTable();
    }
}

static void OnImGuiDraw(void*)
{
    if (!g_showWindow) return;

    ApplyPendingInspect();

    ImGui::SetNextWindowSize(ImVec2(560, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Heat Signature Explorer", &g_showWindow))
    {
        if (ImGui::BeginTabBar("ExplorerTabs"))
        {
            if (ImGui::BeginTabItem("Inspector"))
            {
                DrawInspectorTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Instances"))
            {
                DrawInstancesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    DrawArrayWindow();
}

static void OnMainMenu(void*)
{
    if (!ImGui::BeginMenu("Explorer")) return;
    ImGui::MenuItem("Show window", nullptr, &g_showWindow);
    ImGui::EndMenu();
}

static void OnPlayAsCharacterPost(const char*, CInstance*, CInstance*, RValue*, int argc, RValue** argv, void*)
{
    if (argc > 0)
        g_playerHandle.store(g_api->ResolveInstance(reinterpret_cast<uint32_t*>(argv[0])));
}

extern "C" __declspec(dllexport)
void ModInit(const HS_ModApi* api)
{
    g_api = api;

    void* alloc = nullptr; void* freeFn = nullptr; void* ud = nullptr;
    api->GetImGuiAllocators(&alloc, &freeFn, &ud);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)alloc, (ImGuiMemFreeFunc)freeFn, ud);
    ImGui::SetCurrentContext((ImGuiContext*)api->GetImGuiContext());

    api->SubscribeHookPost("gml_Script_PlayAsCharacter", &OnPlayAsCharacterPost, nullptr);
    api->RegisterImGuiDraw(&OnImGuiDraw, nullptr);
    api->RegisterImGuiMainMenu(&OnMainMenu, nullptr);

    api->Log("HeatSignatureExplorer", "Loaded");
}
