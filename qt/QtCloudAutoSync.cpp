#include "QtCloudAutoSync.h"
#include "QtCloudPull.h"
#include "QtCloudPushPreview.h"
#include <algorithm>

bool QtCloudAutoSyncDue(const CloudSyncConfig& config, std::int64_t nowSeconds,
    std::int64_t lastAttemptSeconds) {
    if (!config.enabled || !config.autoSyncEnabled || lastAttemptSeconds <= 0 || nowSeconds < lastAttemptSeconds)
        return false;
    const auto minutes = std::clamp(config.autoSyncMinutes, 1, 120);
    return nowSeconds - lastAttemptSeconds >= std::int64_t(minutes) * 60;
}

namespace {
QtCloudPushPreviewResult transactionalPush(const CloudSyncConfig& config,
    const std::filesystem::path& workspace, CloudRole role) {
    auto preview = PreviewQtCloudWorkspacePush(config, workspace, role);
    if (!preview.sync.ok || !preview.sync.changed) return preview;
    return RunQtCloudWorkspacePush(config, workspace, role, &preview);
}
}

static QtCloudAutoSyncResult runSync(const CloudSyncConfig& config,
    const std::filesystem::path& workspace, CloudRole role,
    const std::string& unlockedWalletProfileId, bool requirePeriodicSyncEnabled) {
    QtCloudAutoSyncResult result;
    if (!config.enabled || (requirePeriodicSyncEnabled && !config.autoSyncEnabled)) {
        result.message = !config.enabled ? u8"Облако отключено."
            : requirePeriodicSyncEnabled ? u8"Автосинхронизация отключена."
                                         : u8"Настройте хотя бы одно направление синхронизации.";
        return result;
    }

    const bool doPull = config.autoPull;
    const bool doPush = role == CloudRole::Admin && config.autoPush;
    const bool doWallet = role != CloudRole::Admin && !unlockedWalletProfileId.empty();
    result.attempted = doPull || doPush || doWallet;
    if (!result.attempted) {
        result.ok = true;
        result.message = requirePeriodicSyncEnabled ? u8"Автоматические действия синхронизации отключены."
                                                     : u8"Настройте загрузку или выгрузку в разделе «Облако».";
        return result;
    }

    bool ok = true;
    bool changed = false;
    std::string failure;
    auto push = [&] {
        result.pushAttempted = true;
        const auto pushed = transactionalPush(config, workspace, CloudRole::Admin);
        if (!pushed.sync.ok) {
            ok = false;
            if (failure.empty()) failure = pushed.message;
            result.recoveryPending = std::filesystem::exists(workspace / "meta/qt-cloud-push.json");
        } else changed = changed || pushed.sync.changed;
    };
    auto pull = [&] {
        result.pullAttempted = true;
        const auto pulled = RunQtCloudPullTransaction(config, workspace, role);
        if (!pulled.sync.ok) {
            ok = false;
            if (failure.empty()) failure = pulled.message;
            result.storageConflict = pulled.sync.storageConflict;
            result.recoveryPending = std::filesystem::exists(workspace / "meta/qt-cloud-pull.json");
        } else {
            result.pullChanged = pulled.sync.changed;
            changed = changed || pulled.sync.changed;
        }
    };

    // Match the stable client: admins push first when both directions are enabled.
    if (doPush && doPull) push();
    if (doPull && (ok || (!requirePeriodicSyncEnabled && !result.recoveryPending))) pull();
    if (ok && doPush && !doPull) push();
    if (doWallet && (ok || (!requirePeriodicSyncEnabled && !result.recoveryPending))) {
        result.walletAttempted = true;
        const auto wallet = PushProfileWallet(config, workspace, unlockedWalletProfileId);
        if (!wallet.ok) { ok = false; if (failure.empty()) failure = wallet.message; }
        else changed = changed || wallet.changed;
    }

    result.ok = ok;
    result.changed = changed;
    result.message = !ok ? failure : changed
        ? (requirePeriodicSyncEnabled ? u8"Автоматическая синхронизация завершена." : u8"Быстрая синхронизация завершена.")
        : u8"Данные синхронизированы.";
    return result;
}

QtCloudAutoSyncResult RunQtCloudAutoSync(const CloudSyncConfig& config,
    const std::filesystem::path& workspace, CloudRole role,
    const std::string& unlockedWalletProfileId) {
    return runSync(config, workspace, role, unlockedWalletProfileId, true);
}

QtCloudAutoSyncResult RunQtCloudQuickSync(const CloudSyncConfig& config,
    const std::filesystem::path& workspace, CloudRole role,
    const std::string& unlockedWalletProfileId) {
    return runSync(config, workspace, role, unlockedWalletProfileId, false);
}
