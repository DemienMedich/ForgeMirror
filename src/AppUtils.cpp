#include "AppUtils.h"

#include <iostream>
#include <fstream>
#include <iomanip>

#include "IJobStorage.h"
#include "SkillCatalog.h"
#include "Profile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>
#include <random>
#include <chrono>

namespace {

std::string ToRoman(int value) {
    if (value <= 0) return "";
    static const std::pair<int, const char*> numerals[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
    };
    std::string out;
    for (const auto& [num, sym] : numerals) {
        while (value >= num) {
            out += sym;
            value -= num;
        }
    }
    return out;
}

std::string BuildRank(const char* base, int startLevel, int substep, int level) {
    int index = (level - startLevel) / substep + 1;
    if (index <= 1) return std::string(base);
    std::ostringstream ss;
    ss << base << " (" << ToRoman(index) << ")";
    return ss.str();
}

} // namespace


std::string DescribeOverallRank(const Profile& profile) {
    const int overallLevel = profile.overall_level();
    std::string base;
    if (overallLevel < 10) base = "Стажёр";
    else if (overallLevel < 50) base = BuildRank("Джуниор", 10, 10, overallLevel);
    else if (overallLevel < 150) base = BuildRank("Мидл", 50, 10, overallLevel);
    else base = BuildRank("Сеньор", 150, 10, overallLevel);

    std::ostringstream details;
    bool hasDetail = false;
    if (overallLevel >= 150 && !profile.all_categories_mastered()) {
        details << "Сеньор заблокирован: ";
        bool first = true;
        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
            if (profile.is_category_mastered(idx)) continue;
            if (!first) details << ", ";
            details << Profile::kCategoryLabels[idx] << "=" << profile.category_best_score(idx) << "/10";
            const int cooldown = profile.category_cooldown(idx);
            if (cooldown <= 0) details << " (!)";
            else details << " (кулдаун " << cooldown << ")";
            first = false;
        }
        if (first) details << "закончите испытания категорий";
        hasDetail = true;
    }
    if (profile.penalties_enabled() && profile.penalty_active()) {
        if (hasDetail) details << " | ";
        details << "штрафных задач осталось: " << profile.recovery_tasks_remaining();
        hasDetail = true;
    }
    if (profile.inactivity_tasks() > 0) {
        if (hasDetail) details << " | ";
        details << "буфер деградации: " << profile.inactivity_tasks() << " задач";
        hasDetail = true;
    }
    if (hasDetail) {
        return base + " (" + details.str() + ")";
    }
    return base;
}

void EnsureAdminProfile(IJobStorage& storage, SkillCatalog& catalog) {
    constexpr const char* kAdminName = "Admin";
    auto stored = storage.list_profiles();
    bool exists = false;
    for (const auto& info : stored) {
        if (info.name == kAdminName && !info.archived) {
            exists = true;
            // Ensure admin flag is set on the stored profile.
            if (storage.set_active_profile(info.id)) {
                if (auto prof = storage.load_profile()) {
                    if (!prof->is_admin()) {
                        prof->set_admin(true);
                        storage.save_profile(*prof);
                    }
                }
            }
            break;
        }
    }
    if (exists) return;

    Profile admin(kAdminName);
    if (auto skill = catalog.id_for_name("Modeling")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    }
    if (auto skill = catalog.id_for_name("Lighting")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    }
    if (auto skill = catalog.id_for_name("Materials")) {
        admin.add_skill(*skill, 1, catalog.weight(*skill));
    }

    SyncProfileWithCatalog(admin, catalog);
    admin.set_admin(true);

    auto info = storage.create_profile(admin);
    if (info) {
        storage.save_profile(admin);
        std::cout << "Создан профиль администратора '" << admin.name() << "' с ID " << info->id << ".\n";
    } else {
        std::cout << "Внимание: не удалось создать профиль администратора.\n";
    }
}

namespace {

std::filesystem::path GuessProjectRoot() {
    std::error_code ec;
    auto path = std::filesystem::current_path();
    while (!path.empty()) {
        const bool hasMain = std::filesystem::exists(path / "main.cpp", ec);
        const bool hasCMake = std::filesystem::exists(path / "CMakeLists.txt", ec);
        if (!ec && hasMain && hasCMake) {
            return path;
        }
        auto parent = path.parent_path();
        if (parent == path) break;
        path = std::move(parent);
    }
    return std::filesystem::current_path();
}

std::filesystem::path DefaultUserStorageDir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "ForgeMirror";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "ForgeMirror";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".forgemirror";
    }
#endif
    return {};
}

bool EnsureDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool HasStorageData(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return false;
    if (std::filesystem::exists(dir / "skills.txt", ec)) return true;
    if (std::filesystem::exists(dir / "meta" / "gameplay.ini", ec)) return true;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ini") return true;
    }
    auto archiveDir = dir / "archive";
    if (std::filesystem::exists(archiveDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(archiveDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ini") return true;
        }
    }
    return false;
}

bool HasAnyProfileIni(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ini") return true;
    }
    auto archiveDir = dir / "archive";
    if (std::filesystem::exists(archiveDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(archiveDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ini") return true;
        }
    }
    return false;
}

