#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Skill.h"

struct Achievement {
    std::string title;
    std::string skill;
    double bonusPercent = 0.0; // percent applied to skill XP
    std::int64_t awardedAt = 0;
    std::int64_t expiresAt = 0; // 0 = no expiry
    std::string icon;

    bool is_active(std::int64_t now) const {
        return expiresAt == 0 || now <= expiresAt;
    }
};

enum class ProfileSpirit {
    None = 0,
    Good = 1,
    Evil = 2
};

ProfileSpirit ProfileSpiritFromString(const std::string& value);
const char* ProfileSpiritId(ProfileSpirit spirit);
const char* ProfileSpiritLabel(ProfileSpirit spirit);
const char* ProfileSpiritEffectLabel(ProfileSpirit spirit);
int ProfileSpiritXpModifierPercent(ProfileSpirit spirit);
int ApplyProfileSpiritXpModifier(ProfileSpirit spirit, int xp);

class Profile {
public:
    inline static constexpr int kCategoryCount = 5;
    inline static constexpr int kMaxCategoryScore = 10;
    inline static constexpr std::array<const char*, kCategoryCount> kCategoryLabels = {"E", "D", "C", "B", "A"};
    inline static constexpr std::array<int, kCategoryCount> kCategoryBaseXp = {500, 800, 1200, 1700, 2300};

    Profile() = default;
    explicit Profile(std::string n);

    void add_skill(const std::string& skillName, int startLevel = 1, double weight = 1.0);
    bool grant_xp(const std::string& skillName, int amount);

    int overall_level() const { return overallLevel_; }
    int total_xp() const { return totalXp_; }
    int level_progress() const { return levelProgress_; }
    int xp_to_next_level() const { return RequiredXpForLevel(overallLevel_) - levelProgress_; }
    const std::string& name() const { return name_; }
    const std::string& login() const { return login_; }
    void set_login(std::string login) { login_ = std::move(login); }
    const std::string& password_encoded() const { return passwordEncoded_; }
    void set_password_encoded(std::string password) { passwordEncoded_ = std::move(password); }

    double wallet_balance() const { return walletBalance_; }
    void set_wallet_balance(double value) { walletBalance_ = value; }
    std::vector<Skill> list_skills() const;
    void set_skills(const std::vector<Skill>& skills);
    void set_total_xp(int totalXp);
    void grant_global_xp(int amount);
    void set_overall_level(int level);
    void set_level_progress(int progress);
    void set_level_and_progress(int level, int progress);

    const std::array<int, kCategoryCount>& category_best_scores() const { return bestCategoryScores_; }
    const std::array<int, kCategoryCount>& category_cooldowns() const { return categoryCooldowns_; }
    int category_best_score(size_t index) const;
    int category_cooldown(size_t index) const;
    void set_category_best_scores(const std::array<int, kCategoryCount>& scores);
    void set_category_cooldowns(const std::array<int, kCategoryCount>& values);
    void update_category_best_score(size_t index, int score);
    void reset_category_cooldown(size_t index);
    void tick_category_cooldown(size_t index);
    bool is_category_mastered(size_t index) const;
    bool all_categories_mastered() const;

    void set_last_task_timestamp(std::int64_t ts) { lastTaskTimestamp_ = ts; }
    std::int64_t last_task_timestamp() const { return lastTaskTimestamp_; }
    void set_inactivity_tasks(int tasks) { inactivityTasks_ = tasks; }
    int inactivity_tasks() const { return inactivityTasks_; }
    int tasks_completed() const { return tasksCompleted_; }
    void set_tasks_completed(int count);
    void increment_tasks_completed(int delta = 1);
    bool is_admin() const { return isAdmin_; }
    void set_admin(bool value) { isAdmin_ = value; }
    bool is_blocked() const { return blocked_; }
    void set_blocked(bool value) { blocked_ = value; }
    const std::string& profession_id() const { return professionId_; }
    void set_profession_id(std::string id) { professionId_ = std::move(id); }
    ProfileSpirit spirit() const { return spirit_; }
    void set_spirit(ProfileSpirit spirit) { spirit_ = spirit; }
    int spirit_xp_modifier_percent() const { return ProfileSpiritXpModifierPercent(spirit_); }
    bool penalty_active() const { return recoveryTasksRemaining_ > 0; }
    bool penalties_enabled() const;
    int recovery_tasks_remaining() const { return recoveryTasksRemaining_; }
    void start_penalty_recovery(int tasks);
    void consume_penalty_task();

    const std::vector<Achievement>& achievements() const { return achievements_; }
    void set_achievements(const std::vector<Achievement>& items) { achievements_ = items; }
    void add_achievement(const Achievement& ach) { achievements_.push_back(ach); }
    void remove_achievement(size_t idx) { if (idx < achievements_.size()) achievements_.erase(achievements_.begin() + idx); }
    double skill_bonus_multiplier(const std::string& skillName, std::int64_t now) const;
private:
    static int RequiredXpForLevel(int level);
    static int TotalXpForLevel(int level);
    void RecalculateLevel();

    std::string name_;
    std::string login_;
    std::string passwordEncoded_;
    std::unordered_map<std::string, Skill> skills_;
    int levelProgress_ = 0;
    int overallLevel_ = 1;
    int totalXp_ = 0;
    bool isAdmin_ = false;
    std::array<int, kCategoryCount> bestCategoryScores_ = {0, 0, 0, 0, 0};
    std::array<int, kCategoryCount> categoryCooldowns_ = {10, 10, 10, 10, 10};
    std::int64_t lastTaskTimestamp_ = 0;
    int inactivityTasks_ = 0;
    int recoveryTasksRemaining_ = 0;
    int tasksCompleted_ = 0;
    std::vector<Achievement> achievements_;
    std::string professionId_;
    ProfileSpirit spirit_ = ProfileSpirit::None;
    bool blocked_ = false;
    double walletBalance_ = 0.0;
};

