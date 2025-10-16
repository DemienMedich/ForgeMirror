#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "Skill.h"

class Profile {
public:
    Profile() = default;
    explicit Profile(std::string n);

    void add_skill(const std::string& skillName, int startLevel = 1, double weight = 1.0);
    bool grant_xp(const std::string& skillName, int amount);

    int overall_level() const { return overallLevel_; }
    const std::string& name() const { return name_; }
    std::vector<Skill> list_skills() const;
    void set_skills(const std::vector<Skill>& skills);

private:
    void recompute_overall();

    std::string name_;
    std::unordered_map<std::string, Skill> skills_;
    int overallLevel_ = 1;
};
