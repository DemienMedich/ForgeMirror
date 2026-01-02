static void DrawAnalogClock(ImDrawList* drawList, ImVec2 center, float radius, const ImVec4& accent,
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
}

void DrawPomodoroPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Pomodoro, state.storageDir);
    DrawCardHeader(u8"Помодоро");
    ImGui::TextDisabled(u8"Фокус-сессии с подтверждением перехода между интервалами.");
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;

    const bool running = state.pomodoroRunning;
    const bool awaiting = state.pomodoroAwaitConfirm;
    const bool paused = !running && !awaiting && state.pomodoroRemainingSeconds > 0;
    const int phaseSeconds = std::max(1, PomodoroPhaseMinutes(state.ui, state.pomodoroPhase) * 60);
    int remainingSeconds = running || paused ? state.pomodoroRemainingSeconds : phaseSeconds;
    if (awaiting) remainingSeconds = 0;
    const float progress = awaiting ? 1.0f
        : (running || paused)
        ? 1.0f - (static_cast<float>(remainingSeconds) / static_cast<float>(phaseSeconds))
        : 0.0f;
    const std::string remainingText = FormatPomodoroTime(remainingSeconds);

    if (ImGui::BeginTable("pomodoro_layout", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("pomodoro_left", ImGuiTableColumnFlags_WidthStretch, 1.25f);
        ImGui::TableSetupColumn("pomodoro_right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (BeginCard("pomodoro_hero")) {
            DrawCardHeader(u8"Сессия");
            ImGui::TextDisabled(u8"Фаза");
            ImGui::TextUnformatted(PomodoroPhaseLabel(state.pomodoroPhase));
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextDisabled(u8"Осталось");
            ImGui::SetWindowFontScale(1.35f);
            ImGui::Text("%s", remainingText.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-1.0f, 8.0f));
            ImGui::TextDisabled(u8"Фокусов: %d / %d",
                                std::min(state.pomodoroCyclesCompleted, state.ui.pomodoroCyclesBeforeLong),
                                state.ui.pomodoroCyclesBeforeLong);
            if (awaiting) {
                ImGui::TextDisabled(u8"Следующий интервал: %s", PomodoroPhaseLabel(state.pomodoroNextPhase));
            } else if (!running && !paused) {
                ImGui::TextDisabled(u8"Готов к старту.");
            }
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            const float clockSize = std::min(150.0f, ImGui::GetContentRegionAvail().x);
            ImGui::Dummy(ImVec2(clockSize, clockSize));
            const ImVec2 clockMin = ImGui::GetItemRectMin();
            const ImVec2 clockMax = ImGui::GetItemRectMax();
            const ImVec2 clockCenter((clockMin.x + clockMax.x) * 0.5f, (clockMin.y + clockMax.y) * 0.5f);
            ImVec4 dim = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            dim.w = std::max(dim.w, 0.6f);
            DrawAnalogClock(ImGui::GetWindowDrawList(), clockCenter, clockSize * 0.45f, accent, dim);
            std::time_t t = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
            ImGui::TextDisabled("%s", buf);
        }
        EndCard();

        ImGui::TableSetColumnIndex(1);
        if (BeginCard("pomodoro_controls")) {
            DrawCardHeader(u8"Управление");
            if (awaiting) {
                if (ImGui::Button(u8"Начать следующий", ImVec2(-1.0f, 0.0f))) {
                    StartPomodoroPhase(state, state.pomodoroNextPhase);
                }
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    ResetPomodoroState(state);
                }
            } else if (running) {
                if (ImGui::Button(u8"Пауза", ImVec2(-1.0f, 0.0f))) {
                    state.pomodoroRunning = false;
                    state.pomodoroAccumulator = 0.0f;
                }
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    ResetPomodoroState(state);
                }
            } else if (paused) {
                if (ImGui::Button(u8"Продолжить", ImVec2(-1.0f, 0.0f))) {
                    state.pomodoroRunning = true;
                }
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    ResetPomodoroState(state);
                }
            } else {
                if (ImGui::Button(u8"Старт фокуса", ImVec2(-1.0f, 0.0f))) {
                    StartPomodoroPhase(state, static_cast<int>(PomodoroPhase::Work));
                }
            }
        }
        EndCard();

        ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
        if (BeginCard("pomodoro_settings")) {
            DrawCardHeader(u8"Настройки");
            ImGui::TextDisabled(u8"Изменения применяются к следующему интервалу.");
            int workMinutes = state.ui.pomodoroWorkMinutes;
            int breakMinutes = state.ui.pomodoroBreakMinutes;
            int longBreakMinutes = state.ui.pomodoroLongBreakMinutes;
            int cycles = state.ui.pomodoroCyclesBeforeLong;
            bool soundEnabled = state.ui.pomodoroSoundEnabled;
            bool changed = false;
            if (ImGui::SliderInt(u8"Фокус (мин)", &workMinutes, 5, 120)) {
                workMinutes = std::clamp(workMinutes, 5, 120);
                changed = true;
            }
            if (ImGui::SliderInt(u8"Перерыв (мин)", &breakMinutes, 3, 60)) {
                breakMinutes = std::clamp(breakMinutes, 3, 60);
                changed = true;
            }
            if (ImGui::SliderInt(u8"Длинный перерыв (мин)", &longBreakMinutes, 5, 90)) {
                longBreakMinutes = std::clamp(longBreakMinutes, 5, 90);
                changed = true;
            }
            if (ImGui::SliderInt(u8"Фокусов до длинного", &cycles, 1, 8)) {
                cycles = std::clamp(cycles, 1, 8);
                changed = true;
            }
            if (ImGui::Checkbox(u8"Звук окончания интервала", &soundEnabled)) {
                changed = true;
            }
            if (changed) {
                state.ui.pomodoroWorkMinutes = workMinutes;
                state.ui.pomodoroBreakMinutes = breakMinutes;
                state.ui.pomodoroLongBreakMinutes = longBreakMinutes;
                state.ui.pomodoroCyclesBeforeLong = cycles;
                state.ui.pomodoroSoundEnabled = soundEnabled;
                state.uiDirty = true;
                state.pomodoroCyclesCompleted = std::min(state.pomodoroCyclesCompleted, state.ui.pomodoroCyclesBeforeLong);
            }
        }
        EndCard();
        ImGui::EndTable();
    }
}
