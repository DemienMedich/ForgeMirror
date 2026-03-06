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

static void LoadWorkspaceCollections(GuiState& state, const AppContext& app) {
    state.tasks = state.modules.tasks ? LoadTasks(app.storageDir) : std::vector<TaskEntry>();
    state.taskAudit = state.modules.tasks ? LoadTaskAudit(app.storageDir) : std::vector<TaskAuditEntry>();
    state.projects = LoadProjects(app.storageDir);
    state.shortcuts = state.modules.shortcuts ? LoadShortcuts(app.storageDir) : std::vector<ShortcutEntry>();
    state.pipelineSteps = state.modules.pipeline ? LoadPipeline(app.storageDir) : std::vector<PipelineStep>();
    state.professions = state.modules.professions ? LoadProfessions(app.storageDir) : std::vector<ProfessionEntry>();
    state.profileProfessionCache.clear();
    state.profileLoginCache.clear();
    state.profileBlockedCache.clear();
    state.profileWalletCache.clear();
    state.profileProfessionDirty = true;
    state.adminStatsDirty = true;
}

static void ReloadSyncedWorkspaceData(GuiState& state, AppContext& app, bool reloadProfiles = true) {
    if (reloadProfiles) {
        const std::string preferredId = state.active ? state.active->id : state.ui.lastProfileId;
        RefreshProfiles(state, app, preferredId);
    }
    state.tasks = state.modules.tasks ? LoadTasks(app.storageDir) : std::vector<TaskEntry>();
    state.taskAudit = state.modules.tasks ? LoadTaskAudit(app.storageDir) : std::vector<TaskAuditEntry>();
    state.projects = LoadProjects(app.storageDir);
    state.pipelineSteps = state.modules.pipeline ? LoadPipeline(app.storageDir) : std::vector<PipelineStep>();
    state.professions = state.modules.professions ? LoadProfessions(app.storageDir) : std::vector<ProfessionEntry>();
    state.rulesConfig = LoadGameplayConfig(app.storageDir);
    state.rulesDraft = state.rulesConfig;
    state.bannerTexts = LoadBannerTexts(app.storageDir);
    EnsureBannerSelection(state);
    state.bannerLastRotateAt = NowSeconds();
    state.vault = LoadStorageVault(app.storageDir);
    SyncVaultBuffers(state);
    state.profileProfessionCache.clear();
    state.profileLoginCache.clear();
    state.profileBlockedCache.clear();
    state.profileWalletCache.clear();
    state.profileProfessionDirty = true;
    state.adminStatsDirty = true;
    FindStrayStorageFiles(app.storageDir, 5, state.straySamples, state.strayFileCount);
}
