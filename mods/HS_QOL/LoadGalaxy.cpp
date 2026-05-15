#include "LoadGalaxy.h"
#include "GmArgs.h"

#include <imgui.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>

static const HS_ModApi*       g_api = nullptr;
static std::vector<std::string> g_galaxyFolders;

static void LoadGalaxy(const std::string& name)
{
    GmArgs args;
    args.AddStr(g_api, (name + "\\").c_str());

    RValue result{};
    g_api->CallScript(
        "gml_Script_LoadGalaxy",
        nullptr, nullptr,
        &result,
        args.Count(), args.Build());
}

static void RefreshGalaxyList()
{
    g_galaxyFolders.clear();

    const char* appData = std::getenv("APPDATA");
    if (!appData) return;

    std::filesystem::path heatSigPath = std::filesystem::path(appData) / "Heat_Signature";
    if (!std::filesystem::exists(heatSigPath) ||
        !std::filesystem::is_directory(heatSigPath)) return;

    for (const auto& entry : std::filesystem::directory_iterator(heatSigPath))
    {
        if (!entry.is_directory()) continue;

        std::filesystem::path galaxyFile = entry.path() / "Galaxy.txt";
        if (std::filesystem::exists(galaxyFile) &&
            std::filesystem::is_regular_file(galaxyFile))
        {
            g_galaxyFolders.push_back(entry.path().filename().string());
        }
    }
}

static void OnMainMenu(void* /*userData*/)
{
    if (!ImGui::BeginMenu("Load Galaxy")) return;

    if (ImGui::MenuItem("Refresh list"))
        RefreshGalaxyList();

    ImGui::Separator();

    for (const std::string& folder : g_galaxyFolders)
    {
        if (ImGui::MenuItem(folder.c_str()))
            LoadGalaxy(folder);
    }

    ImGui::EndMenu();
}

void LoadGalaxyMenu_Register(const HS_ModApi* api)
{
    g_api = api;
    RefreshGalaxyList();
    api->RegisterImGuiMainMenu(OnMainMenu, nullptr);
}
