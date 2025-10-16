#include "Skill.h"

#include <cmath>

Skill::Skill(std::string n, int lvl, double w) : name(std::move(n)), level(lvl), weight(w) {
    xpToNext = required_xp_for(level + 1);
}

int Skill::required_xp_for(int targetLevel) {
    double base = 100.0;
    double curve = std::pow(targetLevel, 1.3);
    return static_cast<int>(base * curve);
}

bool Skill::add_xp(int amount) {
    if (amount <= 0) return false;
    bool leveled = false;
    xp += amount;
    while (xp >= xpToNext) {
        xp -= xpToNext;
        level += 1;
        xpToNext = required_xp_for(level + 1);
        leveled = true;
    }
    return leveled;
}
