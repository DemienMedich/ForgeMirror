#include "SkillCatalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

using SkillEntry = std::pair<const char*, double>;

const std::vector<SkillEntry> kDefaultSkills = {
    {"Modeling", 1.2},
    {"Sculpting", 1.2},
    {"Texturing", 1.0},
    {"Shading", 1.0},
    {"Rigging", 1.4},
    {"Lighting", 1.1},
    {"UV Mapping", 0.8},
    {"Retopology", 1.2},
    {"Materials", 0.8},
    {"Rendering", 0.9},
    {"Animation", 1.4},
    {"Simulation", 1.3},
    {"Hard Surface", 1.2},
    {"Environment", 1.1},
    {"Props", 0.9}
};

double clamp_weight(double value) {
    const double minW = 0.5;
    const double maxW = 1.6;
    if (value < minW) return minW;
    if (value > maxW) return maxW;
    return value;
}

} // namespace

SkillCatalog::SkillCatalog(std::filesystem::path baseDir)
    : baseDir_(std::move(baseDir)) {
    load();
}

bool SkillCatalog::contains(const std::string& skill) const {
    return index_.count(normalize(skill)) > 0;
}

std::optional<std::string> SkillCatalog::canonical(const std::string& skill) const {
    auto norm = normalize(skill);
    auto it = index_.find(norm);
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

double SkillCatalog::weight(const std::string& skill) const {
    auto norm = normalize(skill);
    auto it = index_.find(norm);
    if (it != index_.end()) {
        auto wIt = weights_.find(it->second);
        if (wIt != weights_.end()) return wIt->second;
    }
    return 1.0;
}

bool SkillCatalog::add_skill(const std::string& skill, double weight) {
    std::string trimmed = trim(skill);
    if (trimmed.empty()) return false;
    weight = clamp_weight(weight);

    auto norm = normalize(trimmed);
    auto it = index_.find(norm);
    if (it != index_.end()) {
        const std::string& canonicalName = it->second;
        double& storedWeight = weights_[canonicalName];
        if (std::abs(storedWeight - weight) < 1e-3) return false; // no change
        storedWeight = weight;
        save();
        return true;
    }

    add_internal(trimmed, weight, true);
    return true;
}

void SkillCatalog::load() {
    orderedSkills_.clear();
    index_.clear();
    weights_.clear();

    std::ifstream in(file_path());
    if (!in) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(entry.first, entry.second, false);
        }
        save();
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        double weight = 1.0;
        auto sep = trimmed.find('|');
        std::string name = trimmed;
        if (sep != std::string::npos) {
            name = trim(trimmed.substr(0, sep));
            std::string weightPart = trim(trimmed.substr(sep + 1));
            if (!weightPart.empty()) {
                try {
                    weight = std::stod(weightPart);
                } catch (...) {
                    weight = 1.0;
                }
            }
        }
        add_internal(name, clamp_weight(weight), false);
    }

    if (orderedSkills_.empty()) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(entry.first, entry.second, false);
        }
        save();
    }
}

void SkillCatalog::save() const {
    auto path = file_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& skill : orderedSkills_) {
        const double w = weight(skill);
        out << skill << "|" << w << "\n";
    }
}

std::filesystem::path SkillCatalog::file_path() const {
    return baseDir_ / "skills.txt";
}

std::string SkillCatalog::trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

std::string SkillCatalog::normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

void SkillCatalog::add_internal(const std::string& skill, double weight, bool persist) {
    const std::string canonical = skill;
    const std::string norm = normalize(skill);
    if (index_.count(norm)) return;

    orderedSkills_.push_back(canonical);
    index_.emplace(norm, canonical);
    weights_[canonical] = clamp_weight(weight);

    if (persist) save();
}
