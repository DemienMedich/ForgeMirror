#include "AppContext.h"
#include "AppDomainTypes.h"
#include "AppProfileService.h"
#include "AppTaskProjectService.h"
#include "AppProfessionService.h"
#include "AppWorkspaceDataService.h"
#include "AppUtils.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"
#include "CloudSync.h"
#include "GuiActions.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <commdlg.h>
#  include <mmsystem.h>
#  include <GL/gl.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <locale>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <deque>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <sstream>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <random>
#include <vector>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "ufbx.h"

IJobStorage* CreateFileStorage(const std::filesystem::path& dir);

namespace {

#include "GuiTypes.inc"

#include "GuiUiData.inc"


#include "GuiMesh.inc"

#include "GuiAdminTypes.inc"

#include "GuiState.inc"

// Walk parent directories trying to locate an asset (fonts, ini, etc.).
#include "GuiStorageUtils.inc"


std::int64_t NowSeconds();
std::string TrimStringGui(std::string s);
int TotalSkillXpGui(const Skill& skill);
void RefreshProfiles(GuiState& state, AppContext& app, const std::string& preferredId = {});
void RefreshProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& preferredId = {});
const char* RankLabelForLevel(int level);
int RankIndexForLevel(int level);
const std::vector<RankOption>& RankOptions();
const char* ClassificationLabel(int categoryIndex);
static void EnsureBannerSelection(GuiState& state);
static void SyncVaultBuffers(GuiState& state);

#include "GuiTextUtils.inc"
#include "GuiAssets.inc"
#include "GuiRanks.inc"
#include "GuiReports.inc"
#include "GuiAdminStats.inc"
#include "GuiStatus.inc"
#include "GuiUiSettings.inc"
#include "GuiUiHelpers.inc"
#include "GuiTasks.inc"
#include "GuiProjects.inc"
#include "GuiProfessions.inc"
#include "GuiPomodoro.inc"
#include "GuiIcons.inc"
#include "GuiShortcuts.inc"
#include "GuiXpUtils.inc"

#include "GuiCharts.inc"
#include "GuiProfileSections.inc"
#include "GuiProfileOps.inc"

#include "GuiPipeline.inc"
#include "GuiAppContext.inc"
#include "GuiPipelinePanel.inc"

#include "GuiRulesPanel.inc"
#include "GuiUiSettingsPanel.inc"
#include "GuiLogsPanel.inc"
#include "GuiAdminStatsPanel.inc"
#include "GuiSkillCatalogPanel.inc"
#include "GuiTasksPanel.inc"
#include "GuiPomodoroPanel.inc"
#include "GuiAboutPanel.inc"
#include "GuiView3dPanels.inc"
#include "GuiProfilePanel.inc"
#include "GuiBannerPanel.inc"
#include "GuiVaultPanel.inc"

#include "GuiMenuHelpers.inc"
#include "GuiNavigationPanel.inc"
#include "GuiMainMenuPanel.inc"
#include "GuiWorkspacePanel.inc"

#include "GuiAdminModal.inc"
#include "GuiSkillModals.inc"
#include "GuiXpModal.inc"
#include "GuiProfileModals.inc"
#include "GuiHeader.inc"
#include "GuiRender.inc"

#include "GuiStateInit.inc"
#include "GuiMainLoop.inc"


#include "GuiStartup.inc"
#include "GuiWindowInit.inc"


} // namespace

// GUI entry point: configure locale, init GLFW/ImGui, then run main loop.
int main() {
    ConfigureLocale();
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

    GLFWwindow* window = CreateMainWindow();
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    ImGuiIO& io = InitImGuiContext();

    auto storageDir = ResolveStorageDirectory();
    CloudSyncConfig cloudConfig = LoadCloudSyncConfig(storageDir);
    CloudSyncResult initialPullResult;
    bool initialPullRan = false;
    bool initialCloudPullOk = false;
    if (cloudConfig.enabled && cloudConfig.autoPull) {
        initialPullResult = PullCloudSnapshot(cloudConfig, storageDir, CloudRole::Viewer);
        initialPullRan = true;
        initialCloudPullOk = initialPullResult.ok;
    }
    static std::string layoutPath;
    ConfigureLayoutPath(io, storageDir, layoutPath);
    LoadGuiFonts(io);

    InitImGuiBackend(window);

    SkillCatalog catalog(storageDir);
    GameplayConfig gameplayConfig;
    std::unique_ptr<IJobStorage> storage;
    InitStorageContext(storageDir, catalog, gameplayConfig, storage);

    ImGuiStyle& style = ImGui::GetStyle();
    GuiState state;
    AppContext app{storageDir, *storage, catalog};
    InitGuiState(state, app, gameplayConfig, style, io, window);
    state.cloudConfig = cloudConfig;
    state.cloudManifest = LoadCloudManifest(state.cloudConfig, storageDir);
    state.cloudUpdateAvailable = IsUpdateAvailable(state.cloudManifest, APP_VERSION);
    if (initialPullRan && initialPullResult.storageConflict) {
        state.storageConflictPopupRequest = true;
        state.storageConflictPath = initialPullResult.storageConflictPath;
    }
    if (initialCloudPullOk) {
        state.lastCloudSyncOk = true;
        state.lastCloudSyncOkAt = NowSeconds();
    }

    RunGuiLoop(window, state, app, style, io, layoutPath);

    SaveUiSettings(storageDir, state.ui);
    ReleaseIconTextures();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main();
}
#endif

