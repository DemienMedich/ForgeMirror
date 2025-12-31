void RunGuiLoop(GLFWwindow* window, GuiState& state, IJobStorage& storage, SkillCatalog& catalog,
ImGuiStyle& style, ImGuiIO& io, std::string& layoutPath) {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        UpdateUiFrame(state, window, style, io, layoutPath);
        DrawNavigationPanel(state);
        DrawMainMenuPanel(state, storage, catalog);
        DrawProfilePanel(state, storage, catalog);
        DrawWorkspacePanel(state, storage, catalog, style);
        DrawAdminLoginModal(state);
        DrawAdminPasswordModal(state);
        DrawSkillCatalogModals(state, storage, catalog);
        DrawXpModal(state, storage, catalog);
        DrawCreateProfileModal(state, storage, catalog);
        DrawConfirmModal(state, storage, catalog);
        FinalizeFrame(state, window);
    }
}
