// JobSkill console client: profile issuing, leveling, and CLI front-end.
// This translation unit wires together profile storage, skill catalog,
// XP accounting commands, and the interactive loop for one console session.

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <limits>
#include <algorithm>
#include <clocale>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <cctype>
#include <optional>
#include <unordered_set>
#include <sstream>
#include <locale>
#include <cmath>

#include "Profile.h"
#include "IJobStorage.h"
#include "IApiClient.h"
#include "AppUtils.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// Blueprint used to auto-create starter profiles with a predefined skill set.
struct ProfileBlueprint {
    std::string name;
    std::vector<std::string> skills;
};

// Wrapper that keeps the loaded profile and its numeric storage id.
struct ActiveProfile {
    Profile profile;
    std::string id;
};

static const std::vector<ProfileBlueprint> kIssuedProfiles = {};

// Find a template profile by name so new users can inherit skill layouts.
static const ProfileBlueprint* find_blueprint(const std::string& name) {
    for (const auto& bp : kIssuedProfiles) {
        if (bp.name == name) return &bp;
    }
    return nullptr;
}

// Simple helper: profile ids are numeric strings (0001, 0002, ...).
static bool is_profile_id(const std::string& value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Standard trimming utility for CLI parsing.
static std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

// Create an actual Profile instance from a blueprint and align skill weights.
static Profile make_profile_from_blueprint(const ProfileBlueprint& bp, SkillCatalog& catalog) {
    Profile profile(bp.name);
    for (const auto& skill : bp.skills) {
        if (auto canonical = catalog.canonical(skill)) {
            profile.add_skill(*canonical, 1, catalog.weight(*canonical));
        } else {
            profile.add_skill(skill, 1, catalog.weight(skill));
        }
    }
    return profile;
}

struct TaskShare {
    std::string skill;
    int percent = 0;
};

struct TaskDetails {
    int categoryIndex = 0;
    int score = 0;
    std::vector<TaskShare> shares;
};

struct SkillResult {
    std::string name;
    int xp = 0;
    bool leveled = false;
    bool apiOk = true;
};

struct TaskOutcome {
    int categoryIndex = 0;
    int score = 0;
    int basePool = 0;
    int effectiveXp = 0;
    bool repeatPenalty = false;
    bool recoveryPenalty = false;
    bool improvedBest = false;
    int newBestScore = 0;
    bool mastered = false;
    std::vector<std::string> notes;
    std::vector<SkillResult> skillResults;
};

static std::optional<int> prompt_category_index();
static std::optional<int> prompt_task_score();
static std::optional<std::vector<TaskShare>> prompt_skill_distribution(const Profile& profile, const SkillCatalog& catalog);
static std::optional<TaskDetails> prompt_task_details(const Profile& profile, const SkillCatalog& catalog);
static TaskOutcome apply_task_to_profile(const TaskDetails& details, Profile& profile, SkillCatalog& catalog, IApiClient& api);

namespace {

std::string FormatTimestamp(std::int64_t seconds) {
    if (seconds <= 0) return "never";
    std::time_t tt = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
        return "unknown";
    }
    return buffer;
}

} // namespace

static std::optional<int> prompt_category_index() {
    while (true) {
        std::cout << "Категория (E/D/C/B/A или 0-" << (Profile::kCategoryCount - 1)
                  << ", пусто — отмена): ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        line = trim_copy(line);
        if (line.empty()) return std::nullopt;
        const bool digitsOnly = std::all_of(line.begin(), line.end(),
                                            [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (digitsOnly) {
            try {
                int idx = std::stoi(line);
                if (idx >= 0 && idx < Profile::kCategoryCount) return idx;
            } catch (...) {}
        }
        char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(line.front())));
        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
            if (Profile::kCategoryLabels[idx][0] == letter) {
                return static_cast<int>(idx);
            }
        }
        std::cout << "Ошибка. Используйте буквы E-A или индексы 0-"
                  << (Profile::kCategoryCount - 1) << ".\n";
    }
}

static std::optional<int> prompt_task_score() {
    while (true) {
        std::cout << "Оценка (1-" << Profile::kMaxCategoryScore << ", пусто — отмена): ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        line = trim_copy(line);
        if (line.empty()) return std::nullopt;
        try {
            int value = std::stoi(line);
            if (value >= 1 && value <= Profile::kMaxCategoryScore) return value;
        } catch (...) {}
        std::cout << "Оценка должна быть целым числом от 1 до "
                  << Profile::kMaxCategoryScore << ".\n";
    }
}

