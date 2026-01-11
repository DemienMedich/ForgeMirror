#include "CloudSync.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <locale>
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

void StripUtf8Bom(std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        // Write BOM for editors (Notepad) to recognize UTF-8
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        out << data;
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

std::filesystem::path CanonicalSafe(const std::filesystem::path& p) {
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(p, ec);
    if (ec) return {};
    return c;
}

bool PathsOverlap(const std::filesystem::path& a, const std::filesystem::path& b) {
    auto ca = CanonicalSafe(a);
    auto cb = CanonicalSafe(b);
    if (ca.empty() || cb.empty()) return false;
    std::error_code ec;
    if (std::filesystem::equivalent(ca, cb, ec)) return true;
    if (ec) ec.clear();
    auto norm = [](std::string s) {
        if (!s.empty() && s.back() != '/') s.push_back('/');
        return s;
    };
    const std::string sa = norm(ca.generic_string());
    const std::string sb = norm(cb.generic_string());
    if (sa.rfind(sb, 0) == 0) return true; // a inside b
    if (sb.rfind(sa, 0) == 0) return true; // b inside a
    return false;
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

bool CopyFileIfExists(const std::filesystem::path& src, const std::filesystem::path& dst,
                      CloudSyncStats& stats, bool count, std::string* errorMessage) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return false;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        stats.ioErrors += 1;
        if (errorMessage && errorMessage->empty()) {
            *errorMessage = u8"Не удалось создать папку для ";
            *errorMessage += dst.filename().u8string();
        }
        return false;
    }
    if (std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
        if (count) stats.filesPulled += 1;
        return true;
    }
    if (ec) {
        stats.ioErrors += 1;
        if (errorMessage && errorMessage->empty()) {
            *errorMessage = u8"Ошибка копирования ";
            *errorMessage += src.filename().u8string();
        }
    }
    return false;
}

bool CopyFileIfExistsPush(const std::filesystem::path& src, const std::filesystem::path& dst,
                          CloudSyncStats& stats, std::string* errorMessage) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return false;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        stats.ioErrors += 1;
        if (errorMessage && errorMessage->empty()) {
            *errorMessage = u8"Не удалось создать папку для ";
            *errorMessage += dst.filename().u8string();
        }
        return false;
    }
    if (std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
        stats.filesPushed += 1;
        return true;
    }
    if (ec) {
        stats.ioErrors += 1;
        if (errorMessage && errorMessage->empty()) {
            *errorMessage = u8"Ошибка копирования ";
            *errorMessage += src.filename().u8string();
        }
    }
    return false;
}

void CopyProfiles(const std::filesystem::path& srcRoot, const std::filesystem::path& dstRoot,
                  CloudRole role, const CloudSyncConfig& config, CloudSyncStats& stats,
                  std::unordered_set<std::string>& skippedAdminIds, bool pulling,
                  std::string* errorMessage) {
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
            if (CopyFileIfExists(entry.path(), dst, stats, true, errorMessage)) stats.profilesPulled += 1;
        } else {
            if (CopyFileIfExistsPush(entry.path(), dst, stats, errorMessage)) stats.profilesPushed += 1;
        }
    }
}

