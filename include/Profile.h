#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "Skill.h"

class Profile {
public:
    inline static constexpr int kCategoryCount = 5;
    inline static constexpr int kMaxCategoryScore = 10;
    inline static constexpr std::array<const char*, kCategoryCount> kCategoryLabels = {"E", "D", "C", "B", "A"};

    Profile() = default;
    explicit Profile(std::string n);

    void add_skill(const std::string& skillName, int startLevel = 1, double weight = 1.0);
    bool grant_xp(const std::string& skillName, int amount);

    int overall_level() const { return overallLevel_; }
    int total_xp() const { return (overallLevel_ - 1) * kXpPerLevel + overallProgress_; }
    int level_progress() const { return overallProgress_; }
    int xp_to_next_level() const { return kXpPerLevel; }
    const std::string& name() const { return name_; }
    std::vector<Skill> list_skills() const;
    void set_skills(const std::vector<Skill>& skills);
    void set_total_xp(int totalXp);
    void grant_global_xp(int amount);
    void set_overall_level(int level);
    void set_level_progress(int progress);
    void set_level_and_progress(int level, int progress);
    const std::array<int, kCategoryCount>& category_best_scores() const { return bestCategoryScores_; }
    int category_best_score(size_t index) const;
    void set_category_best_scores(const std::array<int, kCategoryCount>& scores);
    void update_category_best_score(size_t index, int score);
    bool is_category_mastered(size_t index) const;
    bool all_categories_mastered() const;

private:
    static constexpr int kXpPerLevel = 1000;

    std::string name_;
    std::unordered_map<std::string, Skill> skills_;
    int overallProgress_ = 0;
    int overallLevel_ = 1;
    std::array<int, kCategoryCount> bestCategoryScores_ = {0, 0, 0, 0, 0};
};