bool ShouldSkipSeedCopy(const std::filesystem::path& rel) {
    const std::string relPath = rel.generic_string();
    if (relPath == "meta/ui.ini") return true;
    if (relPath == "meta/gui-layout.ini") return true;
    if (relPath == "meta/shortcuts.json") return true;
    if (relPath.rfind("meta/ui-presets", 0) == 0) return true;
    return false;
}

bool CopyStorageTree(const std::filesystem::path& src, const std::filesystem::path& dst) {
    if (src.empty() || dst.empty() || src == dst) return false;
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return false;
    std::filesystem::create_directories(dst, ec);
    if (ec) return false;
    for (auto it = std::filesystem::recursive_directory_iterator(src, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) return false;
        const auto& entry = *it;
        auto rel = std::filesystem::relative(entry.path(), src, ec);
        if (ec) return false;
        if (ShouldSkipSeedCopy(rel)) {
            if (entry.is_directory()) {
                it.disable_recursion_pending();
            }
            continue;
        }
        auto target = dst / rel;
        if (entry.is_directory()) {
            std::filesystem::create_directories(target, ec);
            if (ec) return false;
            continue;
        }
        if (entry.is_regular_file()) {
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) return false;
            std::filesystem::copy_file(entry.path(), target,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }
    }
    return true;
}

void MergeSeedProfiles(const std::filesystem::path& seedRoot, const std::filesystem::path& storageRoot) {
    if (seedRoot.empty() || storageRoot.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(seedRoot, ec)) return;
    if (std::filesystem::equivalent(seedRoot, storageRoot, ec)) return;
    const std::unordered_set<std::string> allowed = {
        // Add allowed seed profile IDs here (e.g., "0001"). Empty = no seed profiles.
    };
    if (allowed.empty()) return;
    auto copy_missing_profile = [&](const std::filesystem::path& srcFile, const std::filesystem::path& dstDir) {
        if (srcFile.extension() != ".ini") return;
        if (!allowed.empty() && allowed.find(srcFile.stem().string()) == allowed.end()) return;
        std::filesystem::path dstFile = dstDir / srcFile.filename();
        if (std::filesystem::exists(dstFile, ec)) return;
        std::filesystem::create_directories(dstDir, ec);
        if (ec) return;
        std::filesystem::copy_file(srcFile, dstFile, std::filesystem::copy_options::overwrite_existing, ec);
    };
    for (const auto& entry : std::filesystem::directory_iterator(seedRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        copy_missing_profile(entry.path(), storageRoot);
    }
    auto seedArchive = seedRoot / "archive";
    if (std::filesystem::exists(seedArchive, ec)) {
        auto dstArchive = storageRoot / "archive";
        for (const auto& entry : std::filesystem::directory_iterator(seedArchive, ec)) {
            if (!entry.is_regular_file()) continue;
            copy_missing_profile(entry.path(), dstArchive);
        }
    }
}

void MaybeMergeSeedProfiles(const std::filesystem::path& seedRoot, const std::filesystem::path& storageRoot) {
    if (seedRoot.empty() || storageRoot.empty()) return;
    std::error_code ec;
    auto flag = storageRoot / "meta" / "seed.merged";
    if (std::filesystem::exists(flag, ec)) return;
    const bool hasProfiles = HasAnyProfileIni(storageRoot);
    if (hasProfiles) {
        std::filesystem::create_directories(flag.parent_path(), ec);
        if (!ec) {
            std::ofstream out(flag, std::ios::binary | std::ios::trunc);
            if (out) out << "merged";
        }
        return;
    }
    MergeSeedProfiles(seedRoot, storageRoot);
    std::filesystem::create_directories(flag.parent_path(), ec);
    if (!ec) {
        std::ofstream out(flag, std::ios::binary | std::ios::trunc);
        if (out) out << "merged";
    }
}

void CopySeedAchievementIcons(const std::filesystem::path& seedRoot, const std::filesystem::path& storageRoot) {
    if (seedRoot.empty() || storageRoot.empty()) return;
    std::error_code ec;
    auto srcIcons = seedRoot / "achievements" / "icons";
    if (!std::filesystem::exists(srcIcons, ec)) return;
    auto dstIcons = storageRoot / "achievements" / "icons";
    for (const auto& entry : std::filesystem::directory_iterator(srcIcons, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".png") continue;
        auto dstFile = dstIcons / entry.path().filename();
        if (std::filesystem::exists(dstFile, ec)) continue;
        std::filesystem::create_directories(dstIcons, ec);
        if (ec) continue;
        std::filesystem::copy_file(entry.path(), dstFile, std::filesystem::copy_options::overwrite_existing, ec);
    }
}

void CopySeedSpiritIcons(const std::filesystem::path& seedRoot, const std::filesystem::path& storageRoot) {
    if (seedRoot.empty() || storageRoot.empty()) return;
    std::error_code ec;
    auto srcIcons = seedRoot / "spirits";
    if (!std::filesystem::exists(srcIcons, ec)) return;
    auto dstIcons = storageRoot / "spirits";
    for (const auto& entry : std::filesystem::directory_iterator(srcIcons, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".png") continue;
        auto dstFile = dstIcons / entry.path().filename();
        if (std::filesystem::exists(dstFile, ec)) continue;
        std::filesystem::create_directories(dstIcons, ec);
        if (ec) continue;
        std::filesystem::copy_file(entry.path(), dstFile, std::filesystem::copy_options::overwrite_existing, ec);
    }
}

} // namespace

