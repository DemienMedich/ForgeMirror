
static bool PomodoroWithinWindow(const GuiState& state, std::int64_t now) {
    std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const int wday = tm.tm_wday; // 0=Sun
    if (wday < 0 || wday > 6) return false;
    if ((state.vault.pomodoroDaysMask & (1 << wday)) == 0) return false;
    const int minutes = tm.tm_hour * 60 + tm.tm_min;
    const int start = state.vault.pomodoroStartMinutes;
    const int end = state.vault.pomodoroEndMinutes;
    if (start == end) return false;
    if (start < end) {
        return minutes >= start && minutes < end;
    }
    return minutes >= start || minutes < end;
}

static bool CanAwardPomodoroCoin(const GuiState& state, int workMinutes, int remainingSeconds) {
    if (!state.active) return false;
    if (state.vault.pomodoroCoinsPerCycle <= 0) return false;
    if (remainingSeconds > 0) return false; // only full cycle
    const auto& profile = state.active->profile;
    if (profile.password_encoded().empty()) return false;
    if (!IsProfileAccessUnlocked(state, state.active->id)) return false;
    const std::int64_t now = NowSeconds();
    const std::int64_t startedAt = state.pomodoroWorkStartedAt > 0 ? state.pomodoroWorkStartedAt : now;
    int elapsedMinutes = workMinutes;
    if (startedAt > 0 && now >= startedAt) {
        const std::int64_t delta = now - startedAt;
        elapsedMinutes = static_cast<int>((delta + 59) / 60);
        if (elapsedMinutes < workMinutes) elapsedMinutes = workMinutes;
    }
    if (elapsedMinutes < state.vault.pomodoroMinMinutes) return false;
    return PomodoroWithinWindow(state, startedAt);
}

static void QueuePomodoroCoin(GuiState& state, int amount) {
    if (!state.active) return;
    state.pomodoroCoinPending = true;
    state.pomodoroCoinProfileId = state.active->id;
    state.pomodoroCoinAmount = amount;
    state.pomodoroCoinTimestamp = NowSeconds();
}

static void ApplyPomodoroCoin(GuiState& state, IJobStorage& storage) {
    if (!state.pomodoroCoinPending) return;
    const std::string targetId = state.pomodoroCoinProfileId;
    state.pomodoroCoinPending = false;
    state.pomodoroCoinProfileId.clear();
    if (targetId.empty()) return;
    const int amount = std::max(0, state.pomodoroCoinAmount);
    if (amount <= 0) return;
    std::string restoreId = state.active ? state.active->id : std::string{};
    if (!storage.set_active_profile(targetId)) {
        SetStatus(state, u8"Не удалось начислить монету (профиль не найден).", 1.0f, 0.45f, 0.45f);
        return;
    }
    auto profileOpt = storage.load_profile();
    if (!profileOpt) {
        SetStatus(state, u8"Не удалось начислить монету (ошибка профиля).", 1.0f, 0.45f, 0.45f);
        if (!restoreId.empty() && restoreId != targetId) storage.set_active_profile(restoreId);
        return;
    }
    Profile profile = *profileOpt;
    profile.set_wallet_balance(profile.wallet_balance() + amount);
    if (!storage.save_profile(profile)) {
        SetStatus(state, u8"Не удалось сохранить кошелёк профиля.", 1.0f, 0.45f, 0.45f);
    } else {
        if (state.active && state.active->id == targetId) {
            state.active->profile = profile;
        }
        state.profileWalletCache[targetId] = profile.wallet_balance();
        SetStatus(state, u8"Монета начислена за полный цикл помодоро.", 0.45f, 0.9f, 0.45f);
    }
    if (!restoreId.empty() && restoreId != targetId) {
        storage.set_active_profile(restoreId);
    }
    state.pomodoroCoinAmount = 0;
    state.pomodoroCoinTimestamp = 0;
}

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

