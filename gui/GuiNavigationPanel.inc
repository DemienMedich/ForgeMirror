void DrawNavigationPanel(GuiState& state) {
    // Sidebar window
    ImGuiWindowFlags navFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        const float navWidth = kNavigationPanelWidth;
        ImGuiViewportP* viewportP = (ImGuiViewportP*)(void*)viewport;
        ImRect workRect = viewportP->GetBuildWorkRect();
        ImGui::SetNextWindowPos(workRect.Min, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(navWidth, workRect.Max.y - workRect.Min.y), ImGuiCond_Always);
    }
    ImGui::Begin(u8"Навигация", nullptr, navFlags);
    DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImGui::TextDisabled(u8"Разделы и быстрые переходы.");
    DrawCardHeader(u8"Разделы");
    const float navItemHeight = std::max(34.0f, ImGui::GetFrameHeight() * 1.15f);
    auto navButton = [&](const char* id, UiIcon icon, const char* label, bool active, auto onClick) {
        if (NavIconButtonGui(id, icon, label, accent, navItemHeight, true, active)) {
            onClick();
        }
    };
    const bool anyWorkspace = state.showSkillCatalog || state.showPipeline || state.showView3d || state.showTasks ||
    state.showPomodoro ||
    (state.isAdmin && (state.showRules || state.showUiSettings || state.showView3dSettings ||
    state.showLogs || state.showAdminStats));
    const bool profileActive = !anyWorkspace;
    navButton("nav_profile", UiIcon::Home, u8"Профиль [F1]", profileActive, [&]() {
        state.showSkillCatalog = false;
        state.showPipeline = false;
        state.showView3d = false;
        state.showRules = false;
        state.showUiSettings = false;
        state.showView3dSettings = false;
        state.showLogs = false;
        state.showAdminStats = false;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::Profile);
    });
    navButton("nav_catalog", UiIcon::Book, u8"Каталог навыков [F2]", state.showSkillCatalog, [&]() {
        OpenWorkspaceTab(state, state.showSkillCatalog, UiWindowId::SkillCatalog);
    });
    navButton("nav_pipeline", UiIcon::Flow, u8"Пайплайн [F3]", state.showPipeline, [&]() {
        OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
    });
    navButton("nav_tasks", UiIcon::List, u8"Задачи", state.showTasks, [&]() {
        OpenWorkspaceTab(state, state.showTasks, UiWindowId::Tasks);
    });
    navButton("nav_pomodoro", UiIcon::Bell, u8"Помодоро", state.showPomodoro, [&]() {
        OpenWorkspaceTab(state, state.showPomodoro, UiWindowId::Pomodoro);
    });
    navButton("nav_view3d", UiIcon::Cube, u8"3D просмотр", state.showView3d, [&]() {
        OpenWorkspaceTab(state, state.showView3d, UiWindowId::View3D);
    });
    if (state.isAdmin) {
        navButton("nav_rules", UiIcon::LockClosed, u8"Правила [F4]", state.showRules, [&]() {
            OpenWorkspaceTab(state, state.showRules, UiWindowId::Rules);
        });
        navButton("nav_stats", UiIcon::Chart, u8"Статистика [F5]", state.showAdminStats, [&]() {
            OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
        });
        navButton("nav_logs", UiIcon::List, u8"Логи [F6]", state.showLogs, [&]() {
            OpenWorkspaceTab(state, state.showLogs, UiWindowId::Logs);
        });
        navButton("nav_ui_settings", UiIcon::Settings, u8"Настройки интерфейса", state.showUiSettings, [&]() {
            OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
        });
        navButton("nav_view3d_settings", UiIcon::Cube, u8"3D настройки", state.showView3dSettings, [&]() {
            OpenWorkspaceTab(state, state.showView3dSettings, UiWindowId::View3DSettings);
        });
    } else {
        ImGui::TextDisabled(u8"Админ-разделы скрыты.");
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Таймер");
    if (state.pomodoroRunning) {
        const std::string remaining = FormatPomodoroTime(state.pomodoroRemainingSeconds);
        ImGui::Text(u8"%s · %s", PomodoroPhaseLabel(state.pomodoroPhase), remaining.c_str());
    } else if (state.pomodoroAwaitConfirm) {
        ImGui::TextDisabled(u8"Интервал завершён");
        ImGui::TextDisabled(u8"Ждёт подтверждения");
    } else if (state.pomodoroRemainingSeconds > 0) {
        const std::string remaining = FormatPomodoroTime(state.pomodoroRemainingSeconds);
        ImGui::TextDisabled(u8"Пауза · %s", remaining.c_str());
    } else {
        ImGui::TextDisabled(u8"Не запущен");
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Сервис");
    navButton("nav_profiles", UiIcon::Users, u8"Профили", state.showProfilesWindow, [&]() {
        state.showProfilesWindow = !state.showProfilesWindow;
        if (state.showProfilesWindow) {
            state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
        }
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

