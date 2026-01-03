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

static bool PomodoroIsBreakPhase(int phase) {
    const int clamped = PomodoroClampPhase(phase);
    return clamped == static_cast<int>(PomodoroPhase::Break) ||
        clamped == static_cast<int>(PomodoroPhase::LongBreak);
}

static std::string ResolvePomodoroSoundPath(const GuiState& state, int phase) {
    const std::string& relative = PomodoroIsBreakPhase(phase)
        ? state.ui.pomodoroSoundBreak
        : state.ui.pomodoroSoundFocus;
    if (relative.empty()) return {};
    std::filesystem::path path(relative);
    if (path.is_absolute()) return path.string();
    std::filesystem::path candidate = state.storageDir / path;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) return candidate.string();
    std::filesystem::path legacy = std::filesystem::path("data") / path;
    if (auto resolved = FindAssetUpwards(legacy)) {
        return resolved->string();
    }
    if (auto resolved = FindAssetUpwards(path)) {
        return resolved->string();
    }
    return candidate.string();
}

static bool PlayPomodoroSoundFile(const std::string& path) {
#if defined(_WIN32)
    if (path.empty()) return false;
    std::filesystem::path audioPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(audioPath, ec)) return false;
    std::string ext = ToLowerAscii(audioPath.extension().string());
    if (ext == ".wav") {
        return PlaySoundA(audioPath.string().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != 0;
    }
    mciSendStringA("close pomodoro_sound", nullptr, 0, nullptr);
    std::string openCmd = "open \"" + audioPath.string() + "\" type mpegvideo alias pomodoro_sound";
    if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) return false;
    mciSendStringA("play pomodoro_sound from 0", nullptr, 0, nullptr);
    return true;
#else
    (void)path;
    return false;
#endif
}

static void PlayPomodoroSound(const GuiState& state, int phase) {
    if (!state.ui.pomodoroSoundEnabled) return;
    const std::string soundPath = ResolvePomodoroSoundPath(state, phase);
#if defined(_WIN32)
    if (!PlayPomodoroSoundFile(soundPath)) {
        MessageBeep(MB_OK);
    }
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
    PlayPomodoroSound(state, phase);
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