std::filesystem::path ProjectSeedDataDir() {
    auto root = GuessProjectRoot();
    return root / "data";
}

bool HasProjectSeedData() {
    return HasStorageData(ProjectSeedDataDir());
}

std::filesystem::path ResolveStorageDirectory() {
    auto root = GuessProjectRoot();
    auto legacyDir = root / "data";
    if (const char* env = std::getenv("FORGEMIRROR_STORAGE_DIR")) {
        std::filesystem::path custom(env);
        if (EnsureDirectory(custom)) return custom;
    }
    const bool legacyHasData = HasStorageData(legacyDir);
    auto userDir = DefaultUserStorageDir();
    const bool userReady = EnsureDirectory(userDir);
    const bool userHasData = userReady && HasStorageData(userDir);

    auto finalize = [&](std::filesystem::path chosen) {
        MaybeMergeSeedProfiles(legacyDir, chosen);
        CopySeedAchievementIcons(legacyDir, chosen);
        CopySeedSpiritIcons(legacyDir, chosen);
        return chosen;
    };

    if (userReady) {
        if (!userHasData && legacyHasData) {
            CopyStorageTree(legacyDir, userDir);
        }
        return finalize(userDir);
    }
    if (legacyHasData) {
        return finalize(legacyDir);
    }
    EnsureDirectory(legacyDir);
    return finalize(legacyDir);
}

namespace {

std::string TrimCopy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}

std::filesystem::path AdminPasswordPath(const std::filesystem::path& storageDir) {
    auto metaDir = storageDir / "meta";
    std::error_code ec;
    std::filesystem::create_directories(metaDir, ec);
    (void)ec;
    return metaDir / "admin.ini";
}

void StripUtf8Bom(std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

char CaesarShiftChar(char c, int shift) {
    // Shift only printable ASCII range [32, 126]
    const int low = 32;
    const int high = 126;
    const int range = high - low + 1;
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < low || uc > high) return c;
    int pos = static_cast<int>(uc) - low;
    pos = (pos + shift) % range;
    if (pos < 0) pos += range;
    return static_cast<char>(low + pos);
}

std::string CaesarEncode(const std::string& text, int shift) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(CaesarShiftChar(c, shift));
    }
    return out;
}

std::string CaesarDecode(const std::string& text, int shift) {
    return CaesarEncode(text, -shift);
}

static std::string XorData(const std::string& data, const std::string& key) {
    if (key.empty()) return data;
    std::string out;
    out.resize(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        out[i] = static_cast<char>(data[i] ^ key[i % key.size()]);
    }
    return out;
}

static std::string ToHex(const std::string& data) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (unsigned char c : data) {
        out.push_back(kHex[(c >> 4) & 0xF]);
        out.push_back(kHex[c & 0xF]);
    }
    return out;
}

static bool FromHex(const std::string& hex, std::string& out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    auto decode = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = decode(hex[i]);
        int lo = decode(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}


bool SaveAdminPassword(const std::filesystem::path& storageDir, const std::string& password) {
    auto path = AdminPasswordPath(storageDir);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    // Write BOM for editors (Notepad) to recognize UTF-8
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    out << "# ForgeMirror admin password\n";
    const std::string encoded = EncodePassword(password);
    out << "password=" << encoded << "\n";
    return out.good();
}

} // namespace
std::string EncodePassword(const std::string& password) {
    if (password.empty()) return {};
    static const std::string kKey = "ForgeMirror";
    std::string xored = XorData(password, kKey);
    return std::string("xor:") + ToHex(xored);
}

std::string DecodePassword(const std::string& value) {
    if (value.empty()) return {};
    const std::string xorPrefix = "xor:";
    if (value.rfind(xorPrefix, 0) == 0) {
        std::string raw;
        if (!FromHex(value.substr(xorPrefix.size()), raw)) return {};
        static const std::string kKey = "ForgeMirror";
        return XorData(raw, kKey);
    }
    const std::string caesarPrefix = "caesar:";
    if (value.rfind(caesarPrefix, 0) == 0) {
        std::string encoded = value.substr(caesarPrefix.size());
        return CaesarDecode(encoded, 7);
    }
    return value;
}

std::string GenerateRandomPassword(size_t length) {
    static const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    if (length < 6) length = 6;
    std::uniform_int_distribution<size_t> dist(0, std::strlen(kAlphabet) - 1);
    static std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string out;
    out.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(kAlphabet[dist(rng)]);
    }
    return out;
}