void CopyAchievements(const std::filesystem::path& srcRoot, const std::filesystem::path& dstRoot,
                      const std::unordered_set<std::string>& skipIds, CloudSyncStats& stats, bool pulling,
                      std::string* errorMessage) {
    std::error_code ec;
    if (!std::filesystem::exists(srcRoot, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(srcRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        const std::string id = entry.path().stem().string();
        if (skipIds.find(id) != skipIds.end()) continue;
        std::filesystem::path dst = dstRoot / entry.path().filename();
        if (pulling) {
            CopyFileIfExists(entry.path(), dst, stats, true, errorMessage);
        } else {
            CopyFileIfExistsPush(entry.path(), dst, stats, errorMessage);
        }
    }
}

void CopyAchievementIcons(const std::filesystem::path& srcRoot, const std::filesystem::path& dstRoot,
                          CloudSyncStats& stats, bool pulling, std::string* errorMessage) {
    std::error_code ec;
    if (!std::filesystem::exists(srcRoot, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(srcRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        std::filesystem::path dst = dstRoot / entry.path().filename();
        if (pulling) {
            CopyFileIfExists(entry.path(), dst, stats, true, errorMessage);
        } else {
            CopyFileIfExistsPush(entry.path(), dst, stats, errorMessage);
        }
    }
}

bool RemoveOrphanedProfiles(const std::filesystem::path& srcRoot, const std::filesystem::path& dstRoot, int& removed) {
    std::error_code ec;
    if (!std::filesystem::exists(dstRoot, ec)) return false;
    std::unordered_set<std::string> keep;
    if (std::filesystem::exists(srcRoot, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(srcRoot, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".ini") continue;
            keep.insert(entry.path().filename().string());
        }
    }
    bool removedAny = false;
    for (const auto& entry : std::filesystem::directory_iterator(dstRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        const std::string name = entry.path().filename().string();
        if (keep.find(name) != keep.end()) continue;
        std::filesystem::remove(entry.path(), ec);
        if (!ec) {
            removed += 1;
            removedAny = true;
        }
    }
    return removedAny;
}

bool RemoveFileIfMissing(const std::filesystem::path& srcPath, const std::filesystem::path& dstPath, int& removed) {
    std::error_code ec;
    const bool srcExists = std::filesystem::exists(srcPath, ec);
    if (ec) return false;
    if (srcExists) return false;
    if (!std::filesystem::exists(dstPath, ec)) return false;
    std::filesystem::remove(dstPath, ec);
    if (ec) return false;
    removed += 1;
    return true;
}

bool RemoveOrphanedAchievements(const std::filesystem::path& srcRoot, const std::filesystem::path& srcArchive,
                                const std::filesystem::path& dstRoot, int& removed) {
    std::error_code ec;
    if (!std::filesystem::exists(dstRoot, ec)) return false;
    std::unordered_set<std::string> keep;
    auto collect = [&](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".ini") continue;
            keep.insert(entry.path().stem().string());
        }
    };
    collect(srcRoot);
    collect(srcArchive);
    bool removedAny = false;
    for (const auto& entry : std::filesystem::directory_iterator(dstRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        const std::string id = entry.path().stem().string();
        if (keep.find(id) != keep.end()) continue;
        std::filesystem::remove(entry.path(), ec);
        if (!ec) {
            removed += 1;
            removedAny = true;
        }
    }
    return removedAny;
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
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (firstLine) {
            StripUtf8Bom(line);
            firstLine = false;
        }
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
        else if (key == "root") config.root = std::filesystem::u8path(value);
        else if (key == "manifest") config.manifest = std::filesystem::u8path(value);
        else if (key == "releasesDir") config.releasesDir = std::filesystem::u8path(value);
        else if (key == "updateManifestOnPush") config.updateManifestOnPush = ParseBool(value, config.updateManifestOnPush);
        else if (key == "autoSyncEnabled") config.autoSyncEnabled = ParseBool(value, config.autoSyncEnabled);
        else if (key == "autoSyncMinutes") config.autoSyncMinutes = static_cast<int>(ParseInt64(value, config.autoSyncMinutes));
    }
    return config;
}

bool SaveCloudSyncConfig(const std::filesystem::path& storageDir, const CloudSyncConfig& config) {
    const auto path = CloudConfigPath(storageDir);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[cloud]\n";
    out << "enabled=" << (config.enabled ? 1 : 0) << "\n";
    out << "autoPull=" << (config.autoPull ? 1 : 0) << "\n";
    out << "autoPush=" << (config.autoPush ? 1 : 0) << "\n";
    out << "includeAdminProfiles=" << (config.includeAdminProfiles ? 1 : 0) << "\n";
    out << "root=" << config.root.u8string() << "\n";
    out << "manifest=" << config.manifest.u8string() << "\n";
    out << "releasesDir=" << config.releasesDir.u8string() << "\n";
    out << "updateManifestOnPush=" << (config.updateManifestOnPush ? 1 : 0) << "\n";
    out << "autoSyncEnabled=" << (config.autoSyncEnabled ? 1 : 0) << "\n";
    out << "autoSyncMinutes=" << config.autoSyncMinutes << "\n";
    return WriteTextFileAtomic(path, out.str());
}

CloudManifest LoadCloudManifest(const CloudSyncConfig& config, const std::filesystem::path& storageDir) {
    CloudManifest manifest;
    const auto path = ResolveCloudManifestPath(config, storageDir);
    std::ifstream in(path);
    if (!in) return manifest;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (firstLine) {
            StripUtf8Bom(line);
            firstLine = false;
        }
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
    CloudManifest merged = manifest;
    if (merged.notes.empty()) {
        CloudManifest existing = LoadCloudManifest(config, storageDir);
        if (!existing.notes.empty()) merged.notes = existing.notes;
        if (merged.releaseFile.empty() && !existing.releaseFile.empty()) {
            merged.releaseFile = existing.releaseFile;
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "appVersion=" << merged.appVersion << "\n";
    out << "dataUpdatedAt=" << merged.dataUpdatedAt << "\n";
    if (!merged.releaseFile.empty()) {
        out << "releaseFile=" << merged.releaseFile << "\n";
    }
    if (!merged.notes.empty()) {
        out << "notes=" << merged.notes << "\n";
    }
    return WriteTextFileAtomic(path, out.str());
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
    if (PathsOverlap(cloudRoot, storageDir)) {
        result.message = u8"Путь облака не должен совпадать или быть вложенным в локальное хранилище.";
        return result;
    }
    std::error_code ec;
    if (!std::filesystem::exists(cloudRoot, ec)) {
        result.message = u8"Облачное хранилище не найдено.";
        return result;
    }
    std::string ioError;
    std::unordered_set<std::string> skipIds;
    CopyProfiles(cloudRoot, storageDir, role, config, result.stats, skipIds, true, &ioError);
    CopyProfiles(cloudRoot / "archive", storageDir / "archive", role, config, result.stats, skipIds, true, &ioError);
    CopyAchievements(cloudRoot / "achievements", storageDir / "achievements", skipIds, result.stats, true, &ioError);
    CopyAchievementIcons(cloudRoot / "achievements" / "icons", storageDir / "achievements" / "icons", result.stats, true, &ioError);
    CopyFileIfExists(cloudRoot / "skills.txt", storageDir / "skills.txt", result.stats, true, &ioError);
    CopyFileIfExists(cloudRoot / "meta" / "pipeline.json", storageDir / "meta" / "pipeline.json", result.stats, true, &ioError);
    CopyFileIfExists(cloudRoot / "meta" / "tasks.json", storageDir / "meta" / "tasks.json", result.stats, true, &ioError);
    result.ok = result.stats.ioErrors == 0;
    result.changed = result.stats.filesPulled > 0 || result.stats.profilesPulled > 0;
    if (!result.ok) {
        result.message = ioError.empty() ? u8"Ошибка копирования данных из облака." : ioError;
    } else if (result.changed) {
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
    if (PathsOverlap(cloudRoot, storageDir)) {
        result.message = u8"Путь облака не должен совпадать или быть вложенным в локальное хранилище.";
        return result;
    }
    std::error_code ec;
    std::filesystem::create_directories(cloudRoot, ec);
    std::string ioError;
    std::unordered_set<std::string> skipIds;
    CopyProfiles(storageDir, cloudRoot, role, config, result.stats, skipIds, false, &ioError);
    CopyProfiles(storageDir / "archive", cloudRoot / "archive", role, config, result.stats, skipIds, false, &ioError);
    CopyAchievements(storageDir / "achievements", cloudRoot / "achievements", skipIds, result.stats, false, &ioError);
    CopyAchievementIcons(storageDir / "achievements" / "icons", cloudRoot / "achievements" / "icons", result.stats, false, &ioError);
    CopyFileIfExistsPush(storageDir / "skills.txt", cloudRoot / "skills.txt", result.stats, &ioError);
    CopyFileIfExistsPush(storageDir / "meta" / "pipeline.json", cloudRoot / "meta" / "pipeline.json", result.stats, &ioError);
    CopyFileIfExistsPush(storageDir / "meta" / "tasks.json", cloudRoot / "meta" / "tasks.json", result.stats, &ioError);
    int removed = 0;
    const bool removedAny = RemoveOrphanedProfiles(storageDir, cloudRoot, removed)
        || RemoveOrphanedProfiles(storageDir / "archive", cloudRoot / "archive", removed)
        || RemoveOrphanedAchievements(storageDir, storageDir / "archive", cloudRoot / "achievements", removed)
        || RemoveFileIfMissing(storageDir / "skills.txt", cloudRoot / "skills.txt", removed)
        || RemoveFileIfMissing(storageDir / "meta" / "pipeline.json", cloudRoot / "meta" / "pipeline.json", removed)
        || RemoveFileIfMissing(storageDir / "meta" / "tasks.json", cloudRoot / "meta" / "tasks.json", removed);
    if (config.updateManifestOnPush) {
        CloudManifest manifest = LoadCloudManifest(config, storageDir);
        manifest.appVersion = APP_VERSION;
        manifest.dataUpdatedAt = NowSeconds();
        SaveCloudManifest(config, storageDir, manifest);
    }
    result.ok = result.stats.ioErrors == 0;
    result.changed = result.stats.filesPushed > 0 || result.stats.profilesPushed > 0 || removedAny;
    if (!result.ok) {
        result.message = ioError.empty() ? u8"Ошибка копирования данных в облако." : ioError;
    } else if (!result.changed) {
        result.message = u8"Нет данных для выгрузки.";
    } else if (removedAny && result.stats.filesPushed == 0 && result.stats.profilesPushed == 0) {
        result.message = u8"Лишние данные удалены из облака.";
    } else if (removedAny) {
        result.message = u8"Данные выгружены и очищены от лишнего.";
    } else {
        result.message = u8"Данные выгружены в облако.";
    }
    return result;
}
