void InitGuiState(GuiState& state, AppContext& app,
                  const GameplayConfig& gameplayConfig, ImGuiStyle& style, ImGuiIO& io, GLFWwindow* window) {
    auto& storage = app.storage;
    auto& catalog = app.catalog;
    const auto& storageDir = app.storageDir;

    state.modules = LoadModuleToggles();
    state.storageDir = storageDir;
    state.adminPasswordExpected = LoadAdminPassword(storageDir);
    state.rulesConfig = gameplayConfig;
    state.rulesDraft = gameplayConfig;
    state.cloudConfig = LoadCloudSyncConfig(storageDir);
    state.bannerTexts = LoadBannerTexts(storageDir);
    state.vault = LoadStorageVault(storageDir);
    EnsureStorageVaultFile(state, app);
    SyncVaultBuffers(state);
    RefreshCloudManifestState(state, app);
    state.lastCloudSyncAt = NowSeconds();
    state.bannerLastRotateAt = state.lastCloudSyncAt;
    if (!state.bannerTexts.empty()) {
        state.bannerTextCurrent = state.bannerTexts.front();
    }
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
    ApplyProfileAccessSettings(state);
    LoadWorkspaceCollections(state, app);
    state.taskDeletePopupRequest = false;
    state.pendingTaskDeleteId.clear();
    state.taskPlanPriority = 1;
    state.tasksFilterProject = 0;
    state.tasksFilterPriority = 0;
    state.tasksSortMode = 0;
    state.tasksQuickFilter = 0;
    state.projectsSortMode = std::clamp(state.projectsSortMode, 0, 3);
    state.tasksReminderLastAt = 0;
    state.tasksReminderShown24h.clear();
    state.tasksReminderShown2h.clear();
    state.taskSelectionIds.clear();
    state.taskBulkStatus = kTaskStatusInProgress;
    state.taskBulkPriority = kTaskPriorityMedium;
    state.taskBulkProjectIndex = 0;
    state.taskBulkDeletePopupRequest = false;
    state.taskInlineDeadlineEditId.clear();
    state.taskInlineAssigneeEditId.clear();
    state.showTasks = state.modules.tasks && state.showTasks;
    state.showProjects = state.isAdmin && state.showProjects;
    state.showPomodoro = state.modules.pomodoro && state.showPomodoro;
    state.showPipeline = state.modules.pipeline && state.showPipeline;
    state.showProfessions = state.modules.professions && state.showProfessions;
    state.showView3d = state.modules.view3d && state.showView3d;
    state.showView3dSettings = state.modules.view3d && state.showView3dSettings;
    FindStrayStorageFiles(storageDir, 5, state.straySamples, state.strayFileCount);
    glfwGetWindowPos(window, &state.windowedX, &state.windowedY);
    glfwGetWindowSize(window, &state.windowedW, &state.windowedH);
    state.windowFullscreenLast = state.ui.windowFullscreen;
    const bool effectiveDecorated = state.ui.windowDecorated && !state.ui.windowFullscreen;
    glfwSetWindowAttrib(window, GLFW_DECORATED, effectiveDecorated ? GLFW_TRUE : GLFW_FALSE);
    state.windowDecoratedLast = effectiveDecorated;
    if (state.ui.windowFullscreen) {
        state.windowFullscreenLast = false;
        ApplyWindowMode(window, state);
    }
    std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
    std::snprintf(state.cloudRootBuffer.data(), state.cloudRootBuffer.size(), "%s",
                  state.cloudConfig.root.u8string().c_str());
    std::snprintf(state.taskPlanDeadline.data(), state.taskPlanDeadline.size(), "%s", FormatTimestamp(NowSeconds() + 86400, "%Y-%m-%d 18:00").c_str());
    std::snprintf(state.taskBulkDeadline.data(), state.taskBulkDeadline.size(), "%s", FormatTimestamp(NowSeconds() + 86400, "%Y-%m-%d 18:00").c_str());
    RefreshProfiles(state, app, state.ui.lastProfileId);
}


