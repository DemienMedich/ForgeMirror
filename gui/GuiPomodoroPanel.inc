struct PomodoroSoundChoice {
    std::string label;
    std::string value;
};

static std::vector<PomodoroSoundChoice> LoadPomodoroSoundChoices(const std::filesystem::path& storageDir,
                                                                 std::filesystem::path& outDir) {
    std::vector<PomodoroSoundChoice> out;
    out.push_back({u8"Системный сигнал", ""});
    std::filesystem::path musicDir = storageDir / "music";
    std::error_code ec;
    if (!std::filesystem::exists(musicDir, ec)) {
        if (auto resolved = FindAssetUpwards("data/music")) {
            musicDir = *resolved;
        }
    }
    outDir = musicDir;
    if (!std::filesystem::exists(musicDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = ToLowerAscii(entry.path().extension().string());
        if (ext != ".wav" && ext != ".mp3") continue;
        const auto filename = entry.path().filename();
        PomodoroSoundChoice choice;
        choice.label = filename.string();
        choice.value = (std::filesystem::path("music") / filename).generic_string();
        out.push_back(std::move(choice));
    }
    if (out.size() > 1) {
        std::sort(out.begin() + 1, out.end(), [](const PomodoroSoundChoice& a, const PomodoroSoundChoice& b) {
            return a.label < b.label;
        });
    }
    return out;
}

static int FindPomodoroSoundIndex(const std::vector<PomodoroSoundChoice>& choices, const std::string& value) {
    for (size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].value == value) return static_cast<int>(i);
    }
    return 0;
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
            ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-1.0f, 8.0f), "");
            ImGui::TextDisabled(u8"Фокусов: %d / %d",
                                std::min(state.pomodoroCyclesCompleted, state.ui.pomodoroCyclesBeforeLong),
                                state.ui.pomodoroCyclesBeforeLong);
            if (awaiting) {
                ImGui::TextDisabled(u8"Следующий интервал: %s", PomodoroPhaseLabel(state.pomodoroNextPhase));
            } else if (!running && !paused) {
                ImGui::TextDisabled(u8"Готов к старту.");
            }
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
            if (state.isAdmin) {
                if (ImGui::Checkbox(u8"Звук окончания интервала", &soundEnabled)) {
                    changed = true;
                }

                static std::vector<PomodoroSoundChoice> soundChoices;
                static std::filesystem::path soundDir;
                static bool soundLoaded = false;
                if (!soundLoaded) {
                    soundChoices = LoadPomodoroSoundChoices(state.storageDir, soundDir);
                    soundLoaded = true;
                }
                ImGui::TextDisabled(u8"Сигналы берутся из папки music.");
                if (!soundDir.empty()) {
                    ImGui::TextDisabled(u8"Папка: %s", soundDir.string().c_str());
                }
                if (ImGui::Button(u8"Обновить список")) {
                    soundChoices = LoadPomodoroSoundChoices(state.storageDir, soundDir);
                }
                if (soundChoices.size() <= 1) {
                    ImGui::TextDisabled(u8"Файлы .wav/.mp3 не найдены.");
                }

                std::vector<const char*> soundLabels;
                soundLabels.reserve(soundChoices.size());
                for (const auto& choice : soundChoices) {
                    soundLabels.push_back(choice.label.c_str());
                }

                int focusSoundIndex = FindPomodoroSoundIndex(soundChoices, state.ui.pomodoroSoundFocus);
                int breakSoundIndex = FindPomodoroSoundIndex(soundChoices, state.ui.pomodoroSoundBreak);
                if (ImGui::Combo(u8"Сигнал фокуса", &focusSoundIndex,
                                 soundLabels.data(), static_cast<int>(soundLabels.size()))) {
                    state.ui.pomodoroSoundFocus = soundChoices[focusSoundIndex].value;
                    changed = true;
                }
                if (ImGui::Combo(u8"Сигнал перерыва", &breakSoundIndex,
                                 soundLabels.data(), static_cast<int>(soundLabels.size()))) {
                    state.ui.pomodoroSoundBreak = soundChoices[breakSoundIndex].value;
                    changed = true;
                }
            } else {
                ImGui::TextDisabled(u8"Настройки звуков доступны администратору.");
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
