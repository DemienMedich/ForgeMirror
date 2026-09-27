#pragma once

#include "CloudSync.h"
#include <filesystem>
#include <optional>
#include <string>

struct QtCloudReleaseResult {
    bool ok = false;
    bool changed = false;
    std::filesystem::path path;
    std::string sha256;
    std::string message;
};

std::optional<std::filesystem::path> QtCloudReleaseTargetPath(
    const std::filesystem::path& workspaceDirectory, const CloudManifest& manifest);
QtCloudReleaseResult DownloadQtCloudRelease(const CloudSyncConfig& config,
    const std::filesystem::path& workspaceDirectory, const CloudManifest& manifest);

