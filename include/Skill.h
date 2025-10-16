#pragma once
#include <string>

struct Skill {
    std::string name;
    int level = 1;
    int xp = 0;
    int xpToNext = 100;
    double weight = 1.0;

    Skill() = default;
    explicit Skill(std::string n, int lvl = 1, double w = 1.0);

    static int required_xp_for(int targetLevel);
    bool add_xp(int amount);
};
