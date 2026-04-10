#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>

#include "AppUtils.h"
#include "AppWorkspaceDataService.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"
#include "CloudSync.h"

static bool WriteFile(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    return out.good();
}

static bool ReadFile(const std::filesystem::path& path, std::string& outData) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    outData.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

static bool SetFileUnixTimestamp(const std::filesystem::path& path, std::int64_t unixSeconds) {
    using namespace std::chrono;
    std::error_code ec;
    const auto sysTarget = system_clock::time_point(seconds(unixSeconds));
    const auto delta = sysTarget - system_clock::now();
    const auto fsTarget = std::filesystem::file_time_type::clock::now()
        + duration_cast<std::filesystem::file_time_type::duration>(delta);
    std::filesystem::last_write_time(path, fsTarget, ec);
    return !ec;
}

static std::filesystem::path FindBackupByKind(const std::vector<std::filesystem::path>& backups,
                                              const std::string& kind) {
    const std::string token = "." + kind + ".";
    for (const auto& path : backups) {
        const std::string name = path.filename().string();
        if (name.find(token) != std::string::npos) {
            return path;
        }
    }
    return {};
}

static bool TestGameplayConfig(const std::filesystem::path& dir) {
    GameplayConfig cfg;
    cfg.levelBaseXp = 10;
    cfg.levelLinearXp = 2;
    cfg.levelQuadraticXp = 1;
    auto path = dir / "meta" / "gameplay.ini";
    if (!SaveGameplayConfig(cfg, dir)) return false;
    GameplayConfig loaded = LoadGameplayConfig(dir);
    return loaded.levelBaseXp == cfg.levelBaseXp && loaded.levelLinearXp == cfg.levelLinearXp;
}

static bool TestTasksPipelineRoundtrip(const std::filesystem::path& dir) {
    const std::string tasks = R"([{"id":"t1","title":"Task","project":"P"}])";
    const std::string pipeline = R"({"steps":[{"id":"p1","title":"A"}]})";
    const std::string audit = "1700000000|admin|t1|create||Task\n";
    if (!WriteFile(dir / "meta" / "tasks.json", tasks)) return false;
    if (!WriteFile(dir / "meta" / "pipeline.json", pipeline)) return false;
    if (!WriteFile(dir / "meta" / "task-audit.log", audit)) return false;
    ModuleToggles modules;
    WorkspaceSyncHealth health = InspectWorkspaceSyncHealth(dir, modules);
    return health.issueCount == 0 &&
           std::filesystem::exists(dir / "meta" / "tasks.json") &&
           std::filesystem::exists(dir / "meta" / "pipeline.json") &&
           std::filesystem::exists(dir / "meta" / "task-audit.log");
}

static bool TestSyncHealthDetectsBrokenFiles(const std::filesystem::path& dir) {
    if (!WriteFile(dir / "meta" / "tasks.json", "{broken")) return false;
    if (!WriteFile(dir / "meta" / "pipeline.json", "{\"steps\":[{\"id\":\"p1\"}]}")) return false;
    if (!WriteFile(dir / "meta" / "task-audit.log", "bad-line-without-separators")) return false;
    ModuleToggles modules;
    WorkspaceSyncHealth health = InspectWorkspaceSyncHealth(dir, modules);
    bool tasksIssue = false;
    bool pipelineIssue = false;
    bool auditIssue = false;
    for (const auto& issue : health.issues) {
        tasksIssue = tasksIssue || issue.find("meta/tasks.json") != std::string::npos;
        pipelineIssue = pipelineIssue || issue.find("meta/pipeline.json") != std::string::npos;
        auditIssue = auditIssue || issue.find("meta/task-audit.log") != std::string::npos;
    }
    return health.issueCount >= 3 && tasksIssue && pipelineIssue && auditIssue;
}

static bool TestProfileRank() {
    Profile p("Test");
    p.set_overall_level(40);
    const std::string rank = DescribeOverallRank(p);
    return rank.find("Джуниор") != std::string::npos;
}

static bool TestWhitelist(const std::filesystem::path& dir) {
    WriteFile(dir / "bad.txt", "x");
    WriteFile(dir / "meta" / "bad.json", "{}");
    WriteFile(dir / "meta" / "task-audit.log", "1700000000|admin|t1|create||Task\n");
    int removed = 0;
    RemoveStrayFiles(dir, removed);
    return removed >= 2 &&
           !std::filesystem::exists(dir / "bad.txt") &&
           std::filesystem::exists(dir / "meta" / "task-audit.log");
}

