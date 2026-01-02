enum class PomodoroPhase {
    Work = 0,
    Break = 1,
    LongBreak = 2
};

static int PomodoroClampPhase(int phase) {
    return std::clamp(phase, 0, 2);
}

static const char* PomodoroPhaseLabel(int phase) {
    switch (static_cast<PomodoroPhase>(PomodoroClampPhase(phase))) {
        case PomodoroPhase::Work: return u8"Фокус";
        case PomodoroPhase::Break: return u8"Перерыв";
        case PomodoroPhase::LongBreak: return u8"Длинный перерыв";
        default: return u8"Фокус";
    }
}

static int PomodoroPhaseMinutes(const UiSettings& settings, int phase) {
    switch (static_cast<PomodoroPhase>(PomodoroClampPhase(phase))) {
        case PomodoroPhase::Work: return settings.pomodoroWorkMinutes;
        case PomodoroPhase::Break: return settings.pomodoroBreakMinutes;
        case PomodoroPhase::LongBreak: return settings.pomodoroLongBreakMinutes;
        default: return settings.pomodoroWorkMinutes;
    }
}

static void PlayPomodoroSound(const UiSettings& settings) {
    if (!settings.pomodoroSoundEnabled) return;
#if defined(_WIN32)
    MessageBeep(MB_OK);
#else
    std::fputc('\a', stderr);
    std::fflush(stderr);
#endif
}

static void ResetPomodoroState(GuiState& state) {
    state.pomodoroPhase = static_cast<int>(PomodoroPhase::Work);
    state.pomodoroNextPhase = static_cast<int>(PomodoroPhase::Work);
    state.pomodoroRemainingSeconds = 0;
    state.pomodoroCyclesCompleted = 0;
    state.pomodoroAccumulator = 0.0f;
    state.pomodoroRunning = false;
    state.pomodoroAwaitConfirm = false;
}

static void StartPomodoroPhase(GuiState& state, int phase) {
    const int minutes = std::max(1, PomodoroPhaseMinutes(state.ui, phase));
    state.pomodoroPhase = PomodoroClampPhase(phase);
    state.pomodoroRemainingSeconds = minutes * 60;
    state.pomodoroAccumulator = 0.0f;
    state.pomodoroRunning = true;
    state.pomodoroAwaitConfirm = false;
}

static int NextPomodoroPhase(GuiState& state) {
    const int phase = PomodoroClampPhase(state.pomodoroPhase);
    if (phase == static_cast<int>(PomodoroPhase::Work)) {
        if (state.pomodoroCyclesCompleted >= state.ui.pomodoroCyclesBeforeLong) {
            return static_cast<int>(PomodoroPhase::LongBreak);
        }
        return static_cast<int>(PomodoroPhase::Break);
    }
    return static_cast<int>(PomodoroPhase::Work);
}

static void FinishPomodoroInterval(GuiState& state) {
    const int phase = PomodoroClampPhase(state.pomodoroPhase);
    if (phase == static_cast<int>(PomodoroPhase::Work)) {
        state.pomodoroCyclesCompleted = std::min(state.pomodoroCyclesCompleted + 1, 99);
    } else if (phase == static_cast<int>(PomodoroPhase::LongBreak)) {
        state.pomodoroCyclesCompleted = 0;
    }
    state.pomodoroNextPhase = NextPomodoroPhase(state);
    state.pomodoroRunning = false;
    state.pomodoroAwaitConfirm = true;
    state.pomodoroRemainingSeconds = 0;
    state.pomodoroAccumulator = 0.0f;
    PlayPomodoroSound(state.ui);
}

static void UpdatePomodoro(GuiState& state, const ImGuiIO& io) {
    if (!state.pomodoroRunning) return;
    if (state.pomodoroRemainingSeconds <= 0) {
        FinishPomodoroInterval(state);
        return;
    }
    state.pomodoroAccumulator += io.DeltaTime;
    while (state.pomodoroAccumulator >= 1.0f && state.pomodoroRemainingSeconds > 0) {
        state.pomodoroAccumulator -= 1.0f;
        state.pomodoroRemainingSeconds -= 1;
    }
    if (state.pomodoroRemainingSeconds <= 0) {
        FinishPomodoroInterval(state);
    }
}

static std::string FormatPomodoroTime(int seconds) {
    int clamped = std::max(0, seconds);
    int minutes = clamped / 60;
    int secs = clamped % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, secs);
    return buf;
}
