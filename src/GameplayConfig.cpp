#include "GameplayConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <locale>

namespace {

GameplayConfig gConfig;

std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}

std::string sanitize_int(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isdigit(ch)) {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '-' && out.empty()) {
            out.push_back('-');
        }
    }
    if (out.empty()) return value;
    return out;
}

std::string sanitize_float(const std::string& value) {
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
    if (out.empty()) return value;
    return out;
}

float parse_float(const std::string& value) {
    std::stringstream ss(sanitize_float(value));
    ss.imbue(std::locale::classic());
    float result = 0.0f;
    ss >> result;
    return result;
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

const GameplayConfig& GetGameplayConfig() {
    return gConfig;
}

void SetGameplayConfig(const GameplayConfig& config) {
    gConfig = config;
}

GameplayConfig SanitizeGameplayConfig(const GameplayConfig& config) {
    GameplayConfig sanitized = config;
    sanitized.levelBaseXp = std::max(1, sanitized.levelBaseXp);
    sanitized.levelLinearXp = std::max(0, sanitized.levelLinearXp);
    sanitized.levelQuadraticXp = std::max(0, sanitized.levelQuadraticXp);
    for (auto& value : sanitized.categoryBaseXp) {
        value = std::max(0, value);
    }
    sanitized.focusBaseBonus = std::clamp(sanitized.focusBaseBonus, 0.0f, 10.0f);
    sanitized.focusAdditionalBonus = std::clamp(sanitized.focusAdditionalBonus, 0.0f, 10.0f);
    sanitized.repeatRewardFactor = std::clamp(sanitized.repeatRewardFactor, 0.0f, 1.0f);
    sanitized.recoveryRewardFactor = std::clamp(sanitized.recoveryRewardFactor, 0.0f, 1.0f);
    sanitized.recoveryWarmupTasks = std::max(0, sanitized.recoveryWarmupTasks);
    return sanitized;
}

std::filesystem::path GameplayConfigPath(const std::filesystem::path& storageDir) {
    auto metaDir = storageDir / "meta";
    std::error_code ec;
    std::filesystem::create_directories(metaDir, ec);
    (void)ec;
    return metaDir / "gameplay.ini";
}

static GameplayConfig Defaults() {
    return GameplayConfig{};
}

GameplayConfig LoadGameplayConfig(const std::filesystem::path& storageDir) {
    GameplayConfig config = Defaults();
    auto path = GameplayConfigPath(storageDir);
    std::ifstream in(path);
    if (!in) {
        SaveGameplayConfig(config, storageDir);
        return config;
    }
    in.imbue(std::locale::classic());

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        auto t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(t.substr(0, eq));
        auto value = trim(t.substr(eq + 1));
        if (section == "leveling") {
            try {
                if (iequals(key, "base")) config.levelBaseXp = std::max(1, std::stoi(sanitize_int(value)));
                else if (iequals(key, "linear")) config.levelLinearXp = std::max(0, std::stoi(sanitize_int(value)));
                else if (iequals(key, "quadratic")) config.levelQuadraticXp = std::max(0, std::stoi(sanitize_int(value)));
            } catch (...) {}
        } else if (section == "categories") {
            for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                if (iequals(key, Profile::kCategoryLabels[idx])) {
                    try {
                        config.categoryBaseXp[idx] = std::max(0, std::stoi(sanitize_int(value)));
                    } catch (...) {}
                    break;
                }
            }
        } else if (section == "rewards") {
            try {
                if (iequals(key, "focus_base")) {
                    config.focusBaseBonus = std::clamp(parse_float(value), 0.0f, 10.0f);
                } else if (iequals(key, "focus_bonus")) {
                    config.focusAdditionalBonus = std::clamp(parse_float(value), 0.0f, 10.0f);
                } else if (iequals(key, "repeat_factor")) {
                    config.repeatRewardFactor = std::clamp(parse_float(value), 0.0f, 10.0f);
                } else if (iequals(key, "recovery_factor")) {
                    config.recoveryRewardFactor = std::clamp(parse_float(value), 0.0f, 10.0f);
                } else if (iequals(key, "recovery_tasks")) {
                    config.recoveryWarmupTasks = std::max(0, std::stoi(sanitize_int(value)));
                }
            } catch (...) {}
        }
    }
    return SanitizeGameplayConfig(config);
}

bool SaveGameplayConfig(const GameplayConfig& config, const std::filesystem::path& storageDir) {
    GameplayConfig sanitized = SanitizeGameplayConfig(config);
    auto path = GameplayConfigPath(storageDir);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    out << "# Gameplay rules for ForgeMirror\n";
    out << "[leveling]\n";
    out << "base=" << sanitized.levelBaseXp << "\n";
    out << "linear=" << sanitized.levelLinearXp << "\n";
    out << "quadratic=" << sanitized.levelQuadraticXp << "\n\n";

    out << "[categories]\n";
    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
        out << Profile::kCategoryLabels[idx] << "=" << sanitized.categoryBaseXp[idx] << "\n";
    }
    out << "\n[rewards]\n";
    out << "focus_base=" << sanitized.focusBaseBonus << "\n";
    out << "focus_bonus=" << sanitized.focusAdditionalBonus << "\n";
    out << "repeat_factor=" << sanitized.repeatRewardFactor << "\n";
    out << "recovery_factor=" << sanitized.recoveryRewardFactor << "\n";
    out << "recovery_tasks=" << sanitized.recoveryWarmupTasks << "\n";
    return true;
}
