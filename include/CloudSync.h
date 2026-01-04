#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

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
};

struct CloudSyncResult {
    bool ok = false;
    bool changed = false;
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
