#include "AppUtils.h"

#include <iostream>
#include <fstream>

#include "IJobStorage.h"
#include "SkillCatalog.h"
#include "Profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

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

std::filesystem::path LegacyUserStorageDir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "JobSkill";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "JobSkill";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".jobskill";
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

} // namespace

std::filesystem::path ResolveStorageDirectory() {
    auto root = GuessProjectRoot();
    auto legacyDir = root / "data";
    if (const char* env = std::getenv("FORGEMIRROR_STORAGE_DIR")) {
        std::filesystem::path custom(env);
        if (EnsureDirectory(custom)) return custom;
    }
    if (const char* env = std::getenv("JOBSKILL_STORAGE_DIR")) {
        std::filesystem::path custom(env);
        if (EnsureDirectory(custom)) return custom;
    }

    const bool legacyHasData = HasStorageData(legacyDir);
    auto userDir = DefaultUserStorageDir();
    auto legacyUserDir = LegacyUserStorageDir();
    const bool userReady = EnsureDirectory(userDir);
    const bool legacyUserReady = EnsureDirectory(legacyUserDir);
    const bool userHasData = userReady && HasStorageData(userDir);
    const bool legacyUserHasData = legacyUserReady && HasStorageData(legacyUserDir);

    auto finalize = [&](std::filesystem::path chosen) {
        MaybeMergeSeedProfiles(legacyDir, chosen);
        CopySeedAchievementIcons(legacyDir, chosen);
        return chosen;
    };

    if (legacyHasData && userReady && !userHasData) {
        if (CopyStorageTree(legacyDir, userDir)) {
            return finalize(userDir);
        }
        return finalize(legacyDir);
    }
    if (userHasData) {
        return finalize(userDir);
    }
    if (legacyUserHasData) {
        return finalize(legacyUserDir);
    }
    if (legacyHasData) {
        return finalize(legacyDir);
    }
    if (userReady) {
        return finalize(userDir);
    }
    if (legacyUserReady) {
        return finalize(legacyUserDir);
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

bool SaveAdminPassword(const std::filesystem::path& storageDir, const std::string& password) {
    auto path = AdminPasswordPath(storageDir);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    // Write BOM for editors (Notepad) to recognize UTF-8
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    out << "# ForgeMirror admin password\n";
    const std::string encoded = CaesarEncode(password, 7);
    out << "password=caesar:" << encoded << "\n";
    return out.good();
}

} // namespace

std::string LoadAdminPassword(const std::filesystem::path& storageDir) {
    if (const char* env = std::getenv("FORGEMIRROR_ADMIN_PASSWORD")) {
        if (*env != '\0') return env;
    }
    if (const char* env = std::getenv("JOBSKILL_ADMIN_PASSWORD")) {
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
                    const std::string prefix = "caesar:";
                    if (value.rfind(prefix, 0) == 0) {
                        std::string encoded = value.substr(prefix.size());
                        std::string decoded = CaesarDecode(encoded, 7);
                        if (!decoded.empty()) return decoded;
                    }
                    return value;
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

ModuleToggles LoadModuleToggles() {
    ModuleToggles toggles;
    const char* env = std::getenv("FORGEMIRROR_DISABLE_MODULES");
    if (!env || *env == '\0') {
        env = std::getenv("JOBSKILL_DISABLE_MODULES");
    }
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

bool FindStrayStorageFiles(const std::filesystem::path& storageDir, size_t maxSamples,
                           std::vector<std::string>& outSamples, int& totalCount) {
    outSamples.clear();
    totalCount = 0;
    std::error_code ec;
    if (storageDir.empty() || !std::filesystem::exists(storageDir, ec)) return false;

    auto is_allowed = [&](const std::filesystem::path& rel, bool isDir) {
        const std::string relStr = rel.generic_string();
        if (relStr.empty()) return true;
        if (relStr == "archive" || relStr == "achievements" || relStr == "achievements/icons" || relStr == "meta") return true;
        if (relStr == "logs" || relStr == "cloud") return true; // allow optional folders
        if (!isDir) {
            const auto ext = rel.extension().string();
            const auto parent = rel.parent_path().generic_string();
            if (parent.empty()) {
                if (ext == ".ini") return true;
                if (relStr == "skills.txt") return true;
            } else if (parent == "archive") {
                if (ext == ".ini") return true;
            } else if (parent == "meta") {
                if (ext == ".ini" || ext == ".json") return true;
                if (rel.filename() == "seed.merged") return true;
            } else if (parent == "achievements") {
                if (ext == ".json") return true;
            } else if (parent == "achievements/icons") {
                if (ext == ".png") return true;
            } else if (parent == "logs") {
                return true; // allow log files
            }
        }
        return false;
    };

    for (auto it = std::filesystem::recursive_directory_iterator(storageDir, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        auto rel = std::filesystem::relative(entry.path(), storageDir, ec);
        if (ec) break;
        if (!is_allowed(rel, entry.is_directory())) {
            totalCount++;
            if (outSamples.size() < maxSamples && entry.is_regular_file()) {
                outSamples.push_back(rel.generic_string());
            }
        }
    }
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

