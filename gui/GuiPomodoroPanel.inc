void DrawPomodoroPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Pomodoro, state.storageDir);
    DrawCardHeader(u8"Помодоро");
    ImGui::TextDisabled(u8"Фокус-сессии с подтверждением перехода между интервалами.");

    const bool running = state.pomodoroRunning;
    const bool awaiting = state.pomodoroAwaitConfirm;
    const bool paused = !running && !awaiting && state.pomodoroRemainingSeconds > 0;

    DrawCardHeader(u8"Состояние");
    ImGui::TextDisabled(u8"Фаза");
    ImGui::TextUnformatted(PomodoroPhaseLabel(state.pomodoroPhase));
    ImGui::TextDisabled(u8"Осталось");
    if (running || paused) {
        const std::string remaining = FormatPomodoroTime(state.pomodoroRemainingSeconds);
        ImGui::Text("%s", remaining.c_str());
    } else if (awaiting) {
        ImGui::TextDisabled(u8"Интервал завершён");
    } else {
        const int minutes = PomodoroPhaseMinutes(state.ui, state.pomodoroPhase);
        const std::string remaining = FormatPomodoroTime(minutes * 60);
        ImGui::TextDisabled("%s", remaining.c_str());
    }
    ImGui::TextDisabled(u8"Завершено фокусов: %d / %d",
                        std::min(state.pomodoroCyclesCompleted, state.ui.pomodoroCyclesBeforeLong),
                        state.ui.pomodoroCyclesBeforeLong);

    ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
    DrawCardHeader(u8"Управление");
    if (awaiting) {
        const char* nextLabel = PomodoroPhaseLabel(state.pomodoroNextPhase);
        ImGui::TextDisabled(u8"Следующий интервал: %s", nextLabel);
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

    ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
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
