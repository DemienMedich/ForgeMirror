#include "AppUtils.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"
#include "GuiActions.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <GL/gl.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <locale>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <deque>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <sstream>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <vector>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "ufbx.h"

namespace {

constexpr const char* kAdminPassword = "admin123";

// View-model representing a loaded profile + storage id for the GUI.
struct GuiProfile {
    Profile profile;
    std::string id;
};

// User-entered share for a skill in the Add Experience sheet.
struct XpEntry {
    int percent = 0;
};

// Simple description struct for pipeline hints shown in the UI sidebar.
struct PipelineStep {
    const char* title;
    const char* description;
};

// Actions supported by the confirmation modal.
enum class ConfirmAction {
    None,
    Archive,
    Restore,
    Delete
};

enum class AppLogLevel {
    Info,
    Warning,
    Error
};

struct AppLogEntry {
    std::int64_t timestamp = 0;
    AppLogLevel level = AppLogLevel::Info;
    std::string source;
    std::string message;
};

enum class UiWindowId {
    MainMenu = 0,
    Profile,
    SkillCatalog,
    Pipeline,
    Rules,
    UiSettings,
    View3D,
    View3DSettings,
    AdminStats,
    Count
};

constexpr size_t kUiWindowCount = static_cast<size_t>(UiWindowId::Count);

struct UiWindowInfo {
    UiWindowId id;
    const char* label;
};

const std::array<UiWindowInfo, kUiWindowCount>& UiWindows() {
    static const std::array<UiWindowInfo, kUiWindowCount> windows = {{
        {UiWindowId::MainMenu, "Главное меню"},
        {UiWindowId::Profile, "Профиль"},
        {UiWindowId::SkillCatalog, "Каталог навыков"},
        {UiWindowId::Pipeline, "Пайплайн"},
        {UiWindowId::Rules, "Правила"},
        {UiWindowId::UiSettings, "Настройки интерфейса"},
        {UiWindowId::View3D, "3D просмотр"},
        {UiWindowId::View3DSettings, "3D настройки"},
        {UiWindowId::AdminStats, "Статистика"}
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
    ImVec2 windowPadding = ImVec2(8.0f, 8.0f);
    ImVec2 framePadding = ImVec2(6.0f, 4.0f);
    ImVec2 itemSpacing = ImVec2(8.0f, 4.0f);
    bool customColors = false;
    float backgroundAlpha = 0.25f;
    bool backgroundTiled = false;
    float backgroundTileScale = 1.0f;
    bool windowDecorated = true;
    bool windowFullscreen = false;
    std::array<ImVec4, 18> colors{};
    std::array<std::string, kUiWindowCount> backgrounds{};

    std::string modelPath;
    float modelYaw = 0.0f;
    float modelPitch = 0.0f;
    float modelZoom = 1.0f;
    bool modelAutoRotate = true;
    float modelAutoSpeed = 0.6f;
    ImVec4 modelColor = ImVec4(0.6f, 0.85f, 1.0f, 1.0f);
};

struct UiColorEntry {
    const char* name;
    ImGuiCol col;
};

const std::array<UiColorEntry, 18>& UiColorEntries() {
    static const std::array<UiColorEntry, 18> entries = {{
        {"Text", ImGuiCol_Text},
        {"TextDisabled", ImGuiCol_TextDisabled},
        {"WindowBg", ImGuiCol_WindowBg},
        {"ChildBg", ImGuiCol_ChildBg},
        {"PopupBg", ImGuiCol_PopupBg},
        {"Border", ImGuiCol_Border},
        {"FrameBg", ImGuiCol_FrameBg},
        {"FrameBgHovered", ImGuiCol_FrameBgHovered},
        {"FrameBgActive", ImGuiCol_FrameBgActive},
        {"TitleBg", ImGuiCol_TitleBg},
        {"TitleBgActive", ImGuiCol_TitleBgActive},
        {"TitleBgCollapsed", ImGuiCol_TitleBgCollapsed},
        {"Header", ImGuiCol_Header},
        {"HeaderHovered", ImGuiCol_HeaderHovered},
        {"HeaderActive", ImGuiCol_HeaderActive},
        {"Button", ImGuiCol_Button},
        {"ButtonHovered", ImGuiCol_ButtonHovered},
        {"ButtonActive", ImGuiCol_ButtonActive}
    }};
    return entries;
}

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Triangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

struct MeshData {
    std::vector<Triangle> triangles;
    Vec3 min;
    Vec3 max;
    bool valid = false;
};

struct AdminStatsProfile {
    std::string id;
    std::string name;
    int level = 0;
    int totalXp = 0;
    std::int64_t lastTaskTimestamp = 0;
    int recoveryTasksRemaining = 0;
    int achievementsTotal = 0;
    int achievementsActive = 0;
    bool archived = false;
};

struct AdminStatsCache {
    std::int64_t lastUpdated = 0;
    std::vector<AdminStatsProfile> profiles;
    int totalProfiles = 0;
    int archivedProfiles = 0;
    int activeProfiles = 0;
    int maxLevel = 0;
    double avgLevel = 0.0;
    long long totalXp = 0;
    double avgXp = 0.0;
    int achievementsTotal = 0;
    int achievementsActive = 0;
    int profilesWithRecovery = 0;
    int profilesNoActivity = 0;
    int profilesNoAchievements = 0;
    std::array<int, 6> categoryTotals{};
    std::array<int, 6> categoryCounts{};
};

// Aggregates every bit of GUI state (selected profile, popups, sheet values, etc.).
struct GuiState {
    std::vector<IJobStorage::ProfileInfo> profiles;
    int selectedIndex = -1;
    std::optional<GuiProfile> active;
    std::string statusMessage;
    float statusColor[4] = {0.6f, 0.7f, 1.0f, 1.0f};
    std::filesystem::path storageDir;
    GameplayConfig rulesConfig;
    GameplayConfig rulesDraft;
    UiSettings ui;
    bool uiDirty = false;
    int uiLastTheme = -1;
    bool windowDecoratedLast = true;
    bool windowFullscreenLast = false;
    int windowedX = 100;
    int windowedY = 100;
    int windowedW = 1280;
    int windowedH = 720;
    MeshData viewMesh;
    std::string viewMeshPath;
    std::string viewMeshError;
    std::array<char, 260> modelPathBuffer{};
    bool showSkillCatalog = false;
    bool showPipeline = false;
    bool showRules = false;
    bool showUiSettings = false;
    bool showView3d = false;
    bool showView3dSettings = false;
    bool showAdminStats = false;

    bool createPopupRequest = false;
    bool confirmPopupRequest = false;
    bool xpPopupRequest = false;
    bool addSkillPopupRequest = false;
    bool deleteSkillPopupRequest = false;
    bool mergeSkillPopupRequest = false;
    bool adminPopupRequest = false;
    bool isAdmin = false;
    ConfirmAction confirmAction = ConfirmAction::None;
    std::array<char, 128> modalBuffer{};
    std::array<char, 128> newSkillName{};
    std::array<char, 256> newSkillDesc{};
    std::array<char, 128> editSkillName{};
    std::array<char, 256> editSkillDesc{};
    float newSkillWeight = 1.0f;
    std::array<char, 64> adminPassword{};
    std::string pendingSkillDelete;
    std::string pendingMergeFromId;
    std::string pendingMergeToId;
    std::string pendingMergeName;
    std::string pendingMergeDesc;
    float pendingMergeWeight = 1.0f;
    float editedSkillWeight = 1.0f;
    std::array<char, 128> achTitle{};
    std::array<char, 128> achIcon{};
    float achBonus = 0.0f;
    int achDurationDays = 0;
    int selectedAchievementIndex = -1;
    std::vector<XpEntry> xpEntries;
    std::unordered_map<std::string, std::deque<std::string>> activityLogs;
    std::deque<AppLogEntry> appLogs;
    AdminStatsCache adminStats;
    bool adminStatsDirty = true;
    std::array<char, 64> profileFilter{};
    std::array<char, 64> profileSkillFilter{};
    std::array<char, 64> activityFilter{};
    std::array<char, 64> achievementFilter{};
    std::array<char, 64> xpSkillFilter{};
    std::array<float, 2> profileSkillWeightRange{0.0f, 2.0f};
    int profileSkillCategoryFilter = 0;
    int inactivityThresholdDays = 30;
    std::array<char, 64> adminStatsFilter{};
    bool adminStatsIncludeArchived = true;
    int topSkillMode = 0;
    bool showExpiredAchievements = true;
    bool adminStatsAutoRefresh = true;
    int adminStatsRefreshSeconds = 30;
    bool logCompactView = false;
    int xpSortMode = 0;
    int profileSection = 0;
    std::array<char, 64> skillFilter{};
    std::array<char, 96> logFilter{};
    int taskScore = 10;
    int taskCategoryIndex = 0;
    int selectedCatalogIndex = -1;
    int selectedPipelineIndex = 0;
    int selectedRankIndex = 0;
    int lastCatalogSelection = -1;
    int profileSkillSort = 0;
    int profileSort = 0;
    bool showArchivedProfiles = true;
    bool showLogs = false;
    bool logShowInfo = true;
    bool logShowWarning = true;
    bool logShowError = true;
    bool logAutoScroll = true;
    bool requestExit = false;
};

// Walk parent directories trying to locate an asset (fonts, ini, etc.).
std::optional<std::filesystem::path> FindAssetUpwards(const std::filesystem::path& relative) {
    std::error_code ec;
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        auto candidate = current / relative;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        auto parent = current.parent_path();
        if (parent == current) break;
        current = std::move(parent);
    }
    return std::nullopt;
}

// Pull profile list from storage and sort it for deterministic GUI display.
std::vector<IJobStorage::ProfileInfo> LoadProfiles(IJobStorage& storage) {
    auto list = storage.list_profiles();
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return list;
}

std::int64_t NowSeconds();
std::string TrimStringGui(std::string s);
int TotalSkillXpGui(const Skill& skill);
void RefreshProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& preferredId = {});
const char* RankLabelForLevel(int level);

const char* LogLevelLabel(AppLogLevel level) {
    switch (level) {
        case AppLogLevel::Warning: return u8"Предупреждение";
        case AppLogLevel::Error: return u8"Ошибка";
        default: return u8"Инфо";
    }
}

ImVec4 LogLevelColor(AppLogLevel level) {
    switch (level) {
        case AppLogLevel::Warning: return ImVec4(0.95f, 0.75f, 0.3f, 1.0f);
        case AppLogLevel::Error: return ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
        default: return ImVec4(0.45f, 0.8f, 0.9f, 1.0f);
    }
}

std::string ToLowerAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool MatchesFilter(const std::string& text, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    std::string needle = TrimStringGui(filter);
    if (needle.empty()) return true;
    std::string hay = ToLowerAscii(text);
    needle = ToLowerAscii(needle);
    return hay.find(needle) != std::string::npos;
}

std::string FormatTimestamp(std::int64_t ts, const char* format) {
    if (ts <= 0) return {};
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    if (std::strftime(buf, sizeof(buf), format, &tm)) {
        return std::string(buf);
    }
    return {};
}

std::string FormatDurationShort(std::int64_t seconds) {
    if (seconds <= 0) return "00:00:00";
    constexpr std::int64_t daySeconds = 24 * 3600;
    const std::int64_t days = seconds / daySeconds;
    seconds %= daySeconds;
    const std::int64_t hours = seconds / 3600;
    seconds %= 3600;
    const std::int64_t minutes = seconds / 60;
    const std::int64_t secs = seconds % 60;
    std::ostringstream out;
    out << std::setfill('0');
    if (days > 0) {
        out << days << u8"д ";
    }
    out << std::setw(2) << hours << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    return out.str();
}

int WeightCategoryIndex(double weight) {
    if (weight >= 1.3) return 1; // A
    if (weight >= 1.1) return 2; // B
    if (weight >= 0.9) return 3; // C
    if (weight >= 0.7) return 4; // D
    return 5; // E
}

const char* WeightCategoryLabel(int index) {
    static const char* labels[] = {
        u8"Все",
        u8"A (>=1.30)",
        u8"B (1.10-1.29)",
        u8"C (0.90-1.09)",
        u8"D (0.70-0.89)",
        u8"E (<0.70)"
    };
    const int clamped = std::clamp(index, 0, static_cast<int>(IM_ARRAYSIZE(labels)) - 1);
    return labels[clamped];
}

std::string SanitizeLogLine(std::string text) {
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    return text;
}

bool MatchesLogFilters(const GuiState& state, const AppLogEntry& entry) {
    if (entry.level == AppLogLevel::Info && !state.logShowInfo) return false;
    if (entry.level == AppLogLevel::Warning && !state.logShowWarning) return false;
    if (entry.level == AppLogLevel::Error && !state.logShowError) return false;
    if (state.logFilter[0] == '\0') return true;
    std::string hay = entry.source + " " + entry.message;
    return MatchesFilter(hay, state.logFilter.data());
}

int CountFilteredLogs(const GuiState& state) {
    int count = 0;
    for (const auto& entry : state.appLogs) {
        if (MatchesLogFilters(state, entry)) {
            ++count;
        }
    }
    return count;
}

bool ExportAppLogs(const GuiState& state, std::filesystem::path& outPath, int& exportedCount) {
    exportedCount = 0;
    if (state.appLogs.empty()) return false;
    std::error_code ec;
    auto logDir = state.storageDir / "meta" / "logs";
    std::filesystem::create_directories(logDir, ec);
    if (ec) return false;

    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) {
        stamp = std::to_string(NowSeconds());
    }
    outPath = logDir / ("app-log-" + stamp + ".txt");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    for (const auto& entry : state.appLogs) {
        if (!MatchesLogFilters(state, entry)) continue;
        ++exportedCount;
        std::string ts = FormatTimestamp(entry.timestamp, "%Y-%m-%d %H:%M:%S");
        out << (ts.empty() ? "-" : ts) << " | "
            << LogLevelLabel(entry.level) << " | "
            << (entry.source.empty() ? "-" : entry.source) << " | "
            << SanitizeLogLine(entry.message) << "\n";
    }
    return exportedCount > 0;
}

std::string CsvEscape(const std::string& text) {
    bool needsQuotes = false;
    for (char ch : text) {
        if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return text;
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char ch : text) {
        if (ch == '"') out.push_back('"');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

bool ExportProfileReportTxt(const GuiState& state, const SkillCatalog& catalog, const Profile& profile,
                            const std::string& profileId, std::filesystem::path& outPath) {
    std::error_code ec;
    auto reportDir = state.storageDir / "meta" / "reports";
    std::filesystem::create_directories(reportDir, ec);
    if (ec) return false;
    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) stamp = std::to_string(NowSeconds());
    outPath = reportDir / ("profile-" + profileId + "-" + stamp + ".txt");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());

    const int level = profile.overall_level();
    const int totalXp = profile.total_xp();
    const int progress = profile.level_progress();
    const int xpToNext = profile.xp_to_next_level();
    const int xpNeeded = progress + xpToNext;
    const std::int64_t lastTask = profile.last_task_timestamp();
    const std::string lastTaskText = lastTask > 0 ? FormatTimestamp(lastTask, "%Y-%m-%d %H:%M") : std::string(u8"нет данных");
    const std::string elapsedText = lastTask > 0
        ? FormatDurationShort(std::max<std::int64_t>(0, NowSeconds() - lastTask))
        : std::string(u8"—");

    out << "Отчет профиля\n";
    out << "ID: " << profileId << "\n";
    out << "Имя: " << profile.name() << "\n";
    out << "Уровень: " << level << "\n";
    out << "Ранг: " << DescribeOverallRank(profile) << "\n";
    out << "Всего XP: " << totalXp << "\n";
    out << "Прогресс уровня: " << progress << " / " << xpNeeded << "\n";
    out << "Последняя активность: " << lastTaskText << " (" << elapsedText << ")\n";
    if (profile.recovery_tasks_remaining() > 0) {
        out << "Прогрев: осталось задач " << profile.recovery_tasks_remaining() << "\n";
    } else {
        out << "Прогрев: нет активных штрафов\n";
    }

    out << "Категории: ";
    const auto& cats = profile.category_best_scores();
    for (size_t i = 0; i < cats.size(); ++i) {
        if (i) out << ", ";
        out << Profile::kCategoryLabels[i] << "=" << cats[i] << "/10";
    }
    out << "\n\nНавыки:\n";
    out << "ID\tНазвание\tУровень\tXP\tВсегоXP\tВес\tБонус%\n";
    const std::int64_t nowSec = NowSeconds();
    for (const auto& skill : profile.list_skills()) {
        const std::string displayName = catalog.display_name(skill.name);
        double bonusPercent = std::max(0.0, (profile.skill_bonus_multiplier(skill.name, nowSec) - 1.0) * 100.0);
        out << skill.name << "\t" << displayName << "\t" << skill.level << "\t"
            << skill.xp << "/" << skill.xpToNext << "\t"
            << TotalSkillXpGui(skill) << "\t"
            << std::fixed << std::setprecision(2) << skill.weight << "\t"
            << std::fixed << std::setprecision(1) << bonusPercent << "\n";
        out << std::defaultfloat;
    }
    return true;
}

bool ExportProfileReportCsv(const GuiState& state, const SkillCatalog& catalog, const Profile& profile,
                            const std::string& profileId, std::filesystem::path& outPath) {
    std::error_code ec;
    auto reportDir = state.storageDir / "meta" / "reports";
    std::filesystem::create_directories(reportDir, ec);
    if (ec) return false;
    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) stamp = std::to_string(NowSeconds());
    outPath = reportDir / ("profile-" + profileId + "-" + stamp + ".csv");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());

    const std::int64_t lastTask = profile.last_task_timestamp();
    const std::string lastTaskText = lastTask > 0 ? FormatTimestamp(lastTask, "%Y-%m-%d %H:%M") : std::string(u8"нет данных");
    const std::string elapsedText = lastTask > 0
        ? FormatDurationShort(std::max<std::int64_t>(0, NowSeconds() - lastTask))
        : std::string(u8"—");

    out << "Section,Key,Value\n";
    out << "Summary,ID," << CsvEscape(profileId) << "\n";
    out << "Summary,Name," << CsvEscape(profile.name()) << "\n";
    out << "Summary,Level," << profile.overall_level() << "\n";
    out << "Summary,Rank," << CsvEscape(DescribeOverallRank(profile)) << "\n";
    out << "Summary,TotalXP," << profile.total_xp() << "\n";
    out << "Summary,Progress," << profile.level_progress() << "/" << (profile.level_progress() + profile.xp_to_next_level()) << "\n";
    out << "Summary,LastActivity," << CsvEscape(lastTaskText + " (" + elapsedText + ")") << "\n";
    out << "Summary,RecoveryTasks," << profile.recovery_tasks_remaining() << "\n";
    const auto& cats = profile.category_best_scores();
    for (size_t i = 0; i < cats.size(); ++i) {
        std::string key = std::string("Category ") + Profile::kCategoryLabels[i];
        out << "Categories," << CsvEscape(key) << "," << cats[i] << "\n";
    }

    out << "\nSkills\n";
    out << "ID,Name,Level,XP,XPToNext,TotalXP,Weight,BonusPercent\n";
    const std::int64_t nowSec = NowSeconds();
    for (const auto& skill : profile.list_skills()) {
        const std::string displayName = catalog.display_name(skill.name);
        double bonusPercent = std::max(0.0, (profile.skill_bonus_multiplier(skill.name, nowSec) - 1.0) * 100.0);
        out << CsvEscape(skill.name) << ","
            << CsvEscape(displayName) << ","
            << skill.level << ","
            << skill.xp << ","
            << skill.xpToNext << ","
            << TotalSkillXpGui(skill) << ","
            << std::fixed << std::setprecision(2) << skill.weight << ","
            << std::fixed << std::setprecision(1) << bonusPercent << "\n";
        out << std::defaultfloat;
    }
    return true;
}

bool ExportProfileActivity(const GuiState& state, const std::string& profileId,
                           const std::deque<std::string>& log, const char* filter,
                           std::filesystem::path& outPath) {
    std::error_code ec;
    auto reportDir = state.storageDir / "meta" / "reports";
    std::filesystem::create_directories(reportDir, ec);
    if (ec) return false;
    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) stamp = std::to_string(NowSeconds());
    outPath = reportDir / ("activity-" + profileId + "-" + stamp + ".txt");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    out << "Последние действия: " << profileId << "\n";
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        if (!MatchesFilter(*it, filter)) continue;
        out << "- " << SanitizeLogLine(*it) << "\n";
    }
    return true;
}

std::vector<AdminStatsProfile> FilterAdminProfiles(const GuiState& state) {
    std::vector<AdminStatsProfile> out;
    out.reserve(state.adminStats.profiles.size());
    for (const auto& item : state.adminStats.profiles) {
        if (!state.adminStatsIncludeArchived && item.archived) {
            continue;
        }
        const std::string hay = item.id + " " + item.name;
        if (!MatchesFilter(hay, state.adminStatsFilter.data())) {
            continue;
        }
        out.push_back(item);
    }
    return out;
}

bool ExportAdminStatsCsv(const GuiState& state, std::filesystem::path& outPath) {
    std::error_code ec;
    auto reportDir = state.storageDir / "meta" / "reports";
    std::filesystem::create_directories(reportDir, ec);
    if (ec) return false;
    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) stamp = std::to_string(NowSeconds());
    outPath = reportDir / ("admin-stats-" + stamp + ".csv");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());

    out << "ID,Name,Level,Rank,TotalXP,LastActivity,InactiveFor,Archived,RecoveryTasks,AchievementsTotal,AchievementsActive\n";
    const std::int64_t nowSec = NowSeconds();
    const auto items = FilterAdminProfiles(state);
    if (items.empty()) return false;
    for (const auto& item : items) {
        std::string lastText = item.lastTaskTimestamp > 0
            ? FormatTimestamp(item.lastTaskTimestamp, "%Y-%m-%d %H:%M")
            : std::string(u8"нет данных");
        std::string inactiveText = item.lastTaskTimestamp > 0
            ? FormatDurationShort(nowSec - item.lastTaskTimestamp)
            : std::string(u8"—");
        out << CsvEscape(item.id) << ","
            << CsvEscape(item.name) << ","
            << item.level << ","
            << CsvEscape(RankLabelForLevel(item.level)) << ","
            << item.totalXp << ","
            << CsvEscape(lastText) << ","
            << CsvEscape(inactiveText) << ","
            << (item.archived ? "yes" : "no") << ","
            << item.recoveryTasksRemaining << ","
            << item.achievementsTotal << ","
            << item.achievementsActive << "\n";
    }
    return true;
}

