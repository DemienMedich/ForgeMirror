void DrawWorkspacePanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, ImGuiStyle& style) {
    bool anyWorkspace = state.showSkillCatalog || state.showPipeline || state.showView3d ||
    (state.isAdmin && (state.showRules || state.showUiSettings || state.showView3dSettings ||
    state.showLogs || state.showAdminStats));
    if (anyWorkspace) {
        ImGui::Begin(u8"Рабочее окно", nullptr, ImGuiWindowFlags_NoCollapse);
        EnsureWindowVisible();
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
        if (ImGui::BeginTabBar("workspace_tabs")) {
            if (state.showSkillCatalog &&
            ImGui::BeginTabItem(u8"Каталог навыков", &state.showSkillCatalog,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::SkillCatalog)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::SkillCatalog)) {
                    state.workspaceTabRequest = -1;
                }
                DrawSkillCatalogPanel(state, storage, catalog);
                ImGui::EndTabItem();
            }
            if (state.showPipeline &&
            ImGui::BeginTabItem(u8"Пайплайн", &state.showPipeline,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::Pipeline)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Pipeline)) {
                    state.workspaceTabRequest = -1;
                }
                DrawPipelinePanel(state);
                ImGui::EndTabItem();
            }
            if (state.showView3d &&
            ImGui::BeginTabItem(u8"3D просмотр", &state.showView3d,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::View3D)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::View3D)) {
                    state.workspaceTabRequest = -1;
                }
                DrawView3dPanel(state);
                ImGui::EndTabItem();
            }
            if (state.isAdmin && state.showView3dSettings &&
            ImGui::BeginTabItem(u8"3D настройки", &state.showView3dSettings,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::View3DSettings)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::View3DSettings)) {
                    state.workspaceTabRequest = -1;
                }
                DrawView3dSettingsPanel(state);
                ImGui::EndTabItem();
            }
            if (state.isAdmin && state.showLogs &&
            ImGui::BeginTabItem(u8"Логи", &state.showLogs,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::Logs)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Logs)) {
                    state.workspaceTabRequest = -1;
                }
                DrawLogsPanel(state);
                ImGui::EndTabItem();
            }
            if (state.isAdmin && state.showAdminStats &&
            ImGui::BeginTabItem(u8"Статистика", &state.showAdminStats,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::AdminStats)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::AdminStats)) {
                    state.workspaceTabRequest = -1;
                }
                DrawAdminStatsPanel(state, storage, catalog);
                ImGui::EndTabItem();
            }
            if (state.isAdmin && state.showRules &&
            ImGui::BeginTabItem(u8"Правила", &state.showRules,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::Rules)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::Rules)) {
                    state.workspaceTabRequest = -1;
                }
                DrawRulesPanel(state, storage, catalog);
                ImGui::EndTabItem();
            }
            if (state.isAdmin && state.showUiSettings &&
            ImGui::BeginTabItem(u8"Настройки интерфейса", &state.showUiSettings,
            state.workspaceTabRequest == static_cast<int>(UiWindowId::UiSettings)
            ? ImGuiTabItemFlags_SetSelected
            : 0)) {
                if (state.workspaceTabRequest == static_cast<int>(UiWindowId::UiSettings)) {
                    state.workspaceTabRequest = -1;
                }
                DrawUiSettingsPanel(state, style);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

}
