#include "Profile.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

int ClampScore(int value) {
    if (value < 0) return 0;
    if (value > Profile::kMaxCategoryScore) return Profile::kMaxCategoryScore;
    return value;
}

} // namespace

Profile::Profile(std::string n) : name_(std::move(n)) {}

void Profile::add_skill(const std::string& skillName, int startLevel, double weight) {
    auto it = skills_.find(skillName);
    if (it == skills_.end()) {
        skills_.emplace(skillName, Skill(skillName, startLevel, weight));
        return;
    }
    if (startLevel > it->second.level) it->second.level = startLevel;
    if (weight > 0.0) it->second.weight = weight;
}

bool Profile::grant_xp(const std::string& skillName, int amount) {
    auto it = skills_.find(skillName);
    if (it == skills_.end()) return false;
    return it->second.add_xp(amount);
}

std::vector<Skill> Profile::list_skills() const {
    std::vector<Skill> out;
    out.reserve(skills_.size());
    for (const auto& kv : skills_) {
        out.push_back(kv.second);
    }
    return out;
}

void Profile::set_skills(const std::vector<Skill>& skills) {
    skills_.clear();
    for (const auto& s : skills) {
        skills_.emplace(s.name, s);
    }
}

int Profile::RequiredXpForLevel(int level) {
    if (level <= 1) return 1500;
    const int tier = level - 1;
    return 1500 + 250 * tier + 50 * tier * tier;
}

int Profile::TotalXpForLevel(int level) {
    if (level <= 1) return 0;
    int acc = 0;
    for (int i = 1; i < level; ++i) {
        acc += RequiredXpForLevel(i);
    }
    return acc;
}

void Profile::RecalculateLevel() {
    totalXp_ = std::max(0, totalXp_);
    int candidateLevel = 1;
    int remaining = totalXp_;
    while (true) {
        const int need = RequiredXpForLevel(candidateLevel);
        if (remaining < need) {
            overallLevel_ = candidateLevel;
            levelProgress_ = remaining;
            return;
        }
        remaining -= need;
        ++candidateLevel;
        if (candidateLevel > 1000) {
            overallLevel_ = candidateLevel;
            levelProgress_ = 0;
            return;
        }
    }
}

void Profile::set_total_xp(int totalXp) {
    totalXp_ = std::max(0, totalXp);
    RecalculateLevel();
}

void Profile::grant_global_xp(int amount) {
    if (amount <= 0) return;
    totalXp_ += amount;
    RecalculateLevel();
}

void Profile::set_overall_level(int level) {
    overallLevel_ = std::max(1, level);
    totalXp_ = TotalXpForLevel(overallLevel_) + levelProgress_;
    RecalculateLevel();
}

void Profile::set_level_progress(int progress) {
    set_level_and_progress(overallLevel_, progress);
}

void Profile::set_level_and_progress(int level, int progress) {
    overallLevel_ = std::max(1, level);
    levelProgress_ = std::max(0, progress);
    totalXp_ = TotalXpForLevel(overallLevel_) + levelProgress_;
    RecalculateLevel();
}

int Profile::category_best_score(size_t index) const {
    if (index >= bestCategoryScores_.size()) return 0;
    return bestCategoryScores_[index];
}

int Profile::category_cooldown(size_t index) const {
    if (index >= categoryCooldowns_.size()) return 0;
    return categoryCooldowns_[index];
}

void Profile::set_category_best_scores(const std::array<int, kCategoryCount>& scores) {
    bestCategoryScores_ = scores;
    for (auto& score : bestCategoryScores_) {
        score = ClampScore(score);
    }
}

void Profile::set_category_cooldowns(const std::array<int, kCategoryCount>& values) {
    categoryCooldowns_ = values;
}

void Profile::update_category_best_score(size_t index, int score) {
    if (index >= bestCategoryScores_.size()) return;
    bestCategoryScores_[index] = ClampScore(score);
}

void Profile::reset_category_cooldown(size_t index) {
    if (index >= categoryCooldowns_.size()) return;
    categoryCooldowns_[index] = 10;
}

void Profile::tick_category_cooldown(size_t index) {
    if (index >= categoryCooldowns_.size()) return;
    --categoryCooldowns_[index];
}

bool Profile::is_category_mastered(size_t index) const {
    return category_best_score(index) >= kMaxCategoryScore;
}

bool Profile::all_categories_mastered() const {
    return std::all_of(bestCategoryScores_.begin(), bestCategoryScores_.end(),
                       [](int score) { return score >= kMaxCategoryScore; });
}

void Profile::start_penalty_recovery(int tasks) {
    recoveryTasksRemaining_ = std::max(0, tasks);
}

void Profile::consume_penalty_task() {
    if (recoveryTasksRemaining_ > 0) {
        --recoveryTasksRemaining_;
    }
}
