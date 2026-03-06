void RunGuiLoop(GLFWwindow* window, GuiState& state, AppContext& app,
ImGuiStyle& style, ImGuiIO& io, std::string& layoutPath) {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        UpdateUiFrame(state, window, style, io, layoutPath);
        ApplyPomodoroCoin(state, app.storage);
        if (state.cloudConfig.enabled && state.cloudConfig.autoSyncEnabled) {
            const int intervalMinutes = std::clamp(state.cloudConfig.autoSyncMinutes, 1, 120);
            const std::int64_t now = NowSeconds();
            if (state.lastCloudSyncAt <= 0) {
                state.lastCloudSyncAt = now;
            } else if (now - state.lastCloudSyncAt >= static_cast<std::int64_t>(intervalMinutes) * 60) {
                RunCloudQuickSync(state, app, false);
            }
        }
        DrawMainMenuBar(window, state, app.storage, app.catalog);
        DrawUpdateNoticeModal(state);
        DrawBannerStrip(state);
        DrawNavigationPanel(state);
        DrawWorkspacePanel(state, app.storage, app.catalog, style);
        DrawAdminLoginModal(state);
        DrawAdminPasswordModal(state);
        DrawSkillCatalogModals(state, app.storage, app.catalog);
        DrawXpModal(state, app.storage, app.catalog);
        DrawCreateProfileModal(state, app.storage, app.catalog);
        DrawConfirmModal(state, app.storage, app.catalog);
        DrawProfilePasswordModals(state, app.storage);
        DrawStorageConflictModal(state);
        FinalizeFrame(state, window);
    }
}

