#include "CloudSync.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

std::string Trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}

bool ParseBool(const std::string& text, bool fallback) {
    std::string v = Trim(text);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

std::string SanitizeInt(const std::string& value) {
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

std::int64_t ParseInt64(const std::string& value, std::int64_t fallback) {
    try {
        return std::stoll(SanitizeInt(value));
    } catch (...) {
        return fallback;
    }
}

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::filesystem::path CloudConfigPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "cloud.ini";
}

std::filesystem::path ResolveCloudRoot(const CloudSyncConfig& config, const std::filesystem::path& storageDir) {
    if (!config.root.empty()) {
        if (config.root.is_absolute()) return config.root;
        return storageDir / config.root;
    }
    return storageDir / "cloud";
}

std::filesystem::path ResolveCloudManifestPath(const CloudSyncConfig& config,
                                               const std::filesystem::path& storageDir) {
    const auto root = ResolveCloudRoot(config, storageDir);
    if (config.manifest.empty()) {
        return root / "meta" / "manifest.ini";
    }
    if (config.manifest.is_absolute()) return config.manifest;
    return root / config.manifest;
}

bool IsAdminProfileFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool inProfile = false;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            std::string section = t.substr(1, t.size() - 2);
            std::transform(section.begin(), section.end(), section.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            inProfile = (section == "profile");
            continue;
        }
        if (!inProfile) continue;
        if (t.rfind("admin=", 0) == 0) {
            std::string value = t.substr(6);
            return ParseBool(value, false);
        }
    }
    return false;
}

bool CopyFileIfExists(const std::filesystem::path& src, const std::filesystem::path& dst, CloudSyncStats& stats, bool count) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return false;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
        if (count) stats.filesPulled += 1;
        return true;
    }
    return false;
}

bool CopyFileIfExistsPush(const std::filesystem::path& src, const std::filesystem::path& dst, CloudSyncStats& stats) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return false;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
        stats.filesPushed += 1;
        return true;
    }
    return false;
}

void CopyProfiles(const std::filesystem::path& srcRoot, const std::filesystem::path& dstRoot,
                  CloudRole role, const CloudSyncConfig& config, CloudSyncStats& stats,
                  std::unordered_set<std::string>& skippedAdminIds, bool pulling) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(srcRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        const std::string id = entry.path().stem().string();
        const bool isAdmin = IsAdminProfileFile(entry.path());
        if (role == CloudRole::Viewer && isAdmin && !config.includeAdminProfiles) {
            skippedAdminIds.insert(id);
            stats.adminSkipped += 1;
            continue;
        }
        std::filesystem::path dst = dstRoot / entry.path().filename();
        if (pulling) {
            if (CopyFileIfExists(entry.path(), dst, stats, true)) stats.profilesPulled += 1;
        } else {
            if (CopyFileIfExistsPush(entry.path(), dst, stats)) stats.profilesPushed += 1;
        }
    }
}

