#include "SkillCatalog.h"
#include "JsonLite.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
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

double parse_weight(const std::string& value, double fallback) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isdigit(ch)) {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '.' || ch == ',') {
            out.push_back('.');
        } else if (ch == '-' && out.empty()) {
            out.push_back('-');
        }
    }
    if (out.empty()) return fallback;
    try {
        return std::stod(out);
    } catch (...) {
        return fallback;
    }
}

bool decode_utf8(const std::string& s, size_t& i, uint32_t& out) {
    unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        out = c0;
        ++i;
        return true;
    }
    if ((c0 >> 5) == 0x6 && i + 1 < s.size()) {
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80) return false;
        out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        i += 2;
        return true;
    }
    if ((c0 >> 4) == 0xE && i + 2 < s.size()) {
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
        out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        i += 3;
        return true;
    }
    if ((c0 >> 3) == 0x1E && i + 3 < s.size()) {
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
        out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        i += 4;
        return true;
    }
    return false;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

uint32_t lower_codepoint(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp == 0x0401) return 0x0451;
    return cp;
}

std::string lowercase_utf8(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    size_t i = 0;
    while (i < value.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(value, i, cp)) break;
        append_utf8(out, lower_codepoint(cp));
    }
    return out;
}

} // namespace

SkillCatalog::SkillCatalog(std::filesystem::path baseDir)
    : baseDir_(std::move(baseDir)) {
    load();
}

bool SkillCatalog::contains_id(const std::string& id) const {
    return namesById_.count(id) > 0;
}

bool SkillCatalog::contains_name(const std::string& name) const {
    return idByName_.count(normalize(name)) > 0;
}

