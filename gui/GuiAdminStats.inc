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
