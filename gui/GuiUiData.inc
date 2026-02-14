enum class UiWindowId {
    MainMenu = 0,
    Profile,
    SkillCatalog,
    Professions,
    Pipeline,
    Rules,
    UiSettings,
    View3D,
    View3DSettings,
    AdminStats,
    Logs,
    Tasks,
    Projects,
    Pomodoro,
    Shortcuts,
    About,
    BannerAdmin,
    VaultAdmin,
    Count
};

constexpr size_t kUiWindowCount = static_cast<size_t>(UiWindowId::Count);
constexpr float kNavigationPanelWidth = 240.0f;

struct UiWindowInfo {
    UiWindowId id;
    const char* label;
};

const std::array<UiWindowInfo, kUiWindowCount>& UiWindows() {
    static const std::array<UiWindowInfo, kUiWindowCount> windows = {{
        {UiWindowId::MainMenu, "Главное меню"},
        {UiWindowId::Profile, "Профиль"},
        {UiWindowId::SkillCatalog, "Навыки"},
        {UiWindowId::Professions, "Профессии"},
        {UiWindowId::Pipeline, "Пайплайн"},
        {UiWindowId::Rules, "Правила"},
        {UiWindowId::UiSettings, "Настройки"},
        {UiWindowId::View3D, "3D просмотр"},
        {UiWindowId::View3DSettings, "3D настройки"},
        {UiWindowId::AdminStats, "Статистика"},
        {UiWindowId::Logs, "Логи"},
        {UiWindowId::Tasks, "Задачи"},
        {UiWindowId::Projects, "Проекты"},
        {UiWindowId::Pomodoro, "Помодоро"},
        {UiWindowId::Shortcuts, "Ярлыки"},
        {UiWindowId::About, "О программе"},
        {UiWindowId::BannerAdmin, "Баннер"},
        {UiWindowId::VaultAdmin, "Хранилище"}
    }};
    return windows;
}

struct UiSettings {
    int theme = 0; // 0=Dark,1=Light,2=Classic
    float fontScale = 1.0f;
    float alpha = 1.0f;
    float windowRounding = 6.0f;
    float frameRounding = 4.0f;
    float scrollbarRounding = 6.0f;
    float grabRounding = 4.0f;
    ImVec2 windowPadding = ImVec2(12.0f, 12.0f);
    ImVec2 framePadding = ImVec2(8.0f, 6.0f);
    ImVec2 itemSpacing = ImVec2(8.0f, 6.0f);
    bool customColors = false;
    float backgroundAlpha = 0.25f;
    bool backgroundTiled = false;
    float backgroundTileScale = 1.0f;
    bool windowDecorated = true;
    bool windowFullscreen = false;
    int profileTab = 0;
    int profileViewMode = 1;
    std::string lastProfileId;
    std::string profileUnlocked;
    std::string profileRecent;
    std::string logFilter;
    std::string logSourceFilter;
    bool logShowInfo = true;
    bool logShowWarning = true;
    bool logShowError = true;
    bool logAutoScroll = true;
    bool logCompactView = false;
    std::string adminStatsFilter;
    bool adminStatsIncludeArchived = true;
    int adminStatsRankFilter = 0;
    bool adminStatsAutoRefresh = true;
    int adminStatsRefreshSeconds = 30;
    int inactivityThresholdDays = 30;
    int pomodoroWorkMinutes = 25;
    int pomodoroBreakMinutes = 5;
    int pomodoroLongBreakMinutes = 15;
    int pomodoroCyclesBeforeLong = 4;
    bool pomodoroSoundEnabled = true;
    std::string pomodoroSoundFocus;
    std::string pomodoroSoundBreak;
    int pomodoroSoundVolume = 80;
    bool pomodoroAutoAdvance = false;
    std::array<ImVec4, ImGuiCol_COUNT> colors{};
    std::array<std::string, kUiWindowCount> backgrounds{};

