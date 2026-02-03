void RunGuiLoop(GLFWwindow* window, GuiState& state, IJobStorage& storage, SkillCatalog& catalog,
ImGuiStyle& style, ImGuiIO& io, std::string& layoutPath) {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        UpdateUiFrame(state, window, style, io, layoutPath);
        if (state.cloudConfig.enabled && state.cloudConfig.autoSyncEnabled) {
            const int intervalMinutes = std::clamp(state.cloudConfig.autoSyncMinutes, 1, 120);
            const std::int64_t now = NowSeconds();
            if (state.lastCloudSyncAt <= 0) {
                state.lastCloudSyncAt = now;
            } else if (now - state.lastCloudSyncAt >= static_cast<std::int64_t>(intervalMinutes) * 60) {
                RunCloudQuickSync(state, storage, catalog, false);
            }
        }
        DrawMainMenuBar(window, state, storage, catalog);
        DrawBannerStrip(state);
        DrawNavigationPanel(state);
        DrawWorkspacePanel(state, storage, catalog, style);
        DrawAdminLoginModal(state);
        DrawAdminPasswordModal(state);
        DrawSkillCatalogModals(state, storage, catalog);
        DrawXpModal(state, storage, catalog);
        DrawCreateProfileModal(state, storage, catalog);
        DrawConfirmModal(state, storage, catalog);
        DrawProfilePasswordModals(state, storage);
        FinalizeFrame(state, window);
    }
}
