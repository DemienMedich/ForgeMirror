void DrawNavigationPanel(GuiState& state) {
    // Sidebar window
    ImGuiWindowFlags navFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        const float navWidth = 240.0f;
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(navWidth, viewport->WorkSize.y), ImGuiCond_Always);
    }
    ImGui::Begin(u8"Навигация", nullptr, navFlags);
    DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
    ImGui::TextUnformatted(u8"Разделы");
    const float navWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 navButtonSize(navWidth, 0.0f);
    if (ImGui::Button(u8"Профиль [F1]", navButtonSize)) {
        state.showMainMenu = false;
    }
    if (ImGui::Button(u8"Каталог навыков [F2]", navButtonSize)) {
        OpenWorkspaceTab(state, state.showSkillCatalog, UiWindowId::SkillCatalog);
    }
    if (ImGui::Button(u8"Пайплайн [F3]", navButtonSize)) {
        OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
    }
    if (ImGui::Button(u8"3D просмотр", navButtonSize)) {
        OpenWorkspaceTab(state, state.showView3d, UiWindowId::View3D);
    }
    if (state.isAdmin) {
        if (ImGui::Button(u8"Правила [F4]", navButtonSize)) {
            OpenWorkspaceTab(state, state.showRules, UiWindowId::Rules);
        }
        if (ImGui::Button(u8"Статистика [F5]", navButtonSize)) {
            OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
        }
        if (ImGui::Button(u8"Логи [F6]", navButtonSize)) {
            OpenWorkspaceTab(state, state.showLogs, UiWindowId::Logs);
        }
        if (ImGui::Button(u8"Настройки интерфейса", navButtonSize)) {
            OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
        }
        if (ImGui::Button(u8"3D настройки", navButtonSize)) {
            OpenWorkspaceTab(state, state.showView3dSettings, UiWindowId::View3DSettings);
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Сервис");
    if (ImGui::Button(u8"Главное меню", navButtonSize)) {
        state.showMainMenu = !state.showMainMenu;
    }
    ImGui::End();

}

