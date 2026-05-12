#include <algorithm>
#include <cctype>
#include <vector>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

static bool LaunchPath(const std::filesystem::path& path) {
    if (path.empty()) return false;
#if defined(_WIN32)
    std::filesystem::path full(path);
    std::error_code ec;
    if (!full.is_absolute()) {
        full = std::filesystem::absolute(full, ec);
    }
    return ShellExecuteA(nullptr, "open", full.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > (HINSTANCE)32;
#else
    return false;
#endif
}

static void CloseWorkspaceWindows(GuiState& state) {
    state.showSkillCatalog = false;
    state.showPipeline = false;
    state.showView3d = false;
    state.showTasks = false;
    state.showPomodoro = false;
    state.showProjects = false;
    state.showShortcuts = false;
    state.showAbout = false;
    state.showRules = false;
    state.showUiSettings = false;
    state.showView3dSettings = false;
    state.showLogs = false;
    state.showAdminStats = false;
    state.showBannerAdmin = false;
    state.showVaultAdmin = false;
    state.workspaceTabRequest = static_cast<int>(UiWindowId::Profile);
}

static void RefreshCloudState(GuiState& state, const AppContext& app);
static void DrawCloudWorkspaceActionModal(GuiState& state, AppContext& app);
static std::int64_t ToUnixSeconds(std::filesystem::file_time_type time);

static constexpr int kCloudWorkspaceActionApplyCloud = 1;
static constexpr int kCloudWorkspaceActionPushLocal = 2;
static constexpr int kCloudWorkspaceActionRestoreBackup = 3;

struct CloudWorkspacePreviewInfo {
    bool exists = false;
    std::filesystem::path path;
    std::uintmax_t sizeBytes = 0;
    std::int64_t modifiedAt = 0;
    int entryCount = -1;
    int secondaryCount = -1;
    int tertiaryCount = -1;
    std::string entryLabel;
    std::string secondaryLabel;
    std::string tertiaryLabel;
    std::string summary;
    std::string detail;
};

static const char* CloudDriftTargetLabel(const std::string& relativePath) {
    if (relativePath == "meta/tasks.json") return u8"Задачи";
    if (relativePath == "meta/pipeline.json") return u8"Пайплайн";
    return nullptr;
}

static const char* CloudBackupSourceLabel(const std::string& sourceKind) {
    if (sourceKind == "local") return u8"Локальный снимок";
    if (sourceKind == "cloud") return u8"Облачный снимок";
    if (sourceKind == "restore") return u8"Снимок перед восстановлением";
    return u8"Backup";
}

static const char* CloudWorkspaceActionLabel(int mode) {
    switch (mode) {
        case kCloudWorkspaceActionApplyCloud: return u8"Принять облачную";
        case kCloudWorkspaceActionPushLocal: return u8"Оставить локальную";
        case kCloudWorkspaceActionRestoreBackup: return u8"Восстановить снимок";
        default: return u8"Действие";
    }
}

static std::string FormatCloudPreviewSize(std::uintmax_t sizeBytes) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (sizeBytes >= 1024ull * 1024ull) {
        out << std::fixed << std::setprecision(1) << (static_cast<double>(sizeBytes) / (1024.0 * 1024.0)) << " MB";
    } else if (sizeBytes >= 1024ull) {
        out << std::fixed << std::setprecision(1) << (static_cast<double>(sizeBytes) / 1024.0) << " KB";
    } else {
        out << sizeBytes << " B";
    }
    return out.str();
}

static std::string FormatCloudPreviewCountDelta(int delta) {
    if (delta > 0) return "+" + std::to_string(delta);
    return std::to_string(delta);
}

static std::string FormatCloudPreviewMetricValue(int value) {
    return value >= 0 ? std::to_string(value) : std::string(u8"нет");
}

static std::string FormatCloudPreviewSizeDelta(std::int64_t delta) {
    const std::uintmax_t absValue =
        static_cast<std::uintmax_t>(delta < 0 ? -delta : delta);
    const std::string amount = FormatCloudPreviewSize(absValue);
    if (delta > 0) return "+" + amount;
    if (delta < 0) return "-" + amount;
    return amount;
}

static CloudWorkspacePreviewInfo InspectWorkspacePreviewPath(const std::filesystem::path& filePath,
                                                            const std::string& relativePath) {
    CloudWorkspacePreviewInfo info;
    info.path = filePath;
    std::error_code ec;
    info.exists = std::filesystem::exists(info.path, ec);
    if (!info.exists || ec) return info;

    info.sizeBytes = std::filesystem::file_size(info.path, ec);
    ec.clear();
    const auto stamp = std::filesystem::last_write_time(info.path, ec);
    if (!ec) {
        info.modifiedAt = ToUnixSeconds(stamp);
    }

    if (relativePath == "meta/tasks.json") {
        const auto tasks = LoadTasksDataFromFile(info.path);
        int xpPendingCount = 0;
        for (const auto& task : tasks) {
            if (TaskNeedsXpAward(task)) xpPendingCount += 1;
        }
        info.entryCount = static_cast<int>(tasks.size());
        info.secondaryCount = xpPendingCount;
        info.entryLabel = u8"Задач";
        info.secondaryLabel = u8"Ждут XP";
        info.summary = u8"Задач: " + std::to_string(info.entryCount);
        info.detail = u8"Ждут XP: " + std::to_string(xpPendingCount);
    } else if (relativePath == "meta/pipeline.json") {
        const auto steps = LoadPipelineDataFromFile(info.path);
        int branchingCount = 0;
        int finalCount = 0;
        for (const auto& step : steps) {
            if (step.nextIds.empty()) {
                finalCount += 1;
            } else if (step.nextIds.size() > 1) {
                branchingCount += 1;
            }
        }
        info.entryCount = static_cast<int>(steps.size());
        info.secondaryCount = branchingCount;
        info.tertiaryCount = finalCount;
        info.entryLabel = u8"Этапов";
        info.secondaryLabel = u8"Ветвлений";
        info.tertiaryLabel = u8"Финалов";
        info.summary = u8"Этапов: " + std::to_string(info.entryCount);
        info.detail = u8"Ветвлений: " + std::to_string(branchingCount) + u8", финалов: " + std::to_string(finalCount);
    }
    return info;
}

static CloudWorkspacePreviewInfo InspectWorkspaceFilePreview(const std::filesystem::path& storageRoot,
                                                             const std::string& relativePath) {
    return InspectWorkspacePreviewPath(storageRoot / std::filesystem::u8path(relativePath), relativePath);
}

static CloudWorkspacePreviewInfo InspectBackupFilePreview(const CloudWorkspaceBackupEntry& backup) {
    CloudWorkspacePreviewInfo info = InspectWorkspacePreviewPath(backup.path, backup.relativePath);
    if (backup.createdAt > 0) {
        info.modifiedAt = backup.createdAt;
    }
    if (info.summary.empty()) {
        info.summary = CloudBackupSourceLabel(backup.sourceKind);
    }
    if (info.detail.empty() && backup.createdAt > 0) {
        const std::string stamp = FormatTimestamp(backup.createdAt, "%Y-%m-%d %H:%M");
        if (!stamp.empty()) {
            info.detail = u8"Создан: " + stamp;
        }
    }
    return info;
}

static void QueueCloudWorkspaceAction(GuiState& state,
                                      int mode,
                                      const std::string& relativePath,
                                      const std::string& context = std::string(),
                                      const std::string& sourceKind = std::string(),
                                      std::int64_t sourceCreatedAt = 0,
                                      const std::filesystem::path& backupPath = std::filesystem::path()) {
    state.cloudWorkspaceActionPopupRequest = true;
    state.cloudWorkspaceActionMode = mode;
    state.cloudWorkspaceActionRelativePath = relativePath;
    state.cloudWorkspaceActionContext = context;
    state.cloudWorkspaceActionSourceKind = sourceKind;
    state.cloudWorkspaceActionSourceCreatedAt = sourceCreatedAt;
    state.cloudWorkspaceActionBackupPath = backupPath;
}

static void ResetCloudWorkspaceAction(GuiState& state) {
    state.cloudWorkspaceActionPopupRequest = false;
    state.cloudWorkspaceActionMode = 0;
    state.cloudWorkspaceActionRelativePath.clear();
    state.cloudWorkspaceActionContext.clear();
    state.cloudWorkspaceActionSourceKind.clear();
    state.cloudWorkspaceActionSourceCreatedAt = 0;
    state.cloudWorkspaceActionBackupPath.clear();
}

static bool CanOpenCloudDriftTarget(const GuiState& state, const std::string& relativePath) {
    if (relativePath == "meta/tasks.json") return state.modules.tasks;
    if (relativePath == "meta/pipeline.json") return state.modules.pipeline;
    return false;
}

