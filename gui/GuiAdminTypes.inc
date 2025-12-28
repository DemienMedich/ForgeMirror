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