void CopyAchievements(const std::filesystem::path& srcRoot, const std::filesystem::path& dstRoot,
                      const std::unordered_set<std::string>& skipIds, CloudSyncStats& stats, bool pulling) {
    std::error_code ec;
    if (!std::filesystem::exists(srcRoot, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(srcRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        const std::string id = entry.path().stem().string();
        if (skipIds.find(id) != skipIds.end()) continue;
        std::filesystem::path dst = dstRoot / entry.path().filename();
        if (pulling) {
            CopyFileIfExists(entry.path(), dst, stats, true);
        } else {
            CopyFileIfExistsPush(entry.path(), dst, stats);
        }
    }
}

std::vector<int> ParseVersion(const std::string& value) {
    std::vector<int> parts;
    std::stringstream ss(value);
    std::string part;
    while (std::getline(ss, part, '.')) {
        part = Trim(part);
        if (part.empty()) {
            parts.push_back(0);
        } else {
            try {
                parts.push_back(std::stoi(SanitizeInt(part)));
            } catch (...) {
                parts.push_back(0);
            }
        }
    }
    if (parts.empty()) parts.push_back(0);
    return parts;
}

int CompareVersions(const std::string& a, const std::string& b) {
    const auto va = ParseVersion(a);
    const auto vb = ParseVersion(b);
    const size_t count = std::max(va.size(), vb.size());
    for (size_t i = 0; i < count; ++i) {
        const int ai = i < va.size() ? va[i] : 0;
        const int bi = i < vb.size() ? vb[i] : 0;
        if (ai < bi) return -1;
        if (ai > bi) return 1;
    }
    return 0;
}

} // namespace

CloudSyncConfig LoadCloudSyncConfig(const std::filesystem::path& storageDir) {
    CloudSyncConfig config;
    auto path = CloudConfigPath(storageDir);
    std::ifstream in(path);
    if (!in) return config;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(t.substr(0, eq));
        std::string value = Trim(t.substr(eq + 1));
        if (!section.empty() && section != "cloud") continue;
        if (key == "enabled") config.enabled = ParseBool(value, config.enabled);
        else if (key == "autoPull") config.autoPull = ParseBool(value, config.autoPull);
        else if (key == "autoPush") config.autoPush = ParseBool(value, config.autoPush);
        else if (key == "includeAdminProfiles") config.includeAdminProfiles = ParseBool(value, config.includeAdminProfiles);
        else if (key == "root") config.root = value;
        else if (key == "manifest") config.manifest = value;
        else if (key == "releasesDir") config.releasesDir = value;
        else if (key == "updateManifestOnPush") config.updateManifestOnPush = ParseBool(value, config.updateManifestOnPush);
    }
    return config;
}

CloudManifest LoadCloudManifest(const CloudSyncConfig& config, const std::filesystem::path& storageDir) {
    CloudManifest manifest;
    const auto path = ResolveCloudManifestPath(config, storageDir);
    std::ifstream in(path);
    if (!in) return manifest;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(t.substr(0, eq));
        std::string value = Trim(t.substr(eq + 1));
        if (key == "appVersion") manifest.appVersion = value;
        else if (key == "dataUpdatedAt") manifest.dataUpdatedAt = ParseInt64(value, 0);
        else if (key == "releaseFile") manifest.releaseFile = value;
        else if (key == "notes") manifest.notes = value;
    }
    return manifest;
}

bool SaveCloudManifest(const CloudSyncConfig& config, const std::filesystem::path& storageDir, const CloudManifest& manifest) {
    const auto path = ResolveCloudManifestPath(config, storageDir);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream out(path);
    if (!out) return false;
    CloudManifest merged = manifest;
    if (merged.notes.empty()) {
        CloudManifest existing = LoadCloudManifest(config, storageDir);
        if (!existing.notes.empty()) merged.notes = existing.notes;
        if (merged.releaseFile.empty() && !existing.releaseFile.empty()) {
            merged.releaseFile = existing.releaseFile;
        }
    }
    out << "appVersion=" << merged.appVersion << "\n";
    out << "dataUpdatedAt=" << merged.dataUpdatedAt << "\n";
    if (!merged.releaseFile.empty()) {
        out << "releaseFile=" << merged.releaseFile << "\n";
    }
    if (!merged.notes.empty()) {
        out << "notes=" << merged.notes << "\n";
    }
    return true;
}

bool IsUpdateAvailable(const CloudManifest& manifest, const std::string& currentVersion) {
    if (manifest.appVersion.empty()) return false;
    return CompareVersions(manifest.appVersion, currentVersion) > 0;
}