    std::string modelPath;
    float modelYaw = 0.0f;
    float modelPitch = 0.0f;
    float modelZoom = 1.0f;
    bool modelAutoRotate = true;
    float modelAutoSpeed = 0.6f;
    ImVec4 modelColor = ImVec4(0.6f, 0.85f, 1.0f, 1.0f);
};

struct UiFilters {
    std::array<char, 64> profile{};
    std::array<char, 64> profileSkill{};
    std::array<char, 64> activity{};
    std::array<char, 64> achievement{};
    std::array<char, 64> xpSkill{};
    std::array<char, 64> adminStats{};
    std::array<char, 64> skill{};
    std::array<char, 64> skillAch{};
    std::array<char, 64> pipeline{};
    std::array<char, 64> iconPicker{};
    std::array<char, 64> background{};
    std::array<char, 64> model{};
    std::array<char, 96> logs{};
    std::array<char, 64> logSource{};
    std::array<char, 96> tasks{};
};

struct UiColorEntry {
    const char* name;
    ImGuiCol col;
};

bool StartsWith(const char* value, const char* prefix) {
    if (!value || !prefix) return false;
    while (*prefix) {
        if (*value++ != *prefix++) return false;
    }
    return true;
}

const std::vector<UiColorEntry>& UiColorEntries() {
    static std::vector<UiColorEntry> entries;
    if (entries.empty()) {
        entries.reserve(ImGuiCol_COUNT);
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            entries.push_back({ImGui::GetStyleColorName(i), static_cast<ImGuiCol>(i)});
        }
    }
    return entries;
}

struct UiColorGroup {
    const char* label;
    std::vector<int> indices;
};

const std::vector<UiColorGroup>& UiColorGroups() {
    static std::vector<UiColorGroup> groups;
    if (!groups.empty()) return groups;
    const char* orderedLabels[] = {
        u8"Текст",
        u8"Окна и фон",
        u8"Границы",
        u8"Фреймы",
        u8"Заголовки",
        u8"Скроллбар",
        u8"Кнопки",
        u8"Хедеры",
        u8"Разделители",
        u8"Изменение размера",
        u8"Табы",
        u8"Таблицы",
        u8"Графики",
        u8"Навигация",
        u8"Drag&Drop",
        u8"Прочее"
    };
    groups.reserve(IM_ARRAYSIZE(orderedLabels));
    for (const char* label : orderedLabels) {
        groups.push_back({label, {}});
    }
    auto categoryIndex = [&](const char* name) -> int {
        if (StartsWith(name, "Text")) return 0;
        if (StartsWith(name, "Window") || StartsWith(name, "Child") || StartsWith(name, "Popup") || StartsWith(name, "ModalWindow")) return 1;
        if (StartsWith(name, "Border")) return 2;
        if (StartsWith(name, "Frame")) return 3;
        if (StartsWith(name, "Title") || StartsWith(name, "MenuBar")) return 4;
        if (StartsWith(name, "Scrollbar")) return 5;
        if (StartsWith(name, "Button") || StartsWith(name, "CheckMark") || StartsWith(name, "SliderGrab") || StartsWith(name, "Grab")) return 6;
        if (StartsWith(name, "Header")) return 7;
        if (StartsWith(name, "Separator")) return 8;
        if (StartsWith(name, "ResizeGrip")) return 9;
        if (StartsWith(name, "Tab")) return 10;
        if (StartsWith(name, "Table")) return 11;
        if (StartsWith(name, "Plot")) return 12;
        if (StartsWith(name, "Nav")) return 13;
        if (StartsWith(name, "DragDrop")) return 14;
        return 15;
    };
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        int idx = categoryIndex(entries[i].name);
        idx = std::clamp(idx, 0, static_cast<int>(groups.size()) - 1);
        groups[idx].indices.push_back(static_cast<int>(i));
    }
    return groups;
}