std::string SerializeProfileTaskRollbackSnapshot(const Profile& profile) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[task_rollback]\n";
    ss << "totalXp=" << profile.total_xp() << "\n";
    ss << "lastTaskTs=" << profile.last_task_timestamp() << "\n";
    ss << "inertiaTasks=" << profile.inactivity_tasks() << "\n";
    ss << "recoveryTasks=" << profile.recovery_tasks_remaining() << "\n";
    ss << "tasksCompleted=" << profile.tasks_completed() << "\n";

    ss << "\n[skills]\n";
    const auto skills = profile.list_skills();
    ss << "names=";
    for (size_t i = 0; i < skills.size(); ++i) {
        if (i) ss << ",";
        ss << skills[i].name;
    }
    ss << "\n";
    for (const auto& skill : skills) {
        ss << "level_" << skill.name << "=" << skill.level << "\n";
        ss << "xp_" << skill.name << "=" << skill.xp << "\n";
        ss << "xpToNext_" << skill.name << "=" << skill.xpToNext << "\n";
        ss << "weight_" << skill.name << "=" << skill.weight << "\n";
    }

    ss << "\n[categories]\n";
    const auto& scores = profile.category_best_scores();
    const auto& cooldowns = profile.category_cooldowns();
    for (size_t idx = 0; idx < scores.size(); ++idx) {
        ss << "score_" << Profile::kCategoryLabels[idx] << "=" << scores[idx] << "\n";
    }
    for (size_t idx = 0; idx < cooldowns.size(); ++idx) {
        ss << "cooldown_" << Profile::kCategoryLabels[idx] << "=" << cooldowns[idx] << "\n";
    }
    return ss.str();
}

bool ApplyProfileTaskRollbackSnapshot(const std::string& snapshot, Profile& profile) {
    if (snapshot.empty()) return false;
    auto trim = [](std::string value) {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                                [&](unsigned char c) { return !is_space(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(),
                                 [&](unsigned char c) { return !is_space(c); }).base(),
                    value.end());
        return value;
    };
    auto parse_int = [](const std::string& value, int fallback) {
        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    };
    auto parse_int64 = [](const std::string& value, std::int64_t fallback) {
        try {
            return std::stoll(value);
        } catch (...) {
            return fallback;
        }
    };
    auto parse_double = [](const std::string& value, double fallback) {
        try {
            return std::stod(value);
        } catch (...) {
            return fallback;
        }
    };

    std::istringstream in(snapshot);
    std::string line;
    std::string section;
    int totalXp = profile.total_xp();
    std::int64_t lastTaskTs = profile.last_task_timestamp();
    int inertiaTasks = profile.inactivity_tasks();
    int recoveryTasks = profile.recovery_tasks_remaining();
    int tasksCompleted = profile.tasks_completed();
    std::vector<std::string> skillNames;
    std::unordered_map<std::string, int> levelBySkill;
    std::unordered_map<std::string, int> xpBySkill;
    std::unordered_map<std::string, int> xpNextBySkill;
    std::unordered_map<std::string, double> weightBySkill;
    std::array<int, Profile::kCategoryCount> categoryScores = profile.category_best_scores();
    std::array<int, Profile::kCategoryCount> categoryCooldowns = profile.category_cooldowns();

    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        const std::string val = trim(t.substr(eq + 1));

        if (section == "task_rollback") {
            if (key == "totalXp") totalXp = parse_int(val, totalXp);
            else if (key == "lastTaskTs") lastTaskTs = parse_int64(val, lastTaskTs);
            else if (key == "inertiaTasks") inertiaTasks = parse_int(val, inertiaTasks);
            else if (key == "recoveryTasks") recoveryTasks = parse_int(val, recoveryTasks);
            else if (key == "tasksCompleted") tasksCompleted = parse_int(val, tasksCompleted);
        } else if (section == "skills") {
            if (key == "names") {
                skillNames.clear();
                std::istringstream ss(val);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    if (!item.empty()) skillNames.push_back(item);
                }
            } else if (key.rfind("level_", 0) == 0) {
                levelBySkill[key.substr(6)] = parse_int(val, 1);
            } else if (key.rfind("xp_", 0) == 0) {
                xpBySkill[key.substr(3)] = parse_int(val, 0);
            } else if (key.rfind("xpToNext_", 0) == 0) {
                xpNextBySkill[key.substr(9)] = parse_int(val, 0);
            } else if (key.rfind("weight_", 0) == 0) {
                weightBySkill[key.substr(7)] = parse_double(val, 1.0);
            }
        } else if (section == "categories") {
            if (key.rfind("score_", 0) == 0) {
                const std::string label = key.substr(6);
                for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                    if (label == Profile::kCategoryLabels[idx]) {
                        categoryScores[idx] = std::clamp(parse_int(val, categoryScores[idx]), 0, Profile::kMaxCategoryScore);
                        break;
                    }
                }
            } else if (key.rfind("cooldown_", 0) == 0) {
                const std::string label = key.substr(9);
                for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                    if (label == Profile::kCategoryLabels[idx]) {
                        categoryCooldowns[idx] = parse_int(val, categoryCooldowns[idx]);
                        break;
                    }
                }
            }
        }
    }

    std::vector<Skill> restoredSkills;
    restoredSkills.reserve(skillNames.size());
    for (const auto& skillName : skillNames) {
        const int level = std::max(1, parse_int(std::to_string(levelBySkill[skillName]), 1));
        const double weight = weightBySkill.count(skillName) ? weightBySkill[skillName] : 1.0;
        Skill skill(skillName, level, weight);
        if (auto it = xpBySkill.find(skillName); it != xpBySkill.end() && it->second >= 0) {
            skill.xp = it->second;
        }
        if (auto it = xpNextBySkill.find(skillName); it != xpNextBySkill.end() && it->second > 0) {
            skill.xpToNext = it->second;
        } else {
            skill.xpToNext = Skill::required_xp_for(skill.level + 1);
        }
        restoredSkills.push_back(skill);
    }

    profile.set_skills(restoredSkills);
    profile.set_total_xp(totalXp);
    profile.set_category_best_scores(categoryScores);
    profile.set_category_cooldowns(categoryCooldowns);
    profile.set_last_task_timestamp(lastTaskTs);
    profile.set_inactivity_tasks(inertiaTasks);
    profile.start_penalty_recovery(recoveryTasks);
    profile.set_tasks_completed(tasksCompleted);
    return true;
}


