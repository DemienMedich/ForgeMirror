#include "CloudSync.h"
#include "JsonLite.h"

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
    return storageDir / "meta" / "cloud.json";
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
        return root / "meta" / "manifest.json";
    }
    if (config.manifest.is_absolute()) return config.manifest;
    return root / config.manifest;
}

std::string read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_all(const std::filesystem::path& p, const std::string& data) {
    auto parent = p.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return !ec;
}

std::filesystem::path LegacyCloudConfigPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "cloud.ini";
}

std::filesystem::path LegacyCloudManifestPath(const CloudSyncConfig& config,
                                              const std::filesystem::path& storageDir) {
    const auto root = ResolveCloudRoot(config, storageDir);
    if (config.manifest.empty()) {
        return root / "meta" / "manifest.ini";
    }
    if (config.manifest.is_absolute()) return config.manifest;
    return root / config.manifest;
}

bool IsAdminProfileFile(const std::filesystem::path& path) {
    if (path.extension() == ".json") {
        std::string content = read_all(path);
        if (content.empty()) return false;
        JsonLite::Value root;
        if (!JsonLite::Parse(content, root, nullptr)) return false;
        if (const auto* profile = JsonLite::GetObjectValue(root, "profile")) {
            if (const auto* admin = JsonLite::GetObjectValue(*profile, "admin")) {
                return JsonLite::GetBool(*admin, false);
            }
        }
        if (const auto* admin = JsonLite::GetObjectValue(root, "admin")) {
            return JsonLite::GetBool(*admin, false);
        }
        return false;
    }
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
        const auto ext = entry.path().extension();
        if (ext != ".json" && ext != ".ini") continue;
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
    const auto path = CloudConfigPath(storageDir);
    std::string json = read_all(path);
    if (!json.empty()) {
        JsonLite::Value root;
        if (JsonLite::Parse(json, root, nullptr)) {
            if (const auto* v = JsonLite::GetObjectValue(root, "enabled")) {
                config.enabled = JsonLite::GetBool(*v, config.enabled);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "autoPull")) {
                config.autoPull = JsonLite::GetBool(*v, config.autoPull);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "autoPush")) {
                config.autoPush = JsonLite::GetBool(*v, config.autoPush);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "includeAdminProfiles")) {
                config.includeAdminProfiles = JsonLite::GetBool(*v, config.includeAdminProfiles);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "root")) {
                config.root = JsonLite::GetString(*v, config.root.string());
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "manifest")) {
                config.manifest = JsonLite::GetString(*v, config.manifest.string());
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "releasesDir")) {
                config.releasesDir = JsonLite::GetString(*v, config.releasesDir.string());
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "updateManifestOnPush")) {
                config.updateManifestOnPush = JsonLite::GetBool(*v, config.updateManifestOnPush);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "autoSyncEnabled")) {
                config.autoSyncEnabled = JsonLite::GetBool(*v, config.autoSyncEnabled);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "autoSyncMinutes")) {
                config.autoSyncMinutes = static_cast<int>(JsonLite::GetInt64(*v, config.autoSyncMinutes));
            }
            return config;
        }
    }

    auto legacyPath = LegacyCloudConfigPath(storageDir);
    std::ifstream in(legacyPath);
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
        else if (key == "autoSyncEnabled") config.autoSyncEnabled = ParseBool(value, config.autoSyncEnabled);
        else if (key == "autoSyncMinutes") config.autoSyncMinutes = static_cast<int>(ParseInt64(value, config.autoSyncMinutes));
    }
    if (SaveCloudSyncConfig(storageDir, config)) {
        std::error_code ec;
        std::filesystem::remove(legacyPath, ec);
    }
    return config;
}

bool SaveCloudSyncConfig(const std::filesystem::path& storageDir, const CloudSyncConfig& config) {
    const auto path = CloudConfigPath(storageDir);
    std::ostringstream out;
    out << "{\n";
    out << "  \"enabled\": " << (config.enabled ? "true" : "false") << ",\n";
    out << "  \"autoPull\": " << (config.autoPull ? "true" : "false") << ",\n";
    out << "  \"autoPush\": " << (config.autoPush ? "true" : "false") << ",\n";
    out << "  \"includeAdminProfiles\": " << (config.includeAdminProfiles ? "true" : "false") << ",\n";
    out << "  \"root\": \"" << JsonLite::Escape(config.root.string()) << "\",\n";
    out << "  \"manifest\": \"" << JsonLite::Escape(config.manifest.string()) << "\",\n";
    out << "  \"releasesDir\": \"" << JsonLite::Escape(config.releasesDir.string()) << "\",\n";
    out << "  \"updateManifestOnPush\": " << (config.updateManifestOnPush ? "true" : "false") << ",\n";
    out << "  \"autoSyncEnabled\": " << (config.autoSyncEnabled ? "true" : "false") << ",\n";
    out << "  \"autoSyncMinutes\": " << config.autoSyncMinutes << "\n";
    out << "}\n";
    return write_all(path, out.str());
}

CloudManifest LoadCloudManifest(const CloudSyncConfig& config, const std::filesystem::path& storageDir) {
    CloudManifest manifest;
    const auto path = ResolveCloudManifestPath(config, storageDir);
    std::string json = read_all(path);
    if (!json.empty()) {
        JsonLite::Value root;
        if (JsonLite::Parse(json, root, nullptr)) {
            if (const auto* v = JsonLite::GetObjectValue(root, "appVersion")) {
                manifest.appVersion = JsonLite::GetString(*v, manifest.appVersion);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "dataUpdatedAt")) {
                manifest.dataUpdatedAt = JsonLite::GetInt64(*v, manifest.dataUpdatedAt);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "releaseFile")) {
                manifest.releaseFile = JsonLite::GetString(*v, manifest.releaseFile);
            }
            if (const auto* v = JsonLite::GetObjectValue(root, "notes")) {
                manifest.notes = JsonLite::GetString(*v, manifest.notes);
            }
            return manifest;
        }
    }

    const auto legacyPath = LegacyCloudManifestPath(config, storageDir);
    std::ifstream in(legacyPath);
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
    if (SaveCloudManifest(config, storageDir, manifest)) {
        std::error_code ec;
        std::filesystem::remove(legacyPath, ec);
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
    out << "{\n";
    out << "  \"appVersion\": \"" << JsonLite::Escape(merged.appVersion) << "\",\n";
    out << "  \"dataUpdatedAt\": " << merged.dataUpdatedAt;
    if (!merged.releaseFile.empty()) {
        out << ",\n  \"releaseFile\": \"" << JsonLite::Escape(merged.releaseFile) << "\"";
    }
    if (!merged.notes.empty()) {
        out << ",\n  \"notes\": \"" << JsonLite::Escape(merged.notes) << "\"";
    }
    out << "\n}\n";
    return write_all(path, out.str());
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
    CopyFileIfExists(cloudRoot / "meta" / "tasks.json", storageDir / "meta" / "tasks.json", result.stats, true);
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
    CopyFileIfExistsPush(storageDir / "meta" / "tasks.json", cloudRoot / "meta" / "tasks.json", result.stats);
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