static std::optional<std::vector<TaskShare>> prompt_skill_distribution(const Profile& profile,
                                                                       const SkillCatalog& catalog) {
    std::cout << "Распределите 100% XP между навыками (формат: Навык=процент).\n";
    std::cout << "Введите 'even' чтобы распределить поровну, или 'cancel' для отмены.\n";
    while (true) {
        std::vector<TaskShare> shares;
        int assigned = 0;
        while (true) {
            std::cout << "Доля (уже распределено " << assigned << "%): ";
            std::string line;
            if (!std::getline(std::cin, line)) {
                return std::nullopt;
            }
            line = trim_copy(line);
            if (line.empty()) break;
            std::string lowered = line;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (lowered == "cancel") {
                return std::nullopt;
            }
            if (lowered == "even" || lowered == "auto" || lowered == "spread") {
                auto skills = profile.list_skills();
                if (skills.empty()) {
                std::cout << "В профиле пока нет навыков. Укажите навыки вручную.\n";
                continue;
            }
            shares.clear();
                const int count = static_cast<int>(skills.size());
                int base = 100 / count;
                int remainder = 100 % count;
                for (int i = 0; i < count; ++i) {
                    int percent = base + (i < remainder ? 1 : 0);
                    if (percent <= 0) continue;
                    std::string skillName = skills[i].name;
                    if (auto canonical = catalog.canonical(skillName)) {
                        skillName = *canonical;
                    }
                    shares.push_back(TaskShare{skillName, percent});
                }
                return shares;
            }
            auto sep = line.find_first_of("=:");
            if (sep == std::string::npos) {
                std::cout << "Use the format Skill=percent (example: Modeling=60).\n";
                continue;
            }
            std::string skillName = trim_copy(line.substr(0, sep));
            std::string percentStr = trim_copy(line.substr(sep + 1));
            if (skillName.empty() || percentStr.empty()) {
                std::cout << "Provide both skill name and percentage.\n";
                continue;
            }
            int percent = 0;
            try {
                percent = std::stoi(percentStr);
            } catch (...) {
                percent = 0;
            }
            if (percent <= 0 || percent > 100) {
                std::cout << "Percent must be between 1 and 100.\n";
                continue;
            }
            if (assigned + percent > 100) {
                std::cout << "Total would exceed 100%. Currently assigned " << assigned << "%.\n";
                continue;
            }
            auto canonical = catalog.canonical(skillName);
            if (!canonical) {
                std::cout << "Skill '" << skillName << "' not found in the catalog. Use 'skills' to list available entries.\n";
                continue;
            }
            bool merged = false;
            for (auto& share : shares) {
                if (share.skill == *canonical) {
                    share.percent += percent;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                shares.push_back(TaskShare{*canonical, percent});
            }
            assigned += percent;
            if (assigned == 100) break;
        }
        if (shares.empty()) {
            std::cout << "Распределение пустое. Попробуйте снова или введите 'cancel'.\n";
            continue;
        }
        if (assigned != 100) {
            std::cout << "Назначено " << assigned << "%. Нужно ровно 100%. Начнём заново.\n";
            continue;
        }
        return shares;
    }
}

static std::optional<TaskDetails> prompt_task_details(const Profile& profile, const SkillCatalog& catalog) {
    std::cout << "=== Запись задачи ===\n";
    auto category = prompt_category_index();
    if (!category) return std::nullopt;
    auto score = prompt_task_score();
    if (!score) return std::nullopt;
    auto shares = prompt_skill_distribution(profile, catalog);
    if (!shares) return std::nullopt;
    TaskDetails details;
    details.categoryIndex = *category;
    details.score = *score;
    details.shares = std::move(*shares);
    return details;
}

static TaskOutcome apply_task_to_profile(const TaskDetails& details, Profile& profile,
                                         SkillCatalog& catalog, IApiClient& api) {
    TaskOutcome outcome;
    outcome.categoryIndex = details.categoryIndex;
    outcome.score = details.score;

    float maxShare = 0.0f;
    for (const auto& share : details.shares) {
        maxShare = std::max(maxShare, static_cast<float>(share.percent) / 100.0f);
    }
    const auto& rules = GetGameplayConfig();
    const int baseXp = rules.categoryBaseXp[details.categoryIndex];
    const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * maxShare;
    int basePool = static_cast<int>(std::round(
        baseXp *
        std::pow(std::max(0.1f, static_cast<float>(details.score) / static_cast<float>(Profile::kMaxCategoryScore)), 1.35f) *
        focusBonus));
    if (basePool < 0) basePool = 0;
    outcome.basePool = basePool;

    std::vector<int> xpDistribution(details.shares.size(), 0);
    int remainder = basePool;
    int fallbackIndex = -1;
    for (size_t i = 0; i < details.shares.size(); ++i) {
        const int percent = details.shares[i].percent;
        int shareXp = (basePool * percent) / 100;
        xpDistribution[i] = shareXp;
        remainder -= shareXp;
        if (fallbackIndex == -1 || percent > details.shares[static_cast<size_t>(fallbackIndex)].percent) {
            fallbackIndex = static_cast<int>(i);
        }
    }
    if (remainder > 0 && fallbackIndex >= 0) {
        xpDistribution[static_cast<size_t>(fallbackIndex)] += remainder;
    }

    outcome.skillResults.reserve(details.shares.size());
    for (size_t i = 0; i < details.shares.size(); ++i) {
        const auto& share = details.shares[i];
        SkillResult result;
        result.name = share.skill;
        result.xp = xpDistribution[i];
        double weight = catalog.weight(share.skill);
        profile.add_skill(share.skill, 1, weight);
        if (result.xp > 0) {
            result.leveled = profile.grant_xp(share.skill, result.xp);
            result.apiOk = api.post_xp(share.skill, result.xp);
        }
        outcome.skillResults.push_back(result);
    }

    const int storedBest = profile.category_best_score(static_cast<size_t>(details.categoryIndex));
    outcome.repeatPenalty = details.score <= storedBest;
    int effectiveXp = basePool;
    if (outcome.repeatPenalty) {
        effectiveXp = static_cast<int>(std::round(effectiveXp * rules.repeatRewardFactor));
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;
    if (profile.last_task_timestamp() > 0 &&
        (nowSeconds - profile.last_task_timestamp()) > kThirtyDays) {
        profile.start_penalty_recovery(rules.recoveryWarmupTasks);
        if (rules.recoveryWarmupTasks > 0) {
        std::ostringstream warmup;
        warmup << "Обнаружена длительная пауза: начат прогрев (" << rules.recoveryWarmupTasks << " задач).";
        outcome.notes.push_back(warmup.str());
        }
    }

    if (profile.penalty_active()) {
        outcome.recoveryPenalty = true;
        effectiveXp = static_cast<int>(std::round(effectiveXp * rules.recoveryRewardFactor));
        int before = profile.recovery_tasks_remaining();
        profile.consume_penalty_task();
        std::ostringstream ss;
        ss << "Активен штраф за простои: осталось " << profile.recovery_tasks_remaining()
           << " задач.";
        if (before > 0) {
            outcome.notes.push_back(ss.str());
        }
    }

    profile.set_last_task_timestamp(nowSeconds);
    if (effectiveXp > 0) {
        profile.grant_global_xp(effectiveXp);
    }
    outcome.effectiveXp = effectiveXp;

    if (details.score > storedBest) {
        outcome.improvedBest = true;
        outcome.newBestScore = details.score;
        profile.update_category_best_score(static_cast<size_t>(details.categoryIndex), details.score);
        std::ostringstream bestStream;
        bestStream << "Категория " << Profile::kCategoryLabels[details.categoryIndex]
                   << " улучшена до " << details.score << "/10.";
        outcome.notes.push_back(bestStream.str());
        if (details.score == Profile::kMaxCategoryScore) {
            outcome.mastered = true;
            std::ostringstream mastery;
            mastery << "Категория " << Profile::kCategoryLabels[details.categoryIndex] << " полностью освоена!";
            outcome.notes.push_back(mastery.str());
        }
    }

    profile.reset_category_cooldown(static_cast<size_t>(details.categoryIndex));
    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
        if (idx == static_cast<size_t>(details.categoryIndex)) continue;
        profile.tick_category_cooldown(idx);
        if (profile.category_cooldown(idx) < 0) {
            int decayedScore = profile.category_best_score(idx) - 1;
            profile.update_category_best_score(idx, decayedScore);
            profile.reset_category_cooldown(idx);
            std::ostringstream decay;
            decay << "Категория " << Profile::kCategoryLabels[idx]
                  << " деградировала до " << profile.category_best_score(idx) << "/10.";
            outcome.notes.push_back(decay.str());
        }
    }
    int buffer = profile.category_cooldown(0);
    for (size_t idx = 1; idx < Profile::kCategoryCount; ++idx) {
        buffer = std::min(buffer, profile.category_cooldown(idx));
    }
    buffer = std::max(0, buffer);
    profile.set_inactivity_tasks(buffer);
    std::ostringstream bufferMsg;
    bufferMsg << "Буфер деградации обновлён: " << buffer << " задач.";
    outcome.notes.push_back(bufferMsg.str());

    return outcome;
}

// Pretty-print the active profile: level, XP, task category scores, skills.
static void print_profile(const Profile& p) {
    std::cout << "=== Профиль: " << p.name() << " ===\n";
    std::cout << "Общий уровень: " << p.overall_level() << " (" << DescribeOverallRank(p) << ")\n";
    std::cout << "Всего XP: " << p.total_xp() << "\n";
    std::cout << "Прогресс до следующего уровня: " << p.level_progress() << "/" << p.xp_to_next_level() << "\n";
    const auto& categories = p.category_best_scores();
    const auto& cooldowns = p.category_cooldowns();
    std::cout << "Категории задач (оценка / кулдаун):\n";
    for (size_t i = 0; i < categories.size(); ++i) {
        std::cout << " - " << Profile::kCategoryLabels[i] << ": "
                  << categories[i] << "/10 ("
                  << cooldowns[i] << ")\n";
    }
    if (p.penalty_active()) {
        std::cout << "Осталось восстановительных задач: " << p.recovery_tasks_remaining() << "\n";
    }
    std::cout << "Буфер деградации (задач до понижения): " << p.inactivity_tasks() << "\n";
    std::cout << "Последняя задача: " << FormatTimestamp(p.last_task_timestamp());
    if (p.last_task_timestamp() > 0) {
        const auto now = std::chrono::system_clock::now();
        const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        const auto delta = nowSeconds - p.last_task_timestamp();
        if (delta > 0) {
            const int days = static_cast<int>(delta / (24 * 3600));
            std::cout << " (" << days << " дн. назад)";
        }
    }
    std::cout << "\n";
    std::cout << "Навыки:\n";
    auto skills = p.list_skills();
    for (const auto& s : skills) {
        std::cout << " - " << std::left << std::setw(14) << s.name
                  << " L" << s.level
                  << " | XP: " << s.xp << "/" << s.xpToNext
                  << " | W: " << std::fixed << std::setprecision(2) << s.weight << std::defaultfloat
                  << "\n";
    }
}

// Dump available skills and their weights so the user knows what exists.
static void show_skill_catalog(const SkillCatalog& catalog) {
    std::cout << "\nКаталог навыков:\n";
    for (const auto& skill : catalog.skills()) {
        std::cout << " - " << skill << " (вес "
                  << std::fixed << std::setprecision(2) << catalog.weight(skill) << std::defaultfloat
                  << ")\n";
    }
}

// Display saved and issued profiles, showing id/name/archived status.
static void show_available_profiles(IJobStorage& storage) {
    auto stored = storage.list_profiles();
    std::unordered_set<std::string> existing;

    std::cout << "\nСохранённые профили:";
    if (stored.empty()) {
        std::cout << "\n - (пока нет)\n";
    } else {
        std::cout << "\n";
        for (const auto& info : stored) {
            existing.insert(info.name);
            std::cout << " - [" << info.id << "] " << info.name;
            std::cout << (info.archived ? " (в архиве)" : " (активен)") << "\n";
        }
    }

    std::cout << "Выданные шаблоны:";
    if (kIssuedProfiles.empty()) {
        std::cout << "\n - (нет)\n";
    } else {
        std::cout << "\n";
        for (const auto& bp : kIssuedProfiles) {
            std::cout << " - " << bp.name;
            if (existing.count(bp.name)) {
                std::cout << " (уже создан)";
            }
            std::cout << "\n";
        }
    }
}

// Resolve user input (id or name) into a profile: load existing, revive archived,
// or create a new profile based on blueprints and synchronized catalog data.
static std::optional<ActiveProfile> acquire_profile(IJobStorage& storage, SkillCatalog& catalog, const std::string& token) {
    if (is_profile_id(token)) {
        if (!storage.set_active_profile(token)) {
            std::cout << "Профиль с таким ID не найден или находится в архиве.\n";
            return std::nullopt;
        }
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            storage.save_profile(*profile);
            return ActiveProfile{*profile, token};
        }
        std::cout << "Данные профиля отсутствуют.\n";
        return std::nullopt;
    }

    auto stored = storage.list_profiles();
    std::vector<IJobStorage::ProfileInfo> matches;
    for (const auto& info : stored) {
        if (info.name == token) matches.push_back(info);
    }

    if (!matches.empty()) {
        std::optional<IJobStorage::ProfileInfo> chosen;
        for (const auto& info : matches) {
            if (!info.archived) { chosen = info; break; }
        }
        if (!chosen) {
            std::cout << "Все профили с именем '" << token << "' находятся в архиве. Используйте restore <id>.\n";
            return std::nullopt;
        }
        if (!storage.set_active_profile(chosen->id)) {
            std::cout << "Не удалось открыть профиль с ID " << chosen->id << ".\n";
            return std::nullopt;
        }
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            storage.save_profile(*profile);
            return ActiveProfile{*profile, chosen->id};
        }
        std::cout << "Не удалось загрузить данные профиля.\n";
        return std::nullopt;
    }

    Profile profile(token);
    if (auto bp = find_blueprint(token)) {
        profile = make_profile_from_blueprint(*bp, catalog);
    } else {
        std::string confirm;
        std::cout << "Профиль '" << token << "' не найден. Создать новый? (yes/no): ";
        if (!(std::cin >> confirm) || confirm != "yes") {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Создание отменено.\n";
            return std::nullopt;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (auto skill = catalog.canonical("Modeling")) profile.add_skill(*skill, 1, catalog.weight(*skill));
        if (auto skill = catalog.canonical("Texturing")) profile.add_skill(*skill, 1, catalog.weight(*skill));
    }

    SyncProfileWithCatalog(profile, catalog);
    auto info = storage.create_profile(profile);
    if (!info) {
        std::cout << "Не удалось создать профиль.\n";
        return std::nullopt;
    }
    storage.save_profile(profile);
    std::cout << "Создан профиль '" << profile.name() << "' с ID " << info->id << ".\n";
    return ActiveProfile{profile, info->id};
}

