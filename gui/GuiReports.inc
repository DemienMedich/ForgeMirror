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
        if (state.adminStatsRankFilter > 0) {
            const int rankIdx = RankIndexForLevel(item.level);
            if (rankIdx < 0 || rankIdx != state.adminStatsRankFilter - 1) {
                continue;
            }
        }
        const std::string hay = item.id + " " + item.name;
        if (!MatchesFilter(hay, state.filters.adminStats.data())) {
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