CloudSyncResult DownloadCloudRelease(const CloudSyncConfig& config, const std::filesystem::path& storageDir,
                                     const CloudManifest& manifest, std::filesystem::path& outPath) {
    CloudSyncResult result;
    if (!config.enabled) {
        result.message = u8"Облако отключено.";
        return result;
    }
    if (manifest.releaseFile.empty()) {
        result.message = u8"В манифесте не указан файл обновления.";
        return result;
    }
    const auto cloudRoot = ResolveCloudRoot(config, storageDir);
    std::filesystem::path releasesDir = config.releasesDir.empty() ? "releases" : config.releasesDir;
    if (!releasesDir.is_absolute()) {
        releasesDir = cloudRoot / releasesDir;
    }
    const std::filesystem::path source = releasesDir / manifest.releaseFile;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        result.message = u8"Файл обновления не найден в облаке.";
        return result;
    }
    const std::filesystem::path targetDir = storageDir / "meta" / "updates";
    std::filesystem::create_directories(targetDir, ec);
    if (ec) {
        result.message = u8"Не удалось создать папку обновлений.";
        return result;
    }
    outPath = targetDir / manifest.releaseFile;
    if (!std::filesystem::copy_file(source, outPath, std::filesystem::copy_options::overwrite_existing, ec)) {
        result.message = u8"Не удалось скачать обновление.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.message = u8"Обновление скачано.";
    return result;
}

CloudSyncResult PullCloudSnapshot(const CloudSyncConfig& config, const std::filesystem::path& storageDir, CloudRole role) {
    CloudSyncResult result;
    if (!config.enabled) {
        result.message = u8"Облако отключено.";
        return result;
    }
    const auto cloudRoot = ResolveCloudRoot(config, storageDir);
    std::error_code ec;
    if (!std::filesystem::exists(cloudRoot, ec)) {
        result.message = u8"Облачное хранилище не найдено.";
        return result;
    }
    std::unordered_set<std::string> skipIds;
    CopyProfiles(cloudRoot, storageDir, role, config, result.stats, skipIds, true);
    CopyProfiles(cloudRoot / "archive", storageDir / "archive", role, config, result.stats, skipIds, true);
    CopyAchievements(cloudRoot / "achievements", storageDir / "achievements", skipIds, result.stats, true);
    CopyFileIfExists(cloudRoot / "skills.txt", storageDir / "skills.txt", result.stats, true);
    CopyFileIfExists(cloudRoot / "meta" / "gameplay.ini", storageDir / "meta" / "gameplay.ini", result.stats, true);
    result.ok = true;
    result.changed = result.stats.filesPulled > 0 || result.stats.profilesPulled > 0;
    if (result.changed) {
        result.message = u8"Данные из облака обновлены.";
    } else {
        result.message = u8"В облаке нет новых данных.";
    }
    return result;
}

CloudSyncResult PushCloudSnapshot(const CloudSyncConfig& config, const std::filesystem::path& storageDir, CloudRole role) {
    CloudSyncResult result;
    if (!config.enabled) {
        result.message = u8"Облако отключено.";
        return result;
    }
    if (role != CloudRole::Admin) {
        result.message = u8"Нет прав для выгрузки в облако.";
        return result;
    }
    const auto cloudRoot = ResolveCloudRoot(config, storageDir);
    std::error_code ec;
    std::filesystem::create_directories(cloudRoot, ec);
    std::unordered_set<std::string> skipIds;
    CopyProfiles(storageDir, cloudRoot, role, config, result.stats, skipIds, false);
    CopyProfiles(storageDir / "archive", cloudRoot / "archive", role, config, result.stats, skipIds, false);
    CopyAchievements(storageDir / "achievements", cloudRoot / "achievements", skipIds, result.stats, false);
    CopyFileIfExistsPush(storageDir / "skills.txt", cloudRoot / "skills.txt", result.stats);
    CopyFileIfExistsPush(storageDir / "meta" / "gameplay.ini", cloudRoot / "meta" / "gameplay.ini", result.stats);
    if (config.updateManifestOnPush) {
        CloudManifest manifest = LoadCloudManifest(config, storageDir);
        manifest.appVersion = APP_VERSION;
        manifest.dataUpdatedAt = NowSeconds();
        SaveCloudManifest(config, storageDir, manifest);
    }
    result.ok = true;
    result.changed = result.stats.filesPushed > 0 || result.stats.profilesPushed > 0;
    result.message = result.changed ? u8"Данные выгружены в облако." : u8"Нет данных для выгрузки.";
    return result;
}
