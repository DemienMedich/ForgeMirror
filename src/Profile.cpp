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
    recompute_overall();
}

bool Profile::grant_xp(const std::string& skillName, int amount) {
    auto it = skills_.find(skillName);
    if (it == skills_.end()) return false;
    bool leveled = it->second.add_xp(amount);
    recompute_overall();
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
    recompute_overall();
}

void Profile::recompute_overall() {
    if (skills_.empty()) {
        overallLevel_ = 1;
        return;
    }
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    for (const auto& kv : skills_) {
        const double w = kv.second.weight > 0.0 ? kv.second.weight : 1.0;
        weightedSum += kv.second.level * w;
        totalWeight += w;
    }
    if (totalWeight <= 0.0) {
        overallLevel_ = 1;
    } else {
        overallLevel_ = std::max(1, static_cast<int>(std::round(weightedSum / totalWeight)));
    }
}
