void InitGuiState(GuiState& state, IJobStorage& storage, SkillCatalog& catalog,
                  const GameplayConfig& gameplayConfig, const std::filesystem::path& storageDir,
                  ImGuiStyle& style, ImGuiIO& io, GLFWwindow* window) {
    state.modules = LoadModuleToggles();
    state.storageDir = storageDir;
    state.adminPasswordExpected = LoadAdminPassword(storageDir);
    state.rulesConfig = gameplayConfig;
    state.rulesDraft = gameplayConfig;
    state.cloudConfig = LoadCloudSyncConfig(storageDir);
    state.cloudManifest = LoadCloudManifest(state.cloudConfig, storageDir);
    state.cloudUpdateAvailable = IsUpdateAvailable(state.cloudManifest, APP_VERSION);
    state.lastCloudSyncAt = NowSeconds();
    std::error_code ec;
    const auto uiPath = UiSettingsPath(storageDir);
    const bool uiMissing = !std::filesystem::exists(uiPath, ec);
    state.ui = LoadUiSettings(storageDir, style);
    if (uiMissing) {
        ApplyGlassUiPreset(state.ui, style);
        SaveUiSettings(storageDir, state.ui);
    }
    ApplyUiTheme(state.ui.theme, style);
    ApplyUiSettings(state.ui, style, io);
    state.uiLastTheme = state.ui.theme;
    state.profileSection = std::clamp(state.ui.profileTab, 0, 5);
    state.profileTabRequest = state.profileSection;
    ApplyUiFiltersToState(state);
    state.tasks = state.modules.tasks ? LoadTasks(storageDir) : std::vector<TaskEntry>();
    state.taskDeletePopupRequest = false;
    state.pendingTaskDeleteId.clear();
    state.shortcuts = state.modules.shortcuts ? LoadShortcuts(storageDir) : std::vector<ShortcutEntry>();
    state.pipelineSteps = state.modules.pipeline ? LoadPipeline(storageDir) : std::vector<PipelineStep>();
    state.professions = state.modules.professions ? LoadProfessions(storageDir) : std::vector<ProfessionEntry>();
    state.profileProfessionCache.clear();
    state.profileProfessionDirty = true;
    state.newSkillProfessionIndex = 0;
    state.editSkillProfessionIndex = 0;
    state.showTasks = state.modules.tasks && state.showTasks;
    state.showPomodoro = state.modules.pomodoro && state.showPomodoro;
    state.showPipeline = state.modules.pipeline && state.showPipeline;
    state.showProfessions = state.modules.professions && state.showProfessions;
    state.showView3d = state.modules.view3d && state.showView3d;
    state.showView3dSettings = state.modules.view3d && state.showView3dSettings;
    state.catalogCategoryFilter = 0;
    FindStrayStorageFiles(storageDir, 5, state.straySamples, state.strayFileCount);
    glfwGetWindowPos(window, &state.windowedX, &state.windowedY);
    glfwGetWindowSize(window, &state.windowedW, &state.windowedH);
    state.windowFullscreenLast = state.ui.windowFullscreen;
    glfwSetWindowAttrib(window, GLFW_DECORATED, state.ui.windowDecorated ? GLFW_TRUE : GLFW_FALSE);
    state.windowDecoratedLast = state.ui.windowDecorated;
    if (state.ui.windowFullscreen) {
        state.windowFullscreenLast = false;
        ApplyWindowMode(window, state);
    }
    std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
    std::snprintf(state.cloudRootBuffer.data(), state.cloudRootBuffer.size(), "%s",
                  state.cloudConfig.root.u8string().c_str());
    RefreshProfiles(state, storage, catalog, state.ui.lastProfileId);
}
