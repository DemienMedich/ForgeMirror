#include "Profile.h"

#include <algorithm>
#include <cmath>

Profile::Profile(std::string n) : name_(std::move(n)) {}

void Profile::add_skill(const std::string& skillName, int startLevel, double weight) {
    auto it = skills_.find(skillName);
    if (it == skills_.end()) {
        skills_.emplace(skillName, Skill(skillName, startLevel, weight));
    } else {
        if (startLevel > it->second.level) it->second.level = startLevel;
        if (weight > 0.0) it->second.weight = weight;
    }
    // Adding a skill should not influence overall level directly.
}

bool Profile::grant_xp(const std::string& skillName, int amount) {
    auto it = skills_.find(skillName);
    if (it == skills_.end()) return false;
    bool leveled = it->second.add_xp(amount);
    return leveled;
}

std::vector<Skill> Profile::list_skills() const {
        std::vector<Skill> out;
        out.reserve(skills_.size());
        for (const auto& kv : skills_) out.push_back(kv.second);
        return out;
}

void Profile::set_skills(const std::vector<Skill>& skills) {
    skills_.clear();
    for (const auto& s : skills) {
        skills_.emplace(s.name, s);
    }
}

void Profile::set_total_xp(int totalXp) {
    totalXp = std::max(0, totalXp);
    overallLevel_ = totalXp / kXpPerLevel + 1;
    overallProgress_ = totalXp % kXpPerLevel;
}

void Profile::grant_global_xp(int amount) {
    if (amount <= 0) return;
    int total = overallProgress_ + amount;
    overallLevel_ += total / kXpPerLevel;
    overallProgress_ = total % kXpPerLevel;
}

void Profile::set_overall_level(int level) {
    overallLevel_ = std::max(1, level);
}

void Profile::set_level_progress(int progress) {
    set_level_and_progress(overallLevel_, progress);
}

void Profile::set_level_and_progress(int level, int progress) {
    overallLevel_ = std::max(1, level);
    if (progress < 0) progress = 0;
    overallLevel_ += progress / kXpPerLevel;
    overallProgress_ = progress % kXpPerLevel;
}

int Profile::category_best_score(size_t index) const {
    if (index >= bestCategoryScores_.size()) return 0;
    return bestCategoryScores_[index];
}

void Profile::set_category_best_scores(const std::array<int, kCategoryCount>& scores) {
    bestCategoryScores_ = scores;
    for (auto& score : bestCategoryScores_) {
        if (score < 0) score = 0;
        if (score > kMaxCategoryScore) score = kMaxCategoryScore;
    }
}

void Profile::update_category_best_score(size_t index, int score) {
    if (index >= bestCategoryScores_.size()) return;
    if (score < 0) score = 0;
    if (score > kMaxCategoryScore) score = kMaxCategoryScore;
    bestCategoryScores_[index] = score;
}

bool Profile::is_category_mastered(size_t index) const {
    return category_best_score(index) >= kMaxCategoryScore;
}

bool Profile::all_categories_mastered() const {
    return std::all_of(bestCategoryScores_.begin(), bestCategoryScores_.end(),
                       [](int score) { return score >= kMaxCategoryScore; });
}
