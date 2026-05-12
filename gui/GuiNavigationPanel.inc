void PomodoroQuickStart(GuiState& state);
void PomodoroQuickReset(GuiState& state);
void PomodoroQuickNext(GuiState& state);

void DrawNavigationPanel(GuiState& state) {
    // Sidebar window
    ImGuiWindowFlags navFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        const float navWidth = kNavigationPanelWidth;
        ImGuiViewportP* viewportP = (ImGuiViewportP*)(void*)viewport;
        ImRect workRect = viewportP->GetBuildWorkRect();
        const float bannerHeight = BannerHeight();
        workRect.Min.y += bannerHeight;
        ImGui::SetNextWindowPos(workRect.Min, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(navWidth, workRect.Max.y - workRect.Min.y), ImGuiCond_Always);
    }
    ImGui::Begin(u8"Навигация", nullptr, navFlags);
    DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImGui::TextDisabled(u8"Разделы и быстрые переходы.");
    if (!state.active) {
        ImVec4 warn = LogLevelColor(AppLogLevel::Warning);
        warn.w = std::max(warn.w, 0.9f);
        ImGui::TextColored(warn, u8"Выберите профиль для работы.");
    }
    DrawCardHeader(u8"Главное");
    const float navItemHeight = std::max(34.0f, ImGui::GetFrameHeight() * 1.15f);
    auto navButton = [&](const char* id, UiIcon icon, const char* label, bool active, auto onClick) {
        if (NavIconButtonGui(id, icon, label, accent, navItemHeight, true, active)) {
            onClick();
        }
    };
    const bool anyWorkspace = state.showSkillCatalog || state.showPipeline ||
    (state.modules.view3d && state.showView3d) || state.showTasks ||
    state.showPomodoro || state.showUiSettings || state.showShortcuts || state.showAbout ||
    (state.isAdmin && (state.showProjects || state.showRules ||
    (state.modules.view3d && state.showView3dSettings) || state.showLogs ||
    state.showAdminStats || state.showBannerAdmin || state.showVaultAdmin));
    const bool profileActive = !anyWorkspace;
    navButton("nav_profile", UiIcon::Home, u8"Профиль [F1]", profileActive, [&]() {
        state.showSkillCatalog = false;
        state.showPipeline = false;
        state.showView3d = false;
        state.showTasks = false;
        state.showPomodoro = false;
        state.showProjects = false;
        state.showRules = false;
        state.showUiSettings = false;
        state.showShortcuts = false;
        state.showAbout = false;
        state.showView3dSettings = false;
        state.showLogs = false;
        state.showAdminStats = false;
        state.workspaceTabRequest = static_cast<int>(UiWindowId::Profile);
    });
    if (state.modules.tasks) {
        navButton("nav_tasks", UiIcon::Task, u8"Задачи", state.showTasks, [&]() {
            OpenWorkspaceTab(state, state.showTasks, UiWindowId::Tasks);
        });
    }
    if (state.modules.professions && state.isAdmin) {
        navButton("nav_professions", UiIcon::User, u8"Профессии", state.showProfessions, [&]() {
            OpenWorkspaceTab(state, state.showProfessions, UiWindowId::Professions);
        });
    }
    navButton("nav_catalog", UiIcon::Book, u8"Навыки [F2]", state.showSkillCatalog, [&]() {
        OpenWorkspaceTab(state, state.showSkillCatalog, UiWindowId::SkillCatalog);
    });
    if (state.modules.pipeline) {
        navButton("nav_pipeline", UiIcon::Flow, u8"Пайплайн [F3]", state.showPipeline, [&]() {
            OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
        });
    }
    if (state.modules.pomodoro) {
        navButton("nav_pomodoro", UiIcon::Bell, u8"Помодоро", state.showPomodoro, [&]() {
            OpenWorkspaceTab(state, state.showPomodoro, UiWindowId::Pomodoro);
        });
    }
    if (state.modules.view3d) {
        navButton("nav_view3d", UiIcon::Cube, u8"3D просмотр", state.showView3d, [&]() {
            OpenWorkspaceTab(state, state.showView3d, UiWindowId::View3D);
        });
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Админ");
    if (state.isAdmin) {
        navButton("nav_stats", UiIcon::Chart, u8"Статистика [F5]", state.showAdminStats, [&]() {
            OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
        });
        navButton("nav_logs", UiIcon::List, u8"Логи [F6]", state.showLogs, [&]() {
            OpenWorkspaceTab(state, state.showLogs, UiWindowId::Logs);
        });
        navButton("nav_projects", UiIcon::Project, u8"Проекты", state.showProjects, [&]() {
            OpenWorkspaceTab(state, state.showProjects, UiWindowId::Projects);
        });
        navButton("nav_rules", UiIcon::LockClosed, u8"Правила [F4]", state.showRules, [&]() {
            OpenWorkspaceTab(state, state.showRules, UiWindowId::Rules);
        });
        navButton("nav_banner", UiIcon::Info, u8"Баннер", state.showBannerAdmin, [&]() {
            OpenWorkspaceTab(state, state.showBannerAdmin, UiWindowId::BannerAdmin);
        });
        navButton("nav_vault", UiIcon::LockClosed, u8"Хранилище", state.showVaultAdmin, [&]() {
            OpenWorkspaceTab(state, state.showVaultAdmin, UiWindowId::VaultAdmin);
        });
        if (state.modules.view3d) {
            navButton("nav_view3d_settings", UiIcon::Cube, u8"3D настройки", state.showView3dSettings, [&]() {
                OpenWorkspaceTab(state, state.showView3dSettings, UiWindowId::View3DSettings);
            });
        }
    } else {
        ImGui::TextDisabled(u8"Админ-разделы скрыты.");
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Таймер");
    auto drawAnalogClock = [&](ImDrawList* drawList, ImVec2 center, float radius, const ImVec4& accent,
                               const ImVec4& dim) {
        if (!drawList || radius <= 0.0f) return;
        constexpr float kPi = 3.14159265358979323846f;
        ImU32 dimCol = ImGui::GetColorU32(dim);
        ImU32 accentCol = ImGui::GetColorU32(accent);
        drawList->AddCircle(center, radius, dimCol, 64, 1.5f);
        for (int i = 0; i < 60; ++i) {
            float a = (kPi * 2.0f) * (static_cast<float>(i) / 60.0f) - kPi * 0.5f;
            float tickLen = (i % 5 == 0) ? radius * 0.12f : radius * 0.06f;
            float x0 = center.x + std::cos(a) * (radius - tickLen);
            float y0 = center.y + std::sin(a) * (radius - tickLen);
            float x1 = center.x + std::cos(a) * radius;
            float y1 = center.y + std::sin(a) * radius;
            drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), dimCol, (i % 5 == 0) ? 1.8f : 1.0f);
        }

        std::time_t t = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        float sec = static_cast<float>(tm.tm_sec);
        float min = static_cast<float>(tm.tm_min) + sec / 60.0f;
        float hour = static_cast<float>(tm.tm_hour % 12) + min / 60.0f;
        auto hand = [&](float value, float maxValue, float length, float thickness, ImU32 col) {
            float a = (kPi * 2.0f) * (value / maxValue) - kPi * 0.5f;
            ImVec2 p(center.x + std::cos(a) * length, center.y + std::sin(a) * length);
            drawList->AddLine(center, p, col, thickness);
        };
        hand(hour, 12.0f, radius * 0.45f, 3.0f, accentCol);
        hand(min, 60.0f, radius * 0.65f, 2.2f, accentCol);
        hand(sec, 60.0f, radius * 0.75f, 1.4f, ImGui::GetColorU32(BlendColor(accent, dim, 0.35f)));
        drawList->AddCircleFilled(center, radius * 0.06f, accentCol, 12);
    };
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
    const float clockAvail = ImGui::GetContentRegionAvail().x;
    const float clockSize = std::min(96.0f, clockAvail);
    const float clockOffsetX = std::max(0.0f, (clockAvail - clockSize) * 0.5f);
    const float clockBaseX = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(clockBaseX + clockOffsetX);
    ImGui::Dummy(ImVec2(clockSize, clockSize));
    const ImVec2 clockMin = ImGui::GetItemRectMin();
    const ImVec2 clockMax = ImGui::GetItemRectMax();
    const ImVec2 clockCenter((clockMin.x + clockMax.x) * 0.5f, (clockMin.y + clockMax.y) * 0.5f);
    ImVec4 dim = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    dim.w = std::max(dim.w, 0.6f);
    drawAnalogClock(ImGui::GetWindowDrawList(), clockCenter, clockSize * 0.45f, accent, dim);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImGui::SetCursorPosX(clockBaseX + std::max(0.0f, (clockAvail - textSize.x) * 0.5f));
    ImVec4 timeColor = BlendColor(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], accent, 0.35f);
    timeColor.w = std::max(timeColor.w, 0.85f);
    ImGui::TextColored(timeColor, "%s", buf);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (state.modules.pomodoro) {
        DrawCardHeader(u8"Помодоро · быстрые действия");
        const float actionSize = 34.0f;
        const float actionSpacing = 10.0f;
        const float totalWidth = actionSize * 3.0f + actionSpacing * 2.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float offsetX = std::max(0.0f, (avail - totalWidth) * 0.5f);
        ImGui::BeginGroup();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        const bool nextEnabled = !state.ui.pomodoroAutoAdvance || state.pomodoroAwaitConfirm;
        if (IconButtonGui("pom_quick_start", UiIcon::Play, u8"Старт/продолжить", accent, actionSize, true)) {
            PomodoroQuickStart(state);
        }
        ImGui::SameLine(0.0f, actionSpacing);
        if (IconButtonGui("pom_quick_reset", UiIcon::Stop, u8"Сбросить", accent, actionSize, true)) {
            PomodoroQuickReset(state);
        }
        ImGui::SameLine(0.0f, actionSpacing);
        if (IconButtonGui("pom_quick_next", UiIcon::Next, u8"Следующий этап (ручной режим)", accent, actionSize, nextEnabled)) {
            PomodoroQuickNext(state);
        }
        ImGui::EndGroup();
    } else {
        DrawCardHeader(u8"Помодоро отключен");
        ImGui::TextDisabled(u8"Модуль таймера выключен (env FORGEMIRROR_DISABLE_MODULES=pomodoro).");
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Сервис");
    navButton("nav_profiles", UiIcon::Users, u8"Профили", state.showProfilesWindow, [&]() {
        state.showProfilesWindow = !state.showProfilesWindow;
        if (state.showProfilesWindow) {
            state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
        }
    });
    if (state.modules.shortcuts) {
        navButton("nav_shortcuts", UiIcon::Shortcut, u8"Ярлыки", state.showShortcuts, [&]() {
            OpenWorkspaceTab(state, state.showShortcuts, UiWindowId::Shortcuts);
        });
    }
    navButton("nav_ui_settings", UiIcon::Settings, u8"Настройки", state.showUiSettings, [&]() {
        OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
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

