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

QtCloudAutoSyncResult RunQtCloudAutoSync(const CloudSyncConfig& config,
    const std::filesystem::path& workspace, CloudRole role,
    const std::string& unlockedWalletProfileId) {
    QtCloudAutoSyncResult result;
    if (!config.enabled || !config.autoSyncEnabled) {
        result.message = u8"Автосинхронизация отключена.";
        return result;
    }

    const bool doPull = config.autoPull;
    const bool doPush = role == CloudRole::Admin && config.autoPush;
    const bool doWallet = role != CloudRole::Admin && !unlockedWalletProfileId.empty();
    result.attempted = doPull || doPush || doWallet;
    if (!result.attempted) {
        result.ok = true;
        result.message = u8"Автоматические действия синхронизации отключены.";
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
            failure = pushed.message;
            result.recoveryPending = std::filesystem::exists(workspace / "meta/qt-cloud-push.json");
        } else changed = changed || pushed.sync.changed;
    };
    auto pull = [&] {
        result.pullAttempted = true;
        const auto pulled = RunQtCloudPullTransaction(config, workspace, role);
        if (!pulled.sync.ok) {
            ok = false;
            failure = pulled.message;
            result.recoveryPending = std::filesystem::exists(workspace / "meta/qt-cloud-pull.json");
        } else {
            result.pullChanged = pulled.sync.changed;
            changed = changed || pulled.sync.changed;
        }
    };

    // Match the stable client: admins push first when both directions are enabled.
    if (doPush && doPull) push();
    if (ok && doPull) pull();
    if (ok && doPush && !doPull) push();
    if (ok && doWallet) {
        result.walletAttempted = true;
        const auto wallet = PushProfileWallet(config, workspace, unlockedWalletProfileId);
        if (!wallet.ok) { ok = false; failure = wallet.message; }
        else changed = changed || wallet.changed;
    }

    result.ok = ok;
    result.changed = changed;
    result.message = !ok ? failure : changed ? u8"Автоматическая синхронизация завершена."
                                             : u8"Данные синхронизированы.";
    return result;
}