std::string LoadAdminPassword(const std::filesystem::path& storageDir) {
    if (const char* env = std::getenv("FORGEMIRROR_ADMIN_PASSWORD")) {
        if (*env != '\0') return env;
    }

    auto path = AdminPasswordPath(storageDir);
    std::ifstream in(path);
    if (in) {
        std::string line;
        bool firstLine = true;
        while (std::getline(in, line)) {
            if (firstLine) {
                StripUtf8Bom(line);
                firstLine = false;
            }
            std::string t = TrimCopy(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            if (t.rfind("password=", 0) == 0) {
                std::string value = TrimCopy(t.substr(9));
                if (!value.empty()) {
                    return DecodePassword(value);
                }
            } else {
                return t;
            }
        }
    }

    const std::string fallback = "admin123";
    SaveAdminPassword(storageDir, fallback);
    return fallback;
}

bool SetAdminPassword(const std::filesystem::path& storageDir, const std::string& password) {
    std::string trimmed = TrimCopy(password);
    if (trimmed.empty()) return false;
    return SaveAdminPassword(storageDir, trimmed);
}


void AppendProfileAudit(const std::filesystem::path& storageDir, const std::string& profileId,
                        const std::string& action, const std::string& details) {
    if (storageDir.empty() || profileId.empty() || action.empty()) return;
    std::error_code ec;
    const auto path = storageDir / "meta" / "profile-audit.log";
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return;
    auto sanitize = [](std::string value) {
        for (char& c : value) {
            if (c == '\r' || c == '\n' || c == '|') c = ' ';
        }
        return TrimCopy(value);
    };
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    out << ts << "|" << sanitize(profileId) << "|" << sanitize(action);
    if (!details.empty()) {
        out << "|" << sanitize(details);
    }
    out << "\n";
}
std::filesystem::path BannerTextPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "banner.json";
}

static std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::vector<std::string> LoadBannerTexts(const std::filesystem::path& storageDir) {
    std::vector<std::string> out;
    std::ifstream in(BannerTextPath(storageDir), std::ios::binary);
    if (!in) return out;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto itemsPos = content.find("\"items\"");
    if (itemsPos == std::string::npos) return out;
    auto lb = content.find('[', itemsPos);
    auto rb = content.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return out;
    std::string arr = content.substr(lb + 1, rb - lb - 1);
    std::string current;
    bool inStr = false;
    bool esc = false;
    for (char c : arr) {
        if (esc) {
            switch (c) {
                case 'n': current.push_back('\n'); break;
                case 'r': current.push_back('\r'); break;
                case 't': current.push_back('\t'); break;
                default: current.push_back(c); break;
            }
            esc = false;
            continue;
        }
        if (c == '\\') {
            if (inStr) esc = true;
            continue;
        }
        if (c == '"') {
            inStr = !inStr;
            if (!inStr && !current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        if (inStr) current.push_back(c);
    }
    return out;
}

bool SaveBannerTexts(const std::filesystem::path& storageDir, const std::vector<std::string>& texts) {
    std::filesystem::create_directories(storageDir / "meta");
    std::ofstream out(BannerTextPath(storageDir), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\n  \"items\": [";
    for (size_t i = 0; i < texts.size(); ++i) {
        out << "\"" << EscapeJsonString(texts[i]) << "\"";
        if (i + 1 < texts.size()) out << ", ";
    }
    out << "]\n}";
    return true;
}


std::filesystem::path StorageVaultPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "storage.json";
}

static int ClampVaultLogLimit(int value) {
    return std::clamp(value, 10, 50);
}

static int ClampVaultMinutes(int value) {
    return std::clamp(value, 0, 24 * 60 - 1);
}

static std::int64_t VaultNowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

static std::uint64_t Fnv1a64(const std::string& data) {
    const std::uint64_t kOffset = 1469598103934665603ull;
    const std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    for (unsigned char c : data) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

static std::string ToHex64(std::uint64_t value) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
}

static bool ExtractJsonStringField(const std::string& content, const char* key, std::string& out) {
    const std::string token = std::string("\"") + key + "\"";
    size_t pos = content.find(token);
    if (pos == std::string::npos) return false;
    pos = content.find(':', pos + token.size());
    if (pos == std::string::npos) return false;
    pos = content.find('"', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    std::string value;
    bool esc = false;
    for (; pos < content.size(); ++pos) {
        char c = content[pos];
        if (esc) {
            switch (c) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(c); break;
            }
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        value.push_back(c);
    }
    out = value;
    return true;
}

static bool ExtractJsonNumberField(const std::string& content, const char* key, double& out) {
    const std::string token = std::string("\"") + key + "\"";
    size_t pos = content.find(token);
    if (pos == std::string::npos) return false;
    pos = content.find(':', pos + token.size());
    if (pos == std::string::npos) return false;
    ++pos;
    std::string number;
    number.reserve(32);
    bool hasDot = false;
    for (; pos < content.size(); ++pos) {
        const char c = content[pos];
        if (c == ',' || c == '}' || c == ']') break;
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!number.empty()) break;
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '-' && number.empty()) || (c == '.' && !hasDot)) {
            number.push_back(c);
            if (c == '.') hasDot = true;
            continue;
        }
        // Skip unexpected characters (e.g., corrupted digits)
    }
    if (number.empty()) return false;
    char* endPtr = nullptr;
    out = std::strtod(number.c_str(), &endPtr);
    if (endPtr == number.c_str()) return false;
    return true;
}