// Command loop for a single logged-in profile: handles addxp/show/sync/logout/quit.
static bool run_profile_session(Profile& profile, const std::string& profileId, IJobStorage& storage, IApiClient& api, SkillCatalog& catalog) {
    auto sync_now = [&](bool verbose = true) {
        bool ok = storage.save_profile(profile);
        if (!ok) {
            if (verbose) std::cout << "Внимание: не удалось сохранить профиль локально.\n";
        } else if (verbose) {
            std::cout << "Синхронизация завершена.\n";
        }
        return ok;
    };

    sync_now(false);

    std::cout << "\nВы вошли как " << profile.name() << " [" << profileId << "]." << std::endl;
    print_profile(profile);
    std::cout << "\nКоманды: addxp <skill> <amount> | task | show | sync | logout | quit\n";
    if (profile.penalty_active()) {
        std::cout << "Внимание: действует штраф за простои (" << profile.recovery_tasks_remaining()
                  << " задач).\n";
    }

    std::string cmd;
    while (true) {
        std::cout << "> ";
        if (!(std::cin >> cmd)) return true; // EOF => exit app
        if (cmd == "show") {
            print_profile(profile);
            continue;
        }
        if (cmd == "sync") {
            sync_now();
            continue;
        }
        if (cmd == "logout") {
            sync_now(false);
            std::cout << "Вы вышли из профиля.\n";
            return false;
        }
        if (cmd == "quit" || cmd == "exit") {
            sync_now(false);
            return true;
        }
        if (cmd == "task") {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            auto details = prompt_task_details(profile, catalog);
            if (!details) {
                std::cout << "Запись отменена.\n";
                continue;
            }
            TaskOutcome outcome = apply_task_to_profile(*details, profile, catalog, api);
            const char* categoryLabel = Profile::kCategoryLabels[outcome.categoryIndex];
            std::cout << "Задача [" << categoryLabel << "] с оценкой "
                      << outcome.score << " => +" << outcome.effectiveXp
                      << " глобального XP (база " << outcome.basePool << ").\n";
            const auto& rules = GetGameplayConfig();
            if (outcome.repeatPenalty) {
                std::cout << "Повторная оценка: глобальный XP ограничен "
                          << static_cast<int>(rules.repeatRewardFactor * 100.0f + 0.5f) << "%.\n";
            }
            if (outcome.recoveryPenalty) {
                std::cout << "Работает штраф за простои ("
                          << static_cast<int>(rules.recoveryRewardFactor * 100.0f + 0.5f)
                          << "% глобального XP до завершения прогрева).\n";
            }
            std::cout << "Распределение XP по навыкам:\n";
            bool allApiOk = true;
            for (const auto& result : outcome.skillResults) {
                std::cout << " - " << result.name << ": +" << result.xp << " XP";
                if (result.leveled) std::cout << " (уровень вверх)";
                if (!result.apiOk) {
                    std::cout << " [ошибка синхронизации с сервером]";
                    allApiOk = false;
                }
                std::cout << "\n";
            }
            for (const auto& note : outcome.notes) {
                std::cout << " * " << note << "\n";
            }
            if (!allApiOk) {
                std::cout << "Внимание: не удалось отправить часть навыков на сервер.\n";
            }
            bool synced = sync_now(false);
            if (synced) std::cout << "Автосинхронизация успешна.\n";
            else std::cout << "Автосинхронизация не удалась. Попробуйте 'sync'.\n";
            continue;
        }
        if (cmd == "addxp") {
            std::string tail;
            if (!std::getline(std::cin >> std::ws, tail)) {
                return true; // EOF or stream error => exit app
            }
            tail = trim_copy(std::move(tail));
            if (tail.empty()) {
                std::cout << "Использование: addxp <skill> <amount>\n";
                continue;
            }

            auto lastNonWhitespace = tail.find_last_not_of(" \t\r\n");
            if (lastNonWhitespace == std::string::npos) {
                std::cout << "Использование: addxp <skill> <amount>\n";
                continue;
            }
            tail.erase(lastNonWhitespace + 1);

            auto split = tail.find_last_of(" \t");
            if (split == std::string::npos) {
                std::cout << "Использование: addxp <skill> <amount>\n";
                continue;
            }

            std::string amountStr = trim_copy(tail.substr(split + 1));
            std::string skill = trim_copy(tail.substr(0, split));
            if (skill.empty() || amountStr.empty()) {
                std::cout << "Использование: addxp <skill> <amount>\n";
                continue;
            }

            int amount = 0;
            try {
                size_t consumed = 0;
                amount = std::stoi(amountStr, &consumed);
                if (consumed != amountStr.size()) throw std::invalid_argument("trailing");
            } catch (...) {
                std::cout << "Количество должно быть положительным целым.\n";
                continue;
            }
            if (amount <= 0) {
                std::cout << "Количество должно быть больше нуля.\n";
                continue;
            }

            auto canonical = catalog.canonical(skill);
            if (!canonical) {
                std::cout << "Навыка нет в каталоге. Сначала добавьте его в каталоге.\n";
                continue;
            }
            const std::string skillName = *canonical;
            const double skillWeight = catalog.weight(skillName);
            profile.add_skill(skillName, 1, skillWeight);
            const bool leveled = profile.grant_xp(skillName, amount);
            profile.grant_global_xp(amount);
            const bool apiOk = api.post_xp(skillName, amount);
            const bool synced = sync_now(false);
            std::cout << (leveled ? "Новый уровень навыка!" : "XP добавлены.") << "\n";
            if (!apiOk) std::cout << "Внимание: не удалось уведомить сервер.\n";
            if (synced) std::cout << "Автосинхронизация успешна.\n";
            else std::cout << "Автосинхронизация не удалась. Попробуйте 'sync'.\n";
            continue;
        }
        std::cout << "Неизвестная команда. Доступно: addxp / task / show / sync / logout / quit\n";
    }
}

