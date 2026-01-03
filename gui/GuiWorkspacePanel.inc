void DrawWorkspacePanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, ImGuiStyle& style) {
    ImGuiWindowFlags workspaceFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        ImGuiViewportP* viewportP = (ImGuiViewportP*)(void*)viewport;
        ImRect workRect = viewportP->GetBuildWorkRect();
        const float navWidth = kNavigationPanelWidth;
        const float width = std::max(0.0f, (workRect.Max.x - workRect.Min.x) - navWidth);
        ImGui::SetNextWindowPos(ImVec2(workRect.Min.x + navWidth, workRect.Min.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, workRect.Max.y - workRect.Min.y), ImGuiCond_Always);
    }

    ImGui::Begin(u8"Рабочая область", nullptr, workspaceFlags);
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 tab = BlendColor(style.Colors[ImGuiCol_Tab], accent, 0.20f);
    ImVec4 tabHover = BlendColor(style.Colors[ImGuiCol_TabHovered], accent, 0.25f);
    ImVec4 tabActive = BlendColor(style.Colors[ImGuiCol_TabSelected], accent, 0.30f);
    tab.w = std::max(tab.w, 0.75f);
    tabHover.w = std::max(tabHover.w, 0.85f);
    tabActive.w = std::max(tabActive.w, 0.95f);
    ImGui::PushStyleColor(ImGuiCol_Tab, tab);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, tabHover);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, tabActive);

    auto tabFlagsFor = [&](UiWindowId id) {
        return (state.workspaceTabRequest == static_cast<int>(id)) ? ImGuiTabItemFlags_SetSelected : 0;
    };
    auto beginTabBody = [&](const char* id) {
        return ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    };

    if (ImGui::BeginTable("workspace_layout", 2,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("workspace_main", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("workspace_shortcuts", ImGuiTableColumnFlags_WidthFixed, 128.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("workspace_main", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoBackground)) {
            if (ImGui::BeginTabBar("workspace_tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
                if (ImGui::BeginTabItem(u8"Профиль", nullptr, tabFlagsFor(UiWindowId::Profile))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Profile)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_profile")) {
                        DrawProfilePanel(state, storage, catalog);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (state.showProfilesWindow &&
                    ImGui::BeginTabItem(u8"Профили", &state.showProfilesWindow, tabFlagsFor(UiWindowId::MainMenu))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::MainMenu)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_profiles")) {
                        DrawProfilesPanel(state, storage, catalog);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (state.showTasks &&
                    ImGui::BeginTabItem(u8"Задачи", &state.showTasks, tabFlagsFor(UiWindowId::Tasks))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Tasks)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_tasks")) {
                        DrawTasksPanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (state.showSkillCatalog &&
                    ImGui::BeginTabItem(u8"Каталог навыков", &state.showSkillCatalog, tabFlagsFor(UiWindowId::SkillCatalog))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::SkillCatalog)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_catalog")) {
                        DrawSkillCatalogPanel(state, storage, catalog);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.showPipeline &&
                    ImGui::BeginTabItem(u8"Пайплайн", &state.showPipeline, tabFlagsFor(UiWindowId::Pipeline))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Pipeline)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_pipeline")) {
                        DrawPipelinePanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.showPomodoro &&
                    ImGui::BeginTabItem(u8"Помодоро", &state.showPomodoro, tabFlagsFor(UiWindowId::Pomodoro))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Pomodoro)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_pomodoro")) {
                        DrawPomodoroPanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.showShortcuts &&
                    ImGui::BeginTabItem(u8"Ярлыки", &state.showShortcuts, tabFlagsFor(UiWindowId::Shortcuts))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Shortcuts)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_shortcuts")) {
                        DrawShortcutsPanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.showView3d &&
                    ImGui::BeginTabItem(u8"3D просмотр", &state.showView3d, tabFlagsFor(UiWindowId::View3D))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::View3D)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_view3d")) {
                        DrawView3dPanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showAdminStats &&
                    ImGui::BeginTabItem(u8"Статистика", &state.showAdminStats, tabFlagsFor(UiWindowId::AdminStats))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::AdminStats)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_admin_stats")) {
                        DrawAdminStatsPanel(state, storage, catalog);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showLogs &&
                    ImGui::BeginTabItem(u8"Логи", &state.showLogs, tabFlagsFor(UiWindowId::Logs))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Logs)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_logs")) {
                        DrawLogsPanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showRules &&
                    ImGui::BeginTabItem(u8"Правила", &state.showRules, tabFlagsFor(UiWindowId::Rules))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Rules)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_rules")) {
                        DrawRulesPanel(state, storage, catalog);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.showUiSettings &&
                    ImGui::BeginTabItem(u8"Настройки интерфейса", &state.showUiSettings, tabFlagsFor(UiWindowId::UiSettings))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::UiSettings)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_ui_settings")) {
                        DrawUiSettingsPanel(state, style);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showView3dSettings &&
                    ImGui::BeginTabItem(u8"3D настройки", &state.showView3dSettings, tabFlagsFor(UiWindowId::View3DSettings))) {
                    if (state.workspaceTabRequest == static_cast<int>(UiWindowId::View3DSettings)) {
                        state.workspaceTabRequest = -1;
                    }
                    if (beginTabBody("##tab_view3d_settings")) {
                        DrawView3dSettingsPanel(state);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        DrawShortcutsSidebar(state);
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(3);
    ImGui::End();
}