static bool ParseVaultAmount(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && end != text.c_str();
}

static std::string FormatVaultAmount(double value) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss << std::setprecision(2) << value;
    return ss.str();
}

static std::string BuildVaultContentHash(const StorageVaultData& data, const std::string& balancePlain) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss << data.currencyName << "\n";
    ss << data.currencyCode << "\n";
    ss << balancePlain << "\n";
    ss << data.logLimit << "\n";
    ss << data.pomodoroStartMinutes << "\n";
    ss << data.pomodoroEndMinutes << "\n";
    ss << data.pomodoroMinMinutes << "\n";
    ss << data.pomodoroCoinsPerCycle << "\n";
    ss << data.pomodoroDaysMask << "\n";
    for (const auto& entry : data.log) {
        ss << entry.timestamp << "|";
        ss << FormatVaultAmount(entry.amount) << "|";
        ss << entry.action << "|";
        ss << entry.note << "\n";
    }
    return ToHex64(Fnv1a64(ss.str()));
}

static std::vector<StorageLogEntry> ParseVaultLog(const std::string& content) {

    std::vector<StorageLogEntry> log;
    const std::string token = "\"log\"";
    size_t pos = content.find(token);
    if (pos == std::string::npos) return log;
    size_t lb = content.find('[', pos + token.size());
    if (lb == std::string::npos) return log;
    size_t rb = content.find(']', lb + 1);
    if (rb == std::string::npos || rb <= lb) return log;
    size_t cur = lb + 1;
    while (cur < rb) {
        size_t objStart = content.find('{', cur);
        if (objStart == std::string::npos || objStart > rb) break;
        size_t objEnd = content.find('}', objStart + 1);
        if (objEnd == std::string::npos || objEnd > rb) break;
        std::string obj = content.substr(objStart, objEnd - objStart + 1);
        StorageLogEntry entry;
        double value = 0.0;
        if (ExtractJsonNumberField(obj, "ts", value)) {
            entry.timestamp = static_cast<std::int64_t>(value);
        }
        if (ExtractJsonNumberField(obj, "amount", value)) {
            entry.amount = value;
        }
        ExtractJsonStringField(obj, "action", entry.action);
        ExtractJsonStringField(obj, "note", entry.note);
        if (entry.timestamp != 0 || entry.amount != 0.0 || !entry.action.empty() || !entry.note.empty()) {
            log.push_back(entry);
        }
        cur = objEnd + 1;
    }
    return log;
}