static bool PlayPomodoroSoundFile(const std::string& path, int volumePercent) {
#if defined(_WIN32)
    if (path.empty()) return false;
    if (volumePercent <= 0) return true;
    std::filesystem::path audioPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(audioPath, ec)) return false;
    std::string ext = ToLowerAscii(audioPath.extension().string());
    mciSendStringA("close pomodoro_sound", nullptr, 0, nullptr);
    const char* typeName = (ext == ".wav") ? "waveaudio" : "mpegvideo";
    std::string openCmd = "open \"" + audioPath.string() + "\" type " + typeName + " alias pomodoro_sound";
    if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) return false;
    int volume = std::clamp(volumePercent, 0, 100) * 10;
    std::string volumeCmd = "setaudio pomodoro_sound volume to " + std::to_string(volume);
    mciSendStringA(volumeCmd.c_str(), nullptr, 0, nullptr);
    mciSendStringA("play pomodoro_sound from 0", nullptr, 0, nullptr);
    return true;
#else
    (void)path;
    (void)volumePercent;
    return false;
#endif
}

static void PlayPomodoroSound(GuiState& state, int phase) {
    if (!state.ui.pomodoroSoundEnabled) return;
    const std::string soundPath = ResolvePomodoroSoundPath(state, phase);
#if defined(_WIN32)
    if (soundPath.empty()) {
        SetStatus(state, u8"Звук Помодоро не настроен (выберите файл).", 1.0f, 0.75f, 0.35f);
        return;
    }
    if (!std::filesystem::exists(soundPath)) {
        SetStatus(state, u8"Файл звука Помодоро не найден: " + soundPath, 1.0f, 0.75f, 0.35f);
        return;
    }
    if (!PlayPomodoroSoundFile(soundPath, state.ui.pomodoroSoundVolume)) {
        SetStatus(state, u8"Не удалось воспроизвести звук Помодоро (кодек MCI?): " + soundPath, 1.0f, 0.75f, 0.35f);
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
    if (state.pomodoroPhase == static_cast<int>(PomodoroPhase::Work)) {
        state.pomodoroWorkStartedAt = NowSeconds();
    }
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
        const int workMinutes = PomodoroPhaseMinutes(state.ui, phase);
        if (CanAwardPomodoroCoin(state, workMinutes, state.pomodoroRemainingSeconds)) {
            QueuePomodoroCoin(state, std::max(1, state.vault.pomodoroCoinsPerCycle));
        }
        state.pomodoroWorkStartedAt = 0;
        state.pomodoroCyclesCompleted = std::min(state.pomodoroCyclesCompleted + 1, 99);
    } else if (phase == static_cast<int>(PomodoroPhase::LongBreak)) {
        state.pomodoroCyclesCompleted = 0;
    }
    state.pomodoroNextPhase = NextPomodoroPhase(state);
    state.pomodoroRunning = false;
    state.pomodoroRemainingSeconds = 0;
    state.pomodoroAccumulator = 0.0f;
    PlayPomodoroSound(state, phase);
    if (state.ui.pomodoroAutoAdvance) {
        StartPomodoroPhase(state, state.pomodoroNextPhase);
    } else {
        state.pomodoroAwaitConfirm = true;
    }
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

// Quick control helpers (used outside this file)
void PomodoroQuickReset(GuiState& state) {
    ResetPomodoroState(state);
}

void PomodoroQuickStart(GuiState& state) {
    if (state.pomodoroAwaitConfirm) {
        StartPomodoroPhase(state, state.pomodoroNextPhase);
        return;
    }
    if (state.pomodoroRemainingSeconds > 0 && !state.pomodoroRunning) {
        state.pomodoroRunning = true;
        state.pomodoroAccumulator = 0.0f;
        return;
    }
    StartPomodoroPhase(state, static_cast<int>(PomodoroPhase::Work));
}

void PomodoroQuickNext(GuiState& state) {
    if (state.pomodoroAwaitConfirm) {
        StartPomodoroPhase(state, state.pomodoroNextPhase);
    } else if (state.pomodoroRunning) {
        FinishPomodoroInterval(state);
    } else {
        StartPomodoroPhase(state, NextPomodoroPhase(state));
    }
}
