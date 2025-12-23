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

std::string DescribeOverallRank(const Profile& profile) {
    const int overallLevel = profile.overall_level();
    std::string base;
    if (overallLevel < 10) base = "Стажёр";
    else if (overallLevel < 50) base = BuildRank("Джуниор", 10, 10, overallLevel);
    else if (overallLevel < 150) base = BuildRank("Мидл", 50, 10, overallLevel);
    else base = BuildRank("Сеньор", 150, 10, overallLevel);

    std::ostringstream details;
    bool hasDetail = false;
    if (overallLevel >= 150 && !profile.all_categories_mastered()) {
        details << "Сеньор заблокирован: ";
        bool first = true;
        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
            if (profile.is_category_mastered(idx)) continue;
            if (!first) details << ", ";
            details << Profile::kCategoryLabels[idx] << "=" << profile.category_best_score(idx) << "/10";
            const int cooldown = profile.category_cooldown(idx);
            if (cooldown <= 0) details << " (!)";
            else details << " (кулдаун " << cooldown << ")";
            first = false;
        }
        if (first) details << "закончите испытания категорий";
        hasDetail = true;
    }
    if (profile.penalty_active()) {
        if (hasDetail) details << " | ";
        details << "штрафных задач осталось: " << profile.recovery_tasks_remaining();
        hasDetail = true;
    }
    if (profile.inactivity_tasks() > 0) {
        if (hasDetail) details << " | ";
        details << "буфер деградации: " << profile.inactivity_tasks() << " задач";
        hasDetail = true;
    }
    if (hasDetail) {
        return base + " (" + details.str() + ")";
    }
    return base;
}

void EnsureAdminProfile(IJobStorage& storage, SkillCatalog& catalog) {
    constexpr const char* kAdminName = "Admin";
    auto stored = storage.list_profiles();
    bool exists = false;
    for (const auto& info : stored) {
        if (info.name == kAdminName && !info.archived) {
            exists = true;
            // Ensure admin flag is set on the stored profile.
            if (storage.set_active_profile(info.id)) {
                if (auto prof = storage.load_profile()) {
                    if (!prof->is_admin()) {
                        prof->set_admin(true);
                        storage.save_profile(*prof);
                    }
                }
            }
            break;
        }
    }
    if (exists) return;

    Profile admin(kAdminName);
    if (auto skill = catalog.id_for_name("Modeling")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    }
    if (auto skill = catalog.id_for_name("Lighting")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    }
    if (auto skill = catalog.id_for_name("Materials")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    }

    SyncProfileWithCatalog(admin, catalog);
    admin.set_admin(true);

    auto info = storage.create_profile(admin);
    if (info) {
        storage.save_profile(admin);
        std::cout << "Создан профиль администратора '" << admin.name() << "' с ID " << info->id << ".\n";
    } else {
        std::cout << "Внимание: не удалось создать профиль администратора.\n";
    }
}

namespace {

std::filesystem::path GuessProjectRoot() {
    std::error_code ec;
    auto path = std::filesystem::current_path();
    while (!path.empty()) {
        const bool hasMain = std::filesystem::exists(path / "main.cpp", ec);
        const bool hasCMake = std::filesystem::exists(path / "CMakeLists.txt", ec);
        if (!ec && hasMain && hasCMake) {
            return path;
        }
        auto parent = path.parent_path();
        if (parent == path) break;
        path = std::move(parent);
    }
    return std::filesystem::current_path();
}

} // namespace

std::filesystem::path ResolveStorageDirectory() {
    std::error_code ec;
    auto root = GuessProjectRoot();
    auto dataDir = root / "data";
    std::filesystem::create_directories(dataDir, ec);
    if (!ec) {
        return dataDir;
    }

    // Fallback to legacy location near the executable.
    auto fallback = std::filesystem::current_path() / "data";
    std::filesystem::create_directories(fallback, ec);
    if (!ec) {
        return fallback;
    }
    return std::filesystem::current_path();
}

void SyncProfileWithCatalog(Profile& profile, SkillCatalog& catalog) {
    auto skills = profile.list_skills();
    if (skills.empty()) return;
    bool changed = false;
    for (auto& skill : skills) {
        if (!catalog.contains_id(skill.name)) {
            if (auto id = catalog.id_for_name(skill.name)) {
                skill.name = *id;
                changed = true;
            }
        }
        skill.weight = catalog.weight(skill.name);
    }

    auto ach = profile.achievements();
    bool achChanged = false;
    for (auto& a : ach) {
        if (!catalog.contains_id(a.skill)) {
            if (auto id = catalog.id_for_name(a.skill)) {
                a.skill = *id;
                achChanged = true;
            }
        }
    }
    if (achChanged) {
        profile.set_achievements(ach);
    }
    if (changed || achChanged) {
        profile.set_skills(skills);
    }
}

