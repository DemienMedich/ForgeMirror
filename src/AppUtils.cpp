#include "AppUtils.h"

#include <iostream>

#include "IJobStorage.h"
#include "SkillCatalog.h"
#include "Profile.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

std::string DescribeOverallRank(int overallLevel) {
    if (overallLevel < 20) return "Intern";
    if (overallLevel < 50) return "Junior";
    if (overallLevel < 150) return "Middle";
    int extra = (overallLevel - 150) / 50;
    if (extra <= 0) return "Senior";
    std::ostringstream ss;
    ss << "Senior" << " (+" << extra << ")";
    return ss.str();
}

void EnsureAdminProfile(IJobStorage& storage, SkillCatalog& catalog) {
    constexpr const char* kAdminName = "Roman";
    auto stored = storage.list_profiles();
    bool exists = std::any_of(stored.begin(), stored.end(), [&](const IJobStorage::ProfileInfo& info) {
        return info.name == kAdminName && !info.archived;
    });
    if (exists) return;

    Profile admin(kAdminName);
    if (auto skill = catalog.canonical("Modeling")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    } else {
        admin.add_skill("Modeling", 1, catalog.weight("Modeling"));
    }
    if (auto skill = catalog.canonical("Lighting")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    } else {
        admin.add_skill("Lighting", 1, catalog.weight("Lighting"));
    }
    if (auto skill = catalog.canonical("Materials")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    } else {
        admin.add_skill("Materials", 1, catalog.weight("Materials"));
    }

    auto info = storage.create_profile(admin);
    if (info) {
        storage.save_profile(admin);
        std::cout << "Created default admin profile '" << admin.name() << "' with ID " << info->id << ".\n";
    } else {
        std::cout << "Warning: failed to create default admin profile.\n";
    }
}

std::filesystem::path ResolveStorageDirectory() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "JobSkill";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".jobskill";
    }
#endif
    return std::filesystem::current_path();
}