void RefreshAdminStats(GuiState& state, IJobStorage& storage) {
    AdminStatsCache stats;
    stats.lastUpdated = NowSeconds();
    const auto list = storage.list_profiles();
    stats.totalProfiles = static_cast<int>(list.size());
    stats.profiles.reserve(list.size());
    long long totalLevels = 0;
    const std::int64_t nowSec = NowSeconds();
    const std::string activeId = state.active ? state.active->id : std::string{};

    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        auto profile = storage.load_profile();
        if (!profile) continue;
        AdminStatsProfile item;
        item.id = info.id;
        item.name = profile->name();
        item.level = profile->overall_level();
        item.totalXp = profile->total_xp();
        item.lastTaskTimestamp = profile->last_task_timestamp();
        item.recoveryTasksRemaining = profile->recovery_tasks_remaining();
        item.achievementsTotal = static_cast<int>(profile->achievements().size());
        item.achievementsActive = 0;
        for (const auto& ach : profile->achievements()) {
            if (ach.is_active(nowSec)) {
                item.achievementsActive += 1;
            }
        }
        item.archived = info.archived;
        stats.profiles.push_back(std::move(item));
        if (info.archived) {
            stats.archivedProfiles += 1;
        }
        if (profile->recovery_tasks_remaining() > 0) {
            stats.profilesWithRecovery += 1;
        }
        if (profile->last_task_timestamp() == 0) {
            stats.profilesNoActivity += 1;
        }
        if (profile->achievements().empty()) {
            stats.profilesNoAchievements += 1;
        }
        const auto& catScores = profile->category_best_scores();
        for (size_t i = 0; i < catScores.size(); ++i) {
            stats.categoryTotals[i] += catScores[i];
            stats.categoryCounts[i] += 1;
        }
        stats.totalXp += profile->total_xp();
        totalLevels += profile->overall_level();
        stats.maxLevel = std::max(stats.maxLevel, profile->overall_level());
        stats.achievementsTotal += static_cast<int>(profile->achievements().size());
        stats.achievementsActive += item.achievementsActive;
    }
    stats.activeProfiles = stats.totalProfiles - stats.archivedProfiles;
    if (!stats.profiles.empty()) {
        stats.avgLevel = static_cast<double>(totalLevels) / static_cast<double>(stats.profiles.size());
        stats.avgXp = static_cast<double>(stats.totalXp) / static_cast<double>(stats.profiles.size());
    }
    if (!activeId.empty()) {
        storage.set_active_profile(activeId);
    }
    state.adminStats = std::move(stats);
    state.adminStatsDirty = false;
}

void AppendAppLog(GuiState& state, AppLogLevel level, const std::string& source, const std::string& message) {
    if (message.empty()) return;
    AppLogEntry entry;
    entry.timestamp = NowSeconds();
    entry.level = level;
    entry.source = source;
    entry.message = message;
    state.appLogs.push_back(std::move(entry));
    constexpr size_t kMaxLogs = 200;
    if (state.appLogs.size() > kMaxLogs) {
        state.appLogs.pop_front();
    }
}

// Helper to show feedback banner with color-coded message.
void SetStatus(GuiState& state, const std::string& msg, float r, float g, float b, float a = 1.0f,
               std::optional<AppLogLevel> logLevel = std::nullopt, const char* source = nullptr) {
    state.statusMessage = msg;
    state.statusColor[0] = r;
    state.statusColor[1] = g;
    state.statusColor[2] = b;
    state.statusColor[3] = a;
    if (logLevel.has_value()) {
        const char* src = source ? source : "GUI";
        AppendAppLog(state, *logLevel, src, msg);
    }
}

AppLogLevel LogLevelForResult(const ActionResult& result) {
    if (result.ok) return AppLogLevel::Info;
    if (result.userError) return AppLogLevel::Warning;
    return AppLogLevel::Error;
}

int TotalSkillXpGui(const Skill& skill) {
    int total = skill.xp;
    for (int lvl = 2; lvl <= skill.level; ++lvl) {
        total += Skill::required_xp_for(lvl);
    }
    return total;
}

// Reset Add Experience sheet: one row per catalog skill, even split of 100%.
void PrepareXpEntries(GuiState& state, const SkillCatalog& catalog) {
    const auto& skills = catalog.skills();
    state.xpEntries.assign(skills.size(), {});
    state.taskScore = 10;
    state.taskCategoryIndex = 0;
    const int count = static_cast<int>(skills.size());
    if (count <= 0) return;
    const int base = 100 / count;
    int remainder = 100 - base * count;
    for (int i = 0; i < count; ++i) {
        state.xpEntries[i].percent = base;
        if (remainder > 0) {
            state.xpEntries[i].percent += 1;
            --remainder;
        }
    }
}

std::string NormalizeSkillNameGui(const std::string& name) {
    auto decode_utf8 = [](const std::string& s, size_t& i, uint32_t& out) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80) {
            out = c0;
            ++i;
            return true;
        }
        if ((c0 >> 5) == 0x6 && i + 1 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c1 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            i += 2;
            return true;
        }
        if ((c0 >> 4) == 0xE && i + 2 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            i += 3;
            return true;
        }
        if ((c0 >> 3) == 0x1E && i + 3 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            i += 4;
            return true;
        }
        return false;
    };
    auto append_utf8 = [](std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    auto lower_codepoint = [](uint32_t cp) {
        if (cp >= 'A' && cp <= 'Z') return cp + 32;
        if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
        if (cp == 0x0401) return static_cast<uint32_t>(0x0451);
        return cp;
    };

    std::string out;
    out.reserve(name.size());
    size_t i = 0;
    while (i < name.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(name, i, cp)) break;
        if (cp <= 0x7F && std::isspace(static_cast<unsigned char>(cp))) continue;
        append_utf8(out, lower_codepoint(cp));
    }
    return out;
}

std::string TrimStringGui(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}

std::string UiSettingsPath(const std::filesystem::path& storageDir) {
    return (storageDir / "meta" / "ui.ini").string();
}

std::string NormalizeFloatList(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isdigit(ch) || ch == '-' || ch == '+') out.push_back(static_cast<char>(ch));
        else if (ch == '.' || ch == ',') out.push_back('.');
        else if (ch == ' ' || ch == '\t') out.push_back(' ');
    }
    return out;
}

float ParseFloat(const std::string& value, float fallback = 0.0f) {
    try {
        return std::stof(NormalizeFloatList(value));
    } catch (...) {
        return fallback;
    }
}

float ClampFinite(float value, float minValue, float maxValue, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minValue, maxValue);
}

int ParseInt(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(TrimStringGui(value));
    } catch (...) {
        return fallback;
    }
}

bool ParseBool(const std::string& value, bool fallback = false) {
    std::string v = TrimStringGui(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (v == "1" || v == "true" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "no") return false;
    return fallback;
}

ImVec2 ParseVec2(const std::string& value, const ImVec2& fallback) {
    std::stringstream ss(NormalizeFloatList(value));
    ss.imbue(std::locale::classic());
    float x = 0.0f;
    float y = 0.0f;
    if (ss >> x >> y) return ImVec2(x, y);
    return fallback;
}

ImVec4 ParseVec4(const std::string& value, const ImVec4& fallback) {
    std::stringstream ss(NormalizeFloatList(value));
    ss.imbue(std::locale::classic());
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    if (ss >> x >> y >> z >> w) return ImVec4(x, y, z, w);
    return fallback;
}

std::string Vec4ToString(const ImVec4& v) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(3)
       << v.x << " " << v.y << " " << v.z << " " << v.w;
    return ss.str();
}

UiSettings LoadUiSettings(const std::filesystem::path& storageDir, const ImGuiStyle& style) {
    UiSettings settings;
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        settings.colors[i] = style.Colors[entries[i].col];
    }
    auto path = UiSettingsPath(storageDir);
    std::ifstream in(path);
    if (!in) return settings;
    in.imbue(std::locale::classic());
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        auto t = TrimStringGui(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = TrimStringGui(t.substr(0, eq));
        std::string value = TrimStringGui(t.substr(eq + 1));
        if (section == "style") {
            if (key == "theme") settings.theme = ParseInt(value, settings.theme);
            else if (key == "fontScale") settings.fontScale = ParseFloat(value, settings.fontScale);
            else if (key == "alpha") settings.alpha = ParseFloat(value, settings.alpha);
            else if (key == "windowRounding") settings.windowRounding = ParseFloat(value, settings.windowRounding);
            else if (key == "frameRounding") settings.frameRounding = ParseFloat(value, settings.frameRounding);
            else if (key == "scrollbarRounding") settings.scrollbarRounding = ParseFloat(value, settings.scrollbarRounding);
            else if (key == "grabRounding") settings.grabRounding = ParseFloat(value, settings.grabRounding);
            else if (key == "windowPadding") settings.windowPadding = ParseVec2(value, settings.windowPadding);
            else if (key == "framePadding") settings.framePadding = ParseVec2(value, settings.framePadding);
            else if (key == "itemSpacing") settings.itemSpacing = ParseVec2(value, settings.itemSpacing);
            else if (key == "customColors") settings.customColors = ParseBool(value, settings.customColors);
            else if (key == "backgroundAlpha") settings.backgroundAlpha = ParseFloat(value, settings.backgroundAlpha);
            else if (key == "backgroundTiled") settings.backgroundTiled = ParseBool(value, settings.backgroundTiled);
            else if (key == "backgroundTileScale") settings.backgroundTileScale = ParseFloat(value, settings.backgroundTileScale);
            else if (key == "windowDecorated") settings.windowDecorated = ParseBool(value, settings.windowDecorated);
            else if (key == "windowFullscreen") settings.windowFullscreen = ParseBool(value, settings.windowFullscreen);
        } else if (section == "colors") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (key == entries[i].name) {
                    settings.colors[i] = ParseVec4(value, settings.colors[i]);
                    settings.customColors = true;
                    break;
                }
            }
        } else if (section == "backgrounds") {
                const auto& windows = UiWindows();
                for (size_t i = 0; i < windows.size(); ++i) {
                if (key == windows[i].label) {
                    settings.backgrounds[i] = value;
                    break;
                }
            }
        } else if (section == "view3d") {
            if (key == "modelPath") settings.modelPath = value;
            else if (key == "yaw") settings.modelYaw = ParseFloat(value, settings.modelYaw);
            else if (key == "pitch") settings.modelPitch = ParseFloat(value, settings.modelPitch);
            else if (key == "zoom") settings.modelZoom = ParseFloat(value, settings.modelZoom);
            else if (key == "autoRotate") settings.modelAutoRotate = ParseBool(value, settings.modelAutoRotate);
            else if (key == "autoSpeed") settings.modelAutoSpeed = ParseFloat(value, settings.modelAutoSpeed);
            else if (key == "color") settings.modelColor = ParseVec4(value, settings.modelColor);
        }
    }
    settings.theme = std::clamp(settings.theme, 0, 2);
    settings.fontScale = ClampFinite(settings.fontScale, 0.6f, 2.0f, 1.0f);
    settings.alpha = ClampFinite(settings.alpha, 0.4f, 1.0f, 1.0f);
    settings.windowRounding = ClampFinite(settings.windowRounding, 0.0f, 24.0f, style.WindowRounding);
    settings.frameRounding = ClampFinite(settings.frameRounding, 0.0f, 24.0f, style.FrameRounding);
    settings.scrollbarRounding = ClampFinite(settings.scrollbarRounding, 0.0f, 24.0f, style.ScrollbarRounding);
    settings.grabRounding = ClampFinite(settings.grabRounding, 0.0f, 24.0f, style.GrabRounding);
    settings.backgroundAlpha = ClampFinite(settings.backgroundAlpha, 0.0f, 1.0f, 0.25f);
    settings.backgroundTileScale = ClampFinite(settings.backgroundTileScale, 0.25f, 4.0f, 1.0f);
    settings.windowPadding.x = ClampFinite(settings.windowPadding.x, 0.0f, 32.0f, style.WindowPadding.x);
    settings.windowPadding.y = ClampFinite(settings.windowPadding.y, 0.0f, 32.0f, style.WindowPadding.y);
    settings.framePadding.x = ClampFinite(settings.framePadding.x, 0.0f, 24.0f, style.FramePadding.x);
    settings.framePadding.y = ClampFinite(settings.framePadding.y, 0.0f, 24.0f, style.FramePadding.y);
    settings.itemSpacing.x = ClampFinite(settings.itemSpacing.x, 0.0f, 32.0f, style.ItemSpacing.x);
    settings.itemSpacing.y = ClampFinite(settings.itemSpacing.y, 0.0f, 32.0f, style.ItemSpacing.y);
    settings.modelYaw = ClampFinite(settings.modelYaw, -50.0f, 50.0f, 0.0f);
    settings.modelPitch = ClampFinite(settings.modelPitch, -5.0f, 5.0f, 0.0f);
    settings.modelZoom = ClampFinite(settings.modelZoom, 0.3f, 3.0f, 1.0f);
    settings.modelAutoSpeed = ClampFinite(settings.modelAutoSpeed, 0.0f, 5.0f, 0.6f);
    settings.modelColor.x = ClampFinite(settings.modelColor.x, 0.0f, 1.0f, settings.modelColor.x);
    settings.modelColor.y = ClampFinite(settings.modelColor.y, 0.0f, 1.0f, settings.modelColor.y);
    settings.modelColor.z = ClampFinite(settings.modelColor.z, 0.0f, 1.0f, settings.modelColor.z);
    settings.modelColor.w = ClampFinite(settings.modelColor.w, 0.0f, 1.0f, settings.modelColor.w);
    if (settings.customColors) {
        for (size_t i = 0; i < settings.colors.size(); ++i) {
            settings.colors[i].x = ClampFinite(settings.colors[i].x, 0.0f, 1.0f, style.Colors[entries[i].col].x);
            settings.colors[i].y = ClampFinite(settings.colors[i].y, 0.0f, 1.0f, style.Colors[entries[i].col].y);
            settings.colors[i].z = ClampFinite(settings.colors[i].z, 0.0f, 1.0f, style.Colors[entries[i].col].z);
            settings.colors[i].w = ClampFinite(settings.colors[i].w, 0.0f, 1.0f, style.Colors[entries[i].col].w);
        }
    }
    return settings;
}

void SaveUiSettings(const std::filesystem::path& storageDir, const UiSettings& settings) {
    auto path = std::filesystem::path(UiSettingsPath(storageDir));
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.imbue(std::locale::classic());
    out << "[style]\n";
    out << "theme=" << settings.theme << "\n";
    out << "fontScale=" << settings.fontScale << "\n";
    out << "alpha=" << settings.alpha << "\n";
    out << "windowRounding=" << settings.windowRounding << "\n";
    out << "frameRounding=" << settings.frameRounding << "\n";
    out << "scrollbarRounding=" << settings.scrollbarRounding << "\n";
    out << "grabRounding=" << settings.grabRounding << "\n";
    out << "windowPadding=" << settings.windowPadding.x << " " << settings.windowPadding.y << "\n";
    out << "framePadding=" << settings.framePadding.x << " " << settings.framePadding.y << "\n";
    out << "itemSpacing=" << settings.itemSpacing.x << " " << settings.itemSpacing.y << "\n";
    out << "customColors=" << (settings.customColors ? 1 : 0) << "\n";
    out << "backgroundAlpha=" << settings.backgroundAlpha << "\n\n";
    out << "backgroundTiled=" << (settings.backgroundTiled ? 1 : 0) << "\n";
    out << "backgroundTileScale=" << settings.backgroundTileScale << "\n\n";
    out << "windowDecorated=" << (settings.windowDecorated ? 1 : 0) << "\n\n";
    out << "windowFullscreen=" << (settings.windowFullscreen ? 1 : 0) << "\n\n";

    out << "[colors]\n";
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        out << entries[i].name << "=" << Vec4ToString(settings.colors[i]) << "\n";
    }
    out << "\n[backgrounds]\n";
                const auto& windows = UiWindows();
                for (size_t i = 0; i < windows.size(); ++i) {
        out << windows[i].label << "=" << settings.backgrounds[i] << "\n";
    }
    out << "\n[view3d]\n";
    out << "modelPath=" << settings.modelPath << "\n";
    out << "yaw=" << settings.modelYaw << "\n";
    out << "pitch=" << settings.modelPitch << "\n";
    out << "zoom=" << settings.modelZoom << "\n";
    out << "autoRotate=" << (settings.modelAutoRotate ? 1 : 0) << "\n";
    out << "autoSpeed=" << settings.modelAutoSpeed << "\n";
    out << "color=" << Vec4ToString(settings.modelColor) << "\n";
}

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct IconTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

struct IconCache {
    std::unordered_map<std::string, IconTexture> textures;
    std::unordered_set<std::string> missing;
};

IconCache& GetIconCache() {
    static IconCache cache;
    return cache;
}

std::string NormalizeIconKey(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = path.lexically_normal();
    if (!normalized.is_absolute()) {
        normalized = std::filesystem::absolute(normalized, ec);
        if (ec) {
            normalized = path.lexically_normal();
        }
    }
    auto key = normalized.generic_string();
    if (key.empty()) key = path.generic_string();
    return key;
}

std::optional<std::filesystem::path> ResolveIconPath(const std::string& icon, const std::filesystem::path& storageDir) {
    if (icon.empty()) return std::nullopt;
    std::filesystem::path path(icon);
    std::error_code ec;
    if (path.is_relative()) {
        const std::filesystem::path candidates[] = {
            storageDir / path,
            std::filesystem::current_path() / path,
            storageDir.parent_path() / path
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    if (std::filesystem::exists(path, ec)) {
        return path;
    }
    return std::nullopt;
}

struct IconChoice {
    std::string label;
    std::string relativePath;
    std::filesystem::path absolutePath;
};

std::vector<IconChoice> LoadAchievementIconChoices(const std::filesystem::path& storageDir) {
    std::vector<IconChoice> out;
    std::error_code ec;
    auto iconDir = storageDir / "achievements" / "icons";
    if (!std::filesystem::exists(iconDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(iconDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".png") continue;
        const auto filename = entry.path().filename();
        auto rel = std::filesystem::path("achievements") / "icons" / filename;
        IconChoice choice;
        choice.label = filename.string();
        choice.relativePath = rel.generic_string();
        choice.absolutePath = entry.path();
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(), [](const IconChoice& a, const IconChoice& b) {
        return a.label < b.label;
    });
    return out;
}

struct BackgroundChoice {
    std::string label;
    std::string relativePath;
    std::filesystem::path absolutePath;
};

std::vector<BackgroundChoice> LoadUiBackgroundChoices(const std::filesystem::path& storageDir) {
    std::vector<BackgroundChoice> out;
    std::error_code ec;
    auto bgDir = storageDir / "ui" / "backgrounds";
    if (!std::filesystem::exists(bgDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(bgDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".png") continue;
        const auto filename = entry.path().filename();
        auto rel = std::filesystem::path("ui") / "backgrounds" / filename;
        BackgroundChoice choice;
        choice.label = filename.string();
        choice.relativePath = rel.generic_string();
        choice.absolutePath = entry.path();
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(), [](const BackgroundChoice& a, const BackgroundChoice& b) {
        return a.label < b.label;
    });
    return out;
}

bool LoadIconTexture(const std::filesystem::path& path, IconTexture& out) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_set_flip_vertically_on_load(0);
    std::string pathStr = path.string();
    unsigned char* data = stbi_load(pathStr.c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return false;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    GLint lastTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTex);
    GLint lastUnpack = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &lastUnpack);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, lastUnpack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTex));

    stbi_image_free(data);
    out.id = tex;
    out.width = w;
    out.height = h;
    return true;
}

const IconTexture* GetIconTexture(const std::filesystem::path& path) {
    auto& cache = GetIconCache();
    const std::string key = NormalizeIconKey(path);
    if (auto it = cache.textures.find(key); it != cache.textures.end()) {
        return &it->second;
    }
    if (cache.missing.find(key) != cache.missing.end()) {
        return nullptr;
    }
    IconTexture tex;
    if (!LoadIconTexture(path, tex)) {
        cache.missing.insert(key);
        return nullptr;
    }
    auto [it, inserted] = cache.textures.emplace(key, tex);
    return &it->second;
}

ImVec2 FitIconSize(const IconTexture& tex, float maxSize) {
    if (tex.width <= 0 || tex.height <= 0) return ImVec2(maxSize, maxSize);
    const float maxDim = static_cast<float>(std::max(tex.width, tex.height));
    const float scale = maxDim > 0.0f ? (maxSize / maxDim) : 1.0f;
    return ImVec2(tex.width * scale, tex.height * scale);
}

void ReleaseIconTextures() {
    auto& cache = GetIconCache();
    for (auto& kv : cache.textures) {
        if (kv.second.id != 0) {
            glDeleteTextures(1, &kv.second.id);
            kv.second.id = 0;
        }
    }
    cache.textures.clear();
    cache.missing.clear();
}

struct ModelChoice {
    std::string label;
    std::string relativePath;
    std::filesystem::path absolutePath;
};

