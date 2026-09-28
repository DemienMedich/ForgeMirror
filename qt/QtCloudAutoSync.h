#pragma once

#include "CloudSync.h"
#include <cstdint>
#include <filesystem>
#include <string>

struct QtCloudAutoSyncResult {
    bool ok = false;
    bool attempted = false;
    bool changed = false;
    bool pullAttempted = false;
    bool pullChanged = false;
    bool storageConflict = false;
    bool pushAttempted = false;
    bool walletAttempted = false;
    bool recoveryPending = false;
    std::string message;
};

bool QtCloudAutoSyncDue(const CloudSyncConfig& config, std::int64_t nowSeconds,
    std::int64_t lastAttemptSeconds);

// Runs the configured direction actions on demand, independently of the
// periodic auto-sync toggle. The direction toggles themselves remain honored.
QtCloudAutoSyncResult RunQtCloudQuickSync(const CloudSyncConfig& config,
    const std::filesystem::path& workspaceDirectory, CloudRole role,
    const std::string& unlockedWalletProfileId = {});

// Executes only the explicitly enabled legacy automatic-sync actions, using
// Qt's journaled workspace pull and push transactions.
QtCloudAutoSyncResult RunQtCloudAutoSync(const CloudSyncConfig& config,
    const std::filesystem::path& workspaceDirectory, CloudRole role,
    const std::string& unlockedWalletProfileId = {});