static bool TestStorageVaultRobustParsing(const std::filesystem::path& dir) {
    const std::string nbsp = u8" ";
    std::string storageJson;
    storageJson += "{\n";
    storageJson += "  \"currency_name\": \"Кукоин\",\n";
    storageJson += "  \"currency_code\": \"KUK\",\n";
    storageJson += "  \"balance_enc\": \"xor:76414257\",\n";
    storageJson += "  \"log_limit\": 10,\n";
    storageJson += "  \"rev\": 15,\n";
    storageJson += "  \"updated_at\": 1" + nbsp + "770" + nbsp + "312" + nbsp + "982,\n";
    storageJson += "  \"content_hash\": \"b07b8cc957f0aeb5\",\n";
    storageJson += "  \"pomodoro_start\": 540,\n";
    storageJson += "  \"pomodoro_end\": 1" + nbsp + "200,\n";
    storageJson += "  \"pomodoro_min\": 20,\n";
    storageJson += "  \"pomodoro_coin\": 1,\n";
    storageJson += "  \"pomodoro_days\": 62,\n";
    storageJson += "  \"log\": []\n";
    storageJson += "}\n";
    if (!WriteFile(dir / "meta" / "storage.json", storageJson)) return false;

    StorageVaultData loaded = LoadStorageVault(dir);
    if (loaded.updatedAt != 1770312982LL) return false;
    if (loaded.pomodoroEndMinutes != 1200) return false;
    if (loaded.pomodoroMinMinutes != 20) return false;

    if (!SaveStorageVault(dir, loaded)) return false;
    std::string saved;
    if (!ReadFile(dir / "meta" / "storage.json", saved)) return false;
    if (saved.find(nbsp) != std::string::npos) return false;
    if (saved.find("\"pomodoro_end\": 1200") == std::string::npos) return false;
    return true;
}

static bool TestCloudAtomicOverwrite(const std::filesystem::path& dir) {
    CloudSyncConfig config;
    config.enabled = true;
    config.root = "cloud";
    config.autoPull = true;
    config.autoPush = false;
    config.autoSyncEnabled = true;
    config.autoSyncMinutes = 15;

    if (!SaveCloudSyncConfig(dir, config)) return false;
    config.autoPull = false;
    config.autoPush = true;
    config.autoSyncMinutes = 45;
    if (!SaveCloudSyncConfig(dir, config)) return false;

    const CloudSyncConfig loadedConfig = LoadCloudSyncConfig(dir);
    if (loadedConfig.autoPull != false) return false;
    if (loadedConfig.autoPush != true) return false;
    if (loadedConfig.autoSyncMinutes != 45) return false;

    CloudManifest manifest;
    manifest.appVersion = "0.4.32";
    manifest.dataUpdatedAt = 100;
    manifest.releaseFile = "ForgeMirrorSetup_0.4.32.exe";
    manifest.notes = "First";
    if (!SaveCloudManifest(config, dir, manifest)) return false;

    manifest.appVersion = "0.4.33";
    manifest.dataUpdatedAt = 200;
    manifest.releaseFile.clear();
    manifest.notes.clear();
    if (!SaveCloudManifest(config, dir, manifest)) return false;

    const CloudManifest loadedManifest = LoadCloudManifest(config, dir);
    if (loadedManifest.appVersion != "0.4.33") return false;
    if (loadedManifest.dataUpdatedAt != 200) return false;
    if (loadedManifest.releaseFile != "ForgeMirrorSetup_0.4.32.exe") return false;
    if (loadedManifest.notes != "First") return false;
    return true;
}