static void OpenCloudDriftTarget(GuiState& state, const std::string& relativePath) {
    if (relativePath == "meta/tasks.json" && state.modules.tasks) {
        state.showProjects = false;
        OpenWorkspaceTab(state, state.showTasks, UiWindowId::Tasks);
        return;
    }
    if (relativePath == "meta/pipeline.json" && state.modules.pipeline) {
        OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
    }
}

static void RefreshCloudWorkspaceUiState(GuiState& state,
                                         AppContext& app,
                                         bool reloadWorkspaceData,
                                         bool markSyncSuccess) {
    if (markSyncSuccess) {
        const std::int64_t now = NowSeconds();
        state.lastCloudSyncAt = now;
        state.lastCloudSyncOk = true;
        state.lastCloudSyncOkAt = now;
    }
    if (reloadWorkspaceData) {
        ReloadSyncedWorkspaceData(state, app, false);
    } else {
        state.syncHealth = InspectWorkspaceSyncHealth(state.storageDir, state.modules);
        FindStrayStorageFiles(state.storageDir, 5, state.straySamples, state.strayFileCount);
    }
    RefreshCloudState(state, app);
    state.cloudWorkspaceDrift = InspectCloudWorkspaceDrift(state.cloudConfig, state.storageDir, state.lastCloudSyncOkAt);
    state.lastStorageScanAt = 0;
    state.lastStorageDataAt = 0;
}

static void RefreshCloudState(GuiState& state, const AppContext& app) {
    RefreshCloudManifestState(state, app);
}


static void ApplyCloudResultStatus(GuiState& state, const CloudSyncResult& result) {
    if (result.ok) {
        SetStatus(state, result.message, 0.45f, 0.9f, 0.45f);
    } else {
        SetStatus(state, result.message, 1.0f, 0.75f, 0.35f);
    }
    FindStrayStorageFiles(state.storageDir, 5, state.straySamples, state.strayFileCount);
}

static void TriggerStorageConflict(GuiState& state, const CloudSyncResult& result) {
    if (!result.storageConflict) return;
    state.storageConflictPopupRequest = true;
    state.storageConflictPath = result.storageConflictPath;
}

