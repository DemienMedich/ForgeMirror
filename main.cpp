// ForgeMirror console client: profile issuing, leveling, and CLI front-end.
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
#include "CloudSync.h"

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

static std::string to_lower_ascii(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

static std::vector<std::string> split_command_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            char next = line[i + 1];
            if (next == '"' || next == '\\') {
                current.push_back(next);
                ++i;
                continue;
            }
        }
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (!in_quotes && std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

static std::string join_tokens(const std::vector<std::string>& tokens, size_t start, size_t end) {
    if (start >= end || start >= tokens.size()) return {};
    end = std::min(end, tokens.size());
    std::string out = tokens[start];
    for (size_t i = start + 1; i < end; ++i) {
        out += ' ';
        out += tokens[i];
    }
    return out;
}

// Create an actual Profile instance from a blueprint and align skill weights.
static Profile make_profile_from_blueprint(const ProfileBlueprint& bp, SkillCatalog& catalog) {
    Profile profile(bp.name);
    for (const auto& skill : bp.skills) {
        if (auto id = catalog.id_for_name(skill)) {
            profile.add_skill(*id, 1, catalog.weight(*id));
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
    int baseXp = 0;
    int xp = 0;
    double bonusPercent = 0.0;
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
    if (seconds <= 0) return "нет данных";
    std::time_t tt = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
        return "неизвестно";
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
    std::cout << "Введите 'равномерно' (even) чтобы распределить поровну, или 'отмена' (cancel) для отмены.\n";
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
            if (lowered == "cancel" || line == u8"отмена") {
                return std::nullopt;
            }
            if (lowered == "even" || lowered == "auto" || lowered == "spread" ||
                line == u8"равномерно" || line == u8"поровну") {
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
                    shares.push_back(TaskShare{skills[i].name, percent});
                }
                return shares;
            }
            auto sep = line.find_first_of("=:");
            if (sep == std::string::npos) {
                std::cout << "Используйте формат Навык=процент (пример: Modeling=60).\n";
                continue;
            }
            std::string skillName = trim_copy(line.substr(0, sep));
            std::string percentStr = trim_copy(line.substr(sep + 1));
            if (skillName.empty() || percentStr.empty()) {
                std::cout << "Укажите и навык, и процент.\n";
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
            std::optional<std::string> skillId;
            if (catalog.contains_id(skillName)) {
                skillId = skillName;
            } else {
                skillId = catalog.id_for_name(skillName);
            }
            if (!skillId) {
                std::cout << "Skill '" << skillName << "' not found in the catalog. Use 'skills' to list available entries.\n";
                continue;
            }
            bool merged = false;
            for (auto& share : shares) {
                if (share.skill == *skillId) {
                    share.percent += percent;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                shares.push_back(TaskShare{*skillId, percent});
            }
            assigned += percent;
            if (assigned == 100) break;
        }
        if (shares.empty()) {
            std::cout << "Распределение пустое. Попробуйте снова или введите 'отмена' (cancel).\n";
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
    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

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
        result.name = catalog.display_name(share.skill);
        result.baseXp = xpDistribution[i];
        result.xp = result.baseXp;
        double weight = catalog.weight(share.skill);
        profile.add_skill(share.skill, 1, weight);
        double mult = profile.skill_bonus_multiplier(share.skill, nowSeconds);
        result.bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
        if (result.baseXp > 0) {
            int finalXp = result.baseXp;
            if (result.bonusPercent > 0.01) {
                finalXp = static_cast<int>(std::round(static_cast<double>(result.baseXp) * mult));
            }
            result.xp = finalXp;
            result.leveled = profile.grant_xp(share.skill, finalXp);
            result.apiOk = api.post_xp(share.skill, finalXp);
        }
    outcome.skillResults.push_back(result);
}

    const int storedBest = profile.category_best_score(static_cast<size_t>(details.categoryIndex));
    const bool penaltiesEnabled = profile.penalties_enabled();
    outcome.repeatPenalty = penaltiesEnabled && details.score <= storedBest;
    int effectiveXp = basePool;
    if (outcome.repeatPenalty) {
        effectiveXp = static_cast<int>(std::round(effectiveXp * rules.repeatRewardFactor));
    }

    constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;
    if (penaltiesEnabled && profile.last_task_timestamp() > 0 &&
        (nowSeconds - profile.last_task_timestamp()) > kThirtyDays) {
        profile.start_penalty_recovery(rules.recoveryWarmupTasks);
        if (rules.recoveryWarmupTasks > 0) {
        std::ostringstream warmup;
        warmup << "Обнаружена длительная пауза: начат прогрев (" << rules.recoveryWarmupTasks << " задач).";
        outcome.notes.push_back(warmup.str());
        }
    }

    if (!penaltiesEnabled && profile.penalty_active()) {
        profile.start_penalty_recovery(0);
    }
    if (penaltiesEnabled && profile.penalty_active()) {
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
    profile.increment_tasks_completed();
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

    constexpr bool kDecayEnabled = false;
    if (kDecayEnabled) {
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
    }

    return outcome;
}

// Pretty-print the active profile: level, XP, task category scores, skills.
static void print_profile(const Profile& p, const SkillCatalog& catalog) {
    std::cout << "=== Профиль: " << p.name() << " ===\n";
    std::cout << "Общий уровень: " << p.overall_level() << " (" << DescribeOverallRank(p) << ")\n";
    std::cout << "Всего XP: " << p.total_xp() << "\n";
    std::cout << "Прогресс до следующего уровня: " << p.level_progress() << "/" << p.xp_to_next_level() << "\n";
    std::cout << "Выполнено задач: " << p.tasks_completed() << "\n";
    const auto& categories = p.category_best_scores();
    const auto& cooldowns = p.category_cooldowns();
    std::cout << "Категории задач (оценка / кулдаун):\n";
    for (size_t i = 0; i < categories.size(); ++i) {
        std::cout << " - " << Profile::kCategoryLabels[i] << ": "
                  << categories[i] << "/10 ("
                  << cooldowns[i] << ")\n";
    }
    if (p.penalties_enabled() && p.penalty_active()) {
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
        const std::string displayName = catalog.display_name(s.name);
        std::cout << " - " << std::left << std::setw(14) << displayName
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
        std::cout << " - " << catalog.display_name(skill) << " (вес "
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
static std::optional<ActiveProfile> acquire_profile(IJobStorage& storage, SkillCatalog& catalog, const std::string& token, bool allowCreation) {
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

    if (!allowCreation) {
        std::cout << "Создание профиля доступно только администратору.\n";
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
        if (auto skill = catalog.id_for_name("Modeling")) profile.add_skill(*skill, 1, catalog.weight(*skill));
        if (auto skill = catalog.id_for_name("Texturing")) profile.add_skill(*skill, 1, catalog.weight(*skill));
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
static bool run_profile_session(Profile& profile, const std::string& profileId, IJobStorage& storage, IApiClient& api,
                                SkillCatalog& catalog, bool adminMode,
                                const std::filesystem::path& storageDir, const CloudSyncConfig& cloudConfig) {
    auto sync_now = [&](bool verbose = true) {
        bool ok = storage.save_profile(profile);
        if (!ok) {
            if (verbose) std::cout << "Внимание: не удалось сохранить профиль локально.\n";
        } else if (verbose) {
            std::cout << "Синхронизация завершена.\n";
        }
        if (ok && adminMode && cloudConfig.enabled && cloudConfig.autoPush) {
            CloudSyncResult cloudResult = PushCloudSnapshot(cloudConfig, storageDir, CloudRole::Admin);
            if (verbose && !cloudResult.message.empty()) {
                std::cout << cloudResult.message << "\n";
            }
        }
        return ok;
    };

    sync_now(false);

    std::cout << "\nВы вошли как " << profile.name() << " [" << profileId << "]." << (adminMode ? " (Администратор)" : " (Просмотр)") << std::endl;
    print_profile(profile, catalog);
    if (adminMode) {
        std::cout << "\nКоманды: addxp \"Навык\" <количество> | task | show | sync | logout | quit\n";
        if (profile.penalties_enabled() && profile.penalty_active()) {
            std::cout << "Внимание: действует штраф за простои (" << profile.recovery_tasks_remaining()
                      << " задач).\n";
        }
    } else {
        std::cout << "\nРежим просмотра: доступны команды show | logout | quit\n";
    }

    while (true) {
        std::cout << "> ";
        std::string line;
        if (!std::getline(std::cin >> std::ws, line)) return true; // EOF => exit app
        auto tokens = split_command_line(line);
        if (tokens.empty()) continue;
        std::string cmd = to_lower_ascii(tokens[0]);
        if (cmd == "show") {
            print_profile(profile, catalog);
            continue;
        }
        if (cmd == "sync") {
            if (!adminMode) {
                std::cout << "Недоступно в режиме просмотра.\n";
                continue;
            }
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
            if (!adminMode) {
                std::cout << "Недоступно в режиме просмотра.\n";
                continue;
            }
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
                if (result.bonusPercent > 0.01) {
                    std::cout << " (+" << std::fixed << std::setprecision(1) << result.bonusPercent << "%)" << std::defaultfloat;
                }
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
            if (!adminMode) {
                std::cout << "Недоступно в режиме просмотра.\n";
                continue;
            }
            if (tokens.size() < 3) {
                std::cout << "Использование: addxp \"Навык\" <количество>\n";
                continue;
            }

            std::string amountStr = tokens.back();
            std::string skill = join_tokens(tokens, 1, tokens.size() - 1);
            if (skill.empty() || amountStr.empty()) {
                std::cout << "Использование: addxp \"Навык\" <количество>\n";
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

            std::optional<std::string> skillId;
            if (catalog.contains_id(skill)) {
                skillId = skill;
            } else {
                skillId = catalog.id_for_name(skill);
            }
            if (!skillId) {
                std::cout << "Навыка нет в каталоге. Сначала добавьте его в каталоге.\n";
                continue;
            }
            const double skillWeight = catalog.weight(*skillId);
            profile.add_skill(*skillId, 1, skillWeight);
            const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const double mult = profile.skill_bonus_multiplier(*skillId, nowSeconds);
            const double bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
            int finalAmount = amount;
            if (bonusPercent > 0.01) {
                finalAmount = static_cast<int>(std::round(static_cast<double>(amount) * mult));
            }
            const bool leveled = profile.grant_xp(*skillId, finalAmount);
            profile.grant_global_xp(amount);
            const bool apiOk = api.post_xp(*skillId, finalAmount);
            const bool synced = sync_now(false);
            if (bonusPercent > 0.01) {
                std::cout << "Бонус ачивок: +" << std::fixed << std::setprecision(1) << bonusPercent << "%, итог " << finalAmount << " XP.\n";
                std::cout << std::defaultfloat;
            }
            std::cout << (leveled ? "Новый уровень навыка!" : "XP добавлены.") << "\n";
            if (!apiOk) std::cout << "Внимание: не удалось уведомить сервер.\n";
            if (synced) std::cout << "Автосинхронизация успешна.\n";
            else std::cout << "Автосинхронизация не удалась. Попробуйте 'sync'.\n";
            continue;
        }
        std::cout << "Неизвестная команда. Доступно: addxp / task / show / sync / logout / quit\n";
    }
}

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
    CloudSyncConfig cloudConfig = LoadCloudSyncConfig(storageDir);
    if (cloudConfig.enabled && cloudConfig.autoPull) {
        PullCloudSnapshot(cloudConfig, storageDir, CloudRole::Viewer);
    }
    auto gameplayConfig = LoadGameplayConfig(storageDir);
    SetGameplayConfig(gameplayConfig);
    SkillCatalog catalog(storageDir);
    std::string adminPassword = LoadAdminPassword(storageDir);

    std::unique_ptr<IJobStorage> storage(CreateFileStorage(storageDir));
    EnsureAdminProfile(*storage, catalog);

    std::unique_ptr<IApiClient> api(CreateFakeApi());

    std::cout << "Добро пожаловать в ForgeMirror";
#ifdef APP_VERSION
    std::cout << " (версия " << APP_VERSION << ")";
#endif
    std::cout << "!\n";
    if (cloudConfig.enabled) {
        CloudManifest manifest = LoadCloudManifest(cloudConfig, storageDir);
        if (!manifest.appVersion.empty()) {
            std::cout << "Облако: версия " << manifest.appVersion;
            if (manifest.dataUpdatedAt > 0) {
                std::cout << ", данные обновлены " << FormatTimestamp(manifest.dataUpdatedAt);
            }
            std::cout << ".\n";
            if (IsUpdateAvailable(manifest, APP_VERSION)) {
                std::cout << "Доступно обновление клиента через облако.\n";
            }
        }
    }

    bool adminAuthed = false;
    bool exitApp = false;
    while (!exitApp) {
        std::cout << "\n=== Главное меню ===\n";
        std::cout << (adminAuthed ? "[Режим администратора]\n" : "[Режим просмотра]\n");
        std::cout << "Команды: list | skills | login <id|name> | admin | help | quit";
        if (adminAuthed) std::cout << " | archive <id> | restore <id> | delete <id> | passwd";
        std::cout << "\n> ";
        std::string menuLine;
        if (!std::getline(std::cin >> std::ws, menuLine)) break;
        auto menuTokens = split_command_line(menuLine);
        if (menuTokens.empty()) continue;
        std::string menuCmd = to_lower_ascii(menuTokens[0]);

        if (menuCmd == "quit" || menuCmd == "exit") {
            exitApp = true;
            break;
        }

        if (menuCmd == "help") {
            std::cout << "list - показать профили.\n"
                         "skills - вывести каталог навыков.\n"
                         "login <id|name> - открыть профиль (модификация только при активном администраторе).\n"
                         "admin - вход/выход из режима администратора.\n";
            if (adminAuthed) {
                std::cout << "archive <id> / restore <id> - архивировать или восстановить профиль.\n"
                             "delete <id> - удалить профиль навсегда.\n"
                             "passwd - сменить пароль администратора.\n";
            }
            std::cout << "quit - завершить работу приложения.\n"
                         "Подсказка: если имя содержит пробелы, используйте кавычки (login \"Имя профиля\").\n";
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

        if (menuCmd == "admin") {
            if (adminAuthed) {
                adminAuthed = false;
                std::cout << "Режим администратора отключён.\n";
                continue;
            }
            std::string password;
            if (menuTokens.size() >= 2) {
                password = menuTokens[1];
            } else {
                std::cout << "Введите пароль администратора: ";
                std::getline(std::cin >> std::ws, password);
            }
            if (password == adminPassword) {
                adminAuthed = true;
                std::cout << "Режим администратора активирован.\n";
            } else {
                std::cout << "Неверный пароль.\n";
            }
            continue;
        }

        if (menuCmd == "passwd" || menuCmd == "adminpass") {
            if (!adminAuthed) {
                std::cout << "Недоступно: требуется режим администратора (команда 'admin').\n";
                continue;
            }
            const char* envLabel = "FORGEMIRROR_ADMIN_PASSWORD";
            const char* env = std::getenv(envLabel);
            if (!env || !*env) {
                envLabel = "JOBSKILL_ADMIN_PASSWORD";
                env = std::getenv(envLabel);
            }
            if (env && *env) {
                std::cout << "Пароль задан через " << envLabel << "; изменение из приложения недоступно.\n";
                continue;
            }
            std::string pass1;
            std::string pass2;
            std::cout << "Новый пароль администратора: ";
            std::getline(std::cin >> std::ws, pass1);
            std::cout << "Повторите пароль: ";
            std::getline(std::cin >> std::ws, pass2);
            if (pass1.empty()) {
                std::cout << "Пароль не может быть пустым.\n";
                continue;
            }
            if (pass1 != pass2) {
                std::cout << "Пароли не совпадают.\n";
                continue;
            }
            if (!SetAdminPassword(storageDir, pass1)) {
                std::cout << "Не удалось сохранить пароль.\n";
                continue;
            }
            adminPassword = pass1;
            std::cout << "Пароль администратора обновлён.\n";
            continue;
        }


        if (menuCmd == "archive" || menuCmd == "restore" || menuCmd == "delete") {
            if (!adminAuthed) {
                std::cout << "Недоступно: требуется режим администратора (команда 'admin').\n";
                continue;
            }
            std::string profileId;
            if (menuTokens.size() >= 2) profileId = menuTokens[1];
            if (profileId.empty()) {
                std::cout << "Использование: " << menuCmd << " <id>\n";
                continue;
            }

            if (!is_profile_id(profileId)) {
                std::cout << "Используйте числовые ID (команда 'list').\n";
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
            std::getline(std::cin >> std::ws, confirm);
            if (confirm == "yes" && storage->delete_profile(profileId)) {
                std::cout << "Профиль удалён.\n";
            } else {
                std::cout << "Удаление отменено или не удалось.\n";
            }
            continue;
        }

        if (menuCmd == "login") {
            std::string token;
            if (menuTokens.size() >= 2) {
                token = join_tokens(menuTokens, 1, menuTokens.size());
            }
            if (token.empty()) {
                std::cout << "Использование: login <id|name>\n";
                continue;
            }

            auto activeProfile = acquire_profile(*storage, catalog, token, adminAuthed);
            if (!activeProfile) {
                continue;
            }
            const bool adminMode = adminAuthed && activeProfile->profile.is_admin();

            auto authToken = api->login(activeProfile->profile.name(), "default");
            if (authToken) {
                storage->save_token(*authToken);
                api->set_token(*authToken);
            } else {
                std::cout << "Внимание: не удалось авторизоваться на сервере.\n";
            }

            bool requestedExit = run_profile_session(activeProfile->profile, activeProfile->id, *storage, *api, catalog,
                                                     adminMode, storageDir, cloudConfig);
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