std::optional<std::string> SkillCatalog::id_for_name(const std::string& name) const {
    auto norm = normalize(name);
    auto it = idByName_.find(norm);
    if (it == idByName_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> SkillCatalog::resolve_id(const std::string& idOrName) const {
    if (contains_id(idOrName)) return idOrName;
    return id_for_name(idOrName);
}

double SkillCatalog::weight(const std::string& skill) const {
    auto id = resolve_id(skill);
    if (id) {
        auto it = weightsById_.find(*id);
        if (it != weightsById_.end()) return it->second;
    }
    return 1.0;
}

std::string SkillCatalog::display_name(const std::string& id) const {
    if (auto resolved = resolve_id(id)) {
        auto it = namesById_.find(*resolved);
        if (it != namesById_.end()) return it->second;
    }
    return id;
}

std::string SkillCatalog::description(const std::string& id) const {
    if (auto resolved = resolve_id(id)) {
        auto it = descriptionsById_.find(*resolved);
        if (it != descriptionsById_.end()) return it->second;
    }
    return {};
}

bool SkillCatalog::add_skill(const std::string& skill, double weight, const std::string& description) {
    std::string trimmed = trim(skill);
    if (trimmed.empty()) return false;
    weight = clamp_weight(weight);
    std::string desc = trim(description);

    auto norm = normalize(trimmed);
    auto it = idByName_.find(norm);
    if (it != idByName_.end()) {
        const std::string& id = it->second;
        double& storedWeight = weightsById_[id];
        std::string& storedDesc = descriptionsById_[id];
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

    const std::string id = make_id(trimmed);
    add_internal(id, trimmed, weight, desc, true);
    return true;
}

bool SkillCatalog::update_skill(const std::string& id, const std::string& displayName, double weight, const std::string& description) {
    auto resolved = resolve_id(id);
    if (!resolved) return false;
    std::string trimmed = trim(displayName);
    if (trimmed.empty()) return false;
    weight = clamp_weight(weight);
    std::string desc = trim(description);

    const std::string currentId = *resolved;
    const std::string newNorm = normalize(trimmed);
    auto existing = idByName_.find(newNorm);
    if (existing != idByName_.end() && existing->second != currentId) {
        return false;
    }

    bool changed = false;
    auto nameIt = namesById_.find(currentId);
    if (nameIt != namesById_.end() && nameIt->second != trimmed) {
        if (nameIt->second.size()) {
            idByName_.erase(normalize(nameIt->second));
        }
        nameIt->second = trimmed;
        idByName_[newNorm] = currentId;
        changed = true;
    }

    double& storedWeight = weightsById_[currentId];
    if (std::abs(storedWeight - weight) >= 1e-3) {
        storedWeight = weight;
        changed = true;
    }
    std::string& storedDesc = descriptionsById_[currentId];
    if (storedDesc != desc) {
        storedDesc = std::move(desc);
        changed = true;
    }
    if (changed) save();
    return changed;
}

bool SkillCatalog::remove_skill(const std::string& idOrName) {
    auto resolved = resolve_id(idOrName);
    if (!resolved) return false;
    const std::string id = *resolved;
    auto nameIt = namesById_.find(id);
    if (nameIt == namesById_.end()) return false;

    idByName_.erase(normalize(nameIt->second));
    namesById_.erase(id);
    weightsById_.erase(id);
    descriptionsById_.erase(id);
    orderedIds_.erase(std::remove(orderedIds_.begin(), orderedIds_.end(), id), orderedIds_.end());
    save();
    return true;
}

void SkillCatalog::load() {
    orderedIds_.clear();
    idByName_.clear();
    namesById_.clear();
    weightsById_.clear();
    descriptionsById_.clear();

    std::ifstream in(file_path());
    if (!in) {
        auto legacyPath = baseDir_ / "skills.txt";
        std::ifstream legacy(legacyPath);
        if (!legacy) {
            for (const auto& entry : kDefaultSkills) {
                add_internal(make_id(entry.name), entry.name, entry.weight, entry.description, false);
            }
            save();
            return;
        }
        std::string line;
        while (std::getline(legacy, line)) {
            auto trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            std::vector<std::string> parts;
            std::string part;
            std::istringstream ss(trimmed);
            while (std::getline(ss, part, '|')) {
                parts.push_back(trim(part));
            }
            std::string id;
            std::string name;
            std::string desc;
            double weight = 1.0;

            if (parts.size() >= 4) {
                id = parts[0];
                name = parts[1];
                weight = parse_weight(parts[2], 1.0);
                desc = parts[3];
                if (parts.size() > 4) {
                    for (size_t i = 4; i < parts.size(); ++i) {
                        if (!desc.empty()) desc += "|";
                        desc += parts[i];
                    }
                }
            } else if (parts.size() >= 3) {
                name = parts[0];
                weight = parse_weight(parts[1], 1.0);
                desc = parts[2];
                if (parts.size() > 3) {
                    for (size_t i = 3; i < parts.size(); ++i) {
                        if (!desc.empty()) desc += "|";
                        desc += parts[i];
                    }
                }
            } else if (parts.size() >= 2) {
                name = parts[0];
                weight = parse_weight(parts[1], 1.0);
            } else if (!parts.empty()) {
                name = parts[0];
            }
            name = trim(name);
            if (name.empty()) continue;
            if (id.empty()) id = make_id(name);
            add_internal(id, name, clamp_weight(weight), desc, false);
        }

        if (orderedIds_.empty()) {
            for (const auto& entry : kDefaultSkills) {
                add_internal(make_id(entry.name), entry.name, entry.weight, entry.description, false);
            }
        }
        save();
        std::error_code ec;
        std::filesystem::remove(legacyPath, ec);
        return;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    JsonLite::Value root;
    if (!JsonLite::Parse(ss.str(), root, nullptr)) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(make_id(entry.name), entry.name, entry.weight, entry.description, false);
        }
        save();
        return;
    }

    auto get_value = [](const JsonLite::Value& obj, const char* key) -> const JsonLite::Value* {
        return JsonLite::GetObjectValue(obj, key);
    };
    const auto* skills = JsonLite::GetObjectValue(root, "skills");
    if (skills && skills->type == JsonLite::Type::Array) {
        for (const auto& item : skills->arrayValue) {
            if (item.type != JsonLite::Type::Object) continue;
            std::string id = get_value(item, "id") ? JsonLite::GetString(*get_value(item, "id"), "") : "";
            std::string name = get_value(item, "name") ? JsonLite::GetString(*get_value(item, "name"), "") : "";
            double weight = get_value(item, "weight") ? JsonLite::GetDouble(*get_value(item, "weight"), 1.0) : 1.0;
            std::string desc = get_value(item, "description") ? JsonLite::GetString(*get_value(item, "description"), "") : "";
            name = trim(name);
            if (name.empty()) continue;
            if (id.empty()) id = make_id(name);
            add_internal(id, name, clamp_weight(weight), desc, false);
        }
    }

    if (orderedIds_.empty()) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(make_id(entry.name), entry.name, entry.weight, entry.description, false);
        }
        save();
    } else {
        bool changed = false;
        std::unordered_set<std::string> existing(orderedIds_.begin(), orderedIds_.end());
        for (const auto& entry : kDefaultSkills) {
            const std::string name(entry.name);
            if (!idByName_.count(normalize(name))) {
                const std::string id = make_id(name);
                orderedIds_.push_back(id);
                namesById_[id] = name;
                idByName_[normalize(name)] = id;
                weightsById_[id] = entry.weight;
                descriptionsById_[id] = entry.description;
                changed = true;
            } else {
                const std::string id = *id_for_name(name);
                double& w = weightsById_[id];
                if (std::abs(w - entry.weight) > 1e-3) {
                    w = entry.weight;
                    changed = true;
                }
                auto& desc = descriptionsById_[id];
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
    if (!out) return;
    out.imbue(std::locale::classic());
    out << "{\n  \"skills\": [\n";
    for (size_t i = 0; i < orderedIds_.size(); ++i) {
        const auto& id = orderedIds_[i];
        auto nameIt = namesById_.find(id);
        if (nameIt == namesById_.end()) continue;
        const double w = weight(id);
        out << "    {\"id\":\"" << JsonLite::Escape(id)
            << "\",\"name\":\"" << JsonLite::Escape(nameIt->second)
            << "\",\"weight\":" << w;
        auto it = descriptionsById_.find(id);
        if (it != descriptionsById_.end() && !it->second.empty()) {
            out << ",\"description\":\"" << JsonLite::Escape(it->second) << "\"";
        }
        out << "}";
        out << (i + 1 < orderedIds_.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
}

std::filesystem::path SkillCatalog::file_path() const {
    return baseDir_ / "skills.json";
}

std::string SkillCatalog::trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

std::string SkillCatalog::normalize(const std::string& s) {
    std::string trimmed = trim(s);
    std::string lower = lowercase_utf8(trimmed);
    std::string out;
    out.reserve(lower.size());
    for (char c : lower) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

std::string SkillCatalog::make_id(const std::string& displayName) const {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : displayName) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream ss;
    ss << "sk_" << std::hex << std::setw(16) << std::setfill('0') << hash;
    std::string base = ss.str();
    std::string candidate = base;
    int suffix = 1;
    while (namesById_.count(candidate) > 0) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

void SkillCatalog::add_internal(const std::string& id, const std::string& displayName, double weight, const std::string& description, bool persist) {
    std::string trimmedName = trim(displayName);
    if (trimmedName.empty()) return;
    const std::string norm = normalize(trimmedName);
    auto existingByName = idByName_.find(norm);
    if (existingByName != idByName_.end()) {
        const std::string existingId = existingByName->second;
        namesById_[existingId] = trimmedName;
        weightsById_[existingId] = clamp_weight(weight);
        descriptionsById_[existingId] = trim(description);
        if (persist) save();
        return;
    }

    if (namesById_.count(id) == 0) {
        orderedIds_.push_back(id);
    } else {
        const std::string& oldName = namesById_[id];
        if (!oldName.empty()) {
            idByName_.erase(normalize(oldName));
        }
    }
    namesById_[id] = trimmedName;
    idByName_[norm] = id;
    weightsById_[id] = clamp_weight(weight);
    descriptionsById_[id] = trim(description);

    if (persist) save();
}
