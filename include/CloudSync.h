#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_set>

enum class CloudRole {
    Viewer,
    Admin
};

struct CloudSyncStats {
    int profilesPulled = 0;
    int profilesPushed = 0;
    int filesPulled = 0;
    int filesPushed = 0;
    int adminSkipped = 0;
    int ioErrors = 0;
};

struct CloudSyncResult {
    bool ok = false;
    bool changed = false;
    bool storageConflict = false;
    std::filesystem::path storageConflictPath;
    std::string message;
    CloudSyncStats stats;
};

struct CloudSyncConfig {
    bool enabled = false;
    bool autoPull = true;
    bool autoPush = true;
    bool includeAdminProfiles = false;
    std::filesystem::path root;
    std::filesystem::path manifest;
    std::filesystem::path releasesDir;
    bool updateManifestOnPush = true;
    bool autoSyncEnabled = false;
    int autoSyncMinutes = 15;
};

struct CloudManifest {
    std::string appVersion;
    std::int64_t dataUpdatedAt = 0;
    std::string releaseFile;
    std::string notes;
};

CloudSyncConfig LoadCloudSyncConfig(const std::filesystem::path& storageDir);
bool SaveCloudSyncConfig(const std::filesystem::path& storageDir, const CloudSyncConfig& config);
CloudManifest LoadCloudManifest(const CloudSyncConfig& config, const std::filesystem::path& storageDir);
bool SaveCloudManifest(const CloudSyncConfig& config, const std::filesystem::path& storageDir, const CloudManifest& manifest);
bool IsUpdateAvailable(const CloudManifest& manifest, const std::string& currentVersion);
CloudSyncResult DownloadCloudRelease(const CloudSyncConfig& config, const std::filesystem::path& storageDir,
                                     const CloudManifest& manifest, std::filesystem::path& outPath);
CloudSyncResult PullCloudSnapshot(const CloudSyncConfig& config, const std::filesystem::path& storageDir, CloudRole role);
CloudSyncResult PushCloudSnapshot(const CloudSyncConfig& config, const std::filesystem::path& storageDir, CloudRole role);
CloudSyncResult PushProfileWallet(const CloudSyncConfig& config, const std::filesystem::path& storageDir, const std::string& profileId);
// Remove files/dirs outside whitelist in storageDir; returns true if any removed, writes count.
inline bool RemoveStrayFiles(const std::filesystem::path& storageDir, int& removed) {
    removed = 0;
    std::error_code ec;
    if (!std::filesystem::exists(storageDir, ec)) return false;
    const std::unordered_set<std::string> allowedDirs = {
        "", "archive", "achievements", "achievements/icons", "meta", "meta/patch-notes",
        "meta/ui-presets", "meta/reports", "meta/updates", "logs", "cloud", "cloud/releases"
    };
    const std::unordered_set<std::string> allowedMetaFiles = {
        "pipeline.json", "tasks.json", "projects.json", "gameplay.ini", "shortcuts.json", "ui.ini", "cloud.ini",
        "professions.txt", "banner.json", "storage.json", "profile-audit.log", "task-audit.log", "seed.merged", "gui-layout.ini", "admin.ini"
    };
    bool any = false;
    for (auto it = std::filesystem::recursive_directory_iterator(storageDir, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        auto rel = std::filesystem::relative(entry.path(), storageDir, ec);
        if (ec) break;
        if (rel.empty()) continue;
        const bool isDir = entry.is_directory();
        const std::string dirStr = rel.parent_path().generic_string();
        const std::string name = rel.filename().string();
        auto dirAllowed = [&](const std::string& d) { return allowedDirs.find(d) != allowedDirs.end(); };
        auto fileAllowed = [&]() {
            if (dirStr.empty()) {
                if (entry.path().extension() == ".ini") return true;
                if (name == "skills.txt") return true;
                return false;
            }
            if (dirStr == "archive") return entry.path().extension() == ".ini";
            if (dirStr == "achievements") return entry.path().extension() == ".json";
            if (dirStr == "achievements/icons") return entry.path().extension() == ".png";
            if (dirStr == "meta") return allowedMetaFiles.find(name) != allowedMetaFiles.end();
            if (dirStr == "meta/patch-notes") return entry.path().extension() == ".md";
            if (dirStr == "meta/ui-presets") return entry.path().extension() == ".ini";
            if (dirStr == "meta/reports") return entry.path().extension() == ".txt" || entry.path().extension() == ".csv";
            if (dirStr == "meta/updates") return true;
            if (dirStr == "logs") return true;
            if (dirStr == "cloud") return true;
            if (dirStr == "cloud/releases") return true;
            return false;
        };

        if (isDir) {
            if (!dirAllowed(rel.generic_string())) {
                std::filesystem::remove_all(entry.path(), ec);
                if (!ec) {
                    removed++;
                    any = true;
                    it.disable_recursion_pending();
                }
            }
        } else {
            if (!fileAllowed()) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) {
                    removed++;
                    any = true;
                }
            }
        }
    }
    return any;
}