std::vector<ModelChoice> LoadModelChoices(const std::filesystem::path& storageDir) {
    std::vector<ModelChoice> out;
    std::error_code ec;
    auto modelDir = storageDir / "models";
    if (!std::filesystem::exists(modelDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(modelDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".obj" && ext != ".fbx") continue;
        const auto filename = entry.path().filename();
        auto rel = std::filesystem::path("models") / filename;
        ModelChoice choice;
        choice.label = filename.string();
        choice.relativePath = rel.generic_string();
        choice.absolutePath = entry.path();
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(), [](const ModelChoice& a, const ModelChoice& b) {
        return a.label < b.label;
    });
    return out;
}

void ResetMeshBounds(MeshData& mesh) {
    mesh.min = { std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max() };
    mesh.max = { std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest() };
}

void UpdateBounds(MeshData& mesh, const Vec3& v) {
    mesh.min.x = std::min(mesh.min.x, v.x);
    mesh.min.y = std::min(mesh.min.y, v.y);
    mesh.min.z = std::min(mesh.min.z, v.z);
    mesh.max.x = std::max(mesh.max.x, v.x);
    mesh.max.y = std::max(mesh.max.y, v.y);
    mesh.max.z = std::max(mesh.max.z, v.z);
}

bool LoadObjMesh(const std::filesystem::path& path, MeshData& mesh, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "Не удалось открыть OBJ.";
        return false;
    }
    std::vector<Vec3> positions;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && std::isspace(static_cast<unsigned char>(line[1]))) {
            std::istringstream ss(line.substr(2));
            Vec3 v{};
            ss >> v.x >> v.y >> v.z;
            if (!ss.fail()) positions.push_back(v);
        } else if (line[0] == 'f' && std::isspace(static_cast<unsigned char>(line[1]))) {
            std::istringstream ss(line.substr(2));
            std::vector<int> indices;
            std::string part;
            while (ss >> part) {
                auto slash = part.find('/');
                std::string idxStr = slash == std::string::npos ? part : part.substr(0, slash);
                if (idxStr.empty()) continue;
                int idx = 0;
                try {
                    idx = std::stoi(idxStr);
                } catch (...) {
                    idx = 0;
                }
                if (idx == 0) continue;
                if (idx < 0) idx = static_cast<int>(positions.size()) + idx + 1;
                indices.push_back(idx - 1);
            }
            if (indices.size() >= 3) {
                for (size_t i = 1; i + 1 < indices.size(); ++i) {
                    const int ia = indices[0];
                    const int ib = indices[i];
                    const int ic = indices[i + 1];
                    if (ia < 0 || ib < 0 || ic < 0) continue;
                    if (ia >= static_cast<int>(positions.size()) ||
                        ib >= static_cast<int>(positions.size()) ||
                        ic >= static_cast<int>(positions.size())) {
                        continue;
                    }
                    Triangle tri{positions[ia], positions[ib], positions[ic]};
                    mesh.triangles.push_back(tri);
                }
            }
        }
    }
    if (mesh.triangles.empty()) {
        error = "OBJ не содержит треугольников.";
        return false;
    }
    ResetMeshBounds(mesh);
    for (const auto& tri : mesh.triangles) {
        UpdateBounds(mesh, tri.a);
        UpdateBounds(mesh, tri.b);
        UpdateBounds(mesh, tri.c);
    }
    mesh.valid = true;
    return true;
}

bool LoadFbxMesh(const std::filesystem::path& path, MeshData& mesh, std::string& error) {
    ufbx_error uerr;
    ufbx_load_opts opts{};
    opts.ignore_missing_external_files = true;
    opts.load_external_files = false;
    ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &uerr);
    if (!scene) {
        if (uerr.description.data && uerr.description.length > 0) {
            error.assign(uerr.description.data, uerr.description.length);
        } else {
            error = "Не удалось прочитать FBX.";
        }
        return false;
    }
    std::vector<uint32_t> triIndices;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* umesh = scene->meshes.data[mi];
        if (!umesh->vertex_position.exists) continue;
        const size_t maxTris = umesh->max_face_triangles * 3;
        if (maxTris == 0) continue;
        triIndices.resize(maxTris);
        for (size_t fi = 0; fi < umesh->faces.count; ++fi) {
            ufbx_face face = umesh->faces.data[fi];
            if (face.num_indices < 3) continue;
            const uint32_t triCount = ufbx_triangulate_face(triIndices.data(), triIndices.size(), umesh, face);
            for (uint32_t ti = 0; ti < triCount; ++ti) {
                uint32_t ia = triIndices[ti * 3 + 0];
                uint32_t ib = triIndices[ti * 3 + 1];
                uint32_t ic = triIndices[ti * 3 + 2];
                if (ia >= umesh->vertex_position.indices.count ||
                    ib >= umesh->vertex_position.indices.count ||
                    ic >= umesh->vertex_position.indices.count) {
                    continue;
                }
                uint32_t va = umesh->vertex_position.indices.data[ia];
                uint32_t vb = umesh->vertex_position.indices.data[ib];
                uint32_t vc = umesh->vertex_position.indices.data[ic];
                if (va >= umesh->vertex_position.values.count ||
                    vb >= umesh->vertex_position.values.count ||
                    vc >= umesh->vertex_position.values.count) {
                    continue;
                }
                ufbx_vec3 p0 = umesh->vertex_position.values.data[va];
                ufbx_vec3 p1 = umesh->vertex_position.values.data[vb];
                ufbx_vec3 p2 = umesh->vertex_position.values.data[vc];
                Triangle tri{{static_cast<float>(p0.x), static_cast<float>(p0.y), static_cast<float>(p0.z)},
                             {static_cast<float>(p1.x), static_cast<float>(p1.y), static_cast<float>(p1.z)},
                             {static_cast<float>(p2.x), static_cast<float>(p2.y), static_cast<float>(p2.z)}};
                mesh.triangles.push_back(tri);
            }
        }
    }
    ufbx_free_scene(scene);
    if (mesh.triangles.empty()) {
        error = "FBX не содержит треугольников.";
        return false;
    }
    ResetMeshBounds(mesh);
    for (const auto& tri : mesh.triangles) {
        UpdateBounds(mesh, tri.a);
        UpdateBounds(mesh, tri.b);
        UpdateBounds(mesh, tri.c);
    }
    mesh.valid = true;
    return true;
}

bool LoadMeshFromFile(const std::filesystem::path& path, MeshData& mesh, std::string& error) {
    mesh.triangles.clear();
    mesh.valid = false;
    error.clear();
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".obj") return LoadObjMesh(path, mesh, error);
    if (ext == ".fbx") return LoadFbxMesh(path, mesh, error);
    error = "Неподдерживаемый формат.";
    return false;
}

MeshData MakeCubeMesh() {
    MeshData mesh;
    const float s = 0.5f;
    const Vec3 v0{-s, -s, -s};
    const Vec3 v1{ s, -s, -s};
    const Vec3 v2{ s,  s, -s};
    const Vec3 v3{-s,  s, -s};
    const Vec3 v4{-s, -s,  s};
    const Vec3 v5{ s, -s,  s};
    const Vec3 v6{ s,  s,  s};
    const Vec3 v7{-s,  s,  s};
    mesh.triangles = {
        {v0, v1, v2}, {v0, v2, v3},
        {v4, v6, v5}, {v4, v7, v6},
        {v0, v4, v5}, {v0, v5, v1},
        {v1, v5, v6}, {v1, v6, v2},
        {v2, v6, v7}, {v2, v7, v3},
        {v3, v7, v4}, {v3, v4, v0}
    };
    ResetMeshBounds(mesh);
    for (const auto& tri : mesh.triangles) {
        UpdateBounds(mesh, tri.a);
        UpdateBounds(mesh, tri.b);
        UpdateBounds(mesh, tri.c);
    }
    mesh.valid = true;
    return mesh;
}

struct RankOption {
    const char* label;
    int level;
};

struct RankSpan {
    const char* currentLabel = nullptr;
    int currentLevel = 1;
    const char* nextLabel = nullptr;
    int nextLevel = 0;
    bool hasNext = false;
};

const std::vector<RankOption>& RankOptions() {
    static const std::vector<RankOption> opts = {
        {u8"Стажёр", 1},
        {u8"Джуниор I", 10},
        {u8"Джуниор II", 20},
        {u8"Джуниор III", 30},
        {u8"Джуниор IV", 40},
        {u8"Мидл I", 50},
        {u8"Мидл II", 60},
        {u8"Мидл III", 70},
        {u8"Мидл IV", 80},
        {u8"Мидл V", 90},
        {u8"Мидл VI", 100},
        {u8"Сеньор I", 150},
        {u8"Сеньор II", 160},
        {u8"Сеньор III", 170},
        {u8"Сеньор IV", 180},
        {u8"Сеньор V", 190}
    };
    return opts;
}

RankSpan ComputeRankSpan(int level) {
    RankSpan span;
    const auto& opts = RankOptions();
    if (opts.empty()) return span;
    int currentIdx = 0;
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (level >= opts[i].level) {
            currentIdx = i;
        } else {
            break;
        }
    }
    span.currentLabel = opts[currentIdx].label;
    span.currentLevel = opts[currentIdx].level;
    if (currentIdx + 1 < static_cast<int>(opts.size())) {
        span.nextLabel = opts[currentIdx + 1].label;
        span.nextLevel = opts[currentIdx + 1].level;
        span.hasNext = true;
    }
    return span;
}

int RankIndexForLevel(int level) {
    const auto& opts = RankOptions();
    int bestIdx = 0;
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (level >= opts[i].level) {
            bestIdx = i;
        }
    }
    return bestIdx;
}

const char* RankLabelForLevel(int level) {
    const auto& opts = RankOptions();
    if (opts.empty()) return u8"—";
    int idx = RankIndexForLevel(level);
    idx = std::clamp(idx, 0, static_cast<int>(opts.size()) - 1);
    return opts[idx].label;
}

void ApplyUiTheme(int theme, ImGuiStyle& style) {
    switch (theme) {
        case 1: ImGui::StyleColorsLight(&style); break;
        case 2: ImGui::StyleColorsClassic(&style); break;
        default: ImGui::StyleColorsDark(&style); break;
    }
}

void ApplyUiSettings(const UiSettings& settings, ImGuiStyle& style, ImGuiIO& io) {
    io.FontGlobalScale = settings.fontScale;
    style.Alpha = settings.alpha;
    style.WindowRounding = settings.windowRounding;
    style.FrameRounding = settings.frameRounding;
    style.ScrollbarRounding = settings.scrollbarRounding;
    style.GrabRounding = settings.grabRounding;
    style.WindowPadding = settings.windowPadding;
    style.FramePadding = settings.framePadding;
    style.ItemSpacing = settings.itemSpacing;
    if (settings.customColors) {
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            style.Colors[entries[i].col] = settings.colors[i];
        }
    }
}

void ApplyWindowDecorations(GLFWwindow* window, bool decorated, bool& lastDecorated) {
    if (!window) return;
    if (decorated == lastDecorated) return;
    glfwSetWindowAttrib(window, GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    lastDecorated = decorated;
}

GLFWmonitor* FindMonitorForWindow(GLFWwindow* window) {
    if (!window) return glfwGetPrimaryMonitor();
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (!monitors || count == 0) return glfwGetPrimaryMonitor();
    const int cx = wx + ww / 2;
    const int cy = wy + wh / 2;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        if (cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
            return monitors[i];
        }
    }
    return monitors[0];
}

void ApplyWindowMode(GLFWwindow* window, GuiState& state) {
    if (!window) return;
    if (state.ui.windowFullscreen == state.windowFullscreenLast) return;
    if (state.ui.windowFullscreen) {
        glfwGetWindowPos(window, &state.windowedX, &state.windowedY);
        glfwGetWindowSize(window, &state.windowedW, &state.windowedH);
        GLFWmonitor* monitor = FindMonitorForWindow(window);
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor && mode) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    } else {
        int restoreW = state.windowedW > 0 ? state.windowedW : 1280;
        int restoreH = state.windowedH > 0 ? state.windowedH : 720;
        glfwSetWindowMonitor(window, nullptr, state.windowedX, state.windowedY, restoreW, restoreH, 0);
    }
    state.windowFullscreenLast = state.ui.windowFullscreen;
}

void HandleBorderlessDrag(GLFWwindow* window, const UiSettings& settings) {
    if (!window) return;
    if (settings.windowDecorated || settings.windowFullscreen) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyAlt) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;
    ImVec2 delta = io.MouseDelta;
    if (delta.x == 0.0f && delta.y == 0.0f) return;
    int wx = 0, wy = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwSetWindowPos(window, wx + static_cast<int>(delta.x), wy + static_cast<int>(delta.y));
}

void ResetUiSettings(UiSettings& settings, ImGuiStyle& style, ImGuiIO& io) {
    settings = UiSettings();
    ApplyUiTheme(settings.theme, style);
    ApplyUiSettings(settings, style, io);
}

void EnsureWindowVisible(float padding = 12.0f, ImVec2 minSize = ImVec2(240.0f, 160.0f)) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;
    if (viewport->WorkSize.x <= 0.0f || viewport->WorkSize.y <= 0.0f) return;
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (size.x < minSize.x || size.y < minSize.y) {
        ImVec2 target(std::max(size.x, minSize.x), std::max(size.y, minSize.y));
        ImGui::SetWindowSize(target, ImGuiCond_Always);
        size = target;
    }
    ImVec2 min = viewport->WorkPos;
    ImVec2 max(min.x + viewport->WorkSize.x, min.y + viewport->WorkSize.y);
    const bool offscreen =
        pos.x + size.x < min.x + padding ||
        pos.y + size.y < min.y + padding ||
        pos.x > max.x - padding ||
        pos.y > max.y - padding;
    if (!offscreen) return;
    if (size.x >= viewport->WorkSize.x) {
        pos.x = min.x;
    } else {
        pos.x = std::clamp(pos.x, min.x + padding, max.x - size.x - padding);
    }
    if (size.y >= viewport->WorkSize.y) {
        pos.y = min.y;
    } else {
        pos.y = std::clamp(pos.y, min.y + padding, max.y - size.y - padding);
    }
    ImGui::SetWindowPos(pos, ImGuiCond_Always);
}

void DrawWindowBackground(const UiSettings& settings, UiWindowId id, const std::filesystem::path& storageDir) {
    const auto idx = static_cast<size_t>(id);
    if (idx >= settings.backgrounds.size()) return;
    const std::string& relPath = settings.backgrounds[idx];
    if (relPath.empty()) return;
    auto resolved = ResolveIconPath(relPath, storageDir);
    if (!resolved) return;
    if (const auto* tex = GetIconTexture(*resolved)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, settings.backgroundAlpha));
        if (!settings.backgroundTiled) {
            drawList->AddImage((ImTextureID)(intptr_t)tex->id, pos, ImVec2(pos.x + size.x, pos.y + size.y),
                               ImVec2(0, 0), ImVec2(1, 1), tint);
            return;
        }
        const float scale = std::max(0.05f, settings.backgroundTileScale);
        float tileW = std::max(8.0f, static_cast<float>(tex->width) * scale);
        float tileH = std::max(8.0f, static_cast<float>(tex->height) * scale);
        ImVec2 end(pos.x + size.x, pos.y + size.y);
        for (float y = pos.y; y < end.y; y += tileH) {
            float y2 = std::min(y + tileH, end.y);
            float v1 = (y2 - y) / tileH;
            for (float x = pos.x; x < end.x; x += tileW) {
                float x2 = std::min(x + tileW, end.x);
                float u1 = (x2 - x) / tileW;
                drawList->AddImage((ImTextureID)(intptr_t)tex->id, ImVec2(x, y), ImVec2(x2, y2),
                                   ImVec2(0, 0), ImVec2(u1, v1), tint);
            }
        }
    }
}

Vec3 RotateX(const Vec3& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

Vec3 RotateY(const Vec3& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

ImVec2 ProjectPoint(const Vec3& v, float fov, float aspect, float distance, const ImVec2& center, float scale) {
    const float z = v.z + distance;
    if (z <= 0.001f) return center;
    const float f = 1.0f / std::tan(fov * 0.5f);
    const float x = (v.x * f / aspect) / z;
    const float y = (v.y * f) / z;
    return ImVec2(center.x + x * scale, center.y - y * scale);
}

void DrawMeshWireframe(const MeshData& mesh, const UiSettings& settings, ImDrawList* drawList,
                       const ImVec2& pos, const ImVec2& size) {
    if (!mesh.valid || mesh.triangles.empty()) return;
    Vec3 center{
        (mesh.min.x + mesh.max.x) * 0.5f,
        (mesh.min.y + mesh.max.y) * 0.5f,
        (mesh.min.z + mesh.max.z) * 0.5f
    };
    const float dx = mesh.max.x - mesh.min.x;
    const float dy = mesh.max.y - mesh.min.y;
    const float dz = mesh.max.z - mesh.min.z;
    const float maxDim = std::max({dx, dy, dz, 0.001f});
    const float scale = 0.45f * std::min(size.x, size.y) / maxDim;
    const float fov = 60.0f * 3.1415926535f / 180.0f;
    const float aspect = size.x > 0.0f ? size.x / size.y : 1.0f;
    const float distance = 2.5f / std::max(0.2f, settings.modelZoom);
    const ImVec2 screenCenter(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

    const ImU32 color = ImGui::GetColorU32(settings.modelColor);
    for (const auto& tri : mesh.triangles) {
        Vec3 a = tri.a;
        Vec3 b = tri.b;
        Vec3 c = tri.c;
        a.x -= center.x; a.y -= center.y; a.z -= center.z;
        b.x -= center.x; b.y -= center.y; b.z -= center.z;
        c.x -= center.x; c.y -= center.y; c.z -= center.z;
        a = RotateY(RotateX(a, settings.modelPitch), settings.modelYaw);
        b = RotateY(RotateX(b, settings.modelPitch), settings.modelYaw);
        c = RotateY(RotateX(c, settings.modelPitch), settings.modelYaw);
        ImVec2 pa = ProjectPoint(a, fov, aspect, distance, screenCenter, scale);
        ImVec2 pb = ProjectPoint(b, fov, aspect, distance, screenCenter, scale);
        ImVec2 pc = ProjectPoint(c, fov, aspect, distance, screenCenter, scale);
        drawList->AddLine(pa, pb, color, 1.0f);
        drawList->AddLine(pb, pc, color, 1.0f);
        drawList->AddLine(pc, pa, color, 1.0f);
    }
}

// Maintain a small rolling activity feed per profile.
void AppendLog(GuiState& state, const std::string& profileId, const std::string& message) {
    auto& log = state.activityLogs[profileId];
    if (log.size() == 3) {
        log.pop_front();
    }
    log.push_back(message);
}

// Generic clamp used all over the GUI logic.
int ClampToRange(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int NormalizeCategoryIndex(int index) {
    if (index < 0) return 0;
    if (index >= Profile::kCategoryCount) return Profile::kCategoryCount - 1;
    return index;
}

const char* ClassificationLabel(int categoryIndex) {
    return Profile::kCategoryLabels[NormalizeCategoryIndex(categoryIndex)];
}

// Utility functions below keep the Add Experience sliders in sync.
int SumPercentages(const std::vector<XpEntry>& entries) {
    int total = 0;
    for (const auto& entry : entries) total += entry.percent;
    return total;
}

// Reduce percentages on non-selected rows until the sum drops back to 100%.
void ReduceOthers(std::vector<XpEntry>& entries, int fixedIndex, int overflow) {
    if (overflow <= 0) return;
    const int size = static_cast<int>(entries.size());
    if (size == 0) return;
    std::vector<int> indices;
    indices.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (i == fixedIndex) continue;
        if (entries[i].percent > 0) indices.push_back(i);
    }
    if (indices.empty()) {
        entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
        return;
    }
    while (overflow > 0) {
        bool changed = false;
        int positiveCount = 0;
        for (int idx : indices) {
            if (entries[idx].percent > 0) ++positiveCount;
        }
        if (positiveCount == 0) {
            entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
            break;
        }
        int share = std::max(1, overflow / positiveCount);
        for (int idx : indices) {
            if (overflow <= 0) break;
            if (entries[idx].percent <= 0) continue;
            int delta = std::min(entries[idx].percent, share);
            if (delta <= 0) continue;
            entries[idx].percent -= delta;
            overflow -= delta;
            changed = true;
        }
        if (!changed) {
            for (int idx : indices) {
                if (overflow <= 0) break;
                if (entries[idx].percent <= 0) continue;
                entries[idx].percent -= 1;
                --overflow;
                changed = true;
            }
        }
        if (!changed) {
            entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
            break;
        }
    }
}

// Distribute remaining percent points across other rows to reach 100%.
void IncreaseOthers(std::vector<XpEntry>& entries, int fixedIndex, int deficit) {
    if (deficit <= 0) return;
    const int size = static_cast<int>(entries.size());
    if (size == 0) return;
    std::vector<int> indices;
    indices.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (i == fixedIndex) continue;
        indices.push_back(i);
    }
    if (indices.empty()) {
        entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent + deficit, 0, 100);
        return;
    }
    std::size_t cursor = 0;
    int safety = deficit * static_cast<int>(indices.size() + 1) + 100;
    auto allFull = [&]() {
        for (int idx : indices) {
            if (entries[idx].percent < 100) return false;
        }
        return true;
    };
    while (deficit > 0 && safety-- > 0) {
        if (allFull()) {
            entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent + deficit, 0, 100);
            break;
        }
        int idx = indices[cursor % indices.size()];
        cursor++;
        if (entries[idx].percent < 100) {
            entries[idx].percent += 1;
            --deficit;
        }
    }
}

// High-level balancer that ensures the sheet always totals 100%.
void BalancePercentages(std::vector<XpEntry>& entries, int fixedIndex) {
    if (entries.empty()) return;
    if (fixedIndex < 0 || fixedIndex >= static_cast<int>(entries.size())) {
        fixedIndex = 0;
    }
    int total = SumPercentages(entries);
    if (total > 100) {
        ReduceOthers(entries, fixedIndex, total - 100);
    } else if (total < 100) {
        IncreaseOthers(entries, fixedIndex, 100 - total);
    }
    int adjustedTotal = SumPercentages(entries);
    if (adjustedTotal != 100) {
        int diff = adjustedTotal - 100;
        entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent - diff, 0, 100);
        adjustedTotal = SumPercentages(entries);
        if (adjustedTotal != 100 && !entries.empty()) {
            int correction = 100 - adjustedTotal;
            entries[0].percent = ClampToRange(entries[0].percent + correction, 0, 100);
        }
    }
}

// Entry point invoked when the user drags a slider.
void AdjustSkillShare(GuiState& state, int index, int desiredPercent) {
    if (index < 0 || index >= static_cast<int>(state.xpEntries.size())) return;
    desiredPercent = ClampToRange(desiredPercent, 0, 100);
    state.xpEntries[index].percent = desiredPercent;
    BalancePercentages(state.xpEntries, index);
}

