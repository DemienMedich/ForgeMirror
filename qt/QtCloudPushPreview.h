#pragma once
#include "CloudSync.h"
#include <filesystem>
#include <string>

struct QtCloudPushPreviewResult {
    CloudSyncResult sync;
    int filesAdded = 0;
    int filesReplaced = 0;
    int filesRemoved = 0;
    std::string message;
};

// Simulate the legacy whole-workspace push against a private copy of the cloud.
// This function never writes to the configured cloud root.
QtCloudPushPreviewResult PreviewQtCloudWorkspacePush(const CloudSyncConfig& config,
    const std::filesystem::path& workspaceDirectory, CloudRole role);