static void TrimVaultLog(StorageVaultData& data) {
    data.logLimit = ClampVaultLogLimit(data.logLimit);
    if (data.logLimit <= 0) return;
    if (data.log.size() > static_cast<size_t>(data.logLimit)) {
        const size_t drop = data.log.size() - static_cast<size_t>(data.logLimit);
        data.log.erase(data.log.begin(), data.log.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

StorageVaultData LoadStorageVault(const std::filesystem::path& storageDir) {
    StorageVaultData data;
    data.currencyName = u8"Кукоин";
    data.currencyCode = "KUK";
    data.balance = 0.0;
    data.logLimit = 10;
    data.updatedAt = 0;
    data.revision = 0;
    data.contentHash.clear();
    data.pomodoroStartMinutes = 9 * 60;
    data.pomodoroEndMinutes = 18 * 60;
    data.pomodoroMinMinutes = 20;
    data.pomodoroCoinsPerCycle = 1;
    data.pomodoroDaysMask = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);
    std::ifstream in(StorageVaultPath(storageDir), std::ios::binary);
    if (!in) return data;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string value;
    if (ExtractJsonStringField(content, "currency_name", value) && !value.empty()) {
        data.currencyName = value;
    }
    if (ExtractJsonStringField(content, "currency_code", value) && !value.empty()) {
        data.currencyCode = value;
    }
    double num = 0.0;
    bool hasBalance = false;
    if (ExtractJsonStringField(content, "balance_enc", value) && !value.empty()) {
        const std::string decoded = DecodePassword(value);
        double parsed = 0.0;
        if (ParseVaultAmount(decoded, parsed)) {
            data.balance = parsed;
            hasBalance = true;
        }
    }
    if (!hasBalance && ExtractJsonNumberField(content, "balance", num)) {
        data.balance = num;
    }
    if (ExtractJsonNumberField(content, "log_limit", num)) {
        data.logLimit = ClampVaultLogLimit(static_cast<int>(num));
    }
    if (ExtractJsonNumberField(content, "updated_at", num)) {
        data.updatedAt = static_cast<std::int64_t>(num);
    }
    if (ExtractJsonNumberField(content, "rev", num)) {
        data.revision = static_cast<std::int64_t>(num);
    }
    if (ExtractJsonStringField(content, "content_hash", value)) {
        data.contentHash = value;
    }
    if (ExtractJsonNumberField(content, "pomodoro_start", num)) {
        data.pomodoroStartMinutes = ClampVaultMinutes(static_cast<int>(num));
    }
    if (ExtractJsonNumberField(content, "pomodoro_end", num)) {
        data.pomodoroEndMinutes = ClampVaultMinutes(static_cast<int>(num));
    }
    if (ExtractJsonNumberField(content, "pomodoro_min", num)) {
        data.pomodoroMinMinutes = std::max(1, static_cast<int>(num));
    }
    if (ExtractJsonNumberField(content, "pomodoro_coin", num)) {
        data.pomodoroCoinsPerCycle = std::max(0, static_cast<int>(num));
    }
    if (ExtractJsonNumberField(content, "pomodoro_days", num)) {
        data.pomodoroDaysMask = static_cast<int>(num);
        if (data.pomodoroDaysMask < 0) data.pomodoroDaysMask = 0;
    }
    data.log = ParseVaultLog(content);
    TrimVaultLog(data);
    return data;
}

bool SaveStorageVault(const std::filesystem::path& storageDir, const StorageVaultData& data) {
    std::filesystem::create_directories(storageDir / "meta");
    std::int64_t existingRev = 0;
    {
        std::ifstream in(StorageVaultPath(storageDir), std::ios::binary);
        if (in) {
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            double num = 0.0;
            if (ExtractJsonNumberField(content, "rev", num)) {
                existingRev = static_cast<std::int64_t>(num);
            }
        }
    }
    std::ofstream out(StorageVaultPath(storageDir), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    StorageVaultData save = data;
    if (save.currencyName.empty()) save.currencyName = u8"Кукоин";
    if (save.currencyCode.empty()) save.currencyCode = "KUK";
    save.logLimit = ClampVaultLogLimit(save.logLimit);
    save.updatedAt = VaultNowSeconds();
    save.revision = std::max(save.revision, existingRev) + 1;
    save.pomodoroStartMinutes = ClampVaultMinutes(save.pomodoroStartMinutes);
    save.pomodoroEndMinutes = ClampVaultMinutes(save.pomodoroEndMinutes);
    save.pomodoroMinMinutes = std::max(1, save.pomodoroMinMinutes);
    save.pomodoroCoinsPerCycle = std::max(0, save.pomodoroCoinsPerCycle);
    if (save.pomodoroDaysMask == 0) {
        save.pomodoroDaysMask = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);
    }
    TrimVaultLog(save);

    out << "{\n";
    out << "  \"currency_name\": \"" << EscapeJsonString(save.currencyName) << "\",\n";
    out << "  \"currency_code\": \"" << EscapeJsonString(save.currencyCode) << "\",\n";
    const std::string balancePlain = FormatVaultAmount(save.balance);
    const std::string balanceEnc = EncodePassword(balancePlain);
    const std::string contentHash = BuildVaultContentHash(save, balancePlain);
    save.contentHash = contentHash;
    out << "  \"balance_enc\": \"" << EscapeJsonString(balanceEnc) << "\",\n";
    out << "  \"log_limit\": " << save.logLimit << ",\n";
    out << "  \"rev\": " << static_cast<long long>(save.revision) << ",\n";
    out << "  \"updated_at\": " << static_cast<long long>(save.updatedAt) << ",\n";
    out << "  \"content_hash\": \"" << contentHash << "\",\n";
    out << "  \"pomodoro_start\": " << save.pomodoroStartMinutes << ",\n";
    out << "  \"pomodoro_end\": " << save.pomodoroEndMinutes << ",\n";
    out << "  \"pomodoro_min\": " << save.pomodoroMinMinutes << ",\n";
    out << "  \"pomodoro_coin\": " << save.pomodoroCoinsPerCycle << ",\n";
    out << "  \"pomodoro_days\": " << save.pomodoroDaysMask << ",\n";
    out << "  \"log\": [";
    for (size_t i = 0; i < save.log.size(); ++i) {
        const auto& entry = save.log[i];
        out << "\n    {\"ts\":" << entry.timestamp
            << ",\"action\":\"" << EscapeJsonString(entry.action)
            << "\",\"amount\":" << entry.amount
            << ",\"note\":\"" << EscapeJsonString(entry.note) << "\"}";
        if (i + 1 < save.log.size()) out << ',';
    }
    if (!save.log.empty()) out << "\n  ";
    out << "]\n}";
    return true;
}

ModuleToggles LoadModuleToggles() {
    ModuleToggles toggles;
    const char* env = std::getenv("FORGEMIRROR_DISABLE_MODULES");
    if (!env || *env == '\0') return toggles;

    std::unordered_map<std::string, bool*> map = {
        {"tasks", &toggles.tasks},
        {"pipeline", &toggles.pipeline},
        {"achievements", &toggles.achievements},
        {"ach", &toggles.achievements},
        {"shortcuts", &toggles.shortcuts},
        {"pomodoro", &toggles.pomodoro},
        {"timer", &toggles.pomodoro},
        {"cloud", &toggles.cloud},
        {"sync", &toggles.cloud},
        {"view3d", &toggles.view3d},
        {"3d", &toggles.view3d},
        {"professions", &toggles.professions},
        {"profession", &toggles.professions},
        {"prof", &toggles.professions},
    };

    auto disable_all = [&]() {
        for (auto& kv : map) {
            if (kv.second) *(kv.second) = false;
        }
    };

    std::string raw(env);
    std::replace(raw.begin(), raw.end(), ';', ',');
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::string t = TrimCopy(token);
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (t.empty()) continue;
        if (t == "all") {
            disable_all();
            continue;
        }
        auto it = map.find(t);
        if (it != map.end() && it->second) {
            *(it->second) = false;
        }
    }
    return toggles;
}

namespace {

bool IsAllowedStorageEntry(const std::filesystem::path& rel, bool isDir) {
    const std::string relStr = rel.generic_string();
    if (relStr.empty()) return true;
    const std::unordered_set<std::string> allowedDirs = {
        "", "archive", "achievements", "achievements/icons", "spirits", "meta", "meta/patch-notes",
        "meta/ui-presets", "meta/reports", "meta/updates", "logs", "cloud"
    };
    if (isDir) {
        return allowedDirs.find(relStr) != allowedDirs.end();
    }
    const std::string parent = rel.parent_path().generic_string();
    const std::string name = rel.filename().string();
    const std::string ext = rel.extension().string();
    const std::unordered_set<std::string> allowedMetaFiles = {
        "pipeline.json", "tasks.json", "projects.json", "gameplay.ini", "shortcuts.json", "ui.ini", "cloud.ini",
        "professions.txt", "banner.json", "storage.json", "profile-audit.log", "task-audit.log", "seed.merged", "gui-layout.ini", "admin.ini"
    };
    if (parent.empty()) {
        if (ext == ".ini") return true;
        if (relStr == "skills.txt") return true;
        return false;
    }
    if (parent == "archive") return ext == ".ini";
    if (parent == "achievements") return ext == ".json";
    if (parent == "achievements/icons") return ext == ".png";
    if (parent == "spirits") return ext == ".png";
    if (parent == "meta") return allowedMetaFiles.find(name) != allowedMetaFiles.end();
    if (parent == "meta/patch-notes") return ext == ".md";
    if (parent == "meta/ui-presets") return ext == ".ini";
    if (parent == "meta/reports") return ext == ".txt" || ext == ".csv";
    if (parent == "meta/updates") return true;
    if (parent == "logs") return true;
    if (parent == "cloud") return true;
    return false;
}

} // namespace

bool CollectStrayStorageFiles(const std::filesystem::path& storageDir, std::vector<std::string>& outList) {
    outList.clear();
    std::error_code ec;
    if (storageDir.empty() || !std::filesystem::exists(storageDir, ec)) return false;

    for (auto it = std::filesystem::recursive_directory_iterator(storageDir, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        auto rel = std::filesystem::relative(entry.path(), storageDir, ec);
        if (ec) break;
        if (!IsAllowedStorageEntry(rel, entry.is_directory())) {
            outList.push_back(rel.generic_string());
            if (entry.is_directory()) {
                it.disable_recursion_pending();
            }
        }
    }
    return true;
}

bool FindStrayStorageFiles(const std::filesystem::path& storageDir, size_t maxSamples,
                           std::vector<std::string>& outSamples, int& totalCount) {
    outSamples.clear();
    totalCount = 0;
    std::vector<std::string> all;
    if (!CollectStrayStorageFiles(storageDir, all)) return false;
    totalCount = static_cast<int>(all.size());
    const size_t limit = std::min(maxSamples, all.size());
    outSamples.assign(all.begin(), all.begin() + static_cast<std::vector<std::string>::difference_type>(limit));
    return true;
}

void SyncProfileWithCatalog(Profile& profile, SkillCatalog& catalog) {
    auto skills = profile.list_skills();
    bool changed = false;
    for (auto& skill : skills) {
        if (!catalog.contains_id(skill.name)) {
            if (auto id = catalog.id_for_name(skill.name)) {
                skill.name = *id;
                changed = true;
            }
        }
        const double newWeight = catalog.weight(skill.name);
        if (std::abs(skill.weight - newWeight) > 1e-6) {
            skill.weight = newWeight;
            changed = true;
        }
    }

    auto ach = profile.achievements();
    bool achChanged = false;
    for (auto& a : ach) {
        if (!catalog.contains_id(a.skill)) {
            if (auto id = catalog.id_for_name(a.skill)) {
                a.skill = *id;
                achChanged = true;
            }
        }
    }
    if (achChanged) {
        profile.set_achievements(ach);
    }
    if (changed || achChanged) {
        profile.set_skills(skills);
    }
}