static bool TestCloudDriftResolveRestore(const std::filesystem::path& dir) {
    auto fail = [](const char* message) {
        std::cerr << "cloudWorkspace: " << message << "\n";
        return false;
    };
    std::error_code ec;
    std::filesystem::path cloudRoot = dir;
    cloudRoot += "_cloud";
    std::filesystem::remove_all(cloudRoot, ec);

    CloudSyncConfig config;
    config.enabled = true;
    config.root = cloudRoot;

    const std::string localTasksOriginal = R"([{"id":"t1","title":"Local task"}])";
    const std::string cloudTasksVersion = R"([{"id":"t1","title":"Cloud task"}])";
    const std::string localPipelineOriginal = R"({"steps":[{"id":"p1","title":"Local old"}]})";
    const std::string cloudPipelineOriginal = R"({"steps":[{"id":"p1","title":"Cloud old"}]})";
    const std::string localPipelinePushed = R"({"steps":[{"id":"p1","title":"Local pushed"}]})";

    const auto localTasksPath = dir / "meta" / "tasks.json";
    const auto localPipelinePath = dir / "meta" / "pipeline.json";
    const auto cloudTasksPath = cloudRoot / "meta" / "tasks.json";
    const auto cloudPipelinePath = cloudRoot / "meta" / "pipeline.json";

    if (!WriteFile(localTasksPath, localTasksOriginal)) return fail("write local tasks");
    if (!WriteFile(cloudTasksPath, cloudTasksVersion)) return fail("write cloud tasks");
    if (!WriteFile(localPipelinePath, localPipelineOriginal)) return fail("write local pipeline");
    if (!WriteFile(cloudPipelinePath, cloudPipelineOriginal)) return fail("write cloud pipeline");

    const std::int64_t lastSyncAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 300;
    if (!SetFileUnixTimestamp(localTasksPath, lastSyncAt + 60)) return fail("timestamp local tasks");
    if (!SetFileUnixTimestamp(cloudTasksPath, lastSyncAt + 120)) return fail("timestamp cloud tasks");
    if (!SetFileUnixTimestamp(localPipelinePath, lastSyncAt - 120)) return fail("timestamp local pipeline");
    if (!SetFileUnixTimestamp(cloudPipelinePath, lastSyncAt + 120)) return fail("timestamp cloud pipeline");

    const CloudWorkspaceDriftSummary drift = InspectCloudWorkspaceDrift(config, dir, lastSyncAt);
    bool tasksConflict = false;
    bool pipelineCloudNewer = false;
    for (const auto& file : drift.files) {
        if (file.relativePath == "meta/tasks.json") {
            tasksConflict = file.hasIssue && file.conflict && file.localChanged && file.cloudChanged;
        } else if (file.relativePath == "meta/pipeline.json") {
            pipelineCloudNewer = file.hasIssue && !file.conflict && !file.localChanged && file.cloudChanged;
        }
    }
    if (drift.issueCount < 2 || drift.conflictCount < 1 || !tasksConflict || !pipelineCloudNewer) {
        return fail("unexpected drift summary");
    }

    const CloudWorkspaceResolveResult pullTasks =
        ResolveCloudWorkspaceFileVersion(config, dir, "meta/tasks.json", true);
    if (!pullTasks.ok || !pullTasks.changed) return fail("pull tasks resolve");
    std::string tasksAfterPull;
    if (!ReadFile(localTasksPath, tasksAfterPull) || tasksAfterPull != cloudTasksVersion) return fail("verify pulled tasks");
    if (ListCloudWorkspaceBackups(dir, "meta/tasks.json").size() < 2) return fail("tasks backups");

    const std::filesystem::path localTasksBackup = FindBackupByKind(pullTasks.backupPaths, "local");
    if (localTasksBackup.empty()) return fail("find local tasks backup");
    const CloudWorkspaceResolveResult restoreTasks =
        RestoreCloudWorkspaceBackup(dir, "meta/tasks.json", localTasksBackup);
    if (!restoreTasks.ok || !restoreTasks.changed) return fail("restore tasks backup");
    std::string tasksAfterRestore;
    if (!ReadFile(localTasksPath, tasksAfterRestore) || tasksAfterRestore != localTasksOriginal) return fail("verify restored tasks");

    if (!WriteFile(localPipelinePath, localPipelinePushed)) return fail("write local pipeline pushed");
    if (!SetFileUnixTimestamp(localPipelinePath, lastSyncAt + 180)) return fail("timestamp local pipeline pushed");
    const CloudWorkspaceResolveResult pushPipeline =
        ResolveCloudWorkspaceFileVersion(config, dir, "meta/pipeline.json", false);
    if (!pushPipeline.ok || !pushPipeline.changed) return fail("push pipeline resolve");
    std::string pipelineAfterPush;
    if (!ReadFile(cloudPipelinePath, pipelineAfterPush) || pipelineAfterPush != localPipelinePushed) return fail("verify pushed pipeline");
    if (ListCloudWorkspaceBackups(dir, "meta/pipeline.json").size() < 2) return fail("pipeline backups");

    const std::filesystem::path cloudPipelineBackup = FindBackupByKind(pushPipeline.backupPaths, "cloud");
    if (cloudPipelineBackup.empty()) return fail("find cloud pipeline backup");
    const CloudWorkspaceResolveResult restorePipeline =
        RestoreCloudWorkspaceBackup(dir, "meta/pipeline.json", cloudPipelineBackup);
    if (!restorePipeline.ok || !restorePipeline.changed) return fail("restore pipeline backup");
    std::string pipelineAfterRestore;
    if (!ReadFile(localPipelinePath, pipelineAfterRestore) || pipelineAfterRestore != cloudPipelineOriginal) {
        return fail("verify restored pipeline");
    }

    std::filesystem::remove_all(cloudRoot, ec);
    return true;
}

int main() {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "forgemirror_smoke";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);

    const bool okProfile = TestProfileRank();
    const bool okRules = TestGameplayConfig(tmp);
    const bool okTasks = TestTasksPipelineRoundtrip(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okSyncHealth = TestSyncHealthDetectsBrokenFiles(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okWhitelist = TestWhitelist(tmp);
    const bool okVault = TestStorageVaultRobustParsing(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okCloudOverwrite = TestCloudAtomicOverwrite(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okCloudWorkspace = TestCloudDriftResolveRestore(tmp);

    if (okProfile && okRules && okTasks && okSyncHealth && okWhitelist && okVault &&
        okCloudOverwrite && okCloudWorkspace) {
        std::cout << "smoke_core: OK\n";
        return 0;
    }
    std::cerr << "smoke_core failed: "
              << "profile=" << okProfile
              << " rules=" << okRules
              << " tasks=" << okTasks
              << " syncHealth=" << okSyncHealth
              << " whitelist=" << okWhitelist
              << " vault=" << okVault
              << " cloudOverwrite=" << okCloudOverwrite
              << " cloudWorkspace=" << okCloudWorkspace << "\n";
    return 1;
}
