static void RefreshCloudManifestState(GuiState& state, const AppContext& app) {
    state.cloudManifest = LoadCloudManifest(state.cloudConfig, app.storageDir);
    state.cloudUpdateAvailable = IsUpdateAvailable(state.cloudManifest, APP_VERSION);
}

static void EnsureStorageVaultFile(GuiState& state, const AppContext& app) {
    const auto storageVaultPath = app.storageDir / "meta" / "storage.json";
    std::error_code vaultEc;
    if (!std::filesystem::exists(storageVaultPath, vaultEc)) {
        if (SaveStorageVault(app.storageDir, state.vault)) {
            state.vault = LoadStorageVault(app.storageDir);
        }
    }
}

static void ApplyWorkspaceSnapshot(GuiState& state, WorkspaceDataSnapshot snapshot) {
    state.tasks = std::move(snapshot.tasks);
    state.taskAudit = std::move(snapshot.taskAudit);
    state.projects = std::move(snapshot.projects);
    state.shortcuts = std::move(snapshot.shortcuts);
    state.pipelineSteps = std::move(snapshot.pipelineSteps);
    state.professions = std::move(snapshot.professions);
    state.bannerTexts = std::move(snapshot.bannerTexts);
    state.rulesConfig = snapshot.rulesConfig;
    state.rulesDraft = state.rulesConfig;
    state.vault = snapshot.vault;
    SyncVaultBuffers(state);
    state.profileProfessionCache.clear();
    state.profileLoginCache.clear();
    state.profileBlockedCache.clear();
    state.profileWalletCache.clear();
    state.profileProfessionDirty = true;
    state.adminStatsDirty = true;
}

static void LoadWorkspaceCollections(GuiState& state, const AppContext& app) {
    ApplyWorkspaceSnapshot(state, LoadWorkspaceDataSnapshot(app.storageDir, state.modules));
}

static void ReloadSyncedWorkspaceData(GuiState& state, AppContext& app, bool reloadProfiles = true) {
    if (reloadProfiles) {
        const std::string preferredId = state.active ? state.active->id : state.ui.lastProfileId;
        RefreshProfiles(state, app, preferredId);
    }
    ApplyWorkspaceSnapshot(state, LoadWorkspaceDataSnapshot(app.storageDir, state.modules));
    EnsureBannerSelection(state);
    state.bannerLastRotateAt = NowSeconds();
    FindStrayStorageFiles(app.storageDir, 5, state.straySamples, state.strayFileCount);
}