static void DrawStorageConflictModal(GuiState& state) {
    bool open = state.storageConflictPopupRequest;
    if (open) {
        ImGui::OpenPopup(u8"Конфликт хранилища");
        state.storageConflictPopupRequest = false;
    }
    bool popupOpen = true;
    if (ImGui::BeginPopupModal(u8"Конфликт хранилища", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        ImGui::TextWrapped(u8"Обнаружен конфликт настроек хранилища (storage.json).");
        ImGui::TextWrapped(u8"Облачная версия сохранена в meta/updates.");
        if (!state.storageConflictPath.empty()) {
            ImGui::TextDisabled("%s", state.storageConflictPath.u8string().c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button(u8"Принять облачную", ImVec2(200.0f, 0.0f))) {
            if (!state.storageConflictPath.empty()) {
                std::error_code ec;
                const auto target = state.storageDir / "meta" / "storage.json";
                std::filesystem::copy_file(state.storageConflictPath, target,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    state.vault = LoadStorageVault(state.storageDir);
                    SyncVaultBuffers(state);
                    SetStatus(state, u8"Применена облачная версия хранилища.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось применить облачную версию.", 1.0f, 0.75f, 0.35f);
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"Оставить локальную", ImVec2(200.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void DrawCloudWorkspacePreviewBlock(const char* label, const CloudWorkspacePreviewInfo& info) {
    ImGui::TextDisabled("%s", label);
    if (!info.exists) {
        ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Файл отсутствует");
        return;
    }
    if (!info.summary.empty()) {
        ImGui::Text("%s", info.summary.c_str());
    }
    if (!info.detail.empty()) {
        ImGui::TextDisabled("%s", info.detail.c_str());
    }
    if (info.modifiedAt > 0) {
        ImGui::TextDisabled(u8"Изменён: %s", FormatTimestamp(info.modifiedAt, "%Y-%m-%d %H:%M").c_str());
    }
    ImGui::TextDisabled(u8"Размер: %s", FormatCloudPreviewSize(info.sizeBytes).c_str());
    ImGui::TextDisabled("%s", info.path.u8string().c_str());
}

static bool DrawCloudWorkspacePreviewDiffRow(const std::string& label, int beforeValue, int afterValue) {
    if (label.empty() || (beforeValue < 0 && afterValue < 0)) return false;
    const std::string beforeText = FormatCloudPreviewMetricValue(beforeValue);
    const std::string afterText = FormatCloudPreviewMetricValue(afterValue);
    const bool changed = beforeValue != afterValue;
    if (changed && beforeValue >= 0 && afterValue >= 0) {
        const std::string delta = FormatCloudPreviewCountDelta(afterValue - beforeValue);
        ImGui::BulletText("%s: %s -> %s (%s)", label.c_str(), beforeText.c_str(), afterText.c_str(), delta.c_str());
    } else if (changed) {
        ImGui::BulletText("%s: %s -> %s", label.c_str(), beforeText.c_str(), afterText.c_str());
    } else {
        ImGui::TextDisabled("%s: %s -> %s", label.c_str(), beforeText.c_str(), afterText.c_str());
    }
    return changed;
}

static bool DrawCloudWorkspacePreviewSizeDiffRow(const CloudWorkspacePreviewInfo& sourcePreview,
                                                 const CloudWorkspacePreviewInfo& targetPreview) {
    if (!sourcePreview.exists && !targetPreview.exists) return false;
    const std::string beforeText = targetPreview.exists ? FormatCloudPreviewSize(targetPreview.sizeBytes) : std::string(u8"нет");
    const std::string afterText = sourcePreview.exists ? FormatCloudPreviewSize(sourcePreview.sizeBytes) : std::string(u8"нет");
    const bool changed = !sourcePreview.exists || !targetPreview.exists || sourcePreview.sizeBytes != targetPreview.sizeBytes;
    if (changed && sourcePreview.exists && targetPreview.exists) {
        const std::int64_t delta = static_cast<std::int64_t>(sourcePreview.sizeBytes) -
                                   static_cast<std::int64_t>(targetPreview.sizeBytes);
        const std::string deltaText = FormatCloudPreviewSizeDelta(delta);
        ImGui::BulletText(u8"Размер: %s -> %s (%s)", beforeText.c_str(), afterText.c_str(), deltaText.c_str());
    } else if (changed) {
        ImGui::BulletText(u8"Размер: %s -> %s", beforeText.c_str(), afterText.c_str());
    } else {
        ImGui::TextDisabled(u8"Размер: %s -> %s", beforeText.c_str(), afterText.c_str());
    }
    return changed;
}

static void DrawCloudWorkspacePreviewDiff(const CloudWorkspacePreviewInfo& sourcePreview,
                                          const CloudWorkspacePreviewInfo& targetPreview) {
    ImGui::TextDisabled(u8"Ключевые изменения");
    bool shown = false;
    bool changed = false;

    if (!sourcePreview.entryLabel.empty() || !targetPreview.entryLabel.empty()) {
        shown = true;
        changed = DrawCloudWorkspacePreviewDiffRow(!sourcePreview.entryLabel.empty() ? sourcePreview.entryLabel : targetPreview.entryLabel,
                                                   targetPreview.entryCount, sourcePreview.entryCount) || changed;
    }
    if (!sourcePreview.secondaryLabel.empty() || !targetPreview.secondaryLabel.empty()) {
        shown = true;
        changed = DrawCloudWorkspacePreviewDiffRow(!sourcePreview.secondaryLabel.empty() ? sourcePreview.secondaryLabel : targetPreview.secondaryLabel,
                                                   targetPreview.secondaryCount, sourcePreview.secondaryCount) || changed;
    }
    if (!sourcePreview.tertiaryLabel.empty() || !targetPreview.tertiaryLabel.empty()) {
        shown = true;
        changed = DrawCloudWorkspacePreviewDiffRow(!sourcePreview.tertiaryLabel.empty() ? sourcePreview.tertiaryLabel : targetPreview.tertiaryLabel,
                                                   targetPreview.tertiaryCount, sourcePreview.tertiaryCount) || changed;
    }

    if (sourcePreview.exists || targetPreview.exists) {
        shown = true;
        changed = DrawCloudWorkspacePreviewSizeDiffRow(sourcePreview, targetPreview) || changed;
    }
    if (!shown) {
        ImGui::TextDisabled(u8"Ключевые метрики недоступны для сравнения.");
    } else if (!changed) {
        ImGui::TextDisabled(u8"По ключевым метрикам различий нет.");
    }
}

static void DrawCloudWorkspaceActionModal(GuiState& state, AppContext& app) {
    bool open = state.cloudWorkspaceActionPopupRequest;
    if (open) {
        ImGui::OpenPopup(u8"Подтвердить sync-действие");
        state.cloudWorkspaceActionPopupRequest = false;
    }

    bool popupOpen = true;
    if (ImGui::BeginPopupModal(u8"Подтвердить sync-действие", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        const std::string& relativePath = state.cloudWorkspaceActionRelativePath;
        const char* targetLabel = CloudDriftTargetLabel(relativePath);
        const std::filesystem::path cloudRoot = ResolveCloudRootPath(state.cloudConfig, state.storageDir);
        const int actionMode = state.cloudWorkspaceActionMode;
        const bool canProceed = !relativePath.empty() && actionMode != 0;

        ImGui::Text("%s", CloudWorkspaceActionLabel(actionMode));
        if (targetLabel != nullptr) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", targetLabel);
        }
        if (!relativePath.empty()) {
            ImGui::TextDisabled("%s", relativePath.c_str());
        }
        if (!state.cloudWorkspaceActionContext.empty()) {
            ImGui::TextWrapped("%s", state.cloudWorkspaceActionContext.c_str());
        }
        ImGui::Spacing();

        CloudWorkspacePreviewInfo sourcePreview;
        CloudWorkspacePreviewInfo targetPreview;
        std::string sourceLabel;
        std::string targetAreaLabel;

        if (actionMode == kCloudWorkspaceActionApplyCloud) {
            sourcePreview = InspectWorkspaceFilePreview(cloudRoot, relativePath);
            targetPreview = InspectWorkspaceFilePreview(state.storageDir, relativePath);
            sourceLabel = u8"Источник: облако";
            targetAreaLabel = u8"Будет заменено: локальный workspace";
        } else if (actionMode == kCloudWorkspaceActionPushLocal) {
            sourcePreview = InspectWorkspaceFilePreview(state.storageDir, relativePath);
            targetPreview = InspectWorkspaceFilePreview(cloudRoot, relativePath);
            sourceLabel = u8"Источник: локальный workspace";
            targetAreaLabel = u8"Будет заменено: облако";
        } else if (actionMode == kCloudWorkspaceActionRestoreBackup) {
            CloudWorkspaceBackupEntry backupEntry;
            backupEntry.relativePath = relativePath;
            backupEntry.sourceKind = state.cloudWorkspaceActionSourceKind;
            backupEntry.createdAt = state.cloudWorkspaceActionSourceCreatedAt;
            backupEntry.path = state.cloudWorkspaceActionBackupPath;
            sourcePreview = InspectBackupFilePreview(backupEntry);
            targetPreview = InspectWorkspaceFilePreview(state.storageDir, relativePath);
            sourceLabel = std::string(u8"Источник: ") + CloudBackupSourceLabel(state.cloudWorkspaceActionSourceKind);
            targetAreaLabel = u8"Будет заменено: локальный workspace";
        }

        DrawCloudWorkspacePreviewBlock(sourceLabel.c_str(), sourcePreview);
        ImGui::Spacing();
        DrawCloudWorkspacePreviewBlock(targetAreaLabel.c_str(), targetPreview);
        ImGui::Spacing();
        DrawCloudWorkspacePreviewDiff(sourcePreview, targetPreview);
        ImGui::Spacing();
        if (actionMode == kCloudWorkspaceActionRestoreBackup) {
            ImGui::TextWrapped(u8"Восстановление меняет только локальный workspace. Облако можно синхронизировать после проверки.");
        } else {
            ImGui::TextWrapped(u8"Перед заменой актуальные версии будут сохранены в meta/updates.");
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        if (!canProceed) ImGui::BeginDisabled();
        if (ImGui::Button(CloudWorkspaceActionLabel(actionMode), ImVec2(220.0f, 0.0f))) {
            if (actionMode == kCloudWorkspaceActionApplyCloud || actionMode == kCloudWorkspaceActionPushLocal) {
                const bool preferCloud = actionMode == kCloudWorkspaceActionApplyCloud;
                const CloudWorkspaceResolveResult result =
                    ResolveCloudWorkspaceFileVersion(state.cloudConfig, state.storageDir, relativePath, preferCloud);
                if (result.ok) {
                    RefreshCloudWorkspaceUiState(state, app, preferCloud, true);
                    SetStatus(state, result.message, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, result.message, 1.0f, 0.75f, 0.35f);
                }
            } else if (actionMode == kCloudWorkspaceActionRestoreBackup) {
                const CloudWorkspaceResolveResult result =
                    RestoreCloudWorkspaceBackup(state.storageDir, relativePath, state.cloudWorkspaceActionBackupPath);
                if (result.ok) {
                    RefreshCloudWorkspaceUiState(state, app, true, false);
                    OpenCloudDriftTarget(state, relativePath);
                    SetStatus(state, result.message, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, result.message, 1.0f, 0.75f, 0.35f);
                }
            }
            ResetCloudWorkspaceAction(state);
            ImGui::CloseCurrentPopup();
        }
        if (!canProceed) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(u8"Отмена", ImVec2(180.0f, 0.0f))) {
            ResetCloudWorkspaceAction(state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!popupOpen && ImGui::IsPopupOpen(u8"Подтвердить sync-действие")) {
        ResetCloudWorkspaceAction(state);
        ImGui::CloseCurrentPopup();
    }
}



static void RunCloudPull(GuiState& state, AppContext& app) {
    CloudRole role = state.isAdmin ? CloudRole::Admin : CloudRole::Viewer;
    CloudSyncResult result = PullCloudSnapshot(state.cloudConfig, state.storageDir, role);
    if (result.ok) {
        app.catalog.reload();
        state.lastCatalogSelection = -1;
        ReloadSyncedWorkspaceData(state, app);
    }
    RefreshCloudState(state, app);
    ApplyCloudResultStatus(state, result);
    TriggerStorageConflict(state, result);
    state.lastCloudSyncAt = NowSeconds();
    state.lastCloudSyncOk = result.ok;
    if (result.ok) {
        state.lastCloudSyncOkAt = state.lastCloudSyncAt;
    }
    state.cloudWorkspaceDrift = InspectCloudWorkspaceDrift(state.cloudConfig, state.storageDir, state.lastCloudSyncOkAt);
}


static void RunCloudPush(GuiState& state, const AppContext& app) {
    CloudSyncResult result = PushCloudSnapshot(state.cloudConfig, app.storageDir, CloudRole::Admin);
    RefreshCloudState(state, app);
    ApplyCloudResultStatus(state, result);
    TriggerStorageConflict(state, result);
    state.lastCloudSyncAt = NowSeconds();
    state.lastCloudSyncOk = result.ok;
    if (result.ok) {
        state.lastCloudSyncOkAt = state.lastCloudSyncAt;
    }
    state.cloudWorkspaceDrift = InspectCloudWorkspaceDrift(state.cloudConfig, state.storageDir, state.lastCloudSyncOkAt);
}


static void RunCloudQuickSync(GuiState& state, AppContext& app, bool showStatus) {
    if (!state.cloudConfig.enabled) {
        if (showStatus) {
            SetStatus(state, u8"Облако отключено.", 1.0f, 0.75f, 0.35f);
        }
        return;
    }
    const bool doPull = state.cloudConfig.autoPull;
    const bool doPush = state.isAdmin && state.cloudConfig.autoPush;
    const bool viewerMode = !state.isAdmin;
    CloudSyncResult pullResult;
    CloudSyncResult pushResult;
    bool anyAction = false;
    const bool pushFirst = doPull && doPush;
    if (pushFirst) {
        pushResult = PushCloudSnapshot(state.cloudConfig, app.storageDir, CloudRole::Admin);
        anyAction = true;
    }
    if (doPull) {
        pullResult = PullCloudSnapshot(state.cloudConfig, app.storageDir,
                                       state.isAdmin ? CloudRole::Admin : CloudRole::Viewer);
        anyAction = true;
        if (pullResult.ok) {
            app.catalog.reload();
            state.lastCatalogSelection = -1;
            ReloadSyncedWorkspaceData(state, app);
        }
    }
    if (doPush && !pushFirst) {
        pushResult = PushCloudSnapshot(state.cloudConfig, app.storageDir, CloudRole::Admin);
        anyAction = true;
    }
    CloudSyncResult walletResult;
    const bool doWalletPush = (!state.isAdmin && state.active && !state.active->id.empty()
        && IsProfileAccessUnlocked(state, state.active->id));
    if (doWalletPush) {
        walletResult = PushProfileWallet(state.cloudConfig, app.storageDir, state.active->id);
        anyAction = true;
    }
    RefreshCloudState(state, app);
    if (doPull) {
        TriggerStorageConflict(state, pullResult);
    }

    const bool okPull = !doPull || pullResult.ok;
    const bool okPush = !doPush || pushResult.ok;
    const bool okWallet = !doWalletPush || walletResult.ok;
    const bool ok = okPull && okPush && okWallet;
    const bool changed = (doPull && pullResult.changed) || (doPush && pushResult.changed)
        || (doWalletPush && walletResult.changed);
    std::string message;
    if (!anyAction) {
        message = u8"Действия синхронизации отключены.";
    } else if (!ok) {
        if (doPull && !pullResult.ok) message = pullResult.message;
        else if (doPush && !pushResult.ok) message = pushResult.message;
        else if (doWalletPush && !walletResult.ok) message = walletResult.message;
    } else {
        message = changed ? u8"Синхронизация завершена." : u8"Данные синхронизированы.";
        if (viewerMode) {
            message += u8" Вы в режиме просмотра: выгрузка доступна только для кошелька.";
        }
    }
    if (showStatus || !ok) {
        if (ok) {
            SetStatus(state, message, 0.45f, 0.9f, 0.45f);
        } else {
            SetStatus(state, message, 1.0f, 0.75f, 0.35f);
        }
    }
    state.lastCloudSyncAt = NowSeconds();
    if (anyAction) {
        state.lastCloudSyncOk = ok;
        if (ok) {
            state.lastCloudSyncOkAt = state.lastCloudSyncAt;
        }
    }
    state.cloudWorkspaceDrift = InspectCloudWorkspaceDrift(state.cloudConfig, state.storageDir, state.lastCloudSyncOkAt);
}


static std::int64_t ToUnixSeconds(std::filesystem::file_time_type time) {
    using namespace std::chrono;
    const auto sys = time_point_cast<seconds>(time - std::filesystem::file_time_type::clock::now()
                                              + system_clock::now());
    return sys.time_since_epoch().count();
}

static std::int64_t ComputeStorageStamp(const std::filesystem::path& storageDir) {
    std::error_code ec;
    std::int64_t latest = 0;
    if (!std::filesystem::exists(storageDir, ec)) return latest;
    for (const auto& entry : std::filesystem::directory_iterator(storageDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(entry.path(), ec));
        if (ec) continue;
        latest = std::max(latest, stamp);
    }
    auto archiveDir = storageDir / "archive";
    if (std::filesystem::exists(archiveDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(archiveDir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".ini") continue;
            auto stamp = ToUnixSeconds(std::filesystem::last_write_time(entry.path(), ec));
            if (ec) continue;
            latest = std::max(latest, stamp);
        }
    }
    auto tasksPath = storageDir / "meta" / "tasks.json";
    if (std::filesystem::exists(tasksPath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(tasksPath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    auto projectsPath = storageDir / "meta" / "projects.json";
    if (std::filesystem::exists(projectsPath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(projectsPath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    auto skillsPath = storageDir / "skills.txt";
    if (std::filesystem::exists(skillsPath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(skillsPath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    auto pipelinePath = storageDir / "meta" / "pipeline.json";
    if (std::filesystem::exists(pipelinePath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(pipelinePath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    auto taskAuditPath = storageDir / "meta" / "task-audit.log";
    if (std::filesystem::exists(taskAuditPath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(taskAuditPath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    auto gameplayPath = storageDir / "meta" / "gameplay.ini";
    if (std::filesystem::exists(gameplayPath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(gameplayPath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    auto bannerPath = storageDir / "meta" / "banner.json";
    if (std::filesystem::exists(bannerPath, ec)) {
        auto stamp = ToUnixSeconds(std::filesystem::last_write_time(bannerPath, ec));
        if (!ec) {
            latest = std::max(latest, stamp);
        }
    }
    return latest;
}

static std::int64_t GetStorageStamp(GuiState& state) {
    const std::int64_t now = NowSeconds();
    if (state.lastStorageScanAt <= 0 || now - state.lastStorageScanAt >= 2) {
        state.lastStorageScanAt = now;
        state.lastStorageDataAt = ComputeStorageStamp(state.storageDir);
        state.syncHealth = InspectWorkspaceSyncHealth(state.storageDir, state.modules);
        state.cloudWorkspaceDrift = InspectCloudWorkspaceDrift(state.cloudConfig, state.storageDir, state.lastCloudSyncOkAt);
    }
    return state.lastStorageDataAt;
}


static std::vector<int> ParseVersionPartsLocal(const std::string& value) {
    std::vector<int> parts;
    std::string num;
    for (char c : value) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            num.push_back(c);
        } else if (c == '.') {
            if (!num.empty()) {
                parts.push_back(std::stoi(num));
                num.clear();
            } else {
                parts.push_back(0);
            }
        }
    }
    if (!num.empty()) {
        parts.push_back(std::stoi(num));
    }
    if (parts.empty()) parts.push_back(0);
    return parts;
}

static int CompareVersionsLocal(const std::string& a, const std::string& b) {
    auto pa = ParseVersionPartsLocal(a);
    auto pb = ParseVersionPartsLocal(b);
    const size_t n = std::max(pa.size(), pb.size());
    pa.resize(n, 0);
    pb.resize(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return 1;
    }
    return 0;
}

static bool ExtractReleaseVersion(const std::string& filename, std::string& outVersion) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != "exe") return false;
    std::string base = filename.substr(0, dot);
    const std::string prefix = "ForgeMirrorSetup_";
    if (base.rfind(prefix, 0) == 0) {
        base = base.substr(prefix.size());
    } else {
        auto pos = base.find_last_of('_');
        if (pos == std::string::npos) return false;
        base = base.substr(pos + 1);
    }
    if (base.empty()) return false;
    outVersion = base;
    return true;
}

static void RefreshUpdateNotice(GuiState& state, const std::filesystem::path& cloudRoot) {
    const std::int64_t now = NowSeconds();
    if (state.updateNoticeCheckedAt > 0 && now - state.updateNoticeCheckedAt < 5) return;
    state.updateNoticeCheckedAt = now;
    state.updateNoticeVisible = false;
    if (!state.cloudConfig.enabled) return;
    std::filesystem::path releasesDir = state.cloudConfig.releasesDir.empty()
        ? (cloudRoot / "releases")
        : state.cloudConfig.releasesDir;
    if (!releasesDir.is_absolute()) {
        releasesDir = cloudRoot / releasesDir;
    }
    std::error_code ec;
    if (!std::filesystem::exists(releasesDir, ec)) return;
    std::string bestVersion;
    std::string bestFile;
    for (const auto& entry : std::filesystem::directory_iterator(releasesDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        std::string ver;
        if (!ExtractReleaseVersion(name, ver)) continue;
        if (bestVersion.empty() || CompareVersionsLocal(ver, bestVersion) > 0) {
            bestVersion = ver;
            bestFile = name;
        }
    }
    if (bestVersion.empty()) return;
    if (CompareVersionsLocal(bestVersion, APP_VERSION) <= 0) return;
    if (!state.updateNoticeDismissedVersion.empty() &&
        CompareVersionsLocal(bestVersion, state.updateNoticeDismissedVersion) <= 0) {
        return;
    }
    state.updateNoticeVisible = true;
    state.updateNoticeVersion = bestVersion;
    state.updateNoticeFile = bestFile;
    state.updateNoticeFolder = releasesDir;
}

static void DrawUpdateNoticeModal(GuiState& state) {
    if (!state.updateNoticeVisible) return;
    ImGui::OpenPopup(u8"Доступно обновление");
    bool open = true;
    if (ImGui::BeginPopupModal(u8"Доступно обновление", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        std::string label = u8"Найдена новая версия ";
        label += state.updateNoticeVersion;
        ImGui::TextWrapped("%s", label.c_str());
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button(u8"Установить", ImVec2(200.0f, 0.0f))) {
            LaunchPath(state.updateNoticeFolder);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"Закрыть", ImVec2(200.0f, 0.0f))) {
            state.updateNoticeVisible = false;
            state.updateNoticeDismissedVersion = state.updateNoticeVersion;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawMainMenuBar(GLFWwindow* window, GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    AppContext app{state.storageDir, storage, catalog};
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    std::filesystem::path resolvedCloudRoot = ResolveCloudRootPath(state.cloudConfig, state.storageDir);
    const std::string cloudRootLabel = resolvedCloudRoot.u8string();
    RefreshUpdateNotice(state, resolvedCloudRoot);

    const bool canArchive = state.selectedIndex >= 0 &&
    state.selectedIndex < static_cast<int>(state.profiles.size()) &&
    !state.profiles[state.selectedIndex].archived;
    const bool canRestore = state.selectedIndex >= 0 &&
    state.selectedIndex < static_cast<int>(state.profiles.size()) &&
    state.profiles[state.selectedIndex].archived;
    const bool canDelete = state.selectedIndex >= 0 &&
    state.selectedIndex < static_cast<int>(state.profiles.size());
    const bool hasActive = state.active.has_value();

    ImVec4 menuBg = style.Colors[ImGuiCol_WindowBg];
    menuBg.w = std::max(menuBg.w, 0.85f);
    bool barHovered = false;
    bool barAnyItemHovered = false;
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, menuBg);
    if (ImGui::BeginMainMenuBar()) {
#ifdef APP_VERSION
        ImGui::TextDisabled("ForgeMirror %s", APP_VERSION);
#else
        ImGui::TextDisabled("ForgeMirror");
#endif

        if (ImGui::BeginMenu(u8"Профили")) {
            if (state.active) {
                std::string label = "[" + state.active->id + "] " + state.active->profile.name();
                ImGui::TextDisabled(u8"Активный: %s", label.c_str());
            } else {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Активный: не выбран");
            }
            ImGui::Separator();

            const bool profilesWasOpen = state.showProfilesWindow;
            ImGui::MenuItem(u8"Окно профилей", nullptr, &state.showProfilesWindow);
            if (!profilesWasOpen && state.showProfilesWindow) {
                state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
            }
            if (ImGui::MenuItem(u8"Обновить список")) {
                RefreshProfiles(state, storage, catalog);
            }
            ImGui::Separator();

            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Создать")) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            if (!state.isAdmin || !hasActive) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Добавить опыт")) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            if (!state.isAdmin || !hasActive) ImGui::EndDisabled();

            const bool canArchiveAction = canArchive || canRestore;
            const char* archiveLabel = canRestore ? u8"Вернуть" : u8"В архив";
            if (!state.isAdmin || !canArchiveAction) ImGui::BeginDisabled();
            if (ImGui::MenuItem(archiveLabel)) {
                state.confirmPopupRequest = true;
                state.confirmAction = canRestore ? ConfirmAction::Restore : ConfirmAction::Archive;
            }
            if (!state.isAdmin || !canArchiveAction) ImGui::EndDisabled();

            if (!state.isAdmin || !canDelete) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Удалить")) {
                state.confirmPopupRequest = true;
                state.confirmAction = ConfirmAction::Delete;
            }
            if (!state.isAdmin || !canDelete) ImGui::EndDisabled();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"Разделы")) {
            if (ImGui::MenuItem(u8"Профиль", "F1")) {
                CloseWorkspaceWindows(state);
            }
            if (ImGui::MenuItem(u8"Задачи")) {
                OpenWorkspaceTab(state, state.showTasks, UiWindowId::Tasks);
            }
            if (ImGui::MenuItem(u8"Навыки", "F2")) {
                OpenWorkspaceTab(state, state.showSkillCatalog, UiWindowId::SkillCatalog);
            }
            if (ImGui::MenuItem(u8"Пайплайн", "F3")) {
                OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
            }
            if (ImGui::MenuItem(u8"Помодоро")) {
                OpenWorkspaceTab(state, state.showPomodoro, UiWindowId::Pomodoro);
            }
            if (ImGui::MenuItem(u8"Ярлыки")) {
                OpenWorkspaceTab(state, state.showShortcuts, UiWindowId::Shortcuts);
            }
            if (state.modules.view3d && ImGui::MenuItem(u8"3D просмотр")) {
                OpenWorkspaceTab(state, state.showView3d, UiWindowId::View3D);
            }
            if (ImGui::MenuItem(u8"Настройки")) {
                OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
            }
            if (state.isAdmin) {
                ImGui::Separator();
                if (ImGui::MenuItem(u8"Статистика", "F5")) {
                    OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
                }
                if (ImGui::MenuItem(u8"Логи", "F6")) {
                    OpenWorkspaceTab(state, state.showLogs, UiWindowId::Logs);
                }
                if (ImGui::MenuItem(u8"Проекты")) {
                    OpenWorkspaceTab(state, state.showProjects, UiWindowId::Projects);
                }
                if (ImGui::MenuItem(u8"Правила", "F4")) {
                    OpenWorkspaceTab(state, state.showRules, UiWindowId::Rules);
                }
                if (state.modules.view3d && ImGui::MenuItem(u8"3D настройки")) {
                    OpenWorkspaceTab(state, state.showView3dSettings, UiWindowId::View3DSettings);
                }
                if (ImGui::MenuItem(u8"Баннер")) {
                    OpenWorkspaceTab(state, state.showBannerAdmin, UiWindowId::BannerAdmin);
                }
                if (ImGui::MenuItem(u8"Хранилище")) {
                    OpenWorkspaceTab(state, state.showVaultAdmin, UiWindowId::VaultAdmin);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"Окно")) {
            bool fullscreen = state.ui.windowFullscreen;
            if (ImGui::MenuItem(u8"Во весь экран", "F11", &fullscreen)) {
                state.ui.windowFullscreen = fullscreen;
                state.uiDirty = true;
            }
            bool borderless = !state.ui.windowDecorated;
            if (ImGui::MenuItem(u8"Без рамки", "F10", &borderless)) {
                state.ui.windowDecorated = !borderless;
                state.uiDirty = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(u8"О программе")) {
                OpenWorkspaceTab(state, state.showAbout, UiWindowId::About);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"Админ")) {
            ImGui::TextUnformatted(state.isAdmin ? u8"[Режим администратора]" : u8"[Режим просмотра]");
            ImGui::Separator();
            if (ImGui::MenuItem(state.isAdmin ? u8"Выйти из админа" : u8"Войти как админ")) {
                if (state.isAdmin) {
                    state.isAdmin = false;
                    state.adminStayLoggedIn = false;
                    SetAdminStayLoggedIn(state.storageDir, false);
                    state.showProjects = false;
                    state.showRules = false;
                    state.showView3dSettings = false;
                    state.showLogs = false;
                    state.showAdminStats = false;
                    state.showBannerAdmin = false;
                    state.showVaultAdmin = false;
                } else {
                    state.adminPopupRequest = true;
                    state.adminPassword.fill('\0');
                }
            }
            if (state.isAdmin) {
                if (ImGui::MenuItem(u8"Сменить пароль администратора")) {
                    state.adminChangePopupRequest = true;
                    state.adminPasswordCurrent.fill('\0');
                    state.adminPasswordNew.fill('\0');
                    state.adminPasswordConfirm.fill('\0');
                }
            }
            ImGui::EndMenu();
        }

        if (state.cloudConfig.enabled && ImGui::BeginMenu(u8"Облако")) {
            const std::int64_t menuStorageStamp = GetStorageStamp(state);
            ImGui::TextDisabled(u8"Папка: %s", cloudRootLabel.c_str());
            ImGui::Separator();
            if (state.isAdmin) {
                std::string openDriftPath;
                std::string resolveDriftPath;
                std::string resolveDriftContext;
                int resolveDriftMode = 0;
                std::string restoreBackupRelativePath;
                std::filesystem::path restoreBackupPath;
                std::string restoreBackupSourceKind;
                std::int64_t restoreBackupCreatedAt = 0;
                if (state.cloudWorkspaceDrift.issueCount > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(AppLogLevel::Warning));
                    if (state.cloudWorkspaceDrift.conflictCount > 0) {
                        ImGui::Text(u8"Локально/облако: %d конфликтов, %d расхождений",
                                    state.cloudWorkspaceDrift.conflictCount,
                                    state.cloudWorkspaceDrift.issueCount);
                    } else {
                        ImGui::Text(u8"Локально/облако: %d расхождений", state.cloudWorkspaceDrift.issueCount);
                    }
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled(u8"Файлы:");
                    for (const auto& driftItem : state.cloudWorkspaceDrift.files) {
                        CloudWorkspaceFileDrift file = driftItem;
                        if (!file.hasIssue) continue;
                        ImGui::PushID(file.relativePath.c_str());
                        const char* targetLabel = CloudDriftTargetLabel(file.relativePath);
                        if (targetLabel != nullptr) {
                            ImGui::Text("%s", targetLabel);
                            ImGui::SameLine();
                            ImGui::TextDisabled("[%s]", file.relativePath.c_str());
                        } else {
                            ImGui::Text("%s", file.relativePath.c_str());
                        }
                        ImGui::TextWrapped("%s", file.message.c_str());

                        const bool canOpenTarget = CanOpenCloudDriftTarget(state, file.relativePath);
                        if (!canOpenTarget) ImGui::BeginDisabled();
                        if (ImGui::SmallButton(u8"Открыть модуль")) {
                            openDriftPath = file.relativePath;
                        }
                        if (!canOpenTarget) ImGui::EndDisabled();

                        if (file.cloudExists) {
                            ImGui::SameLine();
                            const char* cloudLabel = file.localExists ? u8"Принять облачную" : u8"Загрузить из облака";
                            if (ImGui::SmallButton(cloudLabel)) {
                                resolveDriftPath = file.relativePath;
                                resolveDriftContext = file.message;
                                resolveDriftMode = 1;
                            }
                        }
                        if (file.localExists) {
                            ImGui::SameLine();
                            const char* localLabel = file.cloudExists ? u8"Оставить локальную" : u8"Выгрузить локальную";
                            if (ImGui::SmallButton(localLabel)) {
                                resolveDriftPath = file.relativePath;
                                resolveDriftContext = file.message;
                                resolveDriftMode = -1;
                            }
                        }
                        ImGui::TextDisabled(u8"Перед заменой версия сохраняется в meta/updates.");
                        ImGui::Separator();
                        ImGui::PopID();
                    }

                    if (!openDriftPath.empty()) {
                        OpenCloudDriftTarget(state, openDriftPath);
                    }
                    if (!resolveDriftPath.empty() && resolveDriftMode != 0) {
                        QueueCloudWorkspaceAction(
                            state,
                            resolveDriftMode > 0 ? kCloudWorkspaceActionApplyCloud : kCloudWorkspaceActionPushLocal,
                            resolveDriftPath,
                            resolveDriftContext);
                    }
                    ImGui::Separator();
                } else {
                    ImGui::TextDisabled(u8"Локально/облако: ok");
                    ImGui::Separator();
                }
                bool hasWorkspaceBackups = false;
                for (const std::string& backupRelativePath : {std::string("meta/tasks.json"), std::string("meta/pipeline.json")}) {
                    const std::vector<CloudWorkspaceBackupEntry> backups = ListCloudWorkspaceBackups(state.storageDir, backupRelativePath);
                    if (backups.empty()) continue;
                    hasWorkspaceBackups = true;
                    const char* targetLabel = CloudDriftTargetLabel(backupRelativePath);
                    ImGui::PushID(backupRelativePath.c_str());
                    ImGui::Text("%s", targetLabel != nullptr ? targetLabel : backupRelativePath.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled(u8"[backup]");
                    constexpr size_t kShownBackups = 5;
                    const size_t shownCount = std::min(kShownBackups, backups.size());
                    ImGui::TextDisabled(u8"Последние снимки: %d из %d", static_cast<int>(shownCount), static_cast<int>(backups.size()));
                    for (size_t backupIndex = 0; backupIndex < shownCount; ++backupIndex) {
                        const CloudWorkspaceBackupEntry& backup = backups[backupIndex];
                        ImGui::PushID(static_cast<int>(backupIndex));
                        const std::string timestamp = FormatTimestamp(backup.createdAt, "%Y-%m-%d %H:%M");
                        ImGui::TextDisabled("%s", timestamp.empty() ? u8"(без даты)" : timestamp.c_str());
                        ImGui::SameLine();
                        ImGui::Text("%s", CloudBackupSourceLabel(backup.sourceKind));
                        ImGui::SameLine();
                        ImGui::TextDisabled("[%s]", backup.path.filename().string().c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton(u8"Восстановить")) {
                            restoreBackupRelativePath = backupRelativePath;
                            restoreBackupPath = backup.path;
                            restoreBackupSourceKind = backup.sourceKind;
                            restoreBackupCreatedAt = backup.createdAt;
                        }
                        ImGui::PopID();
                    }
                    if (backups.size() > shownCount) {
                        ImGui::TextDisabled(u8"Еще %d backup-файлов лежат в meta/updates.", static_cast<int>(backups.size() - shownCount));
                    }
                    ImGui::PopID();
                    ImGui::Separator();
                }
                if (hasWorkspaceBackups) {
                    ImGui::TextDisabled(u8"Восстановление меняет только локальный workspace; облако можно синхронизировать после проверки.");
                    if (!restoreBackupRelativePath.empty() && !restoreBackupPath.empty()) {
                        QueueCloudWorkspaceAction(
                            state,
                            kCloudWorkspaceActionRestoreBackup,
                            restoreBackupRelativePath,
                            std::string(u8"Будет восстановлен выбранный snapshot поверх локальной версии."),
                            restoreBackupSourceKind,
                            restoreBackupCreatedAt,
                            restoreBackupPath);
                    }
                    ImGui::Separator();
                }
                if (state.syncHealth.issueCount > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(AppLogLevel::Warning));
                    ImGui::Text(u8"Sync-файлы: %d проблем", state.syncHealth.issueCount);
                    ImGui::PopStyleColor();
                    for (const auto& issue : state.syncHealth.issues) {
                        ImGui::BulletText("%s", issue.c_str());
                    }
                    ImGui::Separator();
                } else {
                    ImGui::TextDisabled(u8"Sync-файлы: ok");
                    ImGui::Separator();
                }
            }
            if (state.isAdmin && state.strayFileCount > 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(AppLogLevel::Warning));
                ImGui::Text(u8"Лишние файлы: %d", state.strayFileCount);
                ImGui::PopStyleColor();
                if (!state.straySamples.empty()) {
                    ImGui::TextDisabled(u8"Примеры:");
                    for (const auto& s : state.straySamples) {
                        ImGui::BulletText("%s", s.c_str());
                    }
                }
                ImGui::Separator();
            }
            if (state.isAdmin) {
#ifdef APP_VERSION
                ImGui::TextDisabled(u8"Версия приложения: %s", APP_VERSION);
#endif
                const std::filesystem::path legacyDir = ProjectSeedDataDir();
                const bool legacyHasData = HasProjectSeedData();
                ImGui::TextDisabled(u8"Проектный data/: %s", legacyHasData ? u8"обнаружен" : u8"нет");
                if (legacyHasData) {
                    const std::string legacyPath = legacyDir.u8string();
                    ImGui::TextDisabled(u8"Путь: %s", legacyPath.c_str());
                    if (ImGui::SmallButton(u8"Копировать путь##legacy_data")) {
                        ImGui::SetClipboardText(legacyPath.c_str());
                    }
                }
                ImGui::TextDisabled(u8"Штамп данных: %s",
                    menuStorageStamp > 0 ? FormatTimestamp(menuStorageStamp, "%Y-%m-%d %H:%M").c_str() : u8"(нет данных)");
                ImGui::Separator();
            }
            if (ImGui::MenuItem(u8"Обновить из облака")) {
                RunCloudPull(state, app);
            }

            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Очистить лишние файлы")) {
                int removed = 0;
                removed += RemoveStrayFiles(state.storageDir, removed) ? 0 : 0;
                FindStrayStorageFiles(state.storageDir, 5, state.straySamples, state.strayFileCount);
                if (removed > 0) {
                    SetStatus(state, u8"Лишние данные удалены.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Лишние данные не найдены.", 0.6f, 0.7f, 1.0f);
                }
            }
            if (ImGui::MenuItem(u8"Расширенный отчет")) {
                std::vector<std::string> strayList;
                const WorkspaceSyncHealth syncHealth = InspectWorkspaceSyncHealth(state.storageDir, state.modules);
                const CloudWorkspaceDriftSummary cloudDrift = InspectCloudWorkspaceDrift(state.cloudConfig, state.storageDir, state.lastCloudSyncOkAt);
                if (CollectStrayStorageFiles(state.storageDir, strayList)) {
                    std::error_code ec;
                    auto reportDir = state.storageDir / "meta" / "reports";
                    std::filesystem::create_directories(reportDir, ec);
                    if (ec) {
                        SetStatus(state, u8"Не удалось создать папку отчетов.", 1.0f, 0.75f, 0.35f);
                    } else {
                        std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
                        if (stamp.empty()) stamp = std::to_string(NowSeconds());
                        auto outPath = reportDir / ("storage-health-" + stamp + ".txt");
                        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                        if (!out) {
                            SetStatus(state, u8"Не удалось записать отчет.", 1.0f, 0.75f, 0.35f);
                        } else {
                            out.imbue(std::locale::classic());
                            const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
                            out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
                            out << "Отчет хранилища\n";
                            out << "Папка: " << state.storageDir.u8string() << "\n";
                            out << "Дата: " << FormatTimestamp(NowSeconds(), "%Y-%m-%d %H:%M") << "\n";
                            out << "Расхождения локально/облако: " << cloudDrift.issueCount << "\n";
                            if (cloudDrift.issues.empty()) {
                                out << "Локально/облако: ok\n";
                            } else {
                                for (const auto& issue : cloudDrift.issues) {
                                    out << issue << "\n";
                                }
                            }
                            out << "\n";
                            out << "Проблемы sync-файлов: " << syncHealth.issueCount << "\n";
                            if (syncHealth.issues.empty()) {
                                out << "Sync-файлы: ok\n";
                            } else {
                                for (const auto& issue : syncHealth.issues) {
                                    out << issue << "\n";
                                }
                            }
                            out << "\n";
                            out << "Лишние элементы: " << strayList.size() << "\n\n";
                            if (strayList.empty()) {
                                out << "Лишние файлы не найдены.\n";
                            } else {
                                for (const auto& entry : strayList) {
                                    out << entry << "\n";
                                }
                            }
                            std::string msg = u8"Отчет сохранен: ";
                            msg += outPath.u8string();
                            SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                        }
                    }
                } else {
                    SetStatus(state, u8"Не удалось сформировать отчет.", 1.0f, 0.75f, 0.35f);
                }
            }
            if (ImGui::MenuItem(u8"Выгрузить в облако")) {
                RunCloudPush(state, app);
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            if (!state.cloudManifest.appVersion.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled(u8"Версия в облаке: %s", state.cloudManifest.appVersion.c_str());
                if (state.cloudManifest.dataUpdatedAt > 0) {
                    std::string stamp = FormatTimestamp(state.cloudManifest.dataUpdatedAt, "%Y-%m-%d %H:%M");
                    ImGui::TextDisabled(u8"Данные обновлены: %s", stamp.c_str());
                }
                if (!state.cloudManifest.notes.empty()) {
                    ImGui::TextWrapped(u8"Примечание: %s", state.cloudManifest.notes.c_str());
                }
            }

            if (state.cloudUpdateAvailable) {
                ImGui::Separator();
                ImGui::TextColored(accent, u8"Доступно обновление клиента.");
                std::filesystem::path updateFile;
                bool updateReady = false;
                if (state.cloudManifest.releaseFile.empty()) {
                    ImGui::TextDisabled(u8"Файл обновления не указан.");
                } else {
                    updateFile = state.storageDir / "meta" / "updates" / state.cloudManifest.releaseFile;
                    std::error_code ec;
                    updateReady = std::filesystem::exists(updateFile, ec);
                    if (ImGui::MenuItem(u8"Скачать обновление")) {
                        std::filesystem::path outPath;
                        CloudSyncResult dl = DownloadCloudRelease(state.cloudConfig, state.storageDir,
                            state.cloudManifest, outPath);
                        if (dl.ok) {
                            updateFile = outPath;
                            updateReady = true;
                            std::string msg = dl.message;
                            msg += u8" Файл: ";
                            msg += outPath.u8string();
                            SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state, dl.message, 1.0f, 0.75f, 0.35f);
                        }
                    }
                }
                if (updateReady) {
                    ImGui::Separator();
                    if (ImGui::MenuItem(u8"Запустить установщик")) {
                        if (LaunchPath(updateFile)) {
                            SetStatus(state, u8"Установщик запущен. Закройте приложение на время обновления.",
                                      0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state, u8"Не удалось запустить установщик.", 1.0f, 0.75f, 0.35f);
                        }
                    }
                }
            }

            ImGui::EndMenu();
        }

        const float iconSize = ImGui::GetFrameHeight();
        const float iconGap = style.ItemSpacing.x * 0.75f;
        const int iconCount = 7;
        const float iconsWidth = iconCount * iconSize + (iconCount - 1) * iconGap;
        const std::string kbHint = KeyboardHintCompactLabel(false);
        const float kbGap = style.ItemSpacing.x * 0.6f;
        const float kbWidth = ImGui::CalcTextSize(kbHint.c_str()).x;
        const float rightBlockWidth = iconsWidth + kbWidth + kbGap;
        float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - rightBlockWidth;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(rightX);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", kbHint.c_str());
        ImGui::SameLine(0.0f, kbGap);

        if (IconButtonGui("top_profiles", UiIcon::Users, u8"Профили", accent, iconSize, true, state.showProfilesWindow)) {
            state.showProfilesWindow = !state.showProfilesWindow;
            if (state.showProfilesWindow) {
                state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
            }
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_refresh", UiIcon::Refresh, u8"Обновить профили", accent, iconSize)) {
            RefreshProfiles(state, storage, catalog);
        }
        ImGui::SameLine(0.0f, iconGap);
        std::string syncTooltip = u8"Синхронизировать";
        if (!state.cloudConfig.enabled) {
            syncTooltip += u8"\nОблако отключено";
        } else {
            syncTooltip += u8"\nПапка: ";
            syncTooltip += cloudRootLabel;
            if (!state.isAdmin) {
                syncTooltip += u8"\nРежим просмотра: только загрузка";
            }
        }
        if (IconButtonGui("top_sync", UiIcon::Sync, syncTooltip.c_str(), accent, iconSize,
                          state.cloudConfig.enabled)) {
            RunCloudQuickSync(state, app, true);
        }
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 0.35f);
        const float dotSize = std::max(10.0f, iconSize * 0.28f);
        ImGui::InvisibleButton("##sync_status", ImVec2(dotSize, iconSize));
        ImVec2 pMin = ImGui::GetItemRectMin();
        ImVec2 pMax = ImGui::GetItemRectMax();
        ImVec2 center(pMin.x + dotSize * 0.5f, (pMin.y + pMax.y) * 0.5f);
        float radius = dotSize * 0.36f;
        ImVec4 okColor = ImVec4(0.35f, 0.9f, 0.45f, 0.95f);
        ImVec4 warnColor = ImVec4(1.0f, 0.75f, 0.35f, 0.95f);
        ImVec4 offColor = style.Colors[ImGuiCol_TextDisabled];
        ImVec4 dotColor = offColor;
        std::string tooltip;
        if (!state.cloudConfig.enabled) {
            tooltip = u8"Облако отключено.";
        } else {
            const std::int64_t storageStamp = GetStorageStamp(state);
            const bool hasOk = state.lastCloudSyncOk && state.lastCloudSyncOkAt > 0;
            const bool synced = hasOk && storageStamp <= state.lastCloudSyncOkAt;
            const bool hasSyncHealthIssues = state.syncHealth.issueCount > 0;
            const bool hasCloudDriftIssues = state.cloudWorkspaceDrift.issueCount > 0;
            if (!hasOk) {
                dotColor = warnColor;
                tooltip = u8"Синхронизация ещё не выполнялась.";
            } else if (synced && !hasSyncHealthIssues && !hasCloudDriftIssues) {
                dotColor = okColor;
                tooltip = u8"Синхронизировано.";
            } else {
                dotColor = warnColor;
                if (hasCloudDriftIssues) {
                    tooltip = u8"Локальное и облачное состояние задач/пайплайна расходится.";
                } else {
                    tooltip = hasSyncHealthIssues ? u8"Sync-файлы требуют внимания." : u8"Есть несинхронизированные данные.";
                }
            }
            if (state.lastCloudSyncOkAt > 0) {
                tooltip += u8"\nПоследняя синхронизация: ";
                tooltip += FormatTimestamp(state.lastCloudSyncOkAt, "%Y-%m-%d %H:%M");
            }
            if (hasCloudDriftIssues) {
                tooltip += u8"\nЛокально/облако: ";
                tooltip += std::to_string(state.cloudWorkspaceDrift.issueCount);
                if (state.cloudWorkspaceDrift.conflictCount > 0) {
                    tooltip += u8", конфликтов: ";
                    tooltip += std::to_string(state.cloudWorkspaceDrift.conflictCount);
                }
                const size_t shown = std::min<size_t>(3, state.cloudWorkspaceDrift.issues.size());
                for (size_t i = 0; i < shown; ++i) {
                    tooltip += "\n- ";
                    tooltip += state.cloudWorkspaceDrift.issues[i];
                }
                if (state.cloudWorkspaceDrift.issues.size() > shown) {
                    tooltip += u8"\n- ...";
                }
            }
            if (hasSyncHealthIssues) {
                tooltip += u8"\nПроблемы sync-файлов: ";
                tooltip += std::to_string(state.syncHealth.issueCount);
                const size_t shown = std::min<size_t>(3, state.syncHealth.issues.size());
                for (size_t i = 0; i < shown; ++i) {
                    tooltip += "\n- ";
                    tooltip += state.syncHealth.issues[i];
                }
                if (state.syncHealth.issues.size() > shown) {
                    tooltip += u8"\n- ...";
                }
            }
        }
        if (state.strayFileCount > 0) {
            tooltip += u8"\nЛишние файлы в хранилище: ";
            tooltip += std::to_string(state.strayFileCount);
            if (!state.straySamples.empty()) {
                tooltip += u8"\nПримеры: ";
                bool first = true;
                for (const auto& s : state.straySamples) {
                    if (!first) tooltip += ", ";
                    tooltip += s;
                    first = false;
                }
            }
        }
        ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, ImGui::GetColorU32(dotColor));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_chart", UiIcon::Chart, u8"Статистика", accent, iconSize, state.isAdmin, state.showAdminStats)) {
            OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_settings", UiIcon::Settings, u8"Настройки", accent, iconSize, true, state.showUiSettings)) {
            OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_about", UiIcon::Info, u8"О программе", accent, iconSize, true, state.showAbout)) {
            OpenWorkspaceTab(state, state.showAbout, UiWindowId::About);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_exit", UiIcon::Exit, u8"Выход", accent, iconSize)) {
            state.requestExit = true;
        }

        barHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        barAnyItemHovered = ImGui::IsAnyItemHovered();
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor();
    const bool canDragBar = window && !state.ui.windowDecorated && !state.ui.windowFullscreen;
    if (canDragBar && barHovered && !barAnyItemHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        DragWindow(window, ImGui::GetIO().MouseDelta);
    }
}

void DrawProfilesPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);

    if (state.profileProfessionDirty) {
        RefreshProfileProfessionCache(state, storage);
    }

    if (BeginCard("profiles_list_card")) {
        DrawCardHeader(u8"Профили");
        ImGui::TextDisabled(u8"Главное меню: выберите профиль или создайте новый.");
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        const float actionsWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionsWidth < 360.0f;
        if (stackActions) {
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать профиль", ImVec2(-1.0f, 0.0f))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                RefreshProfiles(state, storage, catalog);
            }
        } else if (ImGui::BeginTable("profile_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать профиль", ImVec2(-1.0f, 0.0f))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                RefreshProfiles(state, storage, catalog);
            }
            ImGui::EndTable();
        }
        const float filtersWidth = ImGui::GetContentRegionAvail().x;
        const bool stackFilters = filtersWidth < 640.0f;
        const char* archiveLabels[] = {u8"Все", u8"Активные", u8"Архив"};
        const char* profileSortLabels[] = {u8"ID", u8"Имя"};
        std::vector<std::string> professionLabels;
        std::vector<std::string> professionIds;
        professionLabels.emplace_back(u8"Все профессии");
        professionIds.emplace_back("");
        professionLabels.emplace_back(u8"Без профессии");
        professionIds.emplace_back("__none__");
        for (const auto& prof : state.professions) {
            if (prof.id.empty()) continue;
            professionLabels.push_back(prof.name);
            professionIds.push_back(prof.id);
        }
        std::vector<const char*> professionLabelPtrs;
        professionLabelPtrs.reserve(professionLabels.size());
        for (auto& label : professionLabels) {
            professionLabelPtrs.push_back(label.c_str());
        }
        if (state.profileProfessionFilter < 0 ||
            state.profileProfessionFilter >= static_cast<int>(professionLabelPtrs.size())) {
            state.profileProfessionFilter = 0;
        }
        if (stackFilters) {
            ImGui::TextDisabled(u8"Поиск");
            ImGui::InputTextWithHint(u8"##profile_search", u8"Поиск профиля",
                state.filters.profile.data(), state.filters.profile.size());
            if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
                state.filters.profile.fill('\0');
            }
            ImGui::TextDisabled(u8"Профили");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_archive", &state.profileArchiveFilter, archiveLabels, IM_ARRAYSIZE(archiveLabels));
            ImGui::TextDisabled(u8"Профессия");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_profession", &state.profileProfessionFilter,
                professionLabelPtrs.data(), static_cast<int>(professionLabelPtrs.size()));
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_sort", &state.profileSort, profileSortLabels, IM_ARRAYSIZE(profileSortLabels));
        } else if (ImGui::BeginTable("profile_filters", 5, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("filter_search", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("filter_archive", ImGuiTableColumnFlags_WidthStretch, 0.85f);
            ImGui::TableSetupColumn("filter_profession", ImGuiTableColumnFlags_WidthStretch, 0.95f);
            ImGui::TableSetupColumn("filter_sort", ImGuiTableColumnFlags_WidthStretch, 0.85f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Поиск");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##profile_search", u8"Поиск профиля",
                state.filters.profile.data(), state.filters.profile.size());
            if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
                state.filters.profile.fill('\0');
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Профили");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_archive", &state.profileArchiveFilter, archiveLabels, IM_ARRAYSIZE(archiveLabels));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled(u8"Профессия");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_profession", &state.profileProfessionFilter,
                professionLabelPtrs.data(), static_cast<int>(professionLabelPtrs.size()));
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_sort", &state.profileSort, profileSortLabels, IM_ARRAYSIZE(profileSortLabels));
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (!state.recentProfiles.empty()) {
            ImGui::TextDisabled(u8"Недавние");
            bool anyRecent = false;
            for (const auto& recentId : state.recentProfiles) {
                auto it = std::find_if(state.profiles.begin(), state.profiles.end(),
                    [&](const IJobStorage::ProfileInfo& info) { return info.id == recentId; });
                if (it == state.profiles.end()) continue;
                if (anyRecent) ImGui::SameLine(0.0f, style.ItemSpacing.x * 0.5f);
                anyRecent = true;
                ImGui::PushID(recentId.c_str());
                const std::string label = it->name.empty() ? it->id : it->name;
                if (ImGui::SmallButton(label.c_str())) {
                    state.selectedIndex = static_cast<int>(std::distance(state.profiles.begin(), it));
                    RefreshActiveProfile(state, storage, catalog);
                    SyncRankSelection(state);
                    SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("[%s] %s", it->id.c_str(), it->name.c_str());
                }
                ImGui::PopID();
            }
            if (anyRecent) {
                ImGui::Spacing();
            }
        }
        std::vector<int> visibleIndices;
        visibleIndices.reserve(state.profiles.size());
        std::string selectedProfessionId;
        if (state.profileProfessionFilter > 0 &&
            state.profileProfessionFilter < static_cast<int>(professionIds.size())) {
            selectedProfessionId = professionIds[state.profileProfessionFilter];
        }
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            const auto& info = state.profiles[i];
            if (state.profileArchiveFilter == 1 && info.archived) continue;
            if (state.profileArchiveFilter == 2 && !info.archived) continue;
            std::string profId;
            if (auto it = state.profileProfessionCache.find(info.id); it != state.profileProfessionCache.end()) {
                profId = it->second;
            }
            if (!selectedProfessionId.empty()) {
                if (selectedProfessionId == "__none__") {
                    if (!profId.empty()) continue;
                } else if (profId != selectedProfessionId) {
                    continue;
                }
            }
            std::string login = info.id;
            if (auto it = state.profileLoginCache.find(info.id); it != state.profileLoginCache.end() && !it->second.empty()) {
                login = it->second;
            }
            const std::string profName = ProfessionNameById(state, profId);
            if (!MatchesFilter(info.id + " " + info.name + " " + login + " " + profName,
                               state.filters.profile.data())) {
                continue;
            }
            visibleIndices.push_back(i);
        }
        const int sortMode = std::clamp(state.profileSort, 0, 1);
        std::sort(visibleIndices.begin(), visibleIndices.end(), [&](int a, int b) {
            const auto& pa = state.profiles[a];
            const auto& pb = state.profiles[b];
            if (sortMode == 1) {
                std::string nameA = ToLowerUtf8(pa.name);
                std::string nameB = ToLowerUtf8(pb.name);
                if (nameA != nameB) return nameA < nameB;
            }
            return pa.id < pb.id;
        });

        const float minListHeight = ImGui::GetTextLineHeightWithSpacing() * 10.0f;
        const float listHeight = std::max(220.0f, minListHeight);
        if (ImGui::BeginChild("profiles", ImVec2(0.0f, listHeight), false)) {
            ImGui::TextDisabled(u8"Показано: %d из %d",
            static_cast<int>(visibleIndices.size()),
            static_cast<int>(state.profiles.size()));
            if (state.profiles.empty()) {
                ImGui::TextDisabled(u8"Профили не найдены.");
                if (state.isAdmin) {
                    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
                    ImVec4 softBg = accent;
                    softBg.w = 0.18f;
                    ImVec4 softHover = accent;
                    softHover.w = 0.28f;
                    ImVec4 softActive = accent;
                    softActive.w = 0.38f;
                    ImGui::PushStyleColor(ImGuiCol_Button, softBg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, softHover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, softActive);
                    if (ImGui::Button(u8"Создать первый профиль", ImVec2(-1.0f, 0.0f))) {
                        state.modalBuffer.fill('\0');
                        state.createPopupRequest = true;
                    }
                    ImGui::PopStyleColor(3);
                } else {
                    ImGui::TextDisabled(u8"Создание профилей доступно администратору.");
                }
            } else if (visibleIndices.empty()) {
                if (DrawEmptyStateGui("profiles_filter_empty", UiIcon::Search,
                                      u8"Нет профилей по фильтру",
                                      u8"Сбросьте поиск, чтобы вернуть список профилей.",
                                      UiIcon::Refresh,
                                      u8"Сбросить фильтр",
                                      true)) {
                    state.filters.profile.fill('\0');
                }
            }
            for (int i : visibleIndices) {
                const auto& info = state.profiles[i];
                std::string label = "[" + info.id + "] " + info.name;
                std::string login = info.id;
                if (auto itLogin = state.profileLoginCache.find(info.id); itLogin != state.profileLoginCache.end() && !itLogin->second.empty()) {
                    login = itLogin->second;
                }
                if (!login.empty() && login != info.id) {
                    label += " · " + login;
                }
                if (info.archived) label += u8" (в архиве)";
                if (auto itBlocked = state.profileBlockedCache.find(info.id); itBlocked != state.profileBlockedCache.end() && itBlocked->second) {
                    label += u8" (заблокирован)";
                }
                if (state.isAdmin) {
                    if (auto itWallet = state.profileWalletCache.find(info.id); itWallet != state.profileWalletCache.end()) {
                        std::string currency = !state.vault.currencyCode.empty() ? state.vault.currencyCode : state.vault.currencyName;
                        if (currency.empty()) currency = "KUK";
                        std::ostringstream walletStream;
                        walletStream.imbue(std::locale::classic());
                        walletStream << std::fixed << std::setprecision(0) << itWallet->second;
                        label += u8" · Баланс: ";
                        label += walletStream.str();
                        label += " ";
                        label += currency;
                    }
                }
                if (ImGui::Selectable(label.c_str(), state.selectedIndex == i)) {
                    state.selectedIndex = i;
                    RefreshActiveProfile(state, storage, catalog);
                    SyncRankSelection(state);
                    SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                }
            }
        }
        ImGui::EndChild();
    }
    EndCard();

}





