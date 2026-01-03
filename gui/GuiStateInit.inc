void InitGuiState(GuiState& state, IJobStorage& storage, SkillCatalog& catalog,
                  const GameplayConfig& gameplayConfig, const std::filesystem::path& storageDir,
                  ImGuiStyle& style, ImGuiIO& io, GLFWwindow* window) {
    state.storageDir = storageDir;
    state.adminPasswordExpected = LoadAdminPassword(storageDir);
    state.rulesConfig = gameplayConfig;
    state.rulesDraft = gameplayConfig;
    state.cloudConfig = LoadCloudSyncConfig(storageDir);
    state.cloudManifest = LoadCloudManifest(state.cloudConfig, storageDir);
    state.cloudUpdateAvailable = IsUpdateAvailable(state.cloudManifest, APP_VERSION);
    state.ui = LoadUiSettings(storageDir, style);
    ApplyUiTheme(state.ui.theme, style);
    ApplyUiSettings(state.ui, style, io);
    state.uiLastTheme = state.ui.theme;
    state.profileSection = std::clamp(state.ui.profileTab, 0, 5);
    state.profileTabRequest = state.profileSection;
    ApplyUiFiltersToState(state);
    state.tasks = LoadTasks(storageDir);
    state.shortcuts = LoadShortcuts(storageDir);
    state.pipelineSteps = LoadPipeline(storageDir);
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
    RefreshProfiles(state, storage, catalog, state.ui.lastProfileId);
}
