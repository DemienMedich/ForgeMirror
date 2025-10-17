#include "AppUtils.h"

#include <iostream>

#include "IJobStorage.h"
#include "SkillCatalog.h"
#include "Profile.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

std::string ToRoman(int value) {
    if (value <= 0) return "";
    static const std::pair<int, const char*> numerals[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
    };
    std::string out;
    for (const auto& [num, sym] : numerals) {
        while (value >= num) {
            out += sym;
            value -= num;
        }
    }
    return out;
}

std::string BuildRank(const char* base, int startLevel, int substep, int level) {
    int index = (level - startLevel) / substep;
    if (index <= 0) return std::string(base);
    std::ostringstream ss;
    ss << base << " (" << ToRoman(index) << ")";
    return ss.str();
}

} // namespace

std::string DescribeOverallRank(int overallLevel) {
    if (overallLevel < 10) return "Intern";
    if (overallLevel < 50) return BuildRank("Junior", 10, 10, overallLevel);
    if (overallLevel < 150) return BuildRank("Middle", 50, 10, overallLevel);
    return BuildRank("Senior", 150, 10, overallLevel);
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

    SyncProfileWithCatalog(admin, catalog);

    auto info = storage.create_profile(admin);
    if (info) {
        storage.save_profile(admin);
        std::cout << "Created default admin profile '" << admin.name() << "' with ID " << info->id << ".\n";
    } else {
        std::cout << "Warning: failed to create default admin profile.\n";
    }
}

std::filesystem::path ResolveStorageDirectory() {
    auto base = std::filesystem::current_path() / "data";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) {
        return std::filesystem::current_path();
    }
    return base;
}

void SyncProfileWithCatalog(Profile& profile, SkillCatalog& catalog) {
    auto skills = profile.list_skills();
    if (skills.empty()) return;
    for (auto& skill : skills) {
        if (auto canonical = catalog.canonical(skill.name)) {
            skill.name = *canonical;
        }
        skill.weight = catalog.weight(skill.name);
    }
    profile.set_skills(skills);
}

