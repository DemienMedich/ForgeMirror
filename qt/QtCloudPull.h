#pragma once
#include "CloudSync.h"
#include <filesystem>
#include <string>

struct QtCloudPullResult {
    CloudSyncResult sync;
    bool rolledBack = false;
    std::filesystem::path backupPath;
    std::string message;
};

// Throws on invalid recovery; call before loading workspace data.
bool RecoverQtCloudPull(const std::filesystem::path& workspaceDirectory);

QtCloudPullResult RunQtCloudPullTransaction(const CloudSyncConfig& config,
                                            const std::filesystem::path& workspaceDirectory,
                                            CloudRole role);
