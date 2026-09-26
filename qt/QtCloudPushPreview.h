#pragma once
#include "CloudSync.h"
#include <QByteArray>
#include <filesystem>
#include <string>
#include <vector>

struct QtCloudPushFileChange {
    std::string relativePath;
    bool existedBefore = false;
    bool existsAfter = false;
    QByteArray beforeBytes;
    QByteArray afterBytes;
};

struct QtCloudPushPreviewResult {
    CloudSyncResult sync;
    std::filesystem::path cloudRoot;
    std::filesystem::path backupPath;
    std::vector<QtCloudPushFileChange> changes;
    int filesAdded = 0;
    int filesReplaced = 0;
    int filesRemoved = 0;
    std::string message;
};

// Simulate the legacy whole-workspace push against a private copy of the cloud.
// This function never writes to the configured cloud root.
QtCloudPushPreviewResult PreviewQtCloudWorkspacePush(const CloudSyncConfig& config,
    const std::filesystem::path& workspaceDirectory, CloudRole role);

QtCloudPushPreviewResult RunQtCloudWorkspacePush(const CloudSyncConfig& config,
    const std::filesystem::path& workspaceDirectory, CloudRole role,
    const QtCloudPushPreviewResult* approvedPreview = nullptr);
bool RecoverQtCloudPush(const std::filesystem::path& workspaceDirectory);
void QtSetCloudPushFailureAfterFileWritesForTests(int count);
void QtSetCloudPushLeaveJournalForTests(bool enabled);