constexpr const char* kAdminPassword = "admin123";

namespace {

// Ensure wide/platform console streams speak UTF-8 and pick up system locale.
void ConfigureLocale() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "");
    try {
        std::locale systemLocale("");
        std::locale::global(systemLocale);
        std::cout.imbue(systemLocale);
        std::cin.imbue(systemLocale);
        std::cerr.imbue(systemLocale);
        std::clog.imbue(systemLocale);
        std::wcout.imbue(systemLocale);
        std::wcin.imbue(systemLocale);
        std::wcerr.imbue(systemLocale);
    } catch (...) {
#if defined(_WIN32)
        try {
            std::locale utf8Locale(".UTF-8");
            std::locale::global(utf8Locale);
            std::cout.imbue(utf8Locale);
            std::cin.imbue(utf8Locale);
            std::cerr.imbue(utf8Locale);
            std::clog.imbue(utf8Locale);
            std::wcout.imbue(utf8Locale);
            std::wcin.imbue(utf8Locale);
            std::wcerr.imbue(utf8Locale);
        } catch (...) {
            // keep classic locale
        }
#endif
    }
}

} // namespace

// Entry point: configure locale, bootstrap storage/api/catalog, then run menu loop.
int main() {
    ConfigureLocale();

    extern IJobStorage* CreateFakeStorage();
    extern IJobStorage* CreateFileStorage(const std::filesystem::path& dir);
    extern IApiClient* CreateFakeApi();

    auto storageDir = ResolveStorageDirectory();
    auto gameplayConfig = LoadGameplayConfig(storageDir);
    SetGameplayConfig(gameplayConfig);
    SkillCatalog catalog(storageDir);

    std::unique_ptr<IJobStorage> storage(CreateFileStorage(storageDir));
    EnsureAdminProfile(*storage, catalog);

    std::unique_ptr<IApiClient> api(CreateFakeApi());

    std::cout << "Добро пожаловать в JobSkill";
#ifdef APP_VERSION
    std::cout << " (версия " << APP_VERSION << ")";
#endif
    std::cout << "!\n";

    bool exitApp = false;
    while (!exitApp) {
        std::cout << "\n=== Главное меню ===\n";
        std::cout << "Команды: list | skills | archive <id> | restore <id> | delete <id> | login <id|name> | help | quit\n> ";
        std::string menuCmd;
        if (!(std::cin >> menuCmd)) break;

        if (menuCmd == "quit" || menuCmd == "exit") {
            exitApp = true;
            break;
        }

        if (menuCmd == "help") {
            std::cout << "list — показать профили.\n"
                         "skills — вывести каталог навыков.\n"
                         "archive <id> / restore <id> — архивировать или восстановить профиль.\n"
                         "delete <id> — удалить профиль навсегда.\n"
                         "login <id|name> — войти в профиль по ID или имени.\n"
                         "quit — завершить работу приложения.\n";
            continue;
        }

        if (menuCmd == "list") {
            show_available_profiles(*storage);
            continue;
        }

        if (menuCmd == "skills") {
            show_skill_catalog(catalog);
            continue;
        }


        if (menuCmd == "archive" || menuCmd == "restore" || menuCmd == "delete") {
            std::string profileId;
            if (!(std::cin >> profileId)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Некорректный ввод.\n";
                continue;
            }

            if (!is_profile_id(profileId)) {
                std::cout << "Используйте числовые ID (команда 'list').\n";
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }

            if (menuCmd == "archive") {
                if (storage->set_archived(profileId, true)) {
                    std::cout << "Профиль отправлен в архив.\n";
                } else {
                    std::cout << "Не удалось архивировать профиль.\n";
                }
                continue;
            }

            if (menuCmd == "restore") {
                if (storage->set_archived(profileId, false)) {
                    std::cout << "Профиль восстановлен.\n";
                } else {
                    std::cout << "Не удалось восстановить профиль.\n";
                }
                continue;
            }

            // delete
            std::string confirm;
            std::cout << "Введите 'yes' для удаления профиля " << profileId << ": ";
            std::cin >> confirm;
            if (confirm == "yes" && storage->delete_profile(profileId)) {
                std::cout << "Профиль удалён.\n";
            } else {
                std::cout << "Удаление отменено или не удалось.\n";
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }

        if (menuCmd == "login") {
            std::string token;
            if (!(std::cin >> token)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Некорректный ввод.\n";
                continue;
            }

            auto activeProfile = acquire_profile(*storage, catalog, token);
            if (!activeProfile) {
                continue;
            }

            auto authToken = api->login(activeProfile->profile.name(), "default");
            if (authToken) {
                storage->save_token(*authToken);
                api->set_token(*authToken);
            } else {
                std::cout << "Внимание: не удалось авторизоваться на сервере.\n";
            }

            bool requestedExit = run_profile_session(activeProfile->profile, activeProfile->id, *storage, *api, catalog);
            if (requestedExit) {
                exitApp = true;
            }
            continue;
        }

        std::cout << "Неизвестная команда. Используйте: list / login / help / quit\n";
    }

    std::cout << "До скорого!\n";
    return 0;
}



