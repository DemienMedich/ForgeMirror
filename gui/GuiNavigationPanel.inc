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
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    DrawCardHeader(u8"Разделы");
    const float navWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 navButtonSize(navWidth, 0.0f);
    auto navButton = [&](const char* label, bool active, auto onClick) {
        if (active) {
            ImVec4 base = BlendColor(style.Colors[ImGuiCol_Button], accent, 0.35f);
            ImVec4 hover = BlendColor(style.Colors[ImGuiCol_ButtonHovered], accent, 0.30f);
            ImVec4 pressed = BlendColor(style.Colors[ImGuiCol_ButtonActive], accent, 0.25f);
            base.w = std::max(base.w, 0.85f);
            hover.w = std::max(hover.w, 0.90f);
            pressed.w = std::max(pressed.w, 0.95f);
            ImGui::PushStyleColor(ImGuiCol_Button, base);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressed);
        }
        if (ImGui::Button(label, navButtonSize)) {
            onClick();
        }
        if (active) {
            ImGui::PopStyleColor(3);
        }
    };
    const bool profileActive = !state.showMainMenu;
    navButton(u8"Профиль [F1]", profileActive, [&]() {
        state.showMainMenu = false;
    });
    navButton(u8"Каталог навыков [F2]", state.showSkillCatalog, [&]() {
        OpenWorkspaceTab(state, state.showSkillCatalog, UiWindowId::SkillCatalog);
    });
    navButton(u8"Пайплайн [F3]", state.showPipeline, [&]() {
        OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
    });
    navButton(u8"3D просмотр", state.showView3d, [&]() {
        OpenWorkspaceTab(state, state.showView3d, UiWindowId::View3D);
    });
    if (state.isAdmin) {
        navButton(u8"Правила [F4]", state.showRules, [&]() {
            OpenWorkspaceTab(state, state.showRules, UiWindowId::Rules);
        });
        navButton(u8"Статистика [F5]", state.showAdminStats, [&]() {
            OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
        });
        navButton(u8"Логи [F6]", state.showLogs, [&]() {
            OpenWorkspaceTab(state, state.showLogs, UiWindowId::Logs);
        });
        navButton(u8"Настройки интерфейса", state.showUiSettings, [&]() {
            OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
        });
        navButton(u8"3D настройки", state.showView3dSettings, [&]() {
            OpenWorkspaceTab(state, state.showView3dSettings, UiWindowId::View3DSettings);
        });
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Сервис");
    navButton(u8"Главное меню", state.showMainMenu, [&]() {
        state.showMainMenu = !state.showMainMenu;
    });

    if (state.isAdmin) {
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const float targetY = ImGui::GetWindowHeight() - footerHeight - ImGui::GetStyle().WindowPadding.y;
        if (targetY > ImGui::GetCursorPosY()) {
            ImGui::SetCursorPosY(targetY);
        }
        ImGui::Separator();
#ifdef APP_VERSION
        ImGui::TextDisabled(u8"Версия: %s", APP_VERSION);
#else
        ImGui::TextDisabled(u8"Версия: dev");
#endif
    }
    ImGui::End();

}

