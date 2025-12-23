#include "SkillCatalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

struct SkillEntry {
    const char* name;
    double weight;
    const char* description;
};

const std::vector<SkillEntry> kDefaultSkills = {
    {"Modeling", 1.2, "Building clean base meshes for further detailing or animation."},
    {"Sculpting", 1.2, "High-resolution sculpting for characters, creatures, and props."},
    {"Texturing", 1.0, "Creating texture maps that add color and detail to models."},
    {"Shading", 1.0, "Authoring material networks that react believably to light."},
    {"Rigging", 1.4, "Setting up skeletons and controls to animate characters or props."},
    {"Lighting", 1.1, "Placing lights and balancing exposure for mood and readability."},
    {"UV Mapping", 0.8, "Preparing UV layouts that minimize seams and distortion."},
    {"Retopology", 1.2, "Converting dense sculpts into animation-friendly low-poly meshes."},
    {"Materials", 0.8, "Authoring reusable material presets with consistent PBR values."},
    {"Rendering", 0.9, "Configuring render settings and output passes for final frames."},
    {"Animation", 1.4, "Bringing characters, props, or cameras to life over time."},
    {"Simulation", 1.3, "Driving cloth, hair, fluids, or rigid bodies with dynamic solvers."},
    {"Hard Surface", 1.2, "Designing mechanical or industrial assets with crisp detail."},
    {"Environment", 1.1, "Building large-scale scenes and set dressing for worlds."},
    {"Props", 0.9, "Creating supporting assets that tell stories and fill environments."}
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

std::string SkillCatalog::description(const std::string& skill) const {
    auto norm = normalize(skill);
    auto it = index_.find(norm);
    if (it != index_.end()) {
        auto dIt = descriptions_.find(it->second);
        if (dIt != descriptions_.end()) return dIt->second;
    }
    return {};
}

bool SkillCatalog::add_skill(const std::string& skill, double weight, const std::string& description) {
    std::string trimmed = trim(skill);
    if (trimmed.empty()) return false;
    weight = clamp_weight(weight);
    std::string desc = trim(description);

    auto norm = normalize(trimmed);
    auto it = index_.find(norm);
    if (it != index_.end()) {
        const std::string& canonicalName = it->second;
        double& storedWeight = weights_[canonicalName];
        std::string& storedDesc = descriptions_[canonicalName];
        bool changed = false;
        if (std::abs(storedWeight - weight) >= 1e-3) {
            storedWeight = weight;
            changed = true;
        }
        if (storedDesc != desc) {
            storedDesc = std::move(desc);
            changed = true;
        }
        if (changed) save();
        return changed;
    }

    add_internal(trimmed, weight, desc, true);
    return true;
}

bool SkillCatalog::remove_skill(const std::string& skill) {
    std::string trimmed = trim(skill);
    if (trimmed.empty()) return false;
    const std::string norm = normalize(trimmed);
    auto it = index_.find(norm);
    if (it == index_.end()) return false;
    const std::string canonical = it->second;

    index_.erase(it);
    weights_.erase(canonical);
    descriptions_.erase(canonical);
    orderedSkills_.erase(std::remove(orderedSkills_.begin(), orderedSkills_.end(), canonical), orderedSkills_.end());
    save();
    return true;
}

void SkillCatalog::load() {
    orderedSkills_.clear();
    index_.clear();
    weights_.clear();
    descriptions_.clear();

    std::ifstream in(file_path());
    if (!in) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(entry.name, entry.weight, entry.description, false);
        }
        save();
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        double weight = 1.0;
        std::string name = trimmed;
        std::string description;

        std::vector<std::string> parts;
        std::string part;
        std::istringstream ss(trimmed);
        while (std::getline(ss, part, '|')) {
            parts.push_back(trim(part));
        }
        if (!parts.empty()) name = parts[0];
        if (parts.size() >= 2) {
            if (!parts[1].empty()) {
                try {
                    weight = std::stod(parts[1]);
                } catch (...) {
                    weight = 1.0;
                }
            }
        }
        if (parts.size() >= 3) {
            description = parts[2];
        }
        add_internal(name, clamp_weight(weight), description, false);
    }

    if (orderedSkills_.empty()) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(entry.name, entry.weight, entry.description, false);
        }
        save();
    } else {
        bool changed = false;
        std::unordered_set<std::string> existing(orderedSkills_.begin(), orderedSkills_.end());
        for (const auto& entry : kDefaultSkills) {
            const std::string name(entry.name);
            const std::string norm = normalize(name);
            if (!index_.count(norm)) {
                orderedSkills_.push_back(name);
                index_[norm] = name;
                weights_[name] = entry.weight;
                descriptions_[name] = entry.description;
                changed = true;
            } else {
                auto& canonical = index_[norm];
                double& w = weights_[canonical];
                if (std::abs(w - entry.weight) > 1e-3) {
                    w = entry.weight;
                    changed = true;
                }
                auto& desc = descriptions_[canonical];
                if (desc.empty() && entry.description) {
                    desc = entry.description;
                    changed = true;
                }
            }
        }
        if (changed) save();
    }
}

void SkillCatalog::save() const {
    auto path = file_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& skill : orderedSkills_) {
        const double w = weight(skill);
        out << skill << "|" << w;
        auto it = descriptions_.find(skill);
        if (it != descriptions_.end() && !it->second.empty()) {
            out << "|" << it->second;
        }
        out << "\n";
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

void SkillCatalog::add_internal(const std::string& skill, double weight, const std::string& description, bool persist) {
    const std::string canonical = skill;
    const std::string norm = normalize(skill);
    if (index_.count(norm)) return;

    orderedSkills_.push_back(canonical);
    index_.emplace(norm, canonical);
    weights_[canonical] = clamp_weight(weight);
    descriptions_[canonical] = trim(description);

    if (persist) save();
}