// Render a polar chart of skill levels inside the profile preview.
void DrawSkillRadarChart(const std::vector<Skill>& skills, const ImVec2& canvasPos, const ImVec2& canvasSize, ImDrawList* drawList) {
    if (skills.size() < 3) {
        drawList->AddText(canvasPos, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.5f, 1.0f)),
                          "Недостаточно данных для диаграммы (минимум 3 навыка).");
        return;
    }

    const float radius = 0.4f * std::min(canvasSize.x, canvasSize.y);
    if (radius <= 1.0f) {
        return;
    }

    const ImVec2 center = ImVec2(canvasPos.x + canvasSize.x * 0.5f,
                                 canvasPos.y + canvasSize.y * 0.5f);

    std::vector<float> values;
    values.reserve(skills.size());
    float maxValue = 0.0f;
    for (const auto& skill : skills) {
        float fractionalLevel = static_cast<float>(skill.level);
        if (skill.xpToNext > 0) {
            fractionalLevel += static_cast<float>(skill.xp) / static_cast<float>(skill.xpToNext);
        }
        values.push_back(fractionalLevel);
        if (fractionalLevel > maxValue) maxValue = fractionalLevel;
    }
    if (maxValue <= 0.0f) {
        maxValue = 1.0f;
    }

    std::vector<ImVec2> polygon(values.size());
    std::vector<ImVec2> temp(values.size());

    constexpr float kPi = 3.14159265358979323846f;
    const float angleStep = 2.0f * kPi / static_cast<float>(values.size());
    const float startAngle = -kPi * 0.5f;

    const ImU32 gridColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
    const ImU32 axisColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
    const ImU32 fillColor = ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 0.9f, 0.25f));
    const ImU32 outlineColor = ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 0.9f, 0.85f));
    const ImU32 pointColor = ImGui::GetColorU32(ImVec4(0.95f, 0.98f, 1.0f, 1.0f));
    const ImU32 labelColor = ImGui::GetColorU32(ImVec4(0.8f, 0.9f, 1.0f, 1.0f));

    const int ringCount = 4;
    for (int ring = 1; ring <= ringCount; ++ring) {
        float ringRatio = static_cast<float>(ring) / ringCount;
        float ringRadius = radius * ringRatio;
        for (size_t i = 0; i < values.size(); ++i) {
            float angle = startAngle + angleStep * static_cast<float>(i);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            temp[i] = ImVec2(center.x + cosA * ringRadius,
                             center.y + sinA * ringRadius);
        }
        drawList->AddPolyline(temp.data(), static_cast<int>(temp.size()), gridColor, true, 1.0f);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        float angle = startAngle + angleStep * static_cast<float>(i);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        ImVec2 axisEnd(center.x + cosA * radius, center.y + sinA * radius);
        drawList->AddLine(center, axisEnd, axisColor, 1.0f);

        ImVec2 labelPos(center.x + cosA * (radius + 14.0f),
                        center.y + sinA * (radius + 14.0f));
        const std::string& name = skills[i].name;
        ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
        drawList->AddText(ImVec2(labelPos.x - textSize.x * 0.5f,
                                 labelPos.y - textSize.y * 0.5f),
                          labelColor, name.c_str());
    }

    for (size_t i = 0; i < values.size(); ++i) {
        float angle = startAngle + angleStep * static_cast<float>(i);
        float ratio = values[i] / maxValue;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        polygon[i] = ImVec2(center.x + cosA * radius * ratio,
                            center.y + sinA * radius * ratio);
    }

    drawList->AddConvexPolyFilled(polygon.data(), static_cast<int>(polygon.size()), fillColor);
    drawList->AddPolyline(polygon.data(), static_cast<int>(polygon.size()), outlineColor, true, 2.0f);
    for (const ImVec2& p : polygon) {
        drawList->AddCircleFilled(p, 3.0f, pointColor);
    }

    std::ostringstream legend;
    legend << "Макс. уровень: " << std::fixed << std::setprecision(1) << maxValue;
    drawList->AddText(ImVec2(canvasPos.x + 5.0f, canvasPos.y + 5.0f),
                      ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.7f, 1.0f)),
                      legend.str().c_str());
}
const std::vector<PipelineStep> kPipelineSteps = {
    {"0. Реф-борд",
R"(Сборка реф-листа, наряду с блокингом - важнейшие этапы!
При надобности, создается отдельный! муд-борд!)"},
    {"1. Блокинг",
R"(Блокинг — это процесс создания базовых пропорций и соотношений между моделями и сценами.

На этом этапе особенно важно правильно отобразить пропорции отдельных деталей и модели в целом. В отличие от других этапов, здесь не так важно уделять внимание сетке, как точности и скорости.

Блокинг позволяет увидеть общую форму и силуэт будущей модели, что дает возможность внести необходимые коррективы, если это необходимо.

!!!Важно не вдаваться в детали на данном этапе!!!)"},
    {"2. Создание Low-Poly",
R"(На этом этапе каждый элемент прорабатывается отдельно. Формы уточняются, а топология модели приводится в порядок.

Всё, что требует симметрии, выполняется с помощью модификатора Mirror и включённого параметра Clipping. Для удобства можно добавить объект-пустышку в нулевой координате, чтобы настроить зеркальное отражение на него.

ДЕЛАЕМ CNTRL+A -> SCALE)"},
    {"3. Шейдинг",
R"(На этом этапе мы расставляем шарпэджи, проверяем шейдинг модели и устанавливаем параметры сглаживания.)"},
    {"4. High-poly",
R"(В нашей сцене мы присваиваем всем объектам суффикс "_low" и создаем их дубликат. В копиях меняем суффикс на "_high", используя встроенный инструмент Blender – Batch Rename.

На одном из объектов мы настраиваем модификаторы Bevel и Subdivision Surface:

* Bevel: – Amount: 0.001 – Segments: 2 – Limit Method: Weight – Profile->Shape: 1
* Subdivision Surface: – Levels: 3

Затем, с помощью аддона Copy Attribute (который можно установить через Preferences), мы копируем эти модификаторы на все объекты High-poly.

После этого мы проходим по каждому объекту и устанавливаем значение Bevel Weight (вкладка Item, меню "N") на 1, применяя этот параметр ко всем шарпэджам.

Если требуется, мы модифицируем сетку, чтобы модель соответствовала Low-poly версии.)"},
    {"5. UV-развертка",
R"(Производится только на Low-poly.

Проходим по всем объектам, выравнивая швы на шарпэдах. При необходимости добавляем дополнительные швы.

Затем разворачиваем объект и проверяем цвет UV-Stretch. Он должен быть максимально холодным.

Проверяем, нет ли искажений или деформаций на островках. При необходимости выравниваем их и, если нужно, поворачиваем в нужные нам координаты.

Когда развертка нас устраивает, применяем Mirror, создавая Overlaps.

После завершения работы со всеми объектами, мы приступаем к общей упаковке:

1. Нажимаем CNTRL+A и выбираем SCALE.
2. Выделяем все объекты, переключаемся в Edit Mode и в меню UV выбираем Average Islands Scale.
3. Упаковываем, отключив поворот островков.

margin – 0.004

Texel Density (TD) – 2px/cm при разрешении 4k (средний параметр, для каждого проекта рассчитывается отдельно)

Если необходимо достичь заданной плотности Texels, делим UV на несколько частей.)"},
    {"6. Запекание и текстурирование",
R"(Более подробно я расскажу об этом позже, а сейчас поделюсь основными шагами:

1. Проводим запекание по всем объектам, используя минимальную длину луча. Проверяем, чтобы области High и Low не пересекались.
2. Выполняем пробные запекания, анализируем результаты и завершаем финальное запекание на уровне 8k.
3. Приступаем к текстурированию. В конце добавляем постобработку в виде Ambient Occlusion и, при необходимости, дополнительные затенения к текстурам.)"},
    {"7. Экспорт в движок",
R"(❗️❗️При экспорте из Blender, в настройках эспорта во вкладке Geometry, Smoothing меняем на Face.

Экспорт модели и текстур в движок: Модель и текстуры распределяются по соответствующим папкам (Mesh, Material, Texture). Я отдельно опубликую информацию о том, как именовать текстуры и меш.

Настройка шейдеров (материалов) и текстур, анимаций: На этом этапе мы настраиваем шейдеры и текстуры, а затем проверяем результат.

❗️❗️Соблюдаем нейминг папок, файлов, иерархию.

Упаковка, создание PreFab и отправка разработчикам: все необходимые файлы упаковываются и отправляются разработчикам для дальнейшей работы.

✅Каждый этап отправляется Арт-лиду на одобрение!)"}
};

// Load the currently selected profile from storage and synchronise its skills.
void RefreshActiveProfile(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    state.active.reset();
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.profiles.size())) return;
    const auto& info = state.profiles[state.selectedIndex];
    if (!storage.set_active_profile(info.id)) return;
    if (auto loaded = storage.load_profile()) {
        SyncProfileWithCatalog(*loaded, catalog);
        storage.save_profile(*loaded);
        state.active = GuiProfile{*loaded, info.id};
        state.activityLogs[state.active->id];
        PrepareXpEntries(state, catalog);
    }
}

void ReapplyRulesToProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    auto list = storage.list_profiles();
    std::string focusId = state.active ? state.active->id : std::string{};
    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            // Preserve current level/progress but re-evaluate XP with the new rules.
            int level = profile->overall_level();
            int progress = profile->level_progress();
            profile->set_level_and_progress(level, progress);
            storage.save_profile(*profile);
        }
    }
    if (!focusId.empty()) {
        storage.set_active_profile(focusId);
    }
}

void SyncRankSelection(GuiState& state) {
    if (!state.active) {
        state.selectedRankIndex = 0;
        return;
    }
    const auto& opts = RankOptions();
    int bestIdx = 0;
    for (int idx = 0; idx < static_cast<int>(opts.size()); ++idx) {
        if (state.active->profile.overall_level() >= opts[idx].level) {
            bestIdx = idx;
        }
    }
    state.selectedRankIndex = bestIdx;
}

bool SelectProfileById(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& id) {
    if (id.empty()) return false;
    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
        if (state.profiles[i].id == id) {
            state.selectedIndex = i;
            RefreshActiveProfile(state, storage, catalog);
            SyncRankSelection(state);
            return true;
        }
    }
    RefreshProfiles(state, storage, catalog, id);
    SyncRankSelection(state);
    return state.active.has_value();
}

// Reload profile list and try to keep the previously selected/active entry.
void RefreshProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& preferredId) {
    std::string currentId;
    if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
        currentId = state.profiles[state.selectedIndex].id;
    }

    state.profiles = LoadProfiles(storage);
    state.adminStatsDirty = true;
    if (state.profiles.empty()) {
        state.selectedIndex = -1;
        state.active.reset();
        return;
    }

    std::string targetId = preferredId.empty() ? currentId : preferredId;
    if (!targetId.empty()) {
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            if (state.profiles[i].id == targetId) {
                state.selectedIndex = i;
                RefreshActiveProfile(state, storage, catalog);
                return;
            }
        }
    }

    state.selectedIndex = 0;
    RefreshActiveProfile(state, storage, catalog);
}

void ShowStatus(const GuiState& state) {
    if (state.statusMessage.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(state.statusColor[0], state.statusColor[1], state.statusColor[2], state.statusColor[3]));
    ImGui::TextWrapped("%s", state.statusMessage.c_str());
    ImGui::PopStyleColor();
}

void DrawKpiCard(const char* id, const char* title, const std::string& value, const ImVec4& accent, float height = 56.0f) {
    ImGui::PushID(id);
    ImGui::BeginChild("kpi_card", ImVec2(0.0f, height), true);
    ImGui::TextDisabled("%s", title);
    ImGui::TextColored(accent, "%s", value.c_str());
    ImGui::EndChild();
    ImGui::PopID();
}

void DrawPipelinePanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Pipeline, state.storageDir);
    const int stepCount = static_cast<int>(kPipelineSteps.size());
    if (stepCount == 0) {
        ImGui::TextUnformatted(u8"Пайплайн пуст.");
        return;
    }
    if (state.selectedPipelineIndex < 0 || state.selectedPipelineIndex >= stepCount) {
        state.selectedPipelineIndex = 0;
    }
    if (ImGui::BeginChild("pipeline_list", ImVec2(0, 140), true)) {
        for (int i = 0; i < stepCount; ++i) {
            bool selected = state.selectedPipelineIndex == i;
            if (ImGui::Selectable(kPipelineSteps[i].title, selected)) {
                state.selectedPipelineIndex = i;
            }
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    const PipelineStep& step = kPipelineSteps[state.selectedPipelineIndex];
    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), "%s", step.title);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 45.0f);
    ImGui::TextUnformatted(step.description);
    ImGui::PopTextWrapPos();
}

void DrawRulesPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Rules, state.storageDir);
    GameplayConfig& draft = state.rulesDraft;
    ImGui::TextUnformatted(u8"Кривая уровней");
    ImGui::InputInt(u8"Базовый XP (уровень 1)", &draft.levelBaseXp);
    ImGui::InputInt(u8"Линейный прирост за уровень", &draft.levelLinearXp);
    ImGui::InputInt(u8"Квадратичный прирост за уровень", &draft.levelQuadraticXp);
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Базовый XP категорий");
    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
        std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[idx] + " XP";
        int value = draft.categoryBaseXp[idx];
        if (ImGui::InputInt(label.c_str(), &value)) {
            draft.categoryBaseXp[idx] = value;
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Бонусы и штрафы");
    ImGui::InputFloat(u8"Базовый фокус-бонус", &draft.focusBaseBonus, 0.05f, 0.5f, "%.2f");
    ImGui::InputFloat(u8"Доп. фокус-бонус", &draft.focusAdditionalBonus, 0.05f, 0.5f, "%.2f");
    ImGui::SliderFloat(u8"Коэффициент награды при повторе", &draft.repeatRewardFactor, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(u8"Коэффициент награды при прогреве", &draft.recoveryRewardFactor, 0.0f, 1.0f, "%.2f");
    ImGui::InputInt(u8"Задач прогрева", &draft.recoveryWarmupTasks);
    ImGui::TextDisabled(u8"Изменения применяются в CLI и GUI после сохранения.");
    if (ImGui::Button(u8"Сбросить")) {
        draft = state.rulesConfig;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сохранить и применить")) {
        SaveRulesResult result = SaveGameplayRulesAction(draft, state.storageDir);
        if (result.ok) {
            state.rulesConfig = result.config;
            state.rulesDraft = result.config;
            SetGameplayConfig(result.config);
            ReapplyRulesToProfiles(state, storage, catalog);
            std::string keepId = state.active ? state.active->id : std::string{};
            RefreshProfiles(state, storage, catalog, keepId);
            SetStatus(state, result.message, 0.45f, 0.9f, 0.45f, 1.0f,
                      AppLogLevel::Info, u8"Правила");
        } else {
            SetStatus(state, result.message, 1.0f, 0.45f, 0.45f, 1.0f,
                      AppLogLevel::Error, u8"Правила");
        }
    }
}

void DrawUiSettingsPanel(GuiState& state, ImGuiStyle& style) {
    DrawWindowBackground(state.ui, UiWindowId::UiSettings, state.storageDir);
    ImGui::TextUnformatted(u8"Тема и стиль");
    const char* themes[] = {u8"Тёмная", u8"Светлая", u8"Классика"};
    if (ImGui::Combo(u8"Тема", &state.ui.theme, themes, IM_ARRAYSIZE(themes))) {
        state.uiDirty = true;
    }
    ImGui::TextUnformatted(u8"Пресеты");
    if (ImGui::Button(u8"Презентация")) {
        state.ui.fontScale = 1.25f;
        state.ui.alpha = 1.0f;
        state.ui.windowRounding = 8.0f;
        state.ui.frameRounding = 6.0f;
        state.ui.scrollbarRounding = 8.0f;
        state.ui.grabRounding = 6.0f;
        state.ui.windowPadding = ImVec2(12.0f, 12.0f);
        state.ui.framePadding = ImVec2(8.0f, 6.0f);
        state.ui.itemSpacing = ImVec2(10.0f, 8.0f);
        state.uiDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Компактный")) {
        state.ui.fontScale = 0.9f;
        state.ui.alpha = 0.95f;
        state.ui.windowRounding = 4.0f;
        state.ui.frameRounding = 3.0f;
        state.ui.scrollbarRounding = 4.0f;
        state.ui.grabRounding = 3.0f;
        state.ui.windowPadding = ImVec2(6.0f, 6.0f);
        state.ui.framePadding = ImVec2(5.0f, 3.0f);
        state.ui.itemSpacing = ImVec2(6.0f, 4.0f);
        state.uiDirty = true;
    }
    ImGui::SliderFloat(u8"Масштаб шрифта", &state.ui.fontScale, 0.8f, 1.6f, "%.2f");
    ImGui::SliderFloat(u8"Прозрачность", &state.ui.alpha, 0.6f, 1.0f, "%.2f");
    ImGui::SliderFloat(u8"Скругление окон", &state.ui.windowRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление элементов", &state.ui.frameRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление скролла", &state.ui.scrollbarRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление захвата", &state.ui.grabRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Прозрачность фона", &state.ui.backgroundAlpha, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Окно");
    bool borderless = !state.ui.windowDecorated;
    if (ImGui::Checkbox(u8"Без рамки окна", &borderless)) {
        state.ui.windowDecorated = !borderless;
        state.uiDirty = true;
    }
    if (ImGui::Checkbox(u8"Полноэкранный режим (F11)", &state.ui.windowFullscreen)) {
        state.uiDirty = true;
    }
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Отступы");
    ImGui::SliderFloat2(u8"Отступ окна", &state.ui.windowPadding.x, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat2(u8"Отступ элемента", &state.ui.framePadding.x, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat2(u8"Интервал", &state.ui.itemSpacing.x, 0.0f, 20.0f, "%.1f");

    ImGui::Separator();
    if (ImGui::Checkbox(u8"Кастомные цвета", &state.ui.customColors)) {
        if (state.ui.customColors) {
            const auto& entries = UiColorEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                state.ui.colors[i] = style.Colors[entries[i].col];
            }
        }
    }
    if (state.ui.customColors) {
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            ImGui::ColorEdit4(entries[i].name, &state.ui.colors[i].x);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"Фоны окон");
    ImGui::Checkbox(u8"Замостить фон", &state.ui.backgroundTiled);
    ImGui::SliderFloat(u8"Масштаб плитки", &state.ui.backgroundTileScale, 0.25f, 3.0f, "%.2f");
    const auto backgrounds = LoadUiBackgroundChoices(state.storageDir);
    const auto& windows = UiWindows();
    for (size_t i = 0; i < windows.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        std::string current = state.ui.backgrounds[i];
        std::string label = current.empty() ? u8"(нет)" : std::filesystem::path(current).filename().string();
        if (ImGui::BeginCombo(windows[i].label, label.c_str())) {
            bool noneSelected = current.empty();
            if (ImGui::Selectable(u8"(нет)", noneSelected)) {
                state.ui.backgrounds[i].clear();
                state.uiDirty = true;
            }
            for (const auto& bg : backgrounds) {
                bool selected = bg.relativePath == current;
                ImGui::PushID(bg.label.c_str());
                if (const auto* tex = GetIconTexture(bg.absolutePath)) {
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, ImVec2(32.0f, 32.0f));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(bg.label.c_str(), selected)) {
                    state.ui.backgrounds[i] = bg.relativePath;
                    state.uiDirty = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button(u8"Сохранить настройки")) {
        SaveUiSettings(state.storageDir, state.ui);
        state.uiDirty = false;
        AppendAppLog(state, AppLogLevel::Info, u8"Интерфейс", u8"Настройки интерфейса сохранены.");
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сбросить к теме")) {
        ApplyUiTheme(state.ui.theme, style);
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            state.ui.colors[i] = style.Colors[entries[i].col];
        }
        state.ui.customColors = false;
        state.uiDirty = true;
    }
}

void DrawLogsPanel(GuiState& state) {
    ImGui::TextUnformatted(u8"Фильтр");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint(u8"##log_filter", u8"Поиск по тексту/источнику",
                             state.logFilter.data(), state.logFilter.size());
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"Сброс##log_filter")) {
        state.logFilter.fill('\0');
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Экспорт")) {
        int filteredCount = CountFilteredLogs(state);
        if (filteredCount == 0) {
            SetStatus(state, u8"Нет записей для экспорта.", 1.0f, 0.75f, 0.35f, 1.0f,
                      AppLogLevel::Warning, u8"Логи");
        } else {
            std::filesystem::path outPath;
            int exportedCount = 0;
            if (ExportAppLogs(state, outPath, exportedCount)) {
                std::string msg = u8"Экспортировано записей: ";
                msg += std::to_string(exportedCount);
                msg += u8". Файл: ";
                msg += outPath.string();
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Логи");
            } else {
                SetStatus(state, u8"Не удалось сохранить лог-файл.", 1.0f, 0.45f, 0.45f, 1.0f,
                          AppLogLevel::Error, u8"Логи");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Очистить")) {
        state.appLogs.clear();
    }

    ImGui::Checkbox(u8"Инфо", &state.logShowInfo);
    ImGui::SameLine();
    ImGui::Checkbox(u8"Предупреждения", &state.logShowWarning);
    ImGui::SameLine();
    ImGui::Checkbox(u8"Ошибки", &state.logShowError);
    ImGui::SameLine();
    ImGui::Checkbox(u8"Автопрокрутка", &state.logAutoScroll);
    ImGui::SameLine();
    ImGui::Checkbox(u8"Компактно", &state.logCompactView);
    int filteredCount = CountFilteredLogs(state);
    ImGui::TextDisabled(u8"Показано: %d из %d", filteredCount, static_cast<int>(state.appLogs.size()));

    ImVec2 tableSize(0.0f, 260.0f);
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;
    if (state.logCompactView) {
        if (ImGui::BeginTable("app_logs_compact", 2, flags, tableSize)) {
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(u8"Сообщение", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& entry : state.appLogs) {
                if (!MatchesLogFilters(state, entry)) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImVec4 levelColor = LogLevelColor(entry.level);
                ImGui::TextColored(levelColor, "%s", LogLevelLabel(entry.level));
                ImGui::TableSetColumnIndex(1);
                std::string ts = FormatTimestamp(entry.timestamp, "%H:%M:%S");
                std::string line;
                if (!ts.empty()) {
                    line += ts;
                    line += " ";
                }
                if (!entry.source.empty()) {
                    line += entry.source;
                    line += ": ";
                }
                line += entry.message;
                ImGui::TextWrapped("%s", line.c_str());
            }
            if (state.logAutoScroll) {
                float maxScroll = ImGui::GetScrollMaxY();
                if (ImGui::GetScrollY() >= maxScroll - 1.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndTable();
        }
    } else {
        if (ImGui::BeginTable("app_logs", 4, flags, tableSize)) {
            ImGui::TableSetupColumn(u8"Время", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(u8"Источник", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn(u8"Сообщение", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& entry : state.appLogs) {
                if (!MatchesLogFilters(state, entry)) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string ts = FormatTimestamp(entry.timestamp, "%Y-%m-%d %H:%M:%S");
                ImGui::TextUnformatted(ts.empty() ? "-" : ts.c_str());
                ImGui::TableSetColumnIndex(1);
                ImVec4 levelColor = LogLevelColor(entry.level);
                ImGui::TextColored(levelColor, "%s", LogLevelLabel(entry.level));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(entry.source.empty() ? "-" : entry.source.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", entry.message.c_str());
            }
            if (state.logAutoScroll) {
                float maxScroll = ImGui::GetScrollMaxY();
                if (ImGui::GetScrollY() >= maxScroll - 1.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndTable();
        }
    }
}

void DrawAdminStatsPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::AdminStats, state.storageDir);
    const std::int64_t nowSec = NowSeconds();
    bool refresh = false;
    if (ImGui::Button(u8"Обновить")) {
        refresh = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox(u8"Авто", &state.adminStatsAutoRefresh);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderInt(u8"сек##admin_refresh", &state.adminStatsRefreshSeconds, 5, 120);
    ImGui::SameLine();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint(u8"##admin_stats_filter", u8"Фильтр по ID/имени",
                             state.adminStatsFilter.data(), state.adminStatsFilter.size());
    ImGui::SameLine();
    ImGui::Checkbox(u8"Архив", &state.adminStatsIncludeArchived);
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"Сброс##admin_stats_filter")) {
        state.adminStatsFilter.fill('\0');
        state.adminStatsIncludeArchived = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Экспорт CSV")) {
        std::filesystem::path outPath;
        if (ExportAdminStatsCsv(state, outPath)) {
            std::string msg = u8"Экспортировано: ";
            msg += outPath.string();
            SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Статистика");
        } else {
            SetStatus(state, u8"Нет данных для экспорта.", 1.0f, 0.75f, 0.35f, 1.0f,
                      AppLogLevel::Warning, u8"Статистика");
        }
    }
    std::string updated = FormatTimestamp(state.adminStats.lastUpdated, "%Y-%m-%d %H:%M:%S");
    ImGui::TextDisabled(u8"Обновлено: %s", updated.empty() ? u8"—" : updated.c_str());

    const int refreshSeconds = std::clamp(state.adminStatsRefreshSeconds, 5, 120);
    if (refresh || state.adminStatsDirty || state.adminStats.lastUpdated == 0 ||
        (state.adminStatsAutoRefresh && nowSec - state.adminStats.lastUpdated > refreshSeconds)) {
        RefreshAdminStats(state, storage);
    }

    const auto& stats = state.adminStats;
    const auto filtered = FilterAdminProfiles(state);
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        static_cast<int>(filtered.size()),
                        static_cast<int>(stats.profiles.size()));
    if (filtered.empty()) {
        ImGui::TextDisabled(u8"Нет данных для отображения.");
        return;
    }

    ImGui::TextUnformatted(u8"Сводка");
            if (ImGui::BeginTable("admin_kpi", 3, ImGuiTableFlags_SizingStretchProp)) {
                auto kpi = [&](const char* id, const char* title, const std::string& value, const ImVec4& accent) {
                    ImGui::TableNextColumn();
                    DrawKpiCard(id, title, value, accent);
                };
                kpi("kpi_total_profiles", u8"Профилей всего", std::to_string(stats.totalProfiles), ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
                kpi("kpi_active_profiles", u8"Активных", std::to_string(stats.activeProfiles), ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                kpi("kpi_archived_profiles", u8"В архиве", std::to_string(stats.archivedProfiles), ImVec4(0.9f, 0.7f, 0.4f, 1.0f));

                std::ostringstream avgStream;
                avgStream.imbue(std::locale::classic());
                avgStream << std::fixed << std::setprecision(1) << stats.avgLevel;
                kpi("kpi_avg_level", u8"Средний уровень", avgStream.str(), ImVec4(0.85f, 0.8f, 1.0f, 1.0f));
                kpi("kpi_max_level", u8"Макс. уровень", std::to_string(stats.maxLevel), ImVec4(0.95f, 0.8f, 0.3f, 1.0f));
                kpi("kpi_total_xp", u8"Всего XP", std::to_string(stats.totalXp), ImVec4(0.75f, 0.85f, 1.0f, 1.0f));

                std::ostringstream avgXpStream;
                avgXpStream.imbue(std::locale::classic());
                avgXpStream << std::fixed << std::setprecision(0) << stats.avgXp;
                kpi("kpi_avg_xp", u8"Средний XP", avgXpStream.str(), ImVec4(0.7f, 0.85f, 0.95f, 1.0f));
                kpi("kpi_ach_total", u8"Ачивок всего", std::to_string(stats.achievementsTotal), ImVec4(0.9f, 0.75f, 0.55f, 1.0f));
                kpi("kpi_ach_active", u8"Ачивок активных", std::to_string(stats.achievementsActive), ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                kpi("kpi_recovery", u8"Профилей на прогреве", std::to_string(stats.profilesWithRecovery),
                    ImVec4(0.95f, 0.65f, 0.45f, 1.0f));
                kpi("kpi_no_activity", u8"Без активности", std::to_string(stats.profilesNoActivity),
                    ImVec4(0.9f, 0.6f, 0.6f, 1.0f));
                kpi("kpi_no_ach", u8"Без ачивок", std::to_string(stats.profilesNoAchievements),
                    ImVec4(0.8f, 0.7f, 0.9f, 1.0f));
                ImGui::EndTable();
            }

    ImGui::Separator();
    auto drawTopTable = [&](const char* id, const char* title, const std::vector<AdminStatsProfile>& items) {
        ImGui::TextUnformatted(title);
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable(id, 4, flags, ImVec2(0.0f, 180.0f))) {
            ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"XP", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (const auto& item : items) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string rowId = std::string("##row_") + item.id;
                if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    SelectProfileById(state, storage, catalog, item.id);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(item.id.c_str());
                ImGui::TableSetColumnIndex(1);
                std::string name = item.name + (item.archived ? u8" (архив)" : "");
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", item.level);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", item.totalXp);
            }
            ImGui::EndTable();
        }
    };

    std::vector<AdminStatsProfile> byLevel = filtered;
    std::sort(byLevel.begin(), byLevel.end(), [](const auto& a, const auto& b) {
        if (a.level != b.level) return a.level > b.level;
        return a.totalXp > b.totalXp;
    });
    if (byLevel.size() > 6) byLevel.resize(6);

    std::vector<AdminStatsProfile> byXp = filtered;
    std::sort(byXp.begin(), byXp.end(), [](const auto& a, const auto& b) {
        if (a.totalXp != b.totalXp) return a.totalXp > b.totalXp;
        return a.level > b.level;
    });
    if (byXp.size() > 6) byXp.resize(6);

    drawTopTable("top_level", u8"Топ по уровню", byLevel);
    drawTopTable("top_xp", u8"Топ по XP", byXp);

    std::vector<AdminStatsProfile> byAchievements = filtered;
    std::sort(byAchievements.begin(), byAchievements.end(), [](const auto& a, const auto& b) {
        if (a.achievementsActive != b.achievementsActive) return a.achievementsActive > b.achievementsActive;
        if (a.achievementsTotal != b.achievementsTotal) return a.achievementsTotal > b.achievementsTotal;
        return a.level > b.level;
    });
    if (byAchievements.size() > 6) byAchievements.resize(6);
    ImGui::TextUnformatted(u8"Топ по ачивкам");
    if (ImGui::BeginTable("top_achievements", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                             ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 180.0f))) {
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Активные", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(u8"Всего", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (const auto& item : byAchievements) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowId = std::string("##ach_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", item.achievementsActive);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", item.achievementsTotal);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"Средние категории");
    if (ImGui::BeginTable("avg_categories", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"Категория");
        ImGui::TableSetupColumn(u8"Средняя оценка", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < Profile::kCategoryCount; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[i];
            ImGui::TextUnformatted(label.c_str());
            ImGui::TableSetColumnIndex(1);
            if (stats.categoryCounts[i] > 0) {
                double avg = static_cast<double>(stats.categoryTotals[i]) /
                    static_cast<double>(stats.categoryCounts[i]);
                ImGui::Text("%.2f", avg);
            } else {
                ImGui::TextUnformatted("-");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"Распределение по рангам");
    const auto& opts = RankOptions();
    std::vector<int> rankCounts(opts.size(), 0);
    for (const auto& item : filtered) {
        int idx = RankIndexForLevel(item.level);
        if (idx >= 0 && idx < static_cast<int>(rankCounts.size())) {
            rankCounts[idx] += 1;
        }
    }
    if (ImGui::BeginTable("rank_dist", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"Ранг");
        ImGui::TableSetupColumn(u8"Профилей", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < opts.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(opts[i].label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", rankCounts[i]);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"Неактивные профили");
    int thresholdDays = std::clamp(state.inactivityThresholdDays, 1, 365);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt(u8"Порог (дней)", &thresholdDays, 1, 365);
    state.inactivityThresholdDays = thresholdDays;
    const std::int64_t thresholdSec = static_cast<std::int64_t>(thresholdDays) * 24 * 3600;
    std::vector<AdminStatsProfile> byInactivity;
    byInactivity.reserve(filtered.size());
    for (const auto& item : filtered) {
        if (item.lastTaskTimestamp == 0) {
            byInactivity.push_back(item);
        } else if ((nowSec - item.lastTaskTimestamp) >= thresholdSec) {
            byInactivity.push_back(item);
        }
    }
    std::sort(byInactivity.begin(), byInactivity.end(), [](const auto& a, const auto& b) {
        const std::int64_t ta = a.lastTaskTimestamp;
        const std::int64_t tb = b.lastTaskTimestamp;
        if (ta == tb) return a.totalXp > b.totalXp;
        if (ta == 0) return true;
        if (tb == 0) return false;
        return ta < tb;
    });
    if (byInactivity.size() > 6) byInactivity.resize(6);
    if (ImGui::BeginTable("inactive_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                               ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Последняя активность", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn(u8"Прошло", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Прогрев", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        if (byInactivity.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(u8"Нет профилей старше порога.");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted("-");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted("-");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted("-");
        }
        for (const auto& item : byInactivity) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowId = std::string("##inactive_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string lastText = item.lastTaskTimestamp > 0
                ? FormatTimestamp(item.lastTaskTimestamp, "%Y-%m-%d %H:%M")
                : std::string(u8"нет данных");
            ImGui::TextUnformatted(lastText.c_str());
            ImGui::TableSetColumnIndex(3);
            if (item.lastTaskTimestamp > 0) {
                ImGui::TextUnformatted(FormatDurationShort(nowSec - item.lastTaskTimestamp).c_str());
            } else {
                ImGui::TextUnformatted(u8"—");
            }
            ImGui::TableSetColumnIndex(4);
            if (item.recoveryTasksRemaining > 0) {
                ImGui::Text("%d", item.recoveryTasksRemaining);
            } else {
                ImGui::TextUnformatted("-");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"Профили на прогреве");
    std::vector<AdminStatsProfile> byRecovery;
    byRecovery.reserve(filtered.size());
    for (const auto& item : filtered) {
        if (item.recoveryTasksRemaining > 0) {
            byRecovery.push_back(item);
        }
    }
    std::sort(byRecovery.begin(), byRecovery.end(), [](const auto& a, const auto& b) {
        if (a.recoveryTasksRemaining != b.recoveryTasksRemaining) {
            return a.recoveryTasksRemaining > b.recoveryTasksRemaining;
        }
        return a.totalXp > b.totalXp;
    });
    if (byRecovery.size() > 6) byRecovery.resize(6);
    if (ImGui::BeginTable("recovery_table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Осталось", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        if (byRecovery.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(u8"Нет активных профилей.");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted("-");
        }
        for (const auto& item : byRecovery) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowId = std::string("##recovery_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", item.recoveryTasksRemaining);
        }
        ImGui::EndTable();
    }
}

void DrawView3dSettingsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::View3DSettings, state.storageDir);
    const auto models = LoadModelChoices(state.storageDir);
    std::string modelLabel = state.ui.modelPath.empty() ? u8"(не выбран)" : std::filesystem::path(state.ui.modelPath).filename().string();
    if (ImGui::BeginCombo(u8"Модель", modelLabel.c_str())) {
        bool noneSelected = state.ui.modelPath.empty();
        if (ImGui::Selectable(u8"(не выбрана)", noneSelected)) {
            state.ui.modelPath.clear();
            std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", "");
            state.viewMeshPath.clear();
        }
        for (const auto& model : models) {
            bool selected = model.relativePath == state.ui.modelPath;
            if (ImGui::Selectable(model.label.c_str(), selected)) {
                state.ui.modelPath = model.relativePath;
                std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
                state.viewMeshPath.clear();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::InputText(u8"Путь к модели", state.modelPathBuffer.data(), state.modelPathBuffer.size())) {
        state.ui.modelPath = state.modelPathBuffer.data();
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Загрузить")) {
        state.viewMeshPath.clear();
    }
    ImGui::Checkbox(u8"Авто?вращение", &state.ui.modelAutoRotate);
    ImGui::SliderFloat(u8"Скорость вращения", &state.ui.modelAutoSpeed, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat(u8"Yaw", &state.ui.modelYaw, -3.14f, 3.14f, "%.2f");
    ImGui::SliderFloat(u8"Pitch", &state.ui.modelPitch, -1.57f, 1.57f, "%.2f");
    ImGui::SliderFloat(u8"Zoom", &state.ui.modelZoom, 0.3f, 3.0f, "%.2f");
    ImGui::ColorEdit4(u8"Цвет линий", &state.ui.modelColor.x);
}

void DrawView3dPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::View3D, state.storageDir);
    auto resolved = state.ui.modelPath.empty()
        ? std::optional<std::filesystem::path>()
        : ResolveIconPath(state.ui.modelPath, state.storageDir);
    if (!resolved) {
        if (state.viewMeshPath != "<cube>") {
            state.viewMesh = MakeCubeMesh();
            state.viewMeshPath = "<cube>";
            state.viewMeshError.clear();
        }
    } else if (state.viewMeshPath != resolved->string()) {
        state.viewMeshPath = resolved->string();
        state.viewMeshError.clear();
        state.viewMesh.triangles.clear();
        state.viewMesh.valid = false;
        if (!LoadMeshFromFile(*resolved, state.viewMesh, state.viewMeshError)) {
            state.viewMesh = MakeCubeMesh();
        }
    }
    if (!state.viewMeshError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.viewMeshError.c_str());
    }

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.y < 180.0f) viewportSize.y = 180.0f;
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##view3d", viewportSize, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        state.ui.modelYaw += delta.x * 0.01f;
        state.ui.modelPitch += delta.y * 0.01f;
        state.ui.modelAutoRotate = false;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        state.ui.modelZoom = std::clamp(state.ui.modelZoom + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.0f);
    }
    if (state.ui.modelAutoRotate) {
        state.ui.modelYaw += state.ui.modelAutoSpeed * ImGui::GetIO().DeltaTime;
    }
    if (state.viewMesh.valid) {
        DrawMeshWireframe(state.viewMesh, state.ui, ImGui::GetWindowDrawList(), viewportPos, viewportSize);
    } else {
        ImGui::GetWindowDrawList()->AddText(viewportPos, ImGui::GetColorU32(ImVec4(0.8f, 0.7f, 0.4f, 1.0f)),
                                            u8"Загрузите OBJ/FBX модель.");
    }
}

void DrawSkillCatalogPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::SkillCatalog, state.storageDir);
    const auto& catalogSkills = catalog.skills();
    if (state.selectedCatalogIndex >= static_cast<int>(catalogSkills.size())) {
        state.selectedCatalogIndex = -1;
    }
    if (state.selectedCatalogIndex != state.lastCatalogSelection && state.selectedCatalogIndex >= 0 &&
        state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
        const std::string& selectedId = catalogSkills[state.selectedCatalogIndex];
        const std::string selectedName = catalog.display_name(selectedId);
        const std::string selectedDesc = catalog.description(selectedId);
        state.editedSkillWeight = static_cast<float>(catalog.weight(selectedId));
        state.lastCatalogSelection = state.selectedCatalogIndex;
        state.selectedAchievementIndex = -1;
        state.achTitle.fill('\0');
        state.achIcon.fill('\0');
        state.achBonus = 0.0f;
        state.achDurationDays = 0;
        std::snprintf(state.editSkillName.data(), state.editSkillName.size(), "%s", selectedName.c_str());
        std::snprintf(state.editSkillDesc.data(), state.editSkillDesc.size(), "%s", selectedDesc.c_str());
    }

    ImGui::BeginGroup();
    if (!state.isAdmin) ImGui::BeginDisabled();
    if (ImGui::Button(u8"Добавить навык")) {
        state.addSkillPopupRequest = true;
        std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
        std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
        state.newSkillWeight = 1.0f;
    }
    ImGui::SameLine();
    const bool canDeleteSkill = state.selectedCatalogIndex >= 0 &&
        state.selectedCatalogIndex < static_cast<int>(catalogSkills.size());
    if (!canDeleteSkill) ImGui::BeginDisabled();
    if (ImGui::Button(u8"Удалить навык")) {
        state.deleteSkillPopupRequest = true;
        state.pendingSkillDelete = catalogSkills[state.selectedCatalogIndex];
    }
    if (!canDeleteSkill) ImGui::EndDisabled();
    if (!state.isAdmin) ImGui::EndDisabled();
    ImGui::EndGroup();

    ImGui::Separator();
    ImGui::InputText(u8"Поиск навыка", state.skillFilter.data(), state.skillFilter.size());
    ImGui::SameLine();
    ImGui::TextDisabled(u8"Всего: %d", static_cast<int>(catalogSkills.size()));
    ImGui::Separator();
    int visibleCount = 0;
    ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("catalog_table", 2, tableFlags)) {
        ImGui::TableSetupColumn(u8"Навык");
        ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
            const std::string& skillId = catalogSkills[i];
            const std::string displayName = catalog.display_name(skillId);
            if (!MatchesFilter(displayName, state.skillFilter.data())) {
                continue;
            }
            ++visibleCount;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = state.selectedCatalogIndex == i;
            if (ImGui::Selectable(displayName.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                state.selectedCatalogIndex = i;
            }
            if (ImGui::IsItemHovered()) {
                const std::string desc = catalog.description(skillId);
                if (!desc.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(desc.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f", catalog.weight(skillId));
        }
        ImGui::EndTable();
    }
    if (visibleCount == 0) {
        ImGui::TextDisabled(u8"Нет навыков по фильтру.");
    } else if (visibleCount != static_cast<int>(catalogSkills.size())) {
        ImGui::TextDisabled(u8"Найдено: %d", visibleCount);
    }
    ImGui::Separator();
    const std::int64_t nowSecGlobal = NowSeconds();
    if (state.selectedCatalogIndex >= 0 && state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
        const std::string& skillId = catalogSkills[state.selectedCatalogIndex];
        const std::string displayName = catalog.display_name(skillId);
        const std::string desc = catalog.description(skillId);
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", displayName.c_str());
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextWrapped("%s", desc.empty() ? u8"Описание отсутствует." : desc.c_str());
        ImGui::PopTextWrapPos();
        if (state.isAdmin) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextUnformatted(u8"Вес навыка");
            ImGui::SliderFloat("##edit_weight", &state.editedSkillWeight, 0.5f, 1.6f, "%.2f");
            if (ImGui::Button(u8"Сохранить вес")) {
                double newW = static_cast<double>(state.editedSkillWeight);
                ActionResult result = UpdateSkillWeightAction(catalog, skillId, newW, displayName, desc);
                AppLogLevel level = LogLevelForResult(result);
                SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                          result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Каталог");
                if (result.ok) {
                    PrepareXpEntries(state, catalog);
                    RefreshProfiles(state, storage, catalog, state.active ? state.active->id : std::string{});
                }
            }
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextUnformatted(u8"Редактирование навыка");
            ImGui::InputText(u8"Название", state.editSkillName.data(), state.editSkillName.size());
            ImGui::InputTextMultiline(u8"Описание", state.editSkillDesc.data(), state.editSkillDesc.size(), ImVec2(-1.0f, 80.0f));
            if (ImGui::Button(u8"Сохранить навык")) {
                const std::string newName = TrimStringGui(state.editSkillName.data());
                const std::string newDesc = TrimStringGui(state.editSkillDesc.data());
                auto existingId = newName.empty() ? std::optional<std::string>() : catalog.id_for_name(newName);
                if (existingId && *existingId != skillId) {
                    state.mergeSkillPopupRequest = true;
                    state.pendingMergeFromId = skillId;
                    state.pendingMergeToId = *existingId;
                    state.pendingMergeName = newName;
                    state.pendingMergeDesc = newDesc;
                    state.pendingMergeWeight = state.editedSkillWeight;
                } else {
                    ActionResult result = UpdateSkillDetailsAction(catalog, skillId, newName,
                                                                  static_cast<double>(state.editedSkillWeight),
                                                                  newDesc);
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f,
                              result.ok ? 0.9f : 0.45f, result.ok ? 0.45f : 0.45f, 1.0f,
                              level, u8"Каталог");
                    if (result.ok) {
                        PrepareXpEntries(state, catalog);
                        RefreshProfiles(state, storage, catalog, state.active ? state.active->id : std::string{});
                        state.lastCatalogSelection = -1;
                    }
                }
            }
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextUnformatted(u8"Ачивки навыка");
            if (state.active) {
                auto& ach = state.active->profile.achievements();
                std::vector<int> filtered;
                filtered.reserve(ach.size());
                for (size_t idx = 0; idx < ach.size(); ++idx) {
                    if (ach[idx].skill != skillId) continue;
                    filtered.push_back(static_cast<int>(idx));
                }
                if (ImGui::BeginChild("ach_list", ImVec2(0, 140), true)) {
                    for (int fi = 0; fi < static_cast<int>(filtered.size()); ++fi) {
                        int idx = filtered[fi];
                        const auto& a = ach[idx];
                        const bool expired = !a.is_active(nowSecGlobal);
                        bool selected = state.selectedAchievementIndex == idx;
                        std::string row = a.title + " (" + std::to_string(a.bonusPercent) + "%)";
                        if (expired) row += u8" [истекла]";
                        ImGui::PushID(idx);
                        if (ImGui::Selectable(row.c_str(), selected)) {
                            state.selectedAchievementIndex = idx;
                            std::snprintf(state.achTitle.data(), state.achTitle.size(), "%s", a.title.c_str());
                            std::snprintf(state.achIcon.data(), state.achIcon.size(), "%s", a.icon.c_str());
                            state.achBonus = static_cast<float>(a.bonusPercent);
                            if (a.expiresAt > 0 && a.awardedAt > 0) {
                                auto dur = a.expiresAt - a.awardedAt;
                                state.achDurationDays = static_cast<int>(dur / (24 * 3600));
                            } else {
                                state.achDurationDays = 0;
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::InputText(u8"Название ачивки", state.achTitle.data(), state.achTitle.size());
                ImGui::InputText(u8"Иконка (путь)", state.achIcon.data(), state.achIcon.size());
                ImGui::SameLine();
                if (ImGui::Button(u8"Список иконок")) {
                    ImGui::OpenPopup(u8"Выбор иконки");
                }
                if (ImGui::BeginPopup(u8"Выбор иконки")) {
                    const auto icons = LoadAchievementIconChoices(state.storageDir);
                    if (icons.empty()) {
                        ImGui::TextUnformatted(u8"Иконки не найдены.");
                    } else {
                        if (ImGui::BeginChild("icon_picker", ImVec2(360.0f, 240.0f), true)) {
                            for (size_t i = 0; i < icons.size(); ++i) {
                                const auto& choice = icons[i];
                                ImGui::PushID(static_cast<int>(i));
                                bool selected = choice.relativePath == std::string(state.achIcon.data());
                                if (const auto* tex = GetIconTexture(choice.absolutePath)) {
                                    ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)tex->id), ImVec2(32.0f, 32.0f));
                                } else {
                                    ImGui::Button("?", ImVec2(32.0f, 32.0f));
                                }
                                ImGui::SameLine();
                                if (ImGui::Selectable(choice.label.c_str(), selected)) {
                                    std::snprintf(state.achIcon.data(), state.achIcon.size(), "%s", choice.relativePath.c_str());
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::EndPopup();
                }
                ImGui::InputFloat(u8"Бонус к XP (%)", &state.achBonus, 0.5f, 2.0f, "%.1f");
                ImGui::InputInt(u8"Срок (дней, 0 = без срока)", &state.achDurationDays);
                if (state.achDurationDays < 0) state.achDurationDays = 0;
                if (ImGui::Button(u8"Выдать ачивку")) {
                    ActionResult result = GrantAchievementAction(state.active->profile, storage,
                                                                state.achTitle.data(), skillId,
                                                                static_cast<double>(state.achBonus),
                                                                state.achIcon.data(), nowSecGlobal,
                                                                state.achDurationDays);
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Ачивки");
                    if (result.ok) {
                        RefreshProfiles(state, storage, catalog, state.active->id);
                        state.selectedAchievementIndex = -1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Сохранить изменения")) {
                    ActionResult result = UpdateAchievementAction(state.active->profile, storage,
                                                                 state.selectedAchievementIndex,
                                                                 state.achTitle.data(),
                                                                 static_cast<double>(state.achBonus),
                                                                 state.achIcon.data(), nowSecGlobal,
                                                                 state.achDurationDays);
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Ачивки");
                    if (result.ok) {
                        RefreshProfiles(state, storage, catalog, state.active->id);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Удалить ачивку")) {
                    ActionResult result = DeleteAchievementAction(state.active->profile, storage,
                                                                 state.selectedAchievementIndex);
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Ачивки");
                    if (result.ok) {
                        state.selectedAchievementIndex = -1;
                        RefreshProfiles(state, storage, catalog, state.active->id);
                    }
                }
            } else {
                ImGui::TextDisabled(u8"Ачивки отображаются при выборе профиля.");
            }
        }
    } else {
        ImGui::TextUnformatted(u8"Выберите навык, чтобы увидеть описание.");
    }
}

void ConfigureLocale() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "");
    try {
        std::locale sys("");
        std::locale::global(sys);
        std::cout.imbue(sys);
        std::cerr.imbue(sys);
        std::clog.imbue(sys);
        std::wcout.imbue(sys);
        std::wcerr.imbue(sys);
    } catch (...) {
#if defined(_WIN32)
        try {
            std::locale utf8(".UTF-8");
            std::locale::global(utf8);
            std::cout.imbue(utf8);
            std::cerr.imbue(utf8);
            std::clog.imbue(utf8);
            std::wcout.imbue(utf8);
            std::wcerr.imbue(utf8);
        } catch (...) {
            // ignore
        }
#endif
    }
}

} // namespace

// GUI entry point: configure locale, init GLFW/ImGui, then run main loop.
int main() {
    ConfigureLocale();
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "JobSkill GUI", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    auto storageDir = ResolveStorageDirectory();
    auto metaDir = storageDir / "meta";
    std::error_code iniEc;
    std::filesystem::create_directories(metaDir, iniEc);
    static std::string layoutPath;
    layoutPath = (metaDir / "gui-layout.ini").string();
    if (!layoutPath.empty()) {
        io.IniFilename = layoutPath.c_str();
    }

    const std::filesystem::path fontCandidates[] = {
        "gui/fonts/Roboto-Medium.ttf",
        "gui/fonts/Roboto-Regular.ttf",
        "fonts/Roboto-Medium.ttf",
        "fonts/Roboto-Regular.ttf",
        "Roboto-Medium.ttf",
        "Roboto-Regular.ttf",
        "libs/imgui/misc/fonts/Roboto-Medium.ttf",
        "libs/imgui/misc/fonts/DroidSans.ttf",
        "libs/imgui/misc/fonts/Karla-Regular.ttf",
        "libs/imgui/misc/fonts/Cousine-Regular.ttf"
    };
    ImFont* primaryFont = nullptr;
    for (const auto& candidate : fontCandidates) {
        if (auto resolved = FindAssetUpwards(candidate)) {
            primaryFont = io.Fonts->AddFontFromFileTTF(resolved->string().c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
            if (primaryFont) break;
        }
    }
    if (!primaryFont) {
        ImFont* fallback = io.Fonts->AddFontDefault();
        (void)fallback;
        if (auto droid = FindAssetUpwards("libs/imgui/misc/fonts/DroidSans.ttf")) {
            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(droid->string().c_str(), 18.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    extern IJobStorage* CreateFileStorage(const std::filesystem::path& dir);
    SkillCatalog catalog(storageDir);
    auto gameplayConfig = LoadGameplayConfig(storageDir);
    SetGameplayConfig(gameplayConfig);
    std::unique_ptr<IJobStorage> storage(CreateFileStorage(storageDir));
    EnsureAdminProfile(*storage, catalog);

    ImGuiStyle& style = ImGui::GetStyle();
    GuiState state;
    state.storageDir = storageDir;
    state.rulesConfig = gameplayConfig;
    state.rulesDraft = gameplayConfig;
    state.ui = LoadUiSettings(storageDir, style);
    ApplyUiTheme(state.ui.theme, style);
    ApplyUiSettings(state.ui, style, io);
    state.uiLastTheme = state.ui.theme;
    glfwGetWindowPos(window, &state.windowedX, &state.windowedY);
    glfwGetWindowSize(window, &state.windowedW, &state.windowedH);
    state.windowFullscreenLast = state.ui.windowFullscreen;
    glfwSetWindowAttrib(window, GLFW_DECORATED, state.ui.windowDecorated ? GLFW_TRUE : GLFW_FALSE);
    state.windowDecoratedLast = state.ui.windowDecorated;
    if (state.ui.windowFullscreen) {
        state.windowFullscreenLast = false;
        ApplyWindowMode(window, state);
    }
    std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
    RefreshProfiles(state, *storage, catalog);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
            if (ImGui::GetIO().KeyCtrl) {
                ResetUiSettings(state.ui, style, io);
                state.uiLastTheme = state.ui.theme;
                state.uiDirty = false;
                std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
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
        ApplyUiSettings(state.ui, style, io);
        ApplyWindowMode(window, state);
        ApplyWindowDecorations(window, state.ui.windowDecorated, state.windowDecoratedLast);
        HandleBorderlessDrag(window, state.ui);
        if (!state.isAdmin) {
            state.showRules = false;
            state.showUiSettings = false;
            state.showView3dSettings = false;
            state.showLogs = false;
            state.showAdminStats = false;
        }

        // Main menu window
        ImGui::Begin(u8"Главное меню");
        EnsureWindowVisible();
        DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
        ImGui::TextUnformatted(state.isAdmin ? u8"[Режим администратора]" : u8"[Режим просмотра]");
        if (!state.isAdmin) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        if (ImGui::Button(state.isAdmin ? u8"Выйти из админа" : u8"Войти как админ")) {
            if (state.isAdmin) {
                state.isAdmin = false;
                state.showRules = false;
                state.showUiSettings = false;
                state.showView3dSettings = false;
                state.showLogs = false;
                state.showAdminStats = false;
            } else {
                state.adminPopupRequest = true;
                state.adminPassword.fill('\0');
            }
        }
        if (!state.isAdmin) ImGui::PopStyleColor();
        ImGui::Separator();
        auto toggleButton = [](const char* label, bool& value, ImVec2 size) -> bool {
            bool toggled = false;
            if (value) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.75f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.55f, 0.85f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
            }
            if (ImGui::Button(label, size)) {
                value = !value;
                toggled = true;
            }
            if (value) {
                ImGui::PopStyleColor(3);
            }
            return toggled;
        };

        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float buttonWidth = std::max(80.0f, (availWidth - spacing * 2.0f) / 3.0f);
        const ImVec2 buttonSize(buttonWidth, 0.0f);
        ImGui::TextUnformatted(u8"Окно");
        bool fullscreen = state.ui.windowFullscreen;
        if (toggleButton(u8"Во весь экран", fullscreen, buttonSize)) {
            state.ui.windowFullscreen = fullscreen;
            state.uiDirty = true;
        }
        ImGui::SameLine();
        bool borderless = !state.ui.windowDecorated;
        if (toggleButton(u8"Без рамки", borderless, buttonSize)) {
            state.ui.windowDecorated = !borderless;
            state.uiDirty = true;
        }
        ImGui::Separator();

        const bool canArchive = state.selectedIndex >= 0 &&
            state.selectedIndex < static_cast<int>(state.profiles.size()) &&
            !state.profiles[state.selectedIndex].archived;
        const bool canRestore = state.selectedIndex >= 0 &&
            state.selectedIndex < static_cast<int>(state.profiles.size()) &&
            state.profiles[state.selectedIndex].archived;
        const bool canDelete = state.selectedIndex >= 0 &&
            state.selectedIndex < static_cast<int>(state.profiles.size());
        const bool hasActive = state.active.has_value();

        if (ImGui::Button(u8"Обновить", buttonSize)) {
            RefreshProfiles(state, *storage, catalog);
        }
        ImGui::SameLine();
        if (!state.isAdmin) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Создать", buttonSize)) {
            state.modalBuffer.fill('\0');
            state.createPopupRequest = true;
        }
        if (!state.isAdmin) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!state.isAdmin || !canArchive) ImGui::BeginDisabled();
        if (ImGui::Button(u8"В архив", buttonSize)) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Archive;
        }
        if (!state.isAdmin || !canArchive) ImGui::EndDisabled();

        if (!state.isAdmin || !canRestore) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Вернуть", buttonSize)) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Restore;
        }
        if (!state.isAdmin || !canRestore) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!state.isAdmin || !canDelete) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Удалить", buttonSize)) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Delete;
        }
        if (!state.isAdmin || !canDelete) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!state.isAdmin || !hasActive) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Добавить опыт", buttonSize)) {
            PrepareXpEntries(state, catalog);
            state.xpPopupRequest = true;
        }
        if (!state.isAdmin || !hasActive) ImGui::EndDisabled();

        if (ImGui::Button(u8"Выход", buttonSize)) {
            state.requestExit = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted(u8"Меню");
        const float toggleWidth = std::max(140.0f, (availWidth - spacing) / 2.0f);
        const ImVec2 toggleSize(toggleWidth, 0.0f);
        struct ToggleEntry {
            const char* label;
            bool* value;
            bool adminOnly;
        };
        const ToggleEntry toggleItems[] = {
            {u8"Каталог навыков", &state.showSkillCatalog, false},
            {u8"Пайплайн", &state.showPipeline, false},
            {u8"3D просмотр", &state.showView3d, false},
            {u8"3D настройки", &state.showView3dSettings, true},
            {u8"Статистика", &state.showAdminStats, true},
            {u8"Логи", &state.showLogs, true},
            {u8"Правила", &state.showRules, true},
            {u8"Настройки интерфейса", &state.showUiSettings, true}
        };
        if (ImGui::BeginTable("menu_toggles", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& entry : toggleItems) {
                if (entry.adminOnly && !state.isAdmin) continue;
                ImGui::TableNextColumn();
                toggleButton(entry.label, *entry.value, toggleSize);
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::InputText(u8"Поиск профиля", state.profileFilter.data(), state.profileFilter.size());
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
            state.profileFilter.fill('\0');
        }
        ImGui::SameLine();
        ImGui::Checkbox(u8"Архив", &state.showArchivedProfiles);
        ImGui::SameLine();
        const char* profileSortLabels[] = {u8"ID", u8"Имя"};
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo(u8"Сортировка", &state.profileSort, profileSortLabels, IM_ARRAYSIZE(profileSortLabels));
        ImGui::Separator();
        std::vector<int> visibleIndices;
        visibleIndices.reserve(state.profiles.size());
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            const auto& info = state.profiles[i];
            if (!state.showArchivedProfiles && info.archived) {
                continue;
            }
            if (!MatchesFilter(info.id + " " + info.name, state.profileFilter.data())) {
                continue;
            }
            visibleIndices.push_back(i);
        }
        const int sortMode = std::clamp(state.profileSort, 0, 1);
        std::sort(visibleIndices.begin(), visibleIndices.end(), [&](int a, int b) {
            const auto& pa = state.profiles[a];
            const auto& pb = state.profiles[b];
            if (sortMode == 1) {
                std::string nameA = ToLowerAscii(pa.name);
                std::string nameB = ToLowerAscii(pb.name);
                if (nameA != nameB) return nameA < nameB;
            }
            return pa.id < pb.id;
        });

        if (ImGui::BeginChild("profiles", ImVec2(0, 0), false)) {
            ImGui::TextDisabled(u8"Показано: %d из %d",
                                static_cast<int>(visibleIndices.size()),
                                static_cast<int>(state.profiles.size()));
            if (visibleIndices.empty()) {
                ImGui::TextDisabled(u8"Нет профилей по фильтру.");
            }
            for (int i : visibleIndices) {
                const auto& info = state.profiles[i];
                std::string label = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
                if (ImGui::Selectable(label.c_str(), state.selectedIndex == i)) {
                    state.selectedIndex = i;
                    RefreshActiveProfile(state, *storage, catalog);
                    SyncRankSelection(state);
                    SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();

        // Profile details window
        ImGui::Begin(u8"Профиль");
        EnsureWindowVisible();
        DrawWindowBackground(state.ui, UiWindowId::Profile, state.storageDir);
        if (!state.active) {
            ImGui::TextUnformatted(u8"Выберите профиль, чтобы увидеть детали.");
        } else {
            Profile& profile = state.active->profile;
            const int overallLevel = profile.overall_level();
            const int totalXp = profile.total_xp();
            const int progressXp = profile.level_progress();
            const int xpToNext = profile.xp_to_next_level();
            const int xpNeededThisLevel = progressXp + xpToNext;
            const float progressRatio = xpNeededThisLevel > 0 ? static_cast<float>(progressXp) / static_cast<float>(xpNeededThisLevel) : 0.0f;
            const std::int64_t nowSec = NowSeconds();
            const std::string levelText = u8"Уровень " + std::to_string(overallLevel);
            const std::string progressLabel = std::to_string(progressXp) + " / " + std::to_string(xpNeededThisLevel);

            const float levelScale = 1.8f;
            ImVec2 headerStartScreen = ImGui::GetCursorScreenPos();
            ImVec2 levelSize = ImGui::CalcTextSize(levelText.c_str());
            float scaledWidth = levelSize.x * levelScale;
            ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
            ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
            ImVec2 windowPos = ImGui::GetWindowPos();
            float rightEdge = windowPos.x + contentMax.x;
            float leftEdge = windowPos.x + contentMin.x;
            float levelX = rightEdge - scaledWidth - ImGui::GetStyle().ItemInnerSpacing.x;
            if (levelX < leftEdge) levelX = leftEdge;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * levelScale,
                              ImVec2(levelX, headerStartScreen.y),
                              ImGui::GetColorU32(ImGuiCol_Text), levelText.c_str());

            const std::string rankText = DescribeOverallRank(state.active->profile);
            std::string categoriesLine;
            {
                const auto& catScores = state.active->profile.category_best_scores();
                std::ostringstream catStream;
                for (size_t i = 0; i < catScores.size(); ++i) {
                    if (i) catStream << ", ";
                    catStream << Profile::kCategoryLabels[i] << "=" << catScores[i] << "/10";
                }
                categoriesLine = catStream.str();
            }
            if (ImGui::BeginTable("profile_info", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Имя");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(profile.name().c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Копировать##profile_name")) {
                    ImGui::SetClipboardText(profile.name().c_str());
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"ID");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(state.active->id.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Копировать##profile_id")) {
                    ImGui::SetClipboardText(state.active->id.c_str());
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Ранг");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(rankText.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Категории");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", categoriesLine.c_str());
                ImGui::EndTable();
            }
            ImGui::Separator();
            ImGui::TextUnformatted(u8"Обзор");
            ImVec4 accent(0.9f, 0.85f, 0.3f, 1.0f);
            const int skillCount = static_cast<int>(profile.list_skills().size());
            const int achCount = static_cast<int>(profile.achievements().size());
            int activeAchCount = 0;
            for (const auto& ach : profile.achievements()) {
                if (ach.is_active(nowSec)) {
                    activeAchCount += 1;
                }
            }
            if (ImGui::BeginTable("profile_kpi", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawKpiCard("kpi_total", u8"Всего XP", std::to_string(totalXp), accent);
                ImGui::TableSetColumnIndex(1);
                DrawKpiCard("kpi_next", u8"До следующего уровня", std::to_string(xpToNext), accent);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawKpiCard("kpi_skills", u8"Навыков", std::to_string(skillCount), accent);
                ImGui::TableSetColumnIndex(1);
                DrawKpiCard("kpi_ach", u8"Ачивок активных",
                            std::to_string(activeAchCount) + " / " + std::to_string(achCount), accent);
                ImGui::EndTable();
            }
            ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f), progressLabel.c_str());

            const char* profileTabs[] = {u8"Обзор", u8"Ачивки", u8"Навыки", u8"Диаграмма", u8"Действия"};
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo(u8"Раздел", &state.profileSection, profileTabs, IM_ARRAYSIZE(profileTabs));

            if (state.profileSection == 0) {
            {
                RankSpan span = ComputeRankSpan(overallLevel);
                if (span.hasNext && span.nextLevel > span.currentLevel) {
                    const int levelsLeft = span.nextLevel - overallLevel;
                    std::string nextRankText = std::string(u8"След. ранг: ") + span.nextLabel;
                    std::string rankProgressText = u8"До следующего ранга: " + std::to_string(levelsLeft) + u8" ур.";
                    const float rankProgress = static_cast<float>(overallLevel - span.currentLevel) /
                        static_cast<float>(span.nextLevel - span.currentLevel);
                    ImGui::TextUnformatted(nextRankText.c_str());
                    ImGui::ProgressBar(std::clamp(rankProgress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), rankProgressText.c_str());
                } else {
                    ImGui::TextUnformatted(u8"Ранг: максимум");
                }
            }

            if (ImGui::CollapsingHeader(u8"Топ навыков", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* topModes[] = {u8"По XP", u8"По уровню", u8"По весу"};
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo(u8"Рейтинг", &state.topSkillMode, topModes, IM_ARRAYSIZE(topModes));
                auto skills = profile.list_skills();
                if (skills.empty()) {
                    ImGui::TextUnformatted(u8"Навыков нет.");
                } else {
                    const int mode = std::clamp(state.topSkillMode, 0, 2);
                    std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                        if (mode == 1) {
                            if (a.level != b.level) return a.level > b.level;
                        } else if (mode == 2) {
                            if (a.weight != b.weight) return a.weight > b.weight;
                        } else {
                            int totalA = TotalSkillXpGui(a);
                            int totalB = TotalSkillXpGui(b);
                            if (totalA != totalB) return totalA > totalB;
                        }
                        return a.level > b.level;
                    });
                    if (skills.size() > 5) skills.resize(5);
                    if (ImGui::BeginTable("top_skills", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                         ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn(u8"Навык");
                        ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn(u8"Всего XP", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableHeadersRow();
                        for (const auto& skill : skills) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(catalog.display_name(skill.name).c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%d", skill.level);
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%d", TotalSkillXpGui(skill));
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%.2f", skill.weight);
                        }
                        ImGui::EndTable();
                    }
                }
            }

            {
                constexpr std::int64_t kStaleThreshold = 30LL * 24 * 3600;
                const bool isStale = profile.last_task_timestamp() > 0 &&
                    (nowSec - profile.last_task_timestamp()) > kStaleThreshold;
                int categoriesBelow = 0;
                const auto& catScores = profile.category_best_scores();
                for (size_t i = 0; i < catScores.size(); ++i) {
                    if (catScores[i] < Profile::kMaxCategoryScore) {
                        categoriesBelow++;
                    }
                }
                bool hasRecovery = profile.recovery_tasks_remaining() > 0;
                if (ImGui::CollapsingHeader(u8"Индикаторы", ImGuiTreeNodeFlags_DefaultOpen)) {
                    bool hasAny = false;
                    if (hasRecovery) {
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                           u8"Прогрев активен: осталось задач %d", profile.recovery_tasks_remaining());
                        hasAny = true;
                    }
                    if (categoriesBelow > 0) {
                        ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f),
                                           u8"Категорий ниже 10/10: %d", categoriesBelow);
                        hasAny = true;
                    }
                    if (isStale) {
                        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
                                           u8"Давняя активность: более 30 дней");
                        hasAny = true;
                    }
                    if (!hasAny) {
                        ImGui::TextUnformatted(u8"Все в норме.");
                    }
                }
            }

            if (ImGui::CollapsingHeader(u8"Состояние", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::int64_t lastTask = profile.last_task_timestamp();
                std::string lastTaskLabel = lastTask > 0
                    ? FormatTimestamp(lastTask, "%Y-%m-%d %H:%M")
                    : std::string(u8"нет данных");
                std::string elapsedLabel = lastTask > 0
                    ? FormatDurationShort(std::max<std::int64_t>(0, nowSec - lastTask))
                    : std::string(u8"—");
                const int recoveryLeft = profile.recovery_tasks_remaining();

                std::string categoryAttention;
                const auto& catScores = profile.category_best_scores();
                for (size_t i = 0; i < catScores.size(); ++i) {
                    if (catScores[i] >= Profile::kMaxCategoryScore) continue;
                    if (!categoryAttention.empty()) categoryAttention += ", ";
                    categoryAttention += Profile::kCategoryLabels[i];
                    categoryAttention += "=";
                    categoryAttention += std::to_string(catScores[i]);
                    categoryAttention += "/10";
                }
                if (categoryAttention.empty()) {
                    categoryAttention = u8"все освоены";
                }

                if (ImGui::BeginTable("profile_status", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Последняя активность");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(u8"%s (%s)", lastTaskLabel.c_str(), elapsedLabel.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Прогрев");
                    ImGui::TableSetColumnIndex(1);
                    if (recoveryLeft > 0) {
                        ImGui::Text(u8"осталось задач: %d", recoveryLeft);
                    } else {
                        ImGui::TextUnformatted(u8"нет активных штрафов");
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Категории ниже 10/10");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", categoryAttention.c_str());
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader(u8"Категории", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& catScores = profile.category_best_scores();
                for (size_t i = 0; i < catScores.size(); ++i) {
                    const int score = catScores[i];
                    const float ratio = static_cast<float>(score) / static_cast<float>(Profile::kMaxCategoryScore);
                    std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[i];
                    ImGui::TextUnformatted(label.c_str());
                    std::string barText = std::to_string(score) + "/10";
                    ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), barText.c_str());
                }
            }
            }

            if (state.profileSection == 1) if (ImGui::CollapsingHeader(u8"Ачивки", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (profile.achievements().empty()) {
                    ImGui::TextUnformatted(u8"Ачивок нет.");
                } else {
                    auto formatDate = [](std::int64_t ts) -> std::string {
                        if (ts <= 0) return std::string(u8"без срока");
                        std::time_t t = static_cast<std::time_t>(ts);
                        std::tm tm{};
#if defined(_WIN32)
                        localtime_s(&tm, &t);
#else
                        localtime_r(&t, &tm);
#endif
                        char buf[32];
                        if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm)) {
                            return std::string(buf);
                        }
                        return std::string(u8"срок указан");
                    };
                    int activeCount = 0;
                    int expiredCount = 0;
                    for (const auto& a : profile.achievements()) {
                        if (a.is_active(nowSec)) {
                            activeCount += 1;
                        } else {
                            expiredCount += 1;
                        }
                    }
                    ImGui::InputTextWithHint(u8"##ach_filter", u8"Фильтр ачивок",
                                             state.achievementFilter.data(), state.achievementFilter.size());
                    ImGui::SameLine();
                    ImGui::Checkbox(u8"Показывать истекшие", &state.showExpiredAchievements);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(u8"Сброс##ach_filter")) {
                        state.achievementFilter.fill('\0');
                        state.showExpiredAchievements = true;
                    }
                    int visibleAch = 0;
                    for (const auto& a : profile.achievements()) {
                        const bool active = a.is_active(nowSec);
                        if (!state.showExpiredAchievements && !active) {
                            continue;
                        }
                        const std::string achSkillName = catalog.display_name(a.skill);
                        const std::string filterText = a.title + " " + achSkillName;
                        if (!MatchesFilter(filterText, state.achievementFilter.data())) {
                            continue;
                        }
                        visibleAch += 1;
                    }
                    ImGui::TextDisabled(u8"Показано: %d | активных: %d | истекших: %d",
                                        visibleAch, activeCount, expiredCount);

                    std::vector<std::pair<std::string, std::int64_t>> expiringSoon;
                    constexpr std::int64_t kSoonWindow = 7LL * 24 * 3600;
                    for (const auto& a : profile.achievements()) {
                        if (a.expiresAt == 0) continue;
                        if (!a.is_active(nowSec)) continue;
                        const std::int64_t remaining = a.expiresAt - nowSec;
                        if (remaining <= 0 || remaining > kSoonWindow) continue;
                        std::string label = a.title;
                        const std::string achSkillName = catalog.display_name(a.skill);
                        if (!achSkillName.empty()) {
                            label += " (" + achSkillName + ")";
                        }
                        expiringSoon.emplace_back(std::move(label), remaining);
                    }
                    if (!expiringSoon.empty()) {
                        std::sort(expiringSoon.begin(), expiringSoon.end(),
                                  [](const auto& a, const auto& b) { return a.second < b.second; });
                        ImGui::TextUnformatted(u8"Скоро истекают");
                        const size_t limit = std::min<size_t>(3, expiringSoon.size());
                        for (size_t i = 0; i < limit; ++i) {
                            ImGui::BulletText("%s — %s",
                                              expiringSoon[i].first.c_str(),
                                              FormatDurationShort(expiringSoon[i].second).c_str());
                        }
                        if (expiringSoon.size() > limit) {
                            ImGui::TextDisabled(u8"И ещё: %d", static_cast<int>(expiringSoon.size() - limit));
                        }
                        ImGui::Separator();
                    }

                    constexpr int kIconsPerRow = 6;
                    const float iconCell = 110.0f;
                    int iconIndex = 0;
                    for (const auto& a : profile.achievements()) {
                        bool active = a.is_active(nowSec);
                        if (!state.showExpiredAchievements && !active) {
                            continue;
                        }
                        const std::string achSkillName = catalog.display_name(a.skill);
                        const std::string filterText = a.title + " " + achSkillName;
                        if (!MatchesFilter(filterText, state.achievementFilter.data())) {
                            continue;
                        }
                        const std::string expires = formatDate(a.expiresAt);
                        const auto iconPath = ResolveIconPath(a.icon, state.storageDir);
                        ImGui::PushID(iconIndex);
                        bool hovered = false;
                        if (iconPath) {
                            if (const auto* iconTex = GetIconTexture(*iconPath)) {
                                ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)iconTex->id), ImVec2(iconCell, iconCell));
                                hovered = ImGui::IsItemHovered();
                            } else {
                                ImGui::BeginDisabled();
                                ImGui::Button("?", ImVec2(iconCell, iconCell));
                                ImGui::EndDisabled();
                                hovered = ImGui::IsItemHovered();
                            }
                        } else {
                            ImGui::BeginDisabled();
                            ImGui::Button("?", ImVec2(iconCell, iconCell));
                            ImGui::EndDisabled();
                            hovered = ImGui::IsItemHovered();
                        }
                        if (hovered) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(a.title.c_str());
                            ImGui::Text(u8"%s, %+0.1f%% XP", achSkillName.c_str(), a.bonusPercent);
                            if (a.expiresAt == 0) {
                                ImGui::TextUnformatted(u8"Срок: без срока");
                            } else {
                                ImGui::Text(u8"Срок до: %s", expires.c_str());
                                if (active) {
                                    const std::string remaining = FormatDurationShort(a.expiresAt - nowSec);
                                    ImGui::Text(u8"Осталось: %s", remaining.c_str());
                                } else {
                                    ImGui::TextUnformatted(u8"Срок: истекла");
                                }
                            }
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                        ++iconIndex;
                        if (iconIndex % kIconsPerRow != 0) {
                            ImGui::SameLine();
                        }
                    }
                    if (iconIndex % kIconsPerRow != 0) {
                        ImGui::NewLine();
                    }
                    if (visibleAch == 0) {
                        ImGui::TextUnformatted(u8"Нет ачивок по фильтру.");
                    }
                    ImGui::TextDisabled(u8"Показано: %d | активных: %d | истекших: %d",
                                        visibleAch, activeCount, expiredCount);
                }
            }

            if (state.isAdmin && ImGui::CollapsingHeader(u8"Администрирование", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& opts = RankOptions();
                std::vector<const char*> labels;
                labels.reserve(opts.size());
                for (const auto& o : opts) labels.push_back(o.label);
                ImGui::TextUnformatted(u8"Назначить ранг");
                if (state.selectedRankIndex >= static_cast<int>(opts.size())) state.selectedRankIndex = 0;
                if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(), static_cast<int>(labels.size()))) {
                    state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Применить ранг")) {
                    const int newLevel = opts[state.selectedRankIndex].level;
                    state.active->profile.set_level_and_progress(newLevel, newLevel);
                    storage->save_profile(state.active->profile);
                    SetStatus(state, u8"Ранг обновлён.", 0.45f, 0.9f, 0.45f);
                }
            }

            if (state.isAdmin && ImGui::CollapsingHeader(u8"Отчет", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button(u8"Экспорт TXT")) {
                    std::filesystem::path outPath;
                    if (ExportProfileReportTxt(state, catalog, profile, state.active->id, outPath)) {
                        std::string msg = u8"Отчет сохранен: ";
                        msg += outPath.string();
                        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Экспорт CSV")) {
                    std::filesystem::path outPath;
                    if (ExportProfileReportCsv(state, catalog, profile, state.active->id, outPath)) {
                        std::string msg = u8"Отчет сохранен: ";
                        msg += outPath.string();
                        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                    }
                }
                ImGui::TextDisabled(u8"Отчеты сохраняются в data/meta/reports.");
            }

            if (state.profileSection == 2) if (ImGui::CollapsingHeader(u8"Навыки", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::InputText(u8"Поиск навыка в профиле", state.profileSkillFilter.data(),
                                 state.profileSkillFilter.size());
                const char* sortLabels[] = {u8"По имени", u8"По уровню", u8"По XP", u8"По весу"};
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo(u8"Сортировка", &state.profileSkillSort, sortLabels, IM_ARRAYSIZE(sortLabels));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160.0f);
                const char* weightCategoryLabels[] = {
                    u8"Все",
                    u8"A (>=1.30)",
                    u8"B (1.10-1.29)",
                    u8"C (0.90-1.09)",
                    u8"D (0.70-0.89)",
                    u8"E (<0.70)"
                };
                ImGui::Combo(u8"Категория (вес)", &state.profileSkillCategoryFilter,
                             weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Сброс##skill_filters")) {
                    state.profileSkillFilter.fill('\0');
                    state.profileSkillCategoryFilter = 0;
                    state.profileSkillWeightRange[0] = 0.0f;
                    state.profileSkillWeightRange[1] = 2.0f;
                }
                float range[2] = {state.profileSkillWeightRange[0], state.profileSkillWeightRange[1]};
                if (ImGui::SliderFloat2(u8"Диапазон веса", range, 0.0f, 2.0f, "%.2f")) {
                    if (range[0] > range[1]) std::swap(range[0], range[1]);
                    state.profileSkillWeightRange[0] = range[0];
                    state.profileSkillWeightRange[1] = range[1];
                }
                auto skills = state.active->profile.list_skills();
                const int sortMode = std::clamp(state.profileSkillSort, 0, 3);
                std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                    const std::string nameA = catalog.display_name(a.name);
                    const std::string nameB = catalog.display_name(b.name);
                    if (sortMode == 1) {
                        if (a.level != b.level) return a.level > b.level;
                    } else if (sortMode == 2) {
                        int totalA = TotalSkillXpGui(a);
                        int totalB = TotalSkillXpGui(b);
                        if (totalA != totalB) return totalA > totalB;
                    } else if (sortMode == 3) {
                        if (a.weight != b.weight) return a.weight > b.weight;
                    }
                    return nameA < nameB;
                });
                int visibleCount = 0;
                ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
                if (ImGui::BeginTable("skill_table", 5, tableFlags)) {
                    ImGui::TableSetupColumn(u8"Навык");
                    ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("XP", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                    ImGui::TableSetupColumn(u8"Всего XP", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();
                    for (const auto& skill : skills) {
                        const std::string displayName = catalog.display_name(skill.name);
                        if (!MatchesFilter(displayName, state.profileSkillFilter.data())) {
                            continue;
                        }
                        const float w = static_cast<float>(skill.weight);
                        if (w < state.profileSkillWeightRange[0] || w > state.profileSkillWeightRange[1]) {
                            continue;
                        }
                        if (state.profileSkillCategoryFilter > 0) {
                            const int categoryIdx = WeightCategoryIndex(skill.weight);
                            if (categoryIdx != state.profileSkillCategoryFilter) continue;
                        }
                        ++visibleCount;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(displayName.c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            const std::string desc = catalog.description(skill.name);
                            ImGui::TextUnformatted(displayName.c_str());
                            if (!desc.empty()) {
                                ImGui::Separator();
                                ImGui::TextWrapped("%s", desc.c_str());
                            }
                            const int totalSkillXp = TotalSkillXpGui(skill);
                            double bonusPercent = std::max(0.0, (profile.skill_bonus_multiplier(skill.name, nowSec) - 1.0) * 100.0);
                            ImGui::Separator();
                            ImGui::Text(u8"ID: %s", skill.name.c_str());
                            ImGui::Text(u8"Уровень: %d", skill.level);
                            ImGui::Text(u8"XP: %d / %d", skill.xp, skill.xpToNext);
                            ImGui::Text(u8"Всего XP: %d", totalSkillXp);
                            ImGui::Text(u8"Вес: %.2f", skill.weight);
                            ImGui::Text(u8"Категория веса: %s", WeightCategoryLabel(WeightCategoryIndex(skill.weight)));
                            if (bonusPercent > 0.01) {
                                ImGui::Text(u8"Бонус: +%.1f%%", bonusPercent);
                            }
                            ImGui::EndTooltip();
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", skill.level);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%d / %d", skill.xp, skill.xpToNext);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%d", TotalSkillXpGui(skill));
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%.2f", skill.weight);
                    }
                    ImGui::EndTable();
                }
                ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, static_cast<int>(skills.size()));
                if (visibleCount == 0) {
                    ImGui::TextDisabled(u8"Нет навыков по фильтру.");
                }
            }

            if (state.profileSection == 3) if (ImGui::CollapsingHeader(u8"Диаграмма навыков", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto skills = state.active->profile.list_skills();
                std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                    return catalog.display_name(a.name) < catalog.display_name(b.name);
                });
                ImVec2 radarSize(ImGui::GetContentRegionAvail().x, 320.0f);
                if (radarSize.x < 200.0f) radarSize.x = 200.0f;
                ImVec2 radarPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##skill_radar_canvas", radarSize);
                if (ImGui::IsItemVisible()) {
                    for (auto& skill : skills) {
                        skill.name = catalog.display_name(skill.name);
                    }
                    DrawSkillRadarChart(skills, radarPos, ImGui::GetItemRectSize(), ImGui::GetWindowDrawList());
                }
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            }

            if (state.profileSection == 4) if (ImGui::CollapsingHeader(u8"Последние действия", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginChild("profile_activity", ImVec2(0, 140), true)) {
                    const auto logIt = state.activityLogs.find(state.active->id);
                    const bool hasLog = logIt != state.activityLogs.end() && !logIt->second.empty();
                    ImGui::InputTextWithHint(u8"##activity_filter", u8"Фильтр действий",
                                             state.activityFilter.data(), state.activityFilter.size());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(u8"Сброс##activity_filter")) {
                        state.activityFilter.fill('\0');
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Экспорт")) {
                        if (hasLog) {
                            std::filesystem::path outPath;
                            if (ExportProfileActivity(state, state.active->id, logIt->second,
                                                      state.activityFilter.data(), outPath)) {
                                    std::string msg = u8"История сохранена: ";
                                    msg += outPath.string();
                                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                            } else {
                                SetStatus(state, u8"Не удалось сохранить историю.", 1.0f, 0.45f, 0.45f);
                            }
                        } else {
                            SetStatus(state, u8"Нет действий для экспорта.", 1.0f, 0.75f, 0.35f);
                        }
                    }
                    ImGui::Separator();
                    if (hasLog) {
                        int shown = 0;
                        for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                            if (!MatchesFilter(*it, state.activityFilter.data())) continue;
                            ImGui::BulletText("%s", it->c_str());
                            ++shown;
                        }
                        if (shown == 0) {
                            ImGui::TextUnformatted(u8"Нет действий по фильтру.");
                        }
                    } else {
                        ImGui::TextUnformatted(u8"Пока нет действий.");
                    }
                }
                ImGui::EndChild();
            }

            ShowStatus(state);
        }
        ImGui::End();

        bool anyWorkspace = state.showSkillCatalog || state.showPipeline || state.showView3d ||
            (state.isAdmin && (state.showRules || state.showUiSettings || state.showView3dSettings ||
                               state.showLogs || state.showAdminStats));
        if (anyWorkspace) {
            ImGui::Begin(u8"Рабочее окно", nullptr, ImGuiWindowFlags_NoCollapse);
            EnsureWindowVisible();
            if (ImGui::BeginTabBar("workspace_tabs")) {
                if (state.showSkillCatalog && ImGui::BeginTabItem(u8"Каталог навыков", &state.showSkillCatalog)) {
                    DrawSkillCatalogPanel(state, *storage, catalog);
                    ImGui::EndTabItem();
                }
                if (state.showPipeline && ImGui::BeginTabItem(u8"Пайплайн", &state.showPipeline)) {
                    DrawPipelinePanel(state);
                    ImGui::EndTabItem();
                }
                if (state.showView3d && ImGui::BeginTabItem(u8"3D просмотр", &state.showView3d)) {
                    DrawView3dPanel(state);
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showView3dSettings &&
                    ImGui::BeginTabItem(u8"3D настройки", &state.showView3dSettings)) {
                    DrawView3dSettingsPanel(state);
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showLogs && ImGui::BeginTabItem(u8"Логи", &state.showLogs)) {
                    DrawLogsPanel(state);
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showAdminStats && ImGui::BeginTabItem(u8"Статистика", &state.showAdminStats)) {
                    DrawAdminStatsPanel(state, *storage, catalog);
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showRules && ImGui::BeginTabItem(u8"Правила", &state.showRules)) {
                    DrawRulesPanel(state, *storage, catalog);
                    ImGui::EndTabItem();
                }
                if (state.isAdmin && state.showUiSettings &&
                    ImGui::BeginTabItem(u8"Настройки интерфейса", &state.showUiSettings)) {
                    DrawUiSettingsPanel(state, style);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        // Admin login modal
        if (state.adminPopupRequest) {
            ImGui::OpenPopup(u8"Вход администратора");
            state.adminPopupRequest = false;
        }
        bool adminPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Вход администратора", &adminPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText(u8"Пароль", state.adminPassword.data(), state.adminPassword.size(), ImGuiInputTextFlags_Password);
            if (ImGui::Button(u8"Войти")) {
                if (std::string(state.adminPassword.data()) == kAdminPassword) {
                    state.isAdmin = true;
                    SetStatus(state, "Администратор: доступ открыт.", 0.45f, 0.9f, 0.45f, 1.0f,
                              AppLogLevel::Info, "Админ");
                    ImGui::CloseCurrentPopup();
                    adminPopupOpen = false;
                } else {
                    SetStatus(state, "Неверный пароль администратора.", 1.0f, 0.45f, 0.45f, 1.0f,
                              AppLogLevel::Warning, "Админ");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена")) {
                ImGui::CloseCurrentPopup();
                adminPopupOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!adminPopupOpen && ImGui::IsPopupOpen(u8"Вход администратора")) {
            ImGui::CloseCurrentPopup();
        }

        // Skill Catalog modals
        if (state.addSkillPopupRequest) {
            ImGui::OpenPopup(u8"Добавить навык");
            state.addSkillPopupRequest = false;
        }
        bool addSkillOpen = true;
        if (ImGui::BeginPopupModal(u8"Добавить навык", &addSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText(u8"Название", state.newSkillName.data(), state.newSkillName.size());
            ImGui::InputFloat(u8"Вес (0.5 - 1.6)", &state.newSkillWeight, 0.05f, 0.2f, "%.2f");
            ImGui::InputTextMultiline(u8"Описание", state.newSkillDesc.data(), state.newSkillDesc.size(), ImVec2(360, 120));
            if (ImGui::Button(u8"Сохранить")) {
                AddSkillResult result = AddSkillAction(catalog, state.newSkillName.data(),
                                                      static_cast<double>(state.newSkillWeight),
                                                      state.newSkillDesc.data());
                AppLogLevel level = LogLevelForResult(result);
                SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                          result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Каталог");
                if (result.ok) {
                    PrepareXpEntries(state, catalog);
                    std::string keepId = state.active ? state.active->id : std::string{};
                    RefreshProfiles(state, *storage, catalog, keepId);
                    if (!result.id.empty()) {
                        const auto& skills = catalog.skills();
                        for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                            if (skills[i] == result.id) {
                                state.selectedCatalogIndex = i;
                                break;
                            }
                        }
                    }
                    ImGui::CloseCurrentPopup();
                    addSkillOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена")) {
                ImGui::CloseCurrentPopup();
                addSkillOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!addSkillOpen && ImGui::IsPopupOpen(u8"Добавить навык")) {
            ImGui::CloseCurrentPopup();
        }

        if (state.deleteSkillPopupRequest) {
            ImGui::OpenPopup(u8"Удалить навык");
            state.deleteSkillPopupRequest = false;
        }
        bool deleteSkillOpen = true;
        if (ImGui::BeginPopupModal(u8"Удалить навык", &deleteSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!state.pendingSkillDelete.empty()) {
                const std::string displayName = catalog.display_name(state.pendingSkillDelete);
                ImGui::Text(u8"Удалить навык '%s' из каталога?", displayName.c_str());
                if (ImGui::Button(u8"Удалить")) {
                    bool removedFromProfiles = false;
                    ActionResult result = RemoveSkillAction(*storage, catalog, state.pendingSkillDelete,
                                                           state.active ? state.active->id : std::string{},
                                                           removedFromProfiles);
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Каталог");
                    if (result.ok) {
                        PrepareXpEntries(state, catalog);
                        std::string keepId = state.active ? state.active->id : std::string{};
                        RefreshProfiles(state, *storage, catalog, keepId);
                        if (state.selectedCatalogIndex >= static_cast<int>(catalog.skills().size())) {
                            state.selectedCatalogIndex = static_cast<int>(catalog.skills().size()) - 1;
                        }
                    }
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                    state.pendingSkillDelete.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Отмена")) {
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                    state.pendingSkillDelete.clear();
                }
            } else {
                ImGui::TextUnformatted(u8"Навык не выбран.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!deleteSkillOpen && ImGui::IsPopupOpen(u8"Удалить навык")) {
            ImGui::CloseCurrentPopup();
        }

        if (state.mergeSkillPopupRequest) {
            ImGui::OpenPopup(u8"Слияние навыков");
            state.mergeSkillPopupRequest = false;
        }
        bool mergeSkillOpen = true;
        if (ImGui::BeginPopupModal(u8"Слияние навыков", &mergeSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!state.pendingMergeFromId.empty() && !state.pendingMergeToId.empty()) {
                const std::string fromName = catalog.display_name(state.pendingMergeFromId);
                const std::string toName = catalog.display_name(state.pendingMergeToId);
                ImGui::Text(u8"Навык '%s' уже существует.", toName.c_str());
                ImGui::Text(u8"Слить опыт из '%s' в '%s'?", fromName.c_str(), toName.c_str());
                if (ImGui::Button(u8"Слить")) {
                    ActionResult result = MergeSkillAction(*storage, catalog,
                                                          state.pendingMergeFromId,
                                                          state.pendingMergeToId,
                                                          state.pendingMergeName,
                                                          static_cast<double>(state.pendingMergeWeight),
                                                          state.pendingMergeDesc,
                                                          state.active ? state.active->id : std::string{});
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Каталог");
                    if (result.ok) {
                        PrepareXpEntries(state, catalog);
                        std::string keepId = state.active ? state.active->id : std::string{};
                        RefreshProfiles(state, *storage, catalog, keepId);
                        const auto& skills = catalog.skills();
                        for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                            if (skills[i] == state.pendingMergeToId) {
                                state.selectedCatalogIndex = i;
                                break;
                            }
                        }
                        state.lastCatalogSelection = -1;
                    }
                    state.pendingMergeFromId.clear();
                    state.pendingMergeToId.clear();
                    state.pendingMergeName.clear();
                    state.pendingMergeDesc.clear();
                    state.pendingMergeWeight = 1.0f;
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Отмена")) {
                    state.pendingMergeFromId.clear();
                    state.pendingMergeToId.clear();
                    state.pendingMergeName.clear();
                    state.pendingMergeDesc.clear();
                    state.pendingMergeWeight = 1.0f;
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
            } else {
                ImGui::TextUnformatted(u8"Нет данных для слияния.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!mergeSkillOpen && ImGui::IsPopupOpen(u8"Слияние навыков")) {
            ImGui::CloseCurrentPopup();
        }

        // XP sheet modal
        if (state.xpPopupRequest) {
            ImGui::OpenPopup(u8"Добавление опыта");
            state.xpPopupRequest = false;
        }
        bool xpPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Добавление опыта", &xpPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!state.isAdmin) {
                ImGui::TextUnformatted(u8"Доступно только администратору.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
                }
                ImGui::EndPopup();
                if (!xpPopupOpen && ImGui::IsPopupOpen(u8"Добавление опыта")) {
                    ImGui::CloseCurrentPopup();
                }
                goto skip_xp_body;
            }
            const auto& catalogSkills = catalog.skills();
            if (state.xpEntries.size() != catalogSkills.size()) {
                PrepareXpEntries(state, catalog);
            }
            if (catalogSkills.empty()) {
                ImGui::TextUnformatted(u8"Каталог пуст.");
            } else if (!state.active) {
                ImGui::TextUnformatted(u8"Нет активного профиля.");
            } else {
                state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                const GameplayConfig& rules = GetGameplayConfig();

                ImGui::TextUnformatted(u8"Параметры задачи");
                ImGui::Separator();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo(u8"Категория", &state.taskCategoryIndex,
                                 Profile::kCategoryLabels.data(), Profile::kCategoryCount)) {
                    state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                }
                const int currentCategory = state.taskCategoryIndex;
                const int baseXp = rules.categoryBaseXp[currentCategory];
                const float scoreRatio = static_cast<float>(state.taskScore) / static_cast<float>(Profile::kMaxCategoryScore);
                const float scoreMultiplier = std::pow(std::max(0.1f, scoreRatio), 1.35f);
                const int currentBest = state.active->profile.category_best_score(static_cast<size_t>(currentCategory));
                ImGui::SameLine();
                ImGui::Text(u8"Базовый XP: %d", baseXp);
                ImGui::TextDisabled(u8"Категория задаёт базовый XP; оценка — нелинейный множитель.");
                if (ImGui::SliderInt(u8"Оценка", &state.taskScore, 1, Profile::kMaxCategoryScore)) {
                    state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                }
                ImGui::Text(u8"Множитель за оценку: %.2f", scoreMultiplier);
                if (currentBest >= Profile::kMaxCategoryScore) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                       u8"Лучший результат: %d/10 (категория освоена)", currentBest);
                } else {
                    ImGui::Text(u8"Лучший результат: %d/10", currentBest);
                    if (state.taskScore <= currentBest) {
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                           u8"Повтор оценки — глобальный XP ограничен 35%% (навыки без штрафа).");
                    }
                }

                ImGui::Separator();
                ImGui::TextUnformatted(u8"Распределите навыки (автоподбор до 100%)");
                if (ImGui::Button(u8"Равномерно")) {
                    PrepareXpEntries(state, catalog);
                }
                ImGui::SameLine();
                ImGui::InputTextWithHint(u8"##xp_filter", u8"Фильтр навыков",
                                         state.xpSkillFilter.data(), state.xpSkillFilter.size());
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Сброс##xp_filter")) {
                    state.xpSkillFilter.fill('\0');
                }
                const char* xpSortLabels[] = {u8"По имени", u8"По доле", u8"По бонусу", u8"По XP"};
                ImGui::SetNextItemWidth(140.0f);
                ImGui::Combo(u8"Сортировка##xp", &state.xpSortMode, xpSortLabels, IM_ARRAYSIZE(xpSortLabels));
                const float sliderWidth = ImGui::CalcTextSize("000").x + ImGui::GetStyle().FramePadding.x * 6.0f;
                int percentSum = 0;
                int maxSharePercent = 0;
                for (const auto& entry : state.xpEntries) {
                    percentSum += entry.percent;
                    if (entry.percent > maxSharePercent) {
                        maxSharePercent = entry.percent;
                    }
                }
                const auto now = std::chrono::system_clock::now();
                const std::int64_t nowSeconds =
                    std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                std::vector<std::string> xpNames(catalogSkills.size());
                std::vector<double> xpBonus(catalogSkills.size());
                std::vector<int> xpPreview(catalogSkills.size());
                std::vector<int> xpOrder;
                xpOrder.reserve(catalogSkills.size());
                for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                    const std::string& skillId = catalogSkills[i];
                    xpNames[i] = catalog.display_name(skillId);
                    double mult = state.active->profile.skill_bonus_multiplier(skillId, nowSeconds);
                    xpBonus[i] = std::max(0.0, (mult - 1.0) * 100.0);
                    const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                    xpPreview[i] = static_cast<int>(std::round(previewXp * mult));
                    if (!MatchesFilter(xpNames[i], state.xpSkillFilter.data())) {
                        continue;
                    }
                    xpOrder.push_back(i);
                }
                const int xpSortMode = std::clamp(state.xpSortMode, 0, 3);
                std::sort(xpOrder.begin(), xpOrder.end(), [&](int a, int b) {
                    if (xpSortMode == 1) {
                        if (state.xpEntries[a].percent != state.xpEntries[b].percent) {
                            return state.xpEntries[a].percent > state.xpEntries[b].percent;
                        }
                    } else if (xpSortMode == 2) {
                        if (xpBonus[a] != xpBonus[b]) return xpBonus[a] > xpBonus[b];
                    } else if (xpSortMode == 3) {
                        if (xpPreview[a] != xpPreview[b]) return xpPreview[a] > xpPreview[b];
                    } else {
                        if (xpNames[a] != xpNames[b]) return xpNames[a] < xpNames[b];
                    }
                    return a < b;
                });
                int visibleXpCount = 0;
                if (ImGui::BeginTable("xp_sheet", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn(u8"Навык");
                    ImGui::TableSetupColumn(u8"Доля (%)");
                    ImGui::TableSetupColumn(u8"Ачивки");
                    ImGui::TableSetupColumn("XP");
                    ImGui::TableHeadersRow();
                    for (int orderIdx = 0; orderIdx < static_cast<int>(xpOrder.size()); ++orderIdx) {
                        int i = xpOrder[orderIdx];
                        ImGui::TableNextRow();
                        const std::string& skillId = catalogSkills[i];
                        const std::string& displayName = xpNames[i];
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(displayName.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(i);
                        int percent = state.xpEntries[i].percent;
                        ImGui::PushItemWidth(sliderWidth);
                        if (ImGui::SliderInt("##share", &percent, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                            AdjustSkillShare(state, i, percent);
                            percent = state.xpEntries[i].percent;
                        }
                        ImGui::PopItemWidth();
                        ImGui::PopID();
                        ImGui::TableSetColumnIndex(2);
                        if (xpBonus[i] > 0.01) {
                            ImGui::Text("+%.1f%%", xpBonus[i]);
                        } else {
                            ImGui::TextUnformatted("-");
                        }
                        ImGui::TableSetColumnIndex(3);
                        const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                        if (xpPreview[i] != previewXp) {
                            ImGui::Text("%d -> %d", previewXp, xpPreview[i]);
                        } else {
                            ImGui::Text("%d", previewXp);
                        }
                        visibleXpCount++;
                    }
                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::TextDisabled(u8"Показано: %d из %d",
                                visibleXpCount, static_cast<int>(catalogSkills.size()));
            if (percentSum == 100) {
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), u8"Назначено: %d%%", percentSum);
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), u8"Назначено: %d%%", percentSum);
            }
                const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * (static_cast<float>(maxSharePercent) / 100.0f);
                const int previewPool = static_cast<int>(std::round(baseXp * scoreMultiplier * focusBonus));
                ImGui::Text(u8"Фокус-бонус: %.2f (макс. доля %d%%)", focusBonus, maxSharePercent);
                ImGui::TextDisabled(u8"Прогноз XP по навыкам до штрафов: %d", previewPool);

                const bool repeatPenaltyPreview = state.taskScore <= currentBest;
                const int repeatPercentPreview = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
                const int recoveryPercentPreview = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
                std::string globalHint = u8"Глобальный XP";
                bool previewPenalty = false;
                if (repeatPenaltyPreview) {
                    globalHint += u8" (повтор " + std::to_string(repeatPercentPreview) + "%)";
                    previewPenalty = true;
                }
                if (state.active->profile.penalty_active()) {
                    globalHint += u8" (прогрев " + std::to_string(recoveryPercentPreview) + "%)";
                    previewPenalty = true;
                }
                int effectivePreview = previewPool;
                if (repeatPenaltyPreview) {
                    effectivePreview = static_cast<int>(std::round(effectivePreview * rules.repeatRewardFactor));
                }
                if (state.active->profile.penalty_active()) {
                    effectivePreview = static_cast<int>(std::round(effectivePreview * rules.recoveryRewardFactor));
                }
                ImGui::TextDisabled(u8"%s: %d%s", globalHint.c_str(), effectivePreview,
                                    previewPenalty ? u8" (после штрафов)" : "");

                if (ImGui::Button(u8"Применить")) {
                    const int readyCategory = NormalizeCategoryIndex(state.taskCategoryIndex);
                    const int readyScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                    const int storedBest = state.active->profile.category_best_score(static_cast<size_t>(readyCategory));
                    int percentCheck = 0;
                    bool hasContribution = false;
                    for (const auto& entry : state.xpEntries) {
                        int clamped = ClampToRange(entry.percent, 0, 100);
                        percentCheck += clamped;
                        if (clamped > 0) hasContribution = true;
                    }
                    if (percentCheck != 100) {
                        SetStatus(state, u8"Сумма долей должна быть 100%.", 1.0f, 0.45f, 0.45f);
                    } else if (!hasContribution) {
                        SetStatus(state, u8"Нужно выбрать хотя бы один навык.", 1.0f, 0.45f, 0.45f);
                    } else {
                        float maxShare = 0.0f;
                        if (!state.xpEntries.empty()) {
                            const auto it = std::max_element(
                                state.xpEntries.begin(), state.xpEntries.end(),
                                [](const XpEntry& a, const XpEntry& b) { return a.percent < b.percent; });
                            maxShare = static_cast<float>(it->percent) / 100.0f;
                        }
                        const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * maxShare;
                        int basePool = static_cast<int>(std::round(
                            rules.categoryBaseXp[readyCategory] *
                            std::pow(std::max(0.1f, static_cast<float>(readyScore) / 10.0f), 1.35f) * focusBonus));
                        if (basePool < 0) basePool = 0;
                        std::vector<int> xpDistribution(state.xpEntries.size(), 0);
                        int remainder = basePool;
                        int fallbackIndex = -1;
                        for (int i = 0; i < static_cast<int>(state.xpEntries.size()); ++i) {
                            int percent = ClampToRange(state.xpEntries[i].percent, 0, 100);
                            if (percent <= 0) continue;
                            int shareXp = (basePool * percent) / 100;
                            xpDistribution[i] = shareXp;
                            remainder -= shareXp;
                            if (fallbackIndex == -1 || percent > state.xpEntries[fallbackIndex].percent) {
                                fallbackIndex = i;
                            }
                        }
                        if (remainder > 0 && fallbackIndex >= 0) {
                            xpDistribution[fallbackIndex] += remainder;
                        }
                        const char* categoryLabel = ClassificationLabel(readyCategory);
                        std::ostringstream skillsStream;
                        skillsStream.imbue(std::locale::classic());
                        bool firstSkill = true;
                        for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                            int shareXp = xpDistribution[i];
                            if (shareXp <= 0) continue;
                            const std::string& skillId = catalogSkills[i];
                            const std::string displayName = catalog.display_name(skillId);
                            double weight = catalog.weight(skillId);
                            state.active->profile.add_skill(skillId, 1, weight);
                            double mult = state.active->profile.skill_bonus_multiplier(skillId, nowSeconds);
                            double bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
                            int finalSkillXp = static_cast<int>(std::round(shareXp * mult));
                            bool leveled = state.active->profile.grant_xp(skillId, finalSkillXp);
                            if (!firstSkill) skillsStream << " | ";
                            skillsStream << displayName << " +" << finalSkillXp << " XP (" << state.xpEntries[i].percent << "%";
                            if (bonusPercent > 0.01) {
                                skillsStream << ", +" << std::fixed << std::setprecision(1) << bonusPercent << "%";
                                skillsStream << std::defaultfloat;
                            }
                            skillsStream << ")";
                            if (leveled) skillsStream << u8" уровень вверх";
                            firstSkill = false;
                        }
                        if (!firstSkill) {
                            AppendLog(state, state.active->id, skillsStream.str());
                        }
                        bool repeatPenalty = readyScore <= storedBest;
                        int effectiveXp = basePool;
                        if (repeatPenalty) {
                            effectiveXp = static_cast<int>(std::round(effectiveXp * rules.repeatRewardFactor));
                        }
                        constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;
                        if (state.active->profile.last_task_timestamp() > 0 &&
                            (nowSeconds - state.active->profile.last_task_timestamp()) > kThirtyDays) {
                            state.active->profile.start_penalty_recovery(rules.recoveryWarmupTasks);
                        }
                        bool recoveryPenalty = false;
                        if (state.active->profile.penalty_active()) {
                            recoveryPenalty = true;
                            effectiveXp = static_cast<int>(std::round(effectiveXp * rules.recoveryRewardFactor));
                            state.active->profile.consume_penalty_task();
                        }
                        state.active->profile.set_last_task_timestamp(nowSeconds);
                        if (effectiveXp > 0) {
                            state.active->profile.grant_global_xp(effectiveXp);
                        }
                        if (readyScore > storedBest) {
                            state.active->profile.update_category_best_score(static_cast<size_t>(readyCategory), readyScore);
                            std::ostringstream bestStream;
                            bestStream.imbue(std::locale::classic());
                            bestStream << u8"Категория " << categoryLabel << u8": лучший результат " << readyScore << "/10";
                            AppendLog(state, state.active->id, bestStream.str());
                            if (readyScore == Profile::kMaxCategoryScore) {
                                std::ostringstream mastery;
                                mastery.imbue(std::locale::classic());
                                mastery << u8"Категория " << categoryLabel << u8" освоена!";
                                AppendLog(state, state.active->id, mastery.str());
                            }
                        }
                        constexpr bool kDecayEnabled = false;
                        if (kDecayEnabled) {
                            state.active->profile.reset_category_cooldown(static_cast<size_t>(readyCategory));
                            for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                                if (idx == static_cast<size_t>(readyCategory)) continue;
                                state.active->profile.tick_category_cooldown(idx);
                                if (state.active->profile.category_cooldown(idx) < 0) {
                                    state.active->profile.update_category_best_score(
                                        idx, state.active->profile.category_best_score(idx) - 1);
                                    state.active->profile.reset_category_cooldown(idx);
                                    std::ostringstream decay;
                                    decay.imbue(std::locale::classic());
                                    decay << u8"Категория " << Profile::kCategoryLabels[idx] << u8" снижена до "
                                          << state.active->profile.category_best_score(idx) << "/10";
                                    AppendLog(state, state.active->id, decay.str());
                                }
                            }
                            int buffer = state.active->profile.category_cooldown(0);
                            for (size_t idx = 1; idx < Profile::kCategoryCount; ++idx) {
                                buffer = std::min(buffer, state.active->profile.category_cooldown(idx));
                            }
                            buffer = std::max(0, buffer);
                            state.active->profile.set_inactivity_tasks(buffer);
                        }
                        std::ostringstream summaryStream;
                        summaryStream.imbue(std::locale::classic());
                        summaryStream << u8"Задача [" << categoryLabel << u8"] с оценкой " << readyScore
                                      << u8" => +" << effectiveXp << " XP";
                        const int repeatPercent = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
                        const int recoveryPercent = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
                        if (repeatPenalty) summaryStream << u8" (повтор " << repeatPercent << "%)";
                        if (recoveryPenalty) summaryStream << u8" (прогрев " << recoveryPercent << "%)";
                        const std::string summaryText = summaryStream.str();
                        AppendLog(state, state.active->id, summaryText);
                        storage->save_profile(state.active->profile);
                        PrepareXpEntries(state, catalog);
                        SetStatus(state, summaryText, 0.45f, 0.9f, 0.45f);
                        ImGui::CloseCurrentPopup();
                        xpPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Отмена")) {
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
                }
            }
            ImGui::EndPopup();
        }
skip_xp_body:
        if (!xpPopupOpen && ImGui::IsPopupOpen(u8"Добавление опыта")) {
            ImGui::CloseCurrentPopup();
        }

        // Create profile modal
        if (state.createPopupRequest) {
            ImGui::OpenPopup(u8"Создать профиль");
            state.createPopupRequest = false;
        }
        bool createPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Создать профиль", &createPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText(u8"Имя", state.modalBuffer.data(), state.modalBuffer.size());
            if (ImGui::Button(u8"Создать")) {
                CreateProfileResult result = CreateProfileAction(*storage, catalog, state.modalBuffer.data());
                AppLogLevel level = LogLevelForResult(result);
                SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                          result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Профили");
                if (result.ok) {
                    RefreshProfiles(state, *storage, catalog, result.id);
                    ImGui::CloseCurrentPopup();
                    createPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена")) {
                ImGui::CloseCurrentPopup();
                createPopupOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!createPopupOpen && ImGui::IsPopupOpen(u8"Создать профиль")) {
            ImGui::CloseCurrentPopup();
        }

        // Confirm modal
        if (state.confirmPopupRequest) {
            ImGui::OpenPopup(u8"Подтвердите действие");
            state.confirmPopupRequest = false;
        }
        bool confirmPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Подтвердите действие", &confirmPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
                const auto& info = state.profiles[state.selectedIndex];
                std::string action;
                switch (state.confirmAction) {
                    case ConfirmAction::Archive: action = u8"архивирование"; break;
                    case ConfirmAction::Restore: action = u8"восстановление"; break;
                    case ConfirmAction::Delete: action = u8"удаление"; break;
                    default: action = ""; break;
                }
                ImGui::Text(u8"Подтвердить %s профиля [%s] %s?", action.c_str(), info.id.c_str(), info.name.c_str());
                if (ImGui::Button(u8"Да")) {
                    ActionResult actionResult;
                    if (state.confirmAction == ConfirmAction::Archive) {
                        actionResult = SetProfileArchivedAction(*storage, info.id, true);
                    } else if (state.confirmAction == ConfirmAction::Restore) {
                        actionResult = SetProfileArchivedAction(*storage, info.id, false);
                    } else if (state.confirmAction == ConfirmAction::Delete) {
                        actionResult = DeleteProfileAction(*storage, info.id);
                    }
                    if (actionResult.ok) {
                        SetStatus(state, actionResult.message, 0.45f, 0.9f, 0.45f, 1.0f,
                                  AppLogLevel::Info, u8"Профили");
                        std::string newFocus;
                        if (state.confirmAction != ConfirmAction::Delete) {
                            newFocus = info.id;
                        }
                        RefreshProfiles(state, *storage, catalog, newFocus);
                    } else {
                        SetStatus(state, actionResult.message, 1.0f, 0.45f, 0.45f, 1.0f,
                                  AppLogLevel::Error, u8"Профили");
                    }
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Нет")) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            } else {
                ImGui::TextUnformatted(u8"Профиль не выбран.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!confirmPopupOpen && ImGui::IsPopupOpen(u8"Подтвердите действие")) {
            ImGui::CloseCurrentPopup();
            state.confirmAction = ConfirmAction::None;
        }

        if (state.requestExit) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ReleaseIconTextures();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}



