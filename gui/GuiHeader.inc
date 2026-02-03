static void HandleUiHotkeys(GuiState& state, ImGuiStyle& style, ImGuiIO& io, std::string& layoutPath) {
    if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
        if (ImGui::GetIO().KeyCtrl) {
            ResetUiSettings(state.ui, style, io);
            state.uiLastTheme = state.ui.theme;
            state.uiDirty = false;
            state.profileSection = std::clamp(state.ui.profileTab, 0, 5);
            state.profileTabRequest = state.profileSection;
            std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
            ApplyUiFiltersToState(state);
            SaveUiSettings(state.storageDir, state.ui);
            if (!layoutPath.empty()) {
                std::error_code ec;
                std::filesystem::remove(layoutPath, ec);
            }
            ImGui::ClearIniSettings();
            SetStatus(state, u8"Интерфейс сброшен (Ctrl+F10).", 0.45f, 0.9f, 0.45f);
        } else {
            state.ui.windowDecorated = !state.ui.windowDecorated;
            state.uiDirty = true;
            SetStatus(state, state.ui.windowDecorated ? u8"Рамка окна включена." : u8"Безрамочный режим включён.",
                0.45f, 0.9f, 0.45f);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
        state.ui.windowFullscreen = !state.ui.windowFullscreen;
        state.uiDirty = true;
    }

}

static void SyncUiTheme(GuiState& state, ImGuiStyle& style) {
    if (state.ui.theme != state.uiLastTheme) {
        ApplyUiTheme(state.ui.theme, style);
        state.uiLastTheme = state.ui.theme;
        if (!state.ui.customColors) {
            const auto& entries = UiColorEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                state.ui.colors[i] = style.Colors[entries[i].col];
            }
        }
    }
}

static void ApplyUiWindowState(GuiState& state, GLFWwindow* window, ImGuiStyle& style, ImGuiIO& io) {
    ApplyUiSettings(state.ui, style, io);
    ApplyWindowMode(window, state);
    ApplyWindowDecorations(window, state.ui.windowDecorated, state.windowDecoratedLast);
    HandleBorderlessDrag(window, state.ui);
}

static void ApplyAccessGuardsAndHotkeys(GuiState& state) {
    if (!state.modules.tasks) state.showTasks = false;
    if (!state.modules.pipeline) state.showPipeline = false;
    if (!state.modules.pomodoro) state.showPomodoro = false;
    if (!state.modules.shortcuts) state.showShortcuts = false;
    if (!state.modules.view3d) {
        state.showView3d = false;
        state.showView3dSettings = false;
    }
    if (!state.modules.cloud) {
        state.cloudConfig.enabled = false;
    }
    if (!state.isAdmin) {
        state.showRules = false;
        state.showView3dSettings = false;
        state.showLogs = false;
        state.showAdminStats = false;
        state.showBannerAdmin = false;
        state.showVaultAdmin = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
        state.showSkillCatalog = false;
        state.showPipeline = false;
        state.showView3d = false;
        state.showTasks = false;
        state.showPomodoro = false;
        state.showShortcuts = false;
        state.showAbout = false;
        state.showRules = false;
        state.showUiSettings = false;
        state.showView3dSettings = false;
        state.showLogs = false;
        state.showAdminStats = false;
        state.showBannerAdmin = false;
        state.showVaultAdmin = false;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::Profile);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        state.showSkillCatalog = true;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::SkillCatalog);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3) && state.modules.pipeline) {
        state.showPipeline = true;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::Pipeline);
    }
    if (state.isAdmin && ImGui::IsKeyPressed(ImGuiKey_F4)) {
        state.showRules = true;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::Rules);
    }
    if (state.isAdmin && ImGui::IsKeyPressed(ImGuiKey_F5)) {
        state.showAdminStats = true;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::AdminStats);
    }
    if (state.isAdmin && ImGui::IsKeyPressed(ImGuiKey_F6)) {
        state.showLogs = true;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::Logs);
    }

}

void UpdateUiFrame(GuiState& state, GLFWwindow* window, ImGuiStyle& style, ImGuiIO& io, std::string& layoutPath) {
    HandleUiHotkeys(state, style, io, layoutPath);
    SyncUiTheme(state, style);
    ApplyUiWindowState(state, window, style, io);
    ApplyAccessGuardsAndHotkeys(state);
    UpdateBannerRotation(state);
    if (state.modules.pomodoro) {
        UpdatePomodoro(state, io);
    }
}
