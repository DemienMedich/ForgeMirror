#include "QtWindow.h"
#include "QtWorkspaceImport.h"
#include "QtReportChart.h"
#include "QtProfileAnalytics.h"
#include "QtLogActivityChart.h"
#include "AppTaskProjectService.h"
#include "AppWorkspaceStorageLock.h"
#include "AppTaskCompletionService.h"
#include "AppRecoveryStorage.h"
#include "QtTaskCompletionDialog.h"
#include "QtTheme.h"
#include "QtProfileDialogs.h"
#include "QtAchievements.h"
#include "QtProfessionEditor.h"
#include "QtRulesEditor.h"
#include "QtVaultEditor.h"
#include "QtBannerEditor.h"
#include "QtCloudSettings.h"
#include "QtCloudPull.h"
#include "QtCloudPushPreview.h"
#include "QtCloudAutoSync.h"
#include "QtCloudRelease.h"
#include "QtCloudConflict.h"
#include "QtStorageConflict.h"
#include "QtDisplaySettings.h"
#include "QtDeadlineAgent.h"
#include "QtReportExport.h"
#include "QtProfileReportExport.h"
#include "QtStorageHealthReport.h"
#include "QtAuditExport.h"
#include "QtPipelineEditor.h"
#include "QtPipelineTransition.h"
#include "QtPipelineMap.h"
#include "QtPomodoro.h"
#include "AppPipelineService.h"
#include "AppProfessionService.h"
#include "AppSkillService.h"
#include "AppProfileDeletionService.h"
#include "AppProfileMutationService.h"
#include "AppShortcutsService.h"
#include "CloudSync.h"
#include "QtSkillEditor.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <atomic>
#include <chrono>
#include <thread>
#include <QtTest/QTest>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <set>
#include <fstream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

static QStringList MissingAccessibleNames(QWidget* root);

// File-backed failure injection keeps the test on the real persistence path.
class FailingProfileStorage : public IJobStorage {
public:
    explicit FailingProfileStorage(IJobStorage& delegate) : delegate(delegate) {}
    IJobStorage& delegate;
    int failAt = 0;
    int writes = 0;
    bool failDelete = false;
    bool set_active_profile(const std::string& id) override { return delegate.set_active_profile(id); }
    std::vector<ProfileInfo> list_profiles() override { return delegate.list_profiles(); }
    std::optional<Profile> load_profile() override { return delegate.load_profile(); }
    std::optional<Profile> load_profile_snapshot(const std::string& id, bool archived) override { return delegate.load_profile_snapshot(id, archived); }
    bool save_profile(const Profile& profile) override { return ++writes != failAt && delegate.save_profile(profile); }
    std::optional<ProfileInfo> create_profile(const Profile& p) override { return delegate.create_profile(p); }
    bool set_archived(const std::string& id, bool value) override { return delegate.set_archived(id, value); }
    bool delete_profile(const std::string& id) override { return !failDelete && delegate.delete_profile(id); }
    std::optional<std::string> load_token() override { return delegate.load_token(); }
    bool save_token(const std::string& token) override { return delegate.save_token(token); }
    std::vector<XpEvent> load_queue() override { return delegate.load_queue(); }
    bool save_queue(const std::vector<XpEvent>& events) override { return delegate.save_queue(events); }
};

static bool TestTaskCompletion() {
    auto fail = [](const char* text) { std::cerr << "taskCompletion: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    auto contendingWriterAcquires = [&] {
        bool acquired = false;
        std::thread contender([&] {
            AppWorkspaceStorageWriteLock lock(workspace.directory);
            acquired = lock.acquired();
        });
        contender.join();
        return acquired;
    };
    {
        AppWorkspaceStorageWriteLock outer(workspace.directory);
        AppWorkspaceStorageWriteLock nested(workspace.directory);
        if (!outer.acquired() || !nested.acquired() || contendingWriterAcquires())
            return fail("workspace write lock was not reentrant and exclusive across threads");
    }
    if (!contendingWriterAcquires()) return fail("workspace write lock remained held after scope exit");
    PrepareProjectDeletionRecovery(workspace.directory);
    if (contendingWriterAcquires() || !AppSaveTasks(workspace.directory, workspace.data.tasks))
        return fail("journaled workspace lock did not exclude a writer or admit its own nested save");
    CommitQtRecoveryTransaction(workspace.directory);
    if (!contendingWriterAcquires()) return fail("journal commit did not release the workspace lock");
    workspace.catalog.add_skill("Modeling", 1.0, "Test modeling skill");
    const auto skill = *workspace.catalog.id_for_name("Modeling");
    auto a = workspace.storage->create_profile(Profile("Alice"));
    auto b = workspace.storage->create_profile(Profile("Bob"));
    if (!a || !b) return fail("profiles");
    TaskEntry task;
    task.id = "completion-test";
    task.title = "Completion";
    task.deadlinePenaltyPercent = 20;
    task.createdAt = 1700000000;
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, task, "test", &workspace.data.taskAudit).ok) return fail("task");
    FailingProfileStorage storage(*workspace.storage);
    AppContext context{workspace.directory, storage, workspace.catalog};
    std::vector<std::pair<AppLogLevel, std::string>> coreEvents;
    context.eventLogger = [&](AppLogLevel level, const std::string& event) {
        coreEvents.emplace_back(level, event);
    };
    TaskCompletionInput input;
    input.taskId = task.id;
    input.category = 0;
    input.score = 10;
    input.now = 1800000000;
    input.shares = {{a->id, 60}, {b->id, 40}};
    input.skills = {{skill, 5}};
    input.restoreProfileId = a->id;
    input.actor = "test";
    const auto preview = PreviewTaskCompletion(context, workspace.data.tasks, input);
    if (!preview.ok || preview.rawPool != 500 || preview.finalize.basePool != 400 ||
        preview.finalize.participants[0].globalXp != 240 || preview.finalize.participants[1].skillXp != 160)
        return fail("pool calculation");
    auto invalid = input;
    invalid.shares[0].percent = 50;
    if (PreviewTaskCompletion(context, workspace.data.tasks, invalid).ok) return fail("invalid total accepted");
    invalid = input;
    invalid.shares[1].profileId = a->id;
    if (PreviewTaskCompletion(context, workspace.data.tasks, invalid).ok) return fail("duplicate participant accepted");
    invalid = input;
    invalid.skills[0].rating = 0;
    if (PreviewTaskCompletion(context, workspace.data.tasks, invalid).ok) return fail("zero ratings accepted");
    const std::vector<std::string> paths = {a->id + ".ini", b->id + ".ini", "meta/tasks.json", "meta/task-audit.log", "meta/updates/tasks.last-good.json"};
    auto bytes = [&](const std::string& path) {
        QFile file(temp.path() + "/" + QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    std::vector<QByteArray> originals;
    for (const auto& path : paths) originals.push_back(bytes(path));
    const auto auditSize = workspace.data.taskAudit.size();
    for (int failure = 0; failure < 3; ++failure) {
        storage.writes = 0;
        storage.failAt = failure == 0 ? 2 : 0;
        AppSetTaskAuditFailureHookForTests(failure == 1);
        AppSetRecoveryPrimaryWriteFailureForTests(failure == 2);
        const auto result = CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input);
        AppSetTaskAuditFailureHookForTests(false);
        AppSetRecoveryPrimaryWriteFailureForTests(false);
        if (result.ok) return fail("write failure ignored");
        if (coreEvents.size() != size_t(failure + 1) || coreEvents.back().first != AppLogLevel::Error ||
            coreEvents.back().second != "Task XP transaction failed or was rolled back")
            return fail("core failure outcome was not reported through the optional event sink");
        for (size_t i = 0; i < paths.size(); ++i) if (bytes(paths[i]) != originals[i]) return fail("rollback changed stored bytes");
        if (!workspace.data.tasks[0].participants.empty() || workspace.data.tasks[0].status != 0 || workspace.data.taskAudit.size() != auditSize)
            return fail("rollback changed memory");
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("rollback left pending journal");
    }
    // Simulate a process interruption with a durable journal and partially changed files.
    const auto journal = workspace.directory / "meta/qt-xp-transaction";
    std::filesystem::create_directories(journal);
    {
        std::ofstream manifest(journal / "manifest", std::ios::binary);
        manifest << "FORGEMIRROR_QT_XP_1 " << paths.size() << '\n';
        for (const auto& path : paths) {
            std::filesystem::create_directories((journal / path).parent_path());
            std::filesystem::copy_file(workspace.directory / path, journal / path);
            manifest << std::quoted(path) << " 1\n";
        }
    }
    {
        std::ofstream changed(workspace.directory / (a->id + ".ini"), std::ios::binary | std::ios::trunc);
        changed << "interrupted-write";
    }
    workspace.reload();
    for (size_t i = 0; i < paths.size(); ++i) if (bytes(paths[i]) != originals[i]) return fail("restart recovery failed");
    const auto interruptedProfileCopy = workspace.transactionRecoveryPreservedFiles / (a->id + ".ini");
    QFile interruptedProfile(QString::fromStdWString(interruptedProfileCopy.wstring()));
    if (workspace.transactionRecoveryPreservedFiles.empty() ||
        !interruptedProfile.open(QIODevice::ReadOnly) || interruptedProfile.readAll() != "interrupted-write")
        return fail("restart recovery did not preserve the changed in-flight profile before rollback");
    QFile interruptedManifest(QString::fromStdWString((workspace.transactionRecoveryPreservedFiles / "manifest.txt").wstring()));
    if (!interruptedManifest.open(QIODevice::ReadOnly) || !interruptedManifest.readAll().contains(a->id.c_str()))
        return fail("restart recovery preservation manifest omitted the changed profile");
    const auto completion = CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input);
    if (!completion.ok) { std::cerr << completion.errorMessage << '\n'; return fail("completion failed"); }
    if (coreEvents.size() != 4 || coreEvents.back().first != AppLogLevel::Info ||
        coreEvents.back().second != "Task XP transaction committed" ||
        coreEvents.back().second.find(task.title) != std::string::npos ||
        coreEvents.back().second.find(a->id) != std::string::npos ||
        coreEvents.back().second.find("Alice") != std::string::npos ||
        coreEvents.back().second.find("Bob") != std::string::npos)
        return fail("core telemetry was not generic and privacy-safe");
    if (workspace.data.tasks[0].status != 2 || workspace.data.tasks[0].participants.size() != 2) return fail("completion not recorded");
    storage.set_active_profile(a->id);
    const auto after = storage.load_profile();
    if (!after || after->total_xp() != 240 || after->tasks_completed() != 1 || after->category_best_score(0) != 10) return fail("profile XP not saved");
    const auto rollback = workspace.data.tasks[0].participants[0].rollbackSnapshot;
    if (rollback.rfind("FORGEMIRROR_TASK_ROLLBACK_2\n", 0) != 0 ||
        !ProfileMatchesTaskRollbackPostcondition(rollback, *after)) return fail("rollback postcondition missing");
    const auto persistedRollback = LoadTasksData(workspace.directory).front().participants.front().rollbackSnapshot;
    if (persistedRollback != rollback || !ProfileMatchesTaskRollbackPostcondition(persistedRollback, *after))
        return fail("rollback envelope did not persist");
    const auto auditBeforeAwardedDelete = workspace.data.taskAudit.size();
    const auto profileBytesBeforeAwardedDelete = bytes(a->id + ".ini");
    AppSetTaskAuditFailureHookForTests(true);
    const auto failedAwardedDelete = DeleteAwardedTaskWithRecovery(context, workspace.data.tasks,
        workspace.data.taskAudit, input.taskId, a->id, "test");
    AppSetTaskAuditFailureHookForTests(false);
    storage.set_active_profile(a->id);
    const auto afterFailedAwardedDelete = storage.load_profile();
    if (failedAwardedDelete.ok || workspace.data.tasks.empty() || workspace.data.taskAudit.size() != auditBeforeAwardedDelete ||
        !afterFailedAwardedDelete || !ProfileMatchesTaskRollbackPostcondition(rollback, *afterFailedAwardedDelete) ||
        bytes(a->id + ".ini") != profileBytesBeforeAwardedDelete ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("awarded delete rollback failed");
    auto laterProgress = *afterFailedAwardedDelete;
    laterProgress.grant_global_xp(1);
    if (!storage.save_profile(laterProgress)) return fail("later progress fixture failed");
    const auto staleDelete = DeleteAwardedTaskWithRecovery(context, workspace.data.tasks,
        workspace.data.taskAudit, input.taskId, a->id, "test");
    if (staleDelete.ok || !staleDelete.awardRollbackUnavailable || workspace.data.tasks.empty() ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("later progress did not block awarded delete");
    const auto laterProgressBytes = bytes(a->id + ".ini");
    const auto auditBeforeKeepXp = workspace.data.taskAudit.size();
    AppSetTaskAuditFailureHookForTests(true);
    const auto failedKeepXpDelete = DeleteAwardedTaskRecordKeepXpWithRecovery(workspace.directory,
        workspace.data.tasks, workspace.data.taskAudit, input.taskId, "test");
    AppSetTaskAuditFailureHookForTests(false);
    if (failedKeepXpDelete.ok || workspace.data.tasks.empty() || workspace.data.taskAudit.size() != auditBeforeKeepXp ||
        bytes(a->id + ".ini") != laterProgressBytes || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("failed keep-XP cleanup did not recover task, audit and profile");
    const auto keepXpDelete = DeleteAwardedTaskRecordKeepXpWithRecovery(workspace.directory,
        workspace.data.tasks, workspace.data.taskAudit, input.taskId, "test");
    if (!keepXpDelete.ok || !workspace.data.tasks.empty() || bytes(a->id + ".ini") != laterProgressBytes ||
        workspace.data.taskAudit.size() != auditBeforeKeepXp + 2 || workspace.data.taskAudit.back().field != "xp_disposition" ||
        workspace.data.taskAudit.back().newValue != u8"сохранено в профилях; XP не изменён" ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("stale awarded task cleanup changed XP or missed its audit");
    if (!storage.set_active_profile(a->id) || !storage.save_profile(*afterFailedAwardedDelete)) return fail("later progress fixture restore failed");
    auto advanced = *after;
    advanced.grant_global_xp(1);
    if (ProfileMatchesTaskRollbackPostcondition(rollback, advanced)) return fail("later progress accepted as rollback postcondition");
    if (CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input).ok) return fail("duplicate XP accepted");
    Profile restored = *after;
    if (!ApplyProfileTaskRollbackSnapshot(rollback, restored) || restored.total_xp() != 0 || restored.tasks_completed() != 0)
        return fail("task rollback snapshot invalid");
    // A full deadline penalty closes the task with zero XP, without losing participants.
    task.id = "zero-pool";
    task.deadlinePenaltyPercent = 100;
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, task, "test").ok) return fail("zero fixture");
    input.taskId = task.id;
    auto throwingContext = context;
    throwingContext.eventLogger = [](AppLogLevel, const std::string&) { throw std::runtime_error("observer failure"); };
    if (!CompleteTaskWithXp(throwingContext, workspace.data.tasks, workspace.data.taskAudit, input).ok ||
        workspace.data.tasks.back().participants[0].globalXp != 0)
        return fail("zero pool completion");
    // Exact parity with ImGui: repeat, recovery, achievement and spirit modifiers.
    task.id = "modifiers";
    task.deadlinePenaltyPercent = 20;
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, task, "test").ok) return fail("modifier fixture");
    input.taskId = task.id;
    storage.set_active_profile(a->id);
    auto senior = *storage.load_profile();
    senior.set_category_best_scores({10, 10, 10, 10, 10});
    senior.set_overall_level(150);
    senior.set_last_task_timestamp(input.now - 31LL * 86400);
    senior.set_spirit(ProfileSpirit::Good);
    Achievement achievement;
    achievement.title = "Bonus";
    achievement.skill = skill;
    achievement.bonusPercent = 50;
    senior.add_achievement(achievement);
    if (!senior.penalties_enabled() || !storage.save_profile(senior)) return fail("senior fixture");
    const auto modified = PreviewTaskCompletion(context, workspace.data.tasks, input);
    if (!modified.ok || modified.finalize.participants[0].globalXp != 51 || modified.finalize.participants[0].skillXp != 364 ||
        modified.updatedProfiles[0].recovery_tasks_remaining() != 2) return fail("XP modifiers differ from ImGui");
    senior.set_blocked(true);
    storage.save_profile(senior);
    if (PreviewTaskCompletion(context, workspace.data.tasks, input).ok) return fail("blocked profile accepted");
    senior.set_blocked(false);
    storage.save_profile(senior);
    workspace.catalog.add_skill("Textures", 1.0, "Test texture skill");
    workspace.catalog.add_skill("Animation", 1.0, "Test animation skill");
    input.skills = {{skill, 1}, {*workspace.catalog.id_for_name("Textures"), 1}, {*workspace.catalog.id_for_name("Animation"), 1}};
    const auto rounded = PreviewTaskCompletion(context, workspace.data.tasks, input);
    if (!rounded.ok || rounded.skillPercents != std::vector<int>({34, 33, 33})) return fail("rating remainder mismatch");
    const auto beforeBadJournal = bytes(a->id + ".ini");
    std::filesystem::create_directories(journal);
    {
        std::ofstream manifest(journal / "manifest", std::ios::binary);
        manifest << "FORGEMIRROR_QT_XP_1 4\n\"../outside.ini\" 1\n";
    }
    bool rejectedJournal = false;
    try { RecoverTaskCompletion(workspace.directory); }
    catch (const std::exception&) { rejectedJournal = true; }
    if (!rejectedJournal || bytes(a->id + ".ini") != beforeBadJournal || !std::filesystem::exists(journal))
        return fail("invalid recovery journal was not rejected safely");
    for (const auto* unexpected : {"meta/profile-audit.log", "meta/storage.json"}) {
        std::filesystem::remove_all(journal);
        std::filesystem::create_directories(journal);
        {
            std::ofstream manifest(journal / "manifest", std::ios::binary);
            manifest << "FORGEMIRROR_QT_XP_1 1\n" << std::quoted(unexpected) << " 1\n";
        }
        rejectedJournal = false;
        try { RecoverTaskCompletion(workspace.directory); }
        catch (const std::exception&) { rejectedJournal = true; }
        if (!rejectedJournal || bytes(a->id + ".ini") != beforeBadJournal || !std::filesystem::exists(journal))
            return fail("wallet file escaped recovery journal allowlist");
    }
    return true;
}

static bool TestBulkAwardedTaskDeletion() {
    auto fail = [](const char* text) { std::cerr << "bulkTaskDelete: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    workspace.catalog.add_skill("Bulk fixture", 1.0, "Bulk delete test");
    const auto bulkSkill = *workspace.catalog.id_for_name("Bulk fixture");
    Profile bulkProfile("Bulk delete");
    bulkProfile.add_skill(bulkSkill);
    auto profile = workspace.storage->create_profile(bulkProfile);
    if (!profile) return fail("profile fixture");
    AppContext context{workspace.directory, *workspace.storage, workspace.catalog};
    const std::vector<std::string> ids{"bulk-award-a", "bulk-award-b"};
    for (size_t i = 0; i < ids.size(); ++i) {
        TaskEntry task; task.id = ids[i]; task.title = ids[i]; task.createdAt = 1700000000 + std::int64_t(i);
        if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, task, "test", &workspace.data.taskAudit).ok)
            return fail("task fixture");
        TaskCompletionInput input; input.taskId = ids[i]; input.category = int(i); input.score = 10;
        input.shares = {{profile->id, 100}}; input.skills = {{bulkSkill, 5}}; input.now = 1700000100 + std::int64_t(i);
        input.restoreProfileId = profile->id; input.actor = "test";
        const auto awarded = CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input);
        if (!awarded.ok) { std::cerr << "bulkTaskDelete award error: " << awarded.errorMessage << '\n'; return fail("award fixture"); }
    }
    const auto profilePath = workspace.directory / (profile->id + ".ini");
    auto bytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto profileAfterAwards = bytes(profilePath);
    const auto tasksAfterAwards = bytes(workspace.directory / "meta/tasks.json");
    const auto auditAfterAwards = bytes(workspace.directory / "meta/task-audit.log");
    AppSetTaskAuditFailureHookForTests(true);
    const auto failed = DeleteAwardedTasksWithRecovery(context, workspace.data.tasks,
        workspace.data.taskAudit, ids, profile->id, "test");
    AppSetTaskAuditFailureHookForTests(false);
    if (failed.ok || workspace.data.tasks.size() != 2 || bytes(profilePath) != profileAfterAwards ||
        bytes(workspace.directory / "meta/tasks.json") != tasksAfterAwards ||
        bytes(workspace.directory / "meta/task-audit.log") != auditAfterAwards ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("failed batch did not restore exact bytes");
    if (!workspace.storage->set_active_profile(profile->id)) return fail("reload active profile");
    auto changed = workspace.storage->load_profile();
    if (!changed) return fail("load profile");
    changed->grant_global_xp(1);
    if (!workspace.storage->save_profile(*changed)) return fail("stale profile fixture");
    const auto stale = DeleteAwardedTasksWithRecovery(context, workspace.data.tasks,
        workspace.data.taskAudit, ids, profile->id, "test");
    if (stale.ok || !stale.awardRollbackUnavailable || workspace.data.tasks.size() != 2)
        return fail("later progress did not block batch deletion");
    { std::ofstream restore(profilePath, std::ios::binary | std::ios::trunc); restore.write(profileAfterAwards.data(), std::streamsize(profileAfterAwards.size())); }
    workspace.reload();
    if (!DeleteAwardedTasksWithRecovery(context, workspace.data.tasks, workspace.data.taskAudit,
            ids, profile->id, "test").ok || !workspace.data.tasks.empty())
        return fail("valid rollback chain did not delete batch");
    if (!workspace.storage->set_active_profile(profile->id)) return fail("final active profile");
    const auto restored = workspace.storage->load_profile();
    return restored && restored->total_xp() == 0 && restored->tasks_completed() == 0 &&
        !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction");
}

static bool TestRulesReapplyRecovery() {
    auto fail = [](const char* text) { std::cerr << "rulesReapply: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    Profile first("Active"), second("Archived");
    first.set_total_xp(6000); second.set_total_xp(3200);
    const auto active = workspace.storage->create_profile(first);
    const auto archived = workspace.storage->create_profile(second);
    if (!active || !archived || !workspace.storage->set_archived(archived->id, true)) return fail("fixtures");
    auto read = [&](const std::string& path) {
        QFile file(temp.path() + "/" + QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    const auto activePath = active->id + ".ini";
    const auto archivedPath = "archive/" + archived->id + ".ini";
    const auto activeBefore = read(activePath), archivedBefore = read(archivedPath);
    FailingProfileStorage failing(*workspace.storage);
    AppContext context{workspace.directory, failing, workspace.catalog};
    std::vector<std::pair<AppLogLevel, std::string>> coreEvents;
    context.eventLogger = [&](AppLogLevel level, const std::string& event) { coreEvents.emplace_back(level, event); };
    GameplayConfig changed = GetGameplayConfig();
    changed.levelBaseXp = 300; changed.levelLinearXp = 20; changed.levelQuadraticXp = 2;
    SetGameplayConfig(changed);
    failing.failAt = 2;
    const auto rejected = ReapplyRulesWithRecovery(context, active->id);
    if (rejected.ok || read(activePath) != activeBefore || read(archivedPath) != archivedBefore ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction") || coreEvents.size() != 1 ||
        coreEvents.back().first != AppLogLevel::Warning ||
        coreEvents.back().second != "Rules reapply failed or was rolled back") return fail("partial-write rollback or telemetry");

    PrepareRulesReapplyRecovery(workspace.directory, {{active->id, false}, {archived->id, true}});
    { std::ofstream corrupt(workspace.directory / activePath, std::ios::binary | std::ios::trunc); corrupt << "interrupted"; }
    workspace.reload();
    SetGameplayConfig(changed);
    if (read(activePath) != activeBefore || read(archivedPath) != archivedBefore) return fail("restart recovery");

    failing.writes = 0; failing.failAt = 0;
    const auto applied = ReapplyRulesWithRecovery(context, active->id);
    if (!applied.ok || applied.affectedProfiles != 2 || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction") ||
        coreEvents.size() != 2 || coreEvents.back().first != AppLogLevel::Info ||
        coreEvents.back().second != "Rules reapply transaction committed") {
        std::cerr << "rulesReapply result: " << applied.errorMessage << " affected=" << applied.affectedProfiles << '\n';
        return fail("commit");
    }
    if (!failing.set_active_profile(active->id)) return fail("active selection");
    const auto afterActive = failing.load_profile();
    if (!afterActive || afterActive->total_xp() != 6000 || afterActive->overall_level() == first.overall_level())
        return fail("active XP preservation");
    if (!failing.set_archived(archived->id, false) || !failing.set_active_profile(archived->id)) return fail("archived selection");
    const auto afterArchived = failing.load_profile();
    if (!failing.set_archived(archived->id, true) || !afterArchived || afterArchived->total_xp() != 3200 || afterArchived->overall_level() == second.overall_level())
        return fail("archived XP preservation");
    return true;
}

static bool TestDirectXpRecovery() {
    auto fail = [](const char* text) { std::cerr << "directXp: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    workspace.catalog.add_skill("drawing", 1.0, "Drawing");
    const auto skillId = *workspace.catalog.id_for_name("drawing");
    Profile profile("Artist");
    Achievement bonus; bonus.title = "Bonus"; bonus.skill = skillId; bonus.bonusPercent = 50; bonus.awardedAt = 1;
    profile.add_achievement(bonus);
    const auto created = workspace.storage->create_profile(profile);
    if (!created) return fail("fixture");
    auto read = [&](const std::string& path) {
        QFile file(temp.path() + "/" + QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    const auto profilePath = created->id + ".ini";
    const auto achievementPath = "achievements/" + created->id + ".json";
    const std::string profileAuditPath = "meta/profile-audit.log";
    const auto beforeProfile = read(profilePath), beforeAchievements = read(achievementPath), beforeProfileAudit = read(profileAuditPath);
    FailingProfileStorage failing(*workspace.storage);
    AppContext context{workspace.directory, failing, workspace.catalog};
    std::vector<std::pair<AppLogLevel, std::string>> coreEvents;
    context.eventLogger = [&](AppLogLevel level, const std::string& event) { coreEvents.emplace_back(level, event); };
    failing.failAt = 1;
    const auto rejected = GrantDirectSkillXpWithRecovery(context, created->id, created->id, skillId, 100, 1000);
    if (rejected.ok || read(profilePath) != beforeProfile || read(achievementPath) != beforeAchievements ||
        coreEvents.size() != 1 || coreEvents.back().first != AppLogLevel::Warning ||
        coreEvents.back().second != "Direct skill XP failed or was rolled back" ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("write rollback");

    AppSetProfileAuditFailureHookForTests(true);
    const auto auditRejected = GrantDirectSkillXpWithRecovery(context, created->id, created->id, skillId, 100, 1000);
    AppSetProfileAuditFailureHookForTests(false);
    if (auditRejected.ok || read(profilePath) != beforeProfile || read(achievementPath) != beforeAchievements ||
        coreEvents.size() != 2 || coreEvents.back().first != AppLogLevel::Warning ||
        coreEvents.back().second != "Direct skill XP failed or was rolled back" ||
        read(profileAuditPath) != beforeProfileAudit ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("audit rollback");

    PrepareDirectXpRecovery(workspace.directory, created->id);
    { std::ofstream changed(workspace.directory / profilePath, std::ios::binary | std::ios::trunc); changed << "interrupted"; }
    { std::ofstream changed(workspace.directory / achievementPath, std::ios::binary | std::ios::trunc); changed << "[]"; }
    { std::ofstream changed(workspace.directory / profileAuditPath, std::ios::binary | std::ios::app); changed << "interrupted\n"; }
    workspace.reload();
    if (read(profilePath) != beforeProfile || read(achievementPath) != beforeAchievements ||
        read(profileAuditPath) != beforeProfileAudit) return fail("restart recovery");

    failing.writes = 0; failing.failAt = 0;
    const auto applied = GrantDirectSkillXpWithRecovery(context, created->id, created->id, skillId, 100, 1000);
    if (!applied.ok || applied.awardedGlobalXp != 100 || applied.awardedSkillXp != 150 ||
        coreEvents.size() != 3 || coreEvents.back().first != AppLogLevel::Info ||
        coreEvents.back().second != "Direct skill XP transaction committed" ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("commit");
    if (!read(profileAuditPath).contains("|direct_xp|" + QByteArray::fromStdString(skillId) + " base=100 skill=150"))
        return fail("audit commit");
    failing.set_active_profile(created->id);
    const auto after = failing.load_profile();
    if (!after || after->total_xp() != 100 || after->list_skills().size() != 1 || after->list_skills()[0].xp != 150)
        return fail("persisted values");
    auto blocked = *after; blocked.set_blocked(true);
    if (!failing.save_profile(blocked)) return fail("blocked fixture");
    const auto blockedBytes = read(profilePath);
    const auto denied = GrantDirectSkillXpWithRecovery(context, created->id, created->id, skillId, 10, 1000);
    if (denied.ok || read(profilePath) != blockedBytes || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("blocked guard");
    return true;
}

static bool TestVaultEditor() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    workspace.data.vault.balance = 321.5;
    workspace.data.vault.log.push_back({1700000000, 12.0, "test", "preserve"});
    if (!SaveStorageVault(workspace.directory, workspace.data.vault)) return false;
    workspace.reload();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "vaultEditor") return;
        dialog->findChild<QLineEdit*>("vaultCurrencyName")->setText(QString::fromUtf8("Фаркойн"));
        dialog->findChild<QLineEdit*>("vaultCurrencyCode")->setText("FRC");
        dialog->findChild<QSpinBox*>("vaultLogLimit")->setValue(12);
        dialog->findChild<QTimeEdit*>("vaultPomodoroStart")->setTime(QTime(8, 15));
        dialog->findChild<QTimeEdit*>("vaultPomodoroEnd")->setTime(QTime(19, 45));
        dialog->findChild<QSpinBox*>("vaultPomodoroMinimum")->setValue(25);
        dialog->findChild<QSpinBox*>("vaultPomodoroCoins")->setValue(3);
        for (int index = 0; index < 7; ++index) dialog->findChild<QCheckBox*>(QString("vaultDay%1").arg(index))->setChecked(index == 1 || index == 3);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/vault-editor.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    if (!ShowVaultEditor(nullptr, workspace)) return false;
    const auto saved = LoadStorageVault(workspace.directory);
    if (saved.currencyName != u8"Фаркойн" || saved.currencyCode != "FRC" || saved.logLimit != 12 ||
        saved.pomodoroStartMinutes != 8 * 60 + 15 || saved.pomodoroEndMinutes != 19 * 60 + 45 ||
        saved.pomodoroMinMinutes != 25 || saved.pomodoroCoinsPerCycle != 3 || saved.pomodoroDaysMask != ((1 << 1) | (1 << 3)) ||
        std::abs(saved.balance - 321.5) > 0.000001 || saved.log.size() != 1 || saved.log[0].note != "preserve") return false;
#ifdef _WIN32
    const auto path = (workspace.directory / "meta/storage.json").wstring();
    QFile before(QString::fromStdWString(path)); if (!before.open(QIODevice::ReadOnly)) return false; const auto bytes = before.readAll(); before.close();
    const HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    bool lockFailureSeen = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        dialog->findChild<QLineEdit*>("vaultCurrencyCode")->setText("LOCKED");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        QTimer::singleShot(0, [dialog, &lockFailureSeen] {
            lockFailureSeen = !dialog->findChild<QLabel*>("vaultNotice")->text().isEmpty();
            dialog->reject();
        });
    });
    const bool accepted = ShowVaultEditor(nullptr, workspace); CloseHandle(lock);
    QFile after(QString::fromStdWString(path));
    if (accepted || !lockFailureSeen || !after.open(QIODevice::ReadOnly) || after.readAll() != bytes) return false;
#endif
    return true;
}

static bool TestBannerEditor() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().toStdString()));
    workspace.data.bannerTexts = {u8"Первая фраза"};
    if (!SaveBannerTexts(workspace.directory, workspace.data.bannerTexts)) return false;
    workspace.reload();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "bannerEditor") return;
        dialog->findChild<QPlainTextEdit*>("bannerText")->setPlainText(QString::fromUtf8("Обновлённая фраза"));
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/banner-editor.png"); }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    if (!ShowBannerEditor(nullptr, workspace, 0) || workspace.data.bannerTexts != std::vector<std::string>{u8"Обновлённая фраза"}) return false;
#ifdef _WIN32
    const auto path = (workspace.directory / "meta/banner.json").wstring();
    QFile stored(QString::fromStdWString(path)); if (!stored.open(QIODevice::ReadOnly)) return false; const auto before = stored.readAll(); stored.close();
    const HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    bool failureSeen = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        dialog->findChild<QPlainTextEdit*>("bannerText")->setPlainText("LOCKED");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        QTimer::singleShot(0, [dialog, &failureSeen] { failureSeen = !dialog->findChild<QLabel*>("bannerNotice")->text().isEmpty(); dialog->reject(); });
    });
    const bool accepted = ShowBannerEditor(nullptr, workspace); CloseHandle(lock);
    if (accepted || !failureSeen || !stored.open(QIODevice::ReadOnly) || stored.readAll() != before) return false; stored.close();
#endif
    QString error;
    return DeleteBannerTextChecked(workspace, 0, &error) && workspace.data.bannerTexts.empty() && LoadBannerTexts(workspace.directory).empty();
}

static bool TestCloudSettings() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + QString::fromUtf8("/внешнее-облако")).toUtf8().toStdString());
    std::filesystem::create_directories(workspace); std::filesystem::create_directories(cloud);
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog || dialog->objectName() != "cloudSettings") return;
        dialog->findChild<QCheckBox*>("cloudEnabled")->setChecked(true);
        dialog->findChild<QLineEdit*>("cloudRoot")->setText(QString::fromUtf8(cloud.u8string()));
        dialog->findChild<QCheckBox*>("cloudAutoPush")->setChecked(false);
        dialog->findChild<QCheckBox*>("cloudIncludeAdmin")->setChecked(true);
        dialog->findChild<QCheckBox*>("cloudAutoSync")->setChecked(true);
        dialog->findChild<QSpinBox*>("cloudMinutes")->setValue(27);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/cloud-settings.png"); }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    if (!ShowCloudSettings(nullptr, workspace)) return false;
    const auto saved = LoadCloudSyncConfig(workspace);
    if (!saved.enabled || saved.root != cloud || !saved.autoPull || saved.autoPush || !saved.includeAdminProfiles || !saved.autoSyncEnabled || saved.autoSyncMinutes != 27) return false;
    QFile config(QString::fromUtf8((workspace / "meta/cloud.ini").u8string())); if (!config.open(QIODevice::ReadOnly)) return false;
    const auto before = config.readAll(); config.close();
    bool overlapSeen = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); dialog->findChild<QLineEdit*>("cloudRoot")->setText(QString::fromUtf8(workspace.u8string()));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        QTimer::singleShot(0, [dialog, &overlapSeen] { overlapSeen = !dialog->findChild<QLabel*>("cloudNotice")->text().isEmpty(); dialog->reject(); });
    });
    if (ShowCloudSettings(nullptr, workspace) || !overlapSeen || !config.open(QIODevice::ReadOnly) || config.readAll() != before) return false; config.close();
#ifdef _WIN32
    const auto path = (workspace / "meta/cloud.ini").wstring(); const HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false; bool lockSeen = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); dialog->findChild<QCheckBox*>("cloudAutoPush")->setChecked(true);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        QTimer::singleShot(0, [dialog, &lockSeen] { lockSeen = !dialog->findChild<QLabel*>("cloudNotice")->text().isEmpty(); dialog->reject(); });
    });
    const bool accepted = ShowCloudSettings(nullptr, workspace); CloseHandle(lock);
    if (accepted || !lockSeen || !config.open(QIODevice::ReadOnly) || config.readAll() != before) return false; config.close();
#endif
    return true;
}

static bool TestCloudPullTransaction() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + "/cloud").toUtf8().toStdString());
    std::filesystem::create_directories(workspace / "meta");
    std::filesystem::create_directories(cloud / "meta");
    auto write = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    if (!write(workspace / "meta/tasks.json", "[{\"id\":\"local\"}]") ||
        !write(cloud / "meta/tasks.json", "[{\"id\":\"cloud\"}]")) return false;
    CloudSyncConfig config; config.enabled = true; config.root = cloud;
    const auto result = RunQtCloudPullTransaction(config, workspace, CloudRole::Viewer);
    QFile pulled(QString::fromUtf8((workspace / "meta/tasks.json").u8string()));
    if (!result.sync.ok || !result.sync.changed || result.backupPath.empty() ||
        !std::filesystem::is_directory(result.backupPath) || !pulled.open(QIODevice::ReadOnly) ||
        !pulled.readAll().contains("cloud")) return false;
    QFile backup(QString::fromUtf8((result.backupPath / "meta/tasks.json").u8string()));
    if (!backup.open(QIODevice::ReadOnly) || !backup.readAll().contains("local")) return false;
    pulled.close(); backup.close();
    auto bytes = [](const std::filesystem::path& path) {
        QFile file(QString::fromUtf8(path.u8string()));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    const auto identical = RunQtCloudPullTransaction(config, workspace, CloudRole::Viewer);
    if (!identical.sync.ok || identical.sync.changed || !identical.backupPath.empty()) return false;
    auto overlap = config; overlap.root = workspace;
    if (RunQtCloudPullTransaction(overlap, workspace, CloudRole::Viewer).sync.ok) return false;
    overlap.root = workspace.parent_path();
    if (RunQtCloudPullTransaction(overlap, workspace, CloudRole::Viewer).sync.ok) return false;
    if (!write(cloud / "meta/tasks.json", "{broken")) return false;
    if (RunQtCloudPullTransaction(config, workspace, CloudRole::Viewer).sync.ok ||
        !bytes(workspace / "meta/tasks.json").contains("cloud")) return false;
    if (!write(cloud / "meta/tasks.json", "[{\"id\":\"next\"}]") ||
        !write(workspace / "meta/storage.json", "{\"rev\":1,\"balance\":1}") ||
        !write(cloud / "meta/storage.json", "{\"rev\":1,\"balance\":2}")) return false;
    const auto conflict = RunQtCloudPullTransaction(config, workspace, CloudRole::Viewer);
    if (conflict.sync.ok || !conflict.sync.storageConflict ||
        !bytes(workspace / "meta/tasks.json").contains("cloud")) return false;
    std::filesystem::remove(cloud / "meta/storage.json");
#ifdef _WIN32
    if (!write(workspace / "meta/banner.json", "[\"local\"]") ||
        !write(cloud / "meta/banner.json", "[\"remote\"]")) return false;
    const HANDLE lock = CreateFileW((workspace / "meta/tasks.json").c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    const auto failed = RunQtCloudPullTransaction(config, workspace, CloudRole::Viewer);
    CloseHandle(lock);
    if (failed.sync.ok || !failed.rolledBack || bytes(workspace / "meta/banner.json") != "[\"local\"]" ||
        !bytes(workspace / "meta/tasks.json").contains("cloud") ||
        std::filesystem::exists(workspace / "meta/qt-cloud-pull.json")) return false;
#endif
    // Recreate an interrupted commit, then recover through the actual workspace constructor.
    const QByteArray before = bytes(result.backupPath / "meta/tasks.json");
    const QByteArray pulledTasks = "[{\"id\":\"cloud\"}]";
    const QByteArray pulledBanner = "[\"remote\"]";
    const QByteArray emptyHash = QCryptographicHash::hash({}, QCryptographicHash::Sha256).toHex();
    QJsonArray entries{QJsonObject{{"path", "meta/tasks.json"}, {"existed", true},
        {"hash", QString::fromLatin1(QCryptographicHash::hash(before, QCryptographicHash::Sha256).toHex())},
        {"afterExists", true}, {"afterHash", QString::fromLatin1(QCryptographicHash::hash(pulledTasks, QCryptographicHash::Sha256).toHex())}},
        QJsonObject{{"path", "meta/banner.json"}, {"existed", false}, {"hash", QString::fromLatin1(emptyHash)},
            {"afterExists", true}, {"afterHash", QString::fromLatin1(QCryptographicHash::hash(pulledBanner, QCryptographicHash::Sha256).toHex())}}};
    const auto journal = workspace / "meta/qt-cloud-pull.json";
    if (!write(workspace / "meta/tasks.json", pulledTasks) || !write(workspace / "meta/banner.json", pulledBanner)) return false;
    auto journalBytes = QJsonDocument(QJsonObject{{"version", 2},
        {"backup", QString::fromStdWString(result.backupPath.filename().wstring())}, {"files", entries}}).toJson();
    if (!write(journal, journalBytes)) return false;
    {
        QtWorkspace recovered(workspace);
        if (!recovered.cloudPullRecoveryNotice) return false;
        QtWindow recoveryWindow(recovered);
        QFile log(QString::fromUtf8((workspace / "meta/qt-application-log.json").u8string()));
        if (recovered.cloudPullRecoveryNotice || !log.open(QIODevice::ReadOnly)) return false;
        const auto bytes = log.readAll();
        if (!bytes.contains("interrupted manual cloud pull was rolled back") || bytes.contains(cloud.u8string().c_str())) return false;
    }
    if (bytes(workspace / "meta/tasks.json") != before || std::filesystem::exists(journal) ||
        std::filesystem::exists(workspace / "meta/banner.json")) return false;

    // If another client edits one target after an interrupted pull, validate every target
    // before restoring any of them. The conflicting bytes and the other pulled file stay intact.
    if (!write(workspace / "meta/tasks.json", pulledTasks) || !write(workspace / "meta/banner.json", pulledBanner) ||
        !write(journal, journalBytes) || !write(workspace / "meta/tasks.json", "[{\"id\":\"external\"}]")) return false;
    bool externalChangeRejected = false;
    try { RecoverQtCloudPull(workspace); } catch (const std::exception&) { externalChangeRejected = true; }
    if (!externalChangeRejected || bytes(workspace / "meta/tasks.json") != "[{\"id\":\"external\"}]" ||
        bytes(workspace / "meta/banner.json") != pulledBanner || !std::filesystem::exists(journal)) return false;
    if (!write(workspace / "meta/tasks.json", pulledTasks) || !RecoverQtCloudPull(workspace) ||
        bytes(workspace / "meta/tasks.json") != before || std::filesystem::exists(workspace / "meta/banner.json") ||
        std::filesystem::exists(journal)) return false;

    // A v1 journal lacks the applied-state hashes, so a changed target must be left untouched
    // for manual inspection instead of being guessed to be a partial pull write.
    const auto legacyEntries = QJsonArray{QJsonObject{{"path", "meta/tasks.json"}, {"existed", true},
        {"hash", QString::fromLatin1(QCryptographicHash::hash(before, QCryptographicHash::Sha256).toHex())}}};
    const auto legacyJournal = QJsonDocument(QJsonObject{{"version", 1},
        {"backup", QString::fromStdWString(result.backupPath.filename().wstring())}, {"files", legacyEntries}}).toJson();
    if (!write(workspace / "meta/tasks.json", "[{\"id\":\"legacy-external\"}]") || !write(journal, legacyJournal)) return false;
    bool legacyConflictRejected = false;
    try { RecoverQtCloudPull(workspace); } catch (const std::exception&) { legacyConflictRejected = true; }
    if (!legacyConflictRejected || bytes(workspace / "meta/tasks.json") != "[{\"id\":\"legacy-external\"}]" ||
        !std::filesystem::exists(journal)) return false;
    if (!write(workspace / "meta/tasks.json", before) || !RecoverQtCloudPull(workspace) || std::filesystem::exists(journal)) return false;

    entries.append(QJsonObject{{"path", "../outside.ini"}, {"existed", false}});
    journalBytes = QJsonDocument(QJsonObject{{"version", 2},
        {"backup", QString::fromStdWString(result.backupPath.filename().wstring())}, {"files", entries}}).toJson();
    if (!write(journal, journalBytes)) return false;
    bool rejected = false;
    try { RecoverQtCloudPull(workspace); } catch (const std::exception&) { rejected = true; }
    if (!rejected || !std::filesystem::exists(journal) || bytes(workspace / "meta/tasks.json") != before) return false;
    std::filesystem::remove(journal);
    if (!SaveCloudSyncConfig(workspace, config) || !SaveBannerTexts(cloud, {"remote"})) return false;
    QtWorkspace uiWorkspace(workspace);
    QtWindow window(uiWorkspace); window.show(); QApplication::processEvents();
    window.findChild<QListWidget*>("navigation")->setCurrentRow(13);
    auto* pull = window.findChild<QPushButton*>("cloudPull");
    if (!pull || !pull->isEnabled()) return false;
    QTimer::singleShot(0, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Cancel)->click();
    });
    pull->click();
    if (bytes(workspace / "meta/tasks.json") != before) return false;
    bool confirmed = false;
    QTimer::singleShot(0, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            confirmed = box->defaultButton() == box->button(QMessageBox::Cancel);
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); box->grab().save(artifacts + "/cloud-pull-confirm.png"); }
            box->button(QMessageBox::Yes)->click();
        }
    });
    pull->click();
    if (!confirmed || !bytes(workspace / "meta/tasks.json").contains("next") ||
        uiWorkspace.data.bannerTexts != std::vector<std::string>{"remote"}) return false;
    QTimer::singleShot(0, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Yes)->click();
    });
    pull->click();
    QFile telemetry(QString::fromUtf8((workspace / "meta/qt-application-log.json").u8string()));
    if (!telemetry.open(QIODevice::ReadOnly)) return false;
    const auto telemetryBytes = telemetry.readAll();
    if (!telemetryBytes.contains("CoreCloudTransaction") || !telemetryBytes.contains("Manual cloud pull committed") ||
        telemetryBytes.contains(cloud.u8string().c_str())) return false;
    window.close();
    CloudSyncConfig disabled = config; disabled.enabled = false;
    return !RunQtCloudPullTransaction(disabled, workspace, CloudRole::Viewer).sync.ok;
}

static bool TestCloudAutoSync() {
    CloudSyncConfig schedule; schedule.enabled = true; schedule.autoSyncEnabled = true;
    schedule.autoSyncMinutes = 15;
    if (QtCloudAutoSyncDue(schedule, 1000 + 899, 1000) ||
        !QtCloudAutoSyncDue(schedule, 1000 + 900, 1000)) { std::cerr << "auto schedule 15m first=" << QtCloudAutoSyncDue(schedule, 1000 + 899, 1000) << " second=" << QtCloudAutoSyncDue(schedule, 1000 + 900, 1000) << "\n"; return false; }
    schedule.autoSyncMinutes = 0;
    if (!QtCloudAutoSyncDue(schedule, 1060, 1000)) { std::cerr << "auto schedule clamp\n"; return false; }
    schedule.autoSyncMinutes = 120;
    if (QtCloudAutoSyncDue(schedule, 1000 + 7199, 1000)) { std::cerr << "auto schedule 120m\n"; return false; }
    schedule.enabled = false;
    if (QtCloudAutoSyncDue(schedule, 1000 + 7200, 1000)) { std::cerr << "auto schedule disabled\n"; return false; }

    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + "/cloud").toUtf8().toStdString());
    std::filesystem::create_directories(workspace / "meta");
    std::filesystem::create_directories(cloud / "meta");
    auto write = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(QString::fromUtf8(path.u8string())).absolutePath());
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    auto read = [](const std::filesystem::path& path) {
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    if (!write(workspace / "meta/tasks.json", "[{\"id\":\"local\"}]") ||
        !write(cloud / "meta/tasks.json", "[{\"id\":\"remote\"}]")) return false;
    CloudSyncConfig config; config.enabled = true; config.autoSyncEnabled = true;
    config.autoSyncMinutes = 1; config.root = cloud; config.autoPull = true; config.autoPush = false;
    const auto pulled = RunQtCloudAutoSync(config, workspace, CloudRole::Viewer);
    bool backupFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::u8path(temp.path().toUtf8().toStdString())))
        if (entry.is_directory() && entry.path().filename().string().rfind("qt-cloud-backup-", 0) == 0) backupFound = true;
    if (!pulled.ok || !pulled.attempted || !pulled.pullAttempted || pulled.pushAttempted ||
        !pulled.pullChanged || !pulled.changed || !backupFound ||
        read(workspace / "meta/tasks.json") != "[{\"id\":\"remote\"}]") { std::cerr << "auto pull: " << pulled.message << " ok=" << pulled.ok << " attempted=" << pulled.attempted << " pull=" << pulled.pullAttempted << " changed=" << pulled.pullChanged << " backup=" << backupFound << " data=" << read(workspace / "meta/tasks.json").toStdString() << '\n'; return false; }
    auto disabled = config; disabled.autoSyncEnabled = false;
    const auto noOp = RunQtCloudAutoSync(disabled, workspace, CloudRole::Viewer);
    if (noOp.ok || noOp.attempted) { std::cerr << "auto disabled not no-op\n"; return false; }
    auto viewerPush = config; viewerPush.autoPull = false; viewerPush.autoPush = true;
    if (RunQtCloudAutoSync(viewerPush, workspace, CloudRole::Viewer).attempted) { std::cerr << "auto viewer push\n"; return false; }

    if (!write(workspace / "meta/tasks.json", "[{\"id\":\"admin-local\"}]")) return false;
    auto adminPush = config; adminPush.autoPull = false; adminPush.autoPush = true;
    const auto pushed = RunQtCloudAutoSync(adminPush, workspace, CloudRole::Admin);
    const bool success = pushed.ok && pushed.attempted && pushed.pushAttempted && !pushed.pullAttempted && pushed.changed &&
        read(cloud / "meta/tasks.json") == "[{\"id\":\"admin-local\"}]";
    if (!success) std::cerr << "auto push: " << pushed.message << " ok=" << pushed.ok << " push=" << pushed.pushAttempted << " changed=" << pushed.changed << " remote=" << read(cloud / "meta/tasks.json").toStdString() << '\n';
    if (!success || !write(workspace / "meta/tasks.json", "[{\"id\":\"quick-local\"}]")) return false;
    auto quickPull = config;
    quickPull.autoSyncEnabled = false;
    quickPull.autoPull = true;
    quickPull.autoPush = false;
    if (!write(cloud / "meta/tasks.json", "[{\"id\":\"quick-remote\"}]")) return false;
    const auto quickPulled = RunQtCloudQuickSync(quickPull, workspace, CloudRole::Viewer);
    if (!quickPulled.ok || !quickPulled.attempted || !quickPulled.pullAttempted ||
        quickPulled.pushAttempted || !quickPulled.pullChanged ||
        read(workspace / "meta/tasks.json") != "[{\"id\":\"quick-remote\"}]" ||
        RunQtCloudAutoSync(quickPull, workspace, CloudRole::Viewer).attempted) {
        std::cerr << "quick pull should honor direction but not require periodic auto-sync: " << quickPulled.message << '\n';
        return false;
    }
    if (!write(workspace / "meta/tasks.json", "[{\"id\":\"quick-admin\"}]")) return false;
    auto quickPush = quickPull;
    quickPush.autoPull = false;
    quickPush.autoPush = true;
    const auto quickPushed = RunQtCloudQuickSync(quickPush, workspace, CloudRole::Admin);
    if (!quickPushed.ok || !quickPushed.attempted || !quickPushed.pushAttempted ||
        quickPushed.pullAttempted || !quickPushed.changed ||
        read(cloud / "meta/tasks.json") != "[{\"id\":\"quick-admin\"}]" ||
        RunQtCloudAutoSync(quickPush, workspace, CloudRole::Admin).attempted) {
        std::cerr << "quick admin push should honor direction but not require periodic auto-sync: " << quickPushed.message << '\n';
        return false;
    }
    return true;
}

static bool SubmitAdminLoginForTest(const QString& password, bool remember);

static bool TestCloudReleaseUpdate() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + "/cloud").toUtf8().toStdString());
    std::filesystem::create_directories(workspace / "meta");
    std::filesystem::create_directories(cloud / "releases");
    auto write = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(QString::fromUtf8(path.u8string())).absolutePath());
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    auto read = [](const std::filesystem::path& path) {
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QByteArray installerBytes("release fixture\0payload", 24);
    CloudSyncConfig config; config.enabled = true; config.root = cloud;
    auto newerVersion = std::string(APP_VERSION);
    const auto patchSeparator = newerVersion.rfind('.');
    if (patchSeparator == std::string::npos) return false;
    try {
        newerVersion.replace(patchSeparator + 1, std::string::npos,
            std::to_string(std::stoul(newerVersion.substr(patchSeparator + 1)) + 1));
    } catch (const std::exception&) { return false; }
    CloudManifest manifest; manifest.appVersion = newerVersion;
    manifest.releaseFile = "ForgeMirrorSetup_" + newerVersion + ".exe";
    if (!write(cloud / "releases" / manifest.releaseFile, installerBytes)) return false;
    const auto target = QtCloudReleaseTargetPath(workspace, manifest);
    if (!target || *target != workspace / "meta/updates" / manifest.releaseFile) return false;
    const auto downloaded = DownloadQtCloudRelease(config, workspace, manifest);
    const auto expectedHash = QCryptographicHash::hash(installerBytes, QCryptographicHash::Sha256).toHex().toStdString();
    if (!downloaded.ok || !downloaded.changed || downloaded.path != *target || downloaded.sha256 != expectedHash ||
        read(*target) != installerBytes) return false;
    if (!write(*target, "previous file")) return false;
    const auto replaced = DownloadQtCloudRelease(config, workspace, manifest);
    if (!replaced.ok || read(*target) != installerBytes) return false;
    for (const auto& invalid : {"../escape.exe", "folder/update.exe", "NUL.exe", "bad.zip"}) {
        auto invalidManifest = manifest; invalidManifest.releaseFile = invalid;
        if (QtCloudReleaseTargetPath(workspace, invalidManifest) ||
            DownloadQtCloudRelease(config, workspace, invalidManifest).ok) return false;
    }
    auto overlapping = config; overlapping.root = workspace;
    if (DownloadQtCloudRelease(overlapping, workspace, manifest).ok ||
        !write(workspace / "meta/cloud.ini", "leave me unchanged")) return false;

    if (!SaveCloudSyncConfig(workspace, config) || !SaveCloudManifest(config, workspace, manifest)) return false;
    QtWorkspace uiWorkspace(workspace);
    QtWindow window(uiWorkspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* updateButton = window.findChild<QToolButton*>("cloudReleaseActions");
    auto* downloadAction = window.findChild<QAction*>("cloudReleaseDownload");
    auto* launchAction = window.findChild<QAction*>("cloudReleaseLaunch");
    if (!navigation || !updateButton || !downloadAction || !launchAction) return false;
    navigation->setCurrentRow(13); QApplication::processEvents();
    if (!updateButton->isVisible() || !downloadAction->isEnabled() || !launchAction->isEnabled()) return false;
    if (!write(*target, "previous file")) return false;
    downloadAction->trigger();
    if (read(*target) != installerBytes || !window.statusBar()->currentMessage().contains(QString::fromUtf8("SHA-256"))) return false;
    bool canceled = false;
    QTimer::singleShot(0, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            canceled = box->defaultButton() == box->button(QMessageBox::Cancel);
            box->button(QMessageBox::Cancel)->click();
        }
    });
    launchAction->trigger();
    QAction* adminAction = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest(QStringLiteral("admin123"), false); });
    adminAction->trigger();
    auto* auditSource = window.findChild<QComboBox*>("auditSourceFilter");
    auto* auditTable = window.findChild<QTableWidget*>("records");
    if (!navigation || !auditSource || !auditTable) return false;
    navigation->setCurrentRow(7);
    auditSource->setCurrentIndex(5);
    bool releaseAuditVisible = false;
    for (int row = 0; row < auditTable->rowCount(); ++row)
        releaseAuditVisible |= auditTable->item(row, 3)->text() == QString::fromUtf8("Установка обновления") &&
            auditTable->item(row, 6)->text() == QStringLiteral("Cloud release installer downloaded and local copy hashed");
    if (!releaseAuditVisible) return false;
    window.close();
    return canceled;
}

static bool TestQtAdminAuthParity() {
    const char* overrideValue = std::getenv("FORGEMIRROR_ADMIN_PASSWORD");
    if (overrideValue && *overrideValue) return true; // Host override intentionally disables persistence and rotation.
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    QtWorkspace workspace(directory);
    if (!SetAdminPassword(directory, "old-admin-password") || !SetAdminStayLoggedIn(directory, false)) return false;
    for (int iteration = 0; iteration < 20; ++iteration) {
        if (!SetAdminStayLoggedIn(directory, (iteration % 2) == 0) ||
            LoadAdminPassword(directory) != "old-admin-password" ||
            LoadAdminStayLoggedIn(directory) != ((iteration % 2) == 0)) return false;
    }
    if (!SetAdminStayLoggedIn(directory, false)) return false;
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* login = window.findChild<QAction*>("adminLoginAction");
    auto* passwordAction = window.findChild<QAction*>("changeAdminPasswordAction");
    if (!login || login->text() != QString::fromUtf8("Войти как администратор") ||
        !login->toolTip().contains(QString::fromUtf8("восстановление")) ||
        !passwordAction || passwordAction->isVisible()) return false;
    bool rejectedAttempt = false;
    QStringList missingAdminLoginNames;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* password = dialog ? dialog->findChild<QLineEdit*>("adminLoginPassword") : nullptr;
        auto* notice = dialog ? dialog->findChild<QLabel*>("adminLoginNotice") : nullptr;
        auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
        if (!dialog || !password || !notice || !buttons) { if (dialog) dialog->reject(); return; }
        missingAdminLoginNames.append(MissingAccessibleNames(dialog));
        password->setText(QStringLiteral("private-wrong-password-fixture"));
        buttons->button(QDialogButtonBox::Ok)->click();
        rejectedAttempt = notice->text().contains(QString::fromUtf8("Неверный пароль")) && password->text().isEmpty();
        dialog->reject();
    });
    login->trigger();
    if (!rejectedAttempt || LoadAdminStayLoggedIn(directory) || passwordAction->isVisible() || !missingAdminLoginNames.isEmpty()) return false;
    bool rememberControlSeen = false;
    bool rememberStateAnnounced = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "adminLoginDialog") {
            std::cerr << "admin remember login dialog missing; modal=" << (QApplication::activeModalWidget() ? QApplication::activeModalWidget()->metaObject()->className() : "none") << '\n';
            if (QApplication::activeModalWidget()) QApplication::activeModalWidget()->close();
            return;
        }
        auto* password = dialog->findChild<QLineEdit*>("adminLoginPassword");
        auto* remember = dialog->findChild<QCheckBox*>("adminRememberSession");
        auto* workspaceHint = dialog->findChild<QLabel*>("adminLoginWorkspace");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!password || !remember || !workspaceHint || !buttons) {
            std::cerr << "admin login workspace hint missing\n";
            if (dialog) dialog->reject();
            return;
        }
        if (!workspaceHint->text().contains(QDir::toNativeSeparators(temp.path()))) {
            std::cerr << "admin login workspace hint has unexpected path: " << workspaceHint->text().toStdString()
                      << " expected " << QDir::toNativeSeparators(temp.path()).toStdString() << '\n';
            dialog->reject();
            return;
        }
        rememberControlSeen = true;
        password->setText(QString::fromUtf8("old-admin-password"));
        remember->setChecked(true);
        if (auto* accessible = QAccessible::queryAccessibleInterface(remember))
            rememberStateAnnounced = accessible->state().checkable && accessible->state().checked;
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    login->trigger();
    if (!rememberControlSeen || !rememberStateAnnounced || !LoadAdminStayLoggedIn(directory) || !passwordAction->isVisible() ||
        login->text() != QString::fromUtf8("Выйти из режима администратора") ||
        !login->toolTip().contains(QString::fromUtf8("отключить"))) return false;
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* auditSource = window.findChild<QComboBox*>("auditSourceFilter");
    auto* auditTable = window.findChild<QTableWidget*>("records");
    if (!navigation || !auditSource || !auditTable) return false;
    navigation->setCurrentRow(7);
    auditSource->setCurrentIndex(5);
    bool rejectedEvent = false, successEvent = false;
    for (int row = 0; row < auditTable->rowCount(); ++row) {
        rejectedEvent |= auditTable->item(row, 3)->text() == QString::fromUtf8("Аутентификация администратора") &&
            auditTable->item(row, 6)->text() == QStringLiteral("Administrator login rejected");
        successEvent |= auditTable->item(row, 3)->text() == QString::fromUtf8("Аутентификация администратора") &&
            auditTable->item(row, 6)->text() == QStringLiteral("Administrator login succeeded; persistent session enabled");
    }
    QFile authLogFile(QString::fromStdWString((directory / "meta/qt-application-log.json").wstring()));
    if (!rejectedEvent || !successEvent || !authLogFile.open(QIODevice::ReadOnly)) return false;
    const auto authLogBytes = authLogFile.readAll();
    authLogFile.close();
    if (authLogBytes.contains("private-wrong-password-fixture")) return false;
    window.close();
    auto restartDisplay = LoadQtDisplaySettings(directory);
    restartDisplay.lastPage = 8; // Pomodoro is the user's persisted startup page.
    if (!SaveQtDisplaySettings(directory, restartDisplay)) return false;
    QtWindow restarted(workspace); restarted.show(); QApplication::processEvents();
    auto* restartedNavigation = restarted.findChild<QListWidget*>("navigation");
    auto* pomodoroSoundSettings = restarted.findChild<QCheckBox*>("pomodoroSoundEnabled");
    if (!restartedNavigation || restartedNavigation->currentRow() != 8 || !pomodoroSoundSettings ||
        !pomodoroSoundSettings->isVisible()) return false;
    QEventLoop startupStability;
    QTimer::singleShot(3200, &startupStability, &QEventLoop::quit);
    startupStability.exec();
    if (!restarted.isVisible() || restartedNavigation->currentRow() != 8) return false;
    passwordAction = restarted.findChild<QAction*>("changeAdminPasswordAction");
    login = restarted.findChild<QAction*>("adminLoginAction");
    if (!passwordAction || !passwordAction->isVisible() || !login ||
        login->text() != QString::fromUtf8("Выйти из режима администратора")) return false;
    restarted.close();
    for (int launch = 0; launch < 3; ++launch) {
        QtWindow repeatedLaunch(workspace); repeatedLaunch.show(); QApplication::processEvents();
        const auto* repeatedPasswordAction = repeatedLaunch.findChild<QAction*>("changeAdminPasswordAction");
        if (!repeatedPasswordAction || !repeatedPasswordAction->isVisible() ||
            LoadAdminPassword(directory) != "old-admin-password" || !LoadAdminStayLoggedIn(directory)) return false;
        repeatedLaunch.close();
    }
    if (!authLogFile.open(QIODevice::ReadOnly) || !authLogFile.readAll().contains("Administrator session restored")) return false;
    restarted.show(); QApplication::processEvents();
    passwordAction = restarted.findChild<QAction*>("changeAdminPasswordAction");
    login = restarted.findChild<QAction*>("adminLoginAction");
    if (!passwordAction || !passwordAction->isVisible() || !login ||
        login->text() != QString::fromUtf8("Выйти из режима администратора")) return false;
    login->trigger(); // Logged-in admin action logs out and clears the opt-in flag.
    if (LoadAdminStayLoggedIn(directory) || passwordAction->isVisible() ||
        login->text() != QString::fromUtf8("Войти как администратор")) return false;

    bool loginWithoutRemember = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "adminLoginDialog") {
            std::cerr << "admin nonremember login dialog missing\n";
            if (QApplication::activeModalWidget()) QApplication::activeModalWidget()->close();
            return;
        }
        auto* password = dialog->findChild<QLineEdit*>("adminLoginPassword");
        auto* remember = dialog->findChild<QCheckBox*>("adminRememberSession");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!password || !remember || !buttons) return;
        password->setText(QString::fromUtf8("old-admin-password"));
        remember->setChecked(false);
        buttons->button(QDialogButtonBox::Ok)->click();
        loginWithoutRemember = true;
    });
    login->trigger();
    if (!loginWithoutRemember || LoadAdminStayLoggedIn(directory) || !passwordAction->isVisible() || !passwordAction->isEnabled() ||
        login->text() != QString::fromUtf8("Выйти из режима администратора")) return false;

    bool wrongPasswordRejected = false, mismatchRejected = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "adminPasswordDialog") {
            std::cerr << "admin password dialog missing; modal=" << (QApplication::activeModalWidget() ? QApplication::activeModalWidget()->metaObject()->className() : "none") << '\n';
            if (QApplication::activeModalWidget()) QApplication::activeModalWidget()->close();
            return;
        }
        auto* current = dialog->findChild<QLineEdit*>("adminCurrentPassword");
        auto* next = dialog->findChild<QLineEdit*>("adminNewPassword");
        auto* confirm = dialog->findChild<QLineEdit*>("adminConfirmPassword");
        auto* notice = dialog->findChild<QLabel*>("adminPasswordNotice");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!current || !next || !confirm || !notice || !buttons) return;
        auto* save = buttons->button(QDialogButtonBox::Save);
        current->setText(QString::fromUtf8("wrong")); next->setText(QString::fromUtf8("new-admin-password"));
        confirm->setText(QString::fromUtf8("new-admin-password")); save->click();
        wrongPasswordRejected = notice->text().contains(QString::fromUtf8("Неверный текущий пароль"));
        current->setText(QString::fromUtf8("old-admin-password")); next->setText(QString::fromUtf8("new-admin-password"));
        confirm->setText(QString::fromUtf8("mismatch")); save->click();
        mismatchRejected = notice->text().contains(QString::fromUtf8("не совпадают"));
        current->setText(QString::fromUtf8("old-admin-password")); next->setText(QString::fromUtf8("new-admin-password"));
        confirm->setText(QString::fromUtf8("new-admin-password")); save->click();
    });
    passwordAction->trigger();
    const bool success = wrongPasswordRejected && mismatchRejected &&
        LoadAdminPassword(directory) == "new-admin-password" && restarted.findChild<QAction*>("adminLoginAction");
    restarted.close();
    return success;
}

static bool SubmitAdminLoginForTest(const QString& password, bool remember);

static bool TestQtAdminAuthAcrossProcesses() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData()) / "workspace";
    if (!SetAdminPassword(directory, "process-auth-fixture-password") || !SetAdminStayLoggedIn(directory, false)) return false;

    // Exercise the same path as the reported failure: accept a real Qt login with
    // the remember-session option checked, then cross actual process boundaries.
    QtWorkspace workspace(directory);
    QtWindow loginWindow(workspace);
    loginWindow.show();
    QApplication::processEvents();
    auto* loginAction = loginWindow.findChild<QAction*>("adminLoginAction");
    bool loginAccepted = false;
    if (!loginAction) return false;
    QTimer::singleShot(0, [&] {
        loginAccepted = SubmitAdminLoginForTest(QStringLiteral("process-auth-fixture-password"), true);
        if (!loginAccepted && QApplication::activeModalWidget()) QApplication::activeModalWidget()->close();
    });
    loginAction->trigger();
    loginWindow.close();
    if (!loginAccepted || LoadAdminPassword(directory) != "process-auth-fixture-password" ||
        !LoadAdminStayLoggedIn(directory)) {
        std::cerr << "Admin UI login did not persist the checked remember-session option\n";
        return false;
    }

    const QString executable = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ForgeMirrorQt.exe"));
    if (!QFileInfo(executable).isFile()) {
        std::cerr << "Process authentication test cannot find sibling ForgeMirrorQt.exe: " << executable.toStdString() << '\n';
        return false;
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    const auto isolatedLocalData = temp.path() + QStringLiteral("/local-app-data");
    const auto isolatedRoamingData = temp.path() + QStringLiteral("/roaming-app-data");
    QDir().mkpath(isolatedLocalData); QDir().mkpath(isolatedRoamingData);
    environment.insert(QStringLiteral("LOCALAPPDATA"), isolatedLocalData);
    environment.insert(QStringLiteral("APPDATA"), isolatedRoamingData);
    environment.remove(QStringLiteral("FORGEMIRROR_ADMIN_PASSWORD"));
    environment.remove(QStringLiteral("FORGEMIRROR_STORAGE_DIR"));
    environment.remove(QStringLiteral("QT_PLUGIN_PATH"));
    environment.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));

    auto verifyInformationalOption = [&](const QString& option, const QByteArray& expected) {
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(executable, {option});
        if (!process.waitForStarted(5000) || !process.waitForFinished(5000)) {
            process.kill(); process.waitForFinished(2000);
            std::cerr << "Qt CLI option did not exit: " << option.toStdString() << '\n';
            return false;
        }
        const auto output = process.readAllStandardOutput() + process.readAllStandardError();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !output.contains(expected)) {
            std::cerr << "Qt CLI option returned unexpected result: " << option.toStdString()
                      << " exit=" << process.exitCode() << " output=" << output.toStdString() << '\n';
            return false;
        }
        return true;
    };
    if (!verifyInformationalOption(QStringLiteral("--version"), QByteArray(APP_VERSION)) ||
        !verifyInformationalOption(QStringLiteral("--help"), QByteArray("--storage-dir")) ||
        QDir(isolatedLocalData + QStringLiteral("/Pharos/ForgeMirrorQt/workspace")).exists()) return false;

    for (int launch = 1; launch <= 3; ++launch) {
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(executable, {QStringLiteral("--storage-dir"), QString::fromStdWString(directory.wstring()),
            QStringLiteral("--smoke-test")});
        if (!process.waitForStarted(5000) || !process.waitForFinished(7000)) {
            process.kill(); process.waitForFinished(2000);
            std::cerr << "Admin persistence process launch timed out at " << launch << '\n';
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            std::cerr << "Admin persistence process failed at " << launch << ": "
                      << process.readAllStandardError().toStdString() << '\n';
            return false;
        }

        QFile log(QString::fromStdWString((directory / "meta/qt-application-log.json").wstring()));
        if (!log.open(QIODevice::ReadOnly)) return false;
        const auto parsed = QJsonDocument::fromJson(log.readAll());
        if (!parsed.isArray()) return false;
        int restored = 0;
        int rejected = 0;
        for (const auto& value : parsed.array()) {
            const auto message = value.toObject().value("message").toString();
            restored += message == QStringLiteral("Administrator session restored");
            rejected += message == QStringLiteral("Administrator login rejected");
        }
        if (restored != launch || rejected != 0 || LoadAdminPassword(directory) != "process-auth-fixture-password" ||
            !LoadAdminStayLoggedIn(directory)) {
            std::cerr << "Admin session did not survive process restart " << launch
                      << " (restored=" << restored << ", rejected=" << rejected << ")\n";
            return false;
        }
    }
    return true;
}

static bool TestCatalogMutationCoreAudit() {
    const char* overrideValue = std::getenv("FORGEMIRROR_ADMIN_PASSWORD");
    if (overrideValue && *overrideValue) return true;
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    QtWorkspace workspace(directory);
    if (!SetAdminPassword(directory, "catalog-audit-password")) return false;
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* login = window.findChild<QAction*>("adminLoginAction");
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* primary = window.findChild<QPushButton*>("primary");
    if (!login || !navigation || !primary) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest(QString::fromUtf8("catalog-audit-password"), false); });
    login->trigger();
    navigation->setCurrentRow(2);
    QApplication::processEvents();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "projectEditor") {
            if (dialog) dialog->reject();
            return;
        }
        auto* title = dialog->findChild<QLineEdit*>("entryTitle");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!title || !buttons) { dialog->reject(); return; }
        title->setText(QString::fromUtf8("Private project title"));
        buttons->button(QDialogButtonBox::Save)->click();
    });
    primary->click();
    QFile log(QString::fromStdWString((directory / "meta/qt-application-log.json").wstring()));
    if (!log.open(QIODevice::ReadOnly)) return false;
    const auto bytes = log.readAll();
    const auto contents = QString::fromUtf8(bytes.constData(), bytes.size());
    if (!contents.contains(QString::fromUtf8("CoreCatalogMutation")) ||
        !contents.contains(QString::fromUtf8("Project creation committed")) ||
        contents.contains(QString::fromUtf8("Private project title"))) return false;
    navigation->setCurrentRow(7);
    QApplication::processEvents();
    auto* source = window.findChild<QComboBox*>("auditSourceFilter");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!source || !table) return false;
    source->setCurrentIndex(5);
    QApplication::processEvents();
    bool visible = false;
    for (int row = 0; row < table->rowCount(); ++row)
        visible |= table->item(row, 3)->text() == QString::fromUtf8("Изменение справочников") &&
            table->item(row, 6)->text().contains(QString::fromUtf8("Project creation committed"));
    window.close();
    return visible;
}

static bool SubmitAdminLoginForTest(const QString& password, bool remember = false) {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (!dialog || dialog->objectName() != "adminLoginDialog") return false;
    auto* input = dialog->findChild<QLineEdit*>("adminLoginPassword");
    auto* stay = dialog->findChild<QCheckBox*>("adminRememberSession");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    if (!input || !stay || !buttons) return false;
    input->setText(password); stay->setChecked(remember);
    buttons->button(QDialogButtonBox::Ok)->click();
    return dialog->result() == QDialog::Accepted;
}
static bool TestCloudPushPreview() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + "/cloud").toUtf8().toStdString());
    std::filesystem::create_directories(workspace / "meta");
    std::filesystem::create_directories(cloud / "meta");
    auto write = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(QString::fromUtf8(path.u8string())).absolutePath());
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    auto inventory = [](const std::filesystem::path& root) {
        std::map<std::string, QByteArray> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            QFile file(QString::fromUtf8(entry.path().u8string()));
            if (!file.open(QIODevice::ReadOnly)) return std::map<std::string, QByteArray>{};
            files.emplace(entry.path().lexically_relative(root).generic_string(), file.readAll());
        }
        return files;
    };
    if (!SaveCloudSyncConfig(workspace, CloudSyncConfig{})) return false;
    CloudSyncConfig config; config.enabled = true; config.root = cloud;
    config.manifest = cloud / "meta/manifest.ini"; config.updateManifestOnPush = true;
    if (!SaveCloudSyncConfig(workspace, config) || !write(workspace / "meta/tasks.json", "[{\"id\":\"local\"}]") ||
        !write(workspace / "skills.txt", "local skill\n") ||
        !write(cloud / "meta/tasks.json", "[{\"id\":\"remote\"}]") ||
        !write(cloud / "meta/manifest.ini", "appVersion=remote\nreleaseFile=keep.exe\nnotes=preserve\n") ||
        !write(cloud / "orphan.tmp", "must remain only in real cloud\n")) return false;
    QtWorkspace qtWorkspace(workspace);
    const auto profile = qtWorkspace.storage->create_profile(Profile("Preview profile"));
    if (!profile) return false;
    const auto before = inventory(cloud);
    const auto preview = PreviewQtCloudWorkspacePush(config, workspace, CloudRole::Admin);
    if (!preview.sync.ok || !preview.sync.changed || !preview.filesAdded || !preview.filesReplaced || !preview.filesRemoved ||
        preview.message.find("не изменялись") == std::string::npos || inventory(cloud) != before) return false;
    auto outside = config;
    const auto externalManifest = std::filesystem::u8path((temp.path() + "/outside-manifest.ini").toUtf8().toStdString());
    if (!write(externalManifest, "leave this untouched\n")) return false;
    outside.manifest = externalManifest;
    const auto outsideResult = PreviewQtCloudWorkspacePush(outside, workspace, CloudRole::Admin);
    QFile outsideFile(QString::fromUtf8(externalManifest.u8string()));
    if (outsideResult.sync.ok || inventory(cloud) != before || !outsideFile.open(QIODevice::ReadOnly) ||
        outsideFile.readAll() != "leave this untouched\n") return false;
    if (PreviewQtCloudWorkspacePush(config, workspace, CloudRole::Viewer).sync.ok || inventory(cloud) != before) return false;
    auto overlap = config; overlap.root = workspace;
    if (PreviewQtCloudWorkspacePush(overlap, workspace, CloudRole::Admin).sync.ok || inventory(cloud) != before) return false;
    auto read = [](const std::filesystem::path& path) {
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    if (!write(cloud / "meta/tasks.json", "[{\"id\":\"external-change\"}]")) return false;
    const auto staleApproval = RunQtCloudWorkspacePush(config, workspace, CloudRole::Admin, &preview);
    if (staleApproval.sync.ok || read(cloud / "meta/tasks.json") != "[{\"id\":\"external-change\"}]" ||
        !write(cloud / "meta/tasks.json", before.at("meta/tasks.json"))) return false;
    const auto pushed = RunQtCloudWorkspacePush(config, workspace, CloudRole::Admin);
    if (!pushed.sync.ok || !pushed.sync.changed || pushed.backupPath.empty() ||
        !std::filesystem::is_directory(pushed.backupPath) ||
        read(cloud / "meta/tasks.json") != "[{\"id\":\"local\"}]" ||
        std::filesystem::exists(cloud / "orphan.tmp") ||
        !read(cloud / "meta/manifest.ini").contains("notes=preserve") ||
        !read(pushed.backupPath / "meta/tasks.json").contains("remote")) { std::cerr << "push commit: " << pushed.message << " ok=" << pushed.sync.ok << " changed=" << pushed.sync.changed << " added=" << pushed.filesAdded << " replaced=" << pushed.filesReplaced << " removed=" << pushed.filesRemoved << " tasks=" << read(cloud / "meta/tasks.json").toStdString() << " orphan=" << std::filesystem::exists(cloud / "orphan.tmp") << " manifest=" << read(cloud / "meta/manifest.ini").toStdString() << " backupTasks=" << read(pushed.backupPath / "meta/tasks.json").toStdString() << "\n"; return false; }
    if (!write(workspace / "meta/tasks.json", "[{\"id\":\"next-local\"}]") ||
        !write(workspace / "spirits/temporary.png", "created directory rollback\n") ||
        !write(cloud / "zz-orphan.tmp", "restore me\n")) return false;
    const auto beforeInterruptedPush = inventory(cloud);
    const auto interruptedPlan = PreviewQtCloudWorkspacePush(config, workspace, CloudRole::Admin);
    if (!interruptedPlan.sync.ok || std::none_of(interruptedPlan.changes.begin(), interruptedPlan.changes.end(),
        [](const auto& change) { return change.relativePath == "spirits/temporary.png"; })) return false;
    const auto taskPreview = std::find_if(interruptedPlan.changes.begin(), interruptedPlan.changes.end(),
        [](const auto& change) { return change.relativePath == "meta/tasks.json"; });
    if (taskPreview == interruptedPlan.changes.end() || taskPreview->beforeBytes != read(cloud / "meta/tasks.json")) {
        std::cerr << "Interrupted preview tasks mismatch before wait: " << (taskPreview == interruptedPlan.changes.end() ? "missing" : taskPreview->beforeBytes.toHex().toStdString())
            << " actual=" << read(cloud / "meta/tasks.json").toHex().toStdString() << '\n'; return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // Cross a dataUpdatedAt second boundary without dispatching UI timers.
    if (taskPreview->beforeBytes != read(cloud / "meta/tasks.json")) {
        std::cerr << "Cloud task bytes changed during blocking wait: " << taskPreview->beforeBytes.toHex().toStdString()
            << " actual=" << read(cloud / "meta/tasks.json").toHex().toStdString() << '\n'; return false;
    }
    QtSetCloudPushFailureAfterFileWritesForTests(static_cast<int>(interruptedPlan.changes.size()));
    QtSetCloudPushLeaveJournalForTests(true);
    const auto interrupted = RunQtCloudWorkspacePush(config, workspace, CloudRole::Admin, &interruptedPlan);
    QtSetCloudPushLeaveJournalForTests(false);
    QtSetCloudPushFailureAfterFileWritesForTests(-1);
    if (interrupted.sync.ok || !std::filesystem::exists(workspace / "meta/qt-cloud-push.json") ||
        inventory(cloud) == beforeInterruptedPush) { std::cerr << "push interrupted: " << interrupted.message << " ok=" << interrupted.sync.ok << " journal=" << std::filesystem::exists(workspace / "meta/qt-cloud-push.json") << "\n"; return false; }
    const auto pushJournal = workspace / "meta/qt-cloud-push.json";
    const auto validJournal = read(pushJournal);
    auto tampered = QJsonDocument::fromJson(validJournal).object();
    auto entries = tampered["files"].toArray();
    auto firstEntry = entries.at(0).toObject(); firstEntry["path"] = QString::fromLatin1("../outside.ini"); entries[0] = firstEntry;
    tampered["files"] = entries;
    if (!write(pushJournal, QJsonDocument(tampered).toJson())) return false;
    bool rejected = false;
    try { RecoverQtCloudPush(workspace); } catch (const std::exception&) { rejected = true; }
    if (!rejected || inventory(cloud) == beforeInterruptedPush || !write(pushJournal, validJournal)) return false;
    {
        QtWorkspace recovered(workspace);
        if (!recovered.cloudPushRecoveryNotice) return false;
        QtWindow recoveryWindow(recovered);
        QFile log(QString::fromUtf8((workspace / "meta/qt-application-log.json").u8string()));
        if (recovered.cloudPushRecoveryNotice || !log.open(QIODevice::ReadOnly)) return false;
        const auto bytes = log.readAll();
        if (!bytes.contains("interrupted manual cloud push was rolled back") || bytes.contains(cloud.u8string().c_str())) return false;
    }
    if (inventory(cloud) != beforeInterruptedPush || std::filesystem::exists(pushJournal) ||
        std::filesystem::exists(cloud / "spirits")) return false;

    // If an external writer changes any member of an interrupted multi-file push,
    // recovery validates the entire journal before restoring even the files already
    // written by the push. Preserve the external bytes, then recover once the conflict
    // is removed.
    if (interruptedPlan.changes.size() < 2) return false;
    QtSetCloudPushFailureAfterFileWritesForTests(1);
    QtSetCloudPushLeaveJournalForTests(true);
    const auto partialPush = RunQtCloudWorkspacePush(config, workspace, CloudRole::Admin, &interruptedPlan);
    QtSetCloudPushLeaveJournalForTests(false);
    QtSetCloudPushFailureAfterFileWritesForTests(-1);
    if (partialPush.sync.ok || !std::filesystem::exists(pushJournal)) return false;
    const auto& externallyEditedChange = interruptedPlan.changes[1];
    const auto externallyEditedPath = cloud / std::filesystem::u8path(externallyEditedChange.relativePath);
    const bool existedBeforeExternalEdit = std::filesystem::exists(externallyEditedPath);
    const auto bytesBeforeExternalEdit = existedBeforeExternalEdit ? read(externallyEditedPath) : QByteArray();
    const QByteArray externalBytes = "written by another process during recovery";
    if (!write(externallyEditedPath, externalBytes)) return false;
    const auto inventoryWithExternalEdit = inventory(cloud);
    bool externalPushEditRejected = false;
    try { RecoverQtCloudPush(workspace); } catch (const std::exception&) { externalPushEditRejected = true; }
    if (!externalPushEditRejected || inventory(cloud) != inventoryWithExternalEdit ||
        !std::filesystem::exists(pushJournal)) return false;
    if (existedBeforeExternalEdit) {
        if (!write(externallyEditedPath, bytesBeforeExternalEdit)) return false;
    } else if (!std::filesystem::remove(externallyEditedPath)) return false;
    if (!RecoverQtCloudPush(workspace) || inventory(cloud) != beforeInterruptedPush ||
        std::filesystem::exists(pushJournal) || std::filesystem::exists(cloud / "spirits")) return false;

    QtWindow window(qtWorkspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* pushButton = window.findChild<QPushButton*>("cloudPushPreview");
    if (!navigation || !pushButton) return false;
    navigation->setCurrentRow(13);
    if (!pushButton->isVisible() || pushButton->isEnabled()) return false;
    QAction* adminAction = nullptr;
    for (auto* menu : window.findChildren<QMenu*>())
        for (auto* action : menu->actions())
            if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("admin123"); });
    adminAction->trigger();
    if (!pushButton->isEnabled()) return false;
    const auto beforeCancelledUiPush = inventory(cloud);
    bool cancelWasDefault = false;
    QTimer::singleShot(0, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            cancelWasDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
            box->button(QMessageBox::Cancel)->click();
        }
    });
    pushButton->click();
    if (!cancelWasDefault || inventory(cloud) != beforeCancelledUiPush) return false;
    bool confirmed = false;
    QTimer::singleShot(0, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            confirmed = box->defaultButton() == box->button(QMessageBox::Cancel);
            box->button(QMessageBox::Yes)->click();
            QTimer::singleShot(0, [] {
                if (auto* info = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) info->accept();
            });
        }
    });
    pushButton->click();
    if (!confirmed || inventory(cloud) == beforeCancelledUiPush ||
        read(cloud / "meta/tasks.json") != read(workspace / "meta/tasks.json")) return false;
    QFile telemetry(QString::fromUtf8((workspace / "meta/qt-application-log.json").u8string()));
    if (!telemetry.open(QIODevice::ReadOnly)) return false;
    const auto telemetryBytes = telemetry.readAll();
    if (!telemetryBytes.contains("CoreCloudTransaction") || !telemetryBytes.contains("Manual cloud push committed") ||
        telemetryBytes.contains(cloud.u8string().c_str())) return false;
    window.close();
    return true;
}

static bool TestCloudConflictResolver() {
    auto fail = [](const char* step) { std::cerr << "cloudConflict: " << step << '\n'; return false; };
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + "/cloud").toUtf8().toStdString());
    std::filesystem::create_directories(workspace / "meta"); std::filesystem::create_directories(cloud / "meta");
    auto write = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(QString::fromUtf8(path.u8string())).absolutePath());
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    auto read = [](const std::filesystem::path& path) {
        QFile file(QString::fromUtf8(path.u8string())); if (!file.open(QIODevice::ReadOnly)) return QByteArray(); return file.readAll();
    };
    const QByteArray local = "[{\"id\":\"local\",\"title\":\"Local\"}]";
    const QByteArray remote = "[{\"id\":\"remote\",\"title\":\"Cloud\"}]";
    const QByteArray localProjects = "[{\"id\":\"local-project\",\"name\":\"Local project\"}]";
    const QByteArray remoteProjects = "[{\"id\":\"cloud-project\",\"name\":\"Cloud project\"}]";
    const QByteArray localBanner = "{\"items\":[\"Local phrase\"]}";
    const QByteArray remoteBanner = "{\"items\":[\"Cloud phrase\"]}";
    const QByteArray localGameplay = "[leveling]\nbase=1500\nlinear=250\n";
    const QByteArray remoteGameplay = "[leveling]\nbase=1800\nquadratic=75\n";
    const QByteArray localProfessions = "\xEF\xBB\xBF" "pr_local|Local profession|Local description\n";
    const QByteArray remoteProfessions = "\xEF\xBB\xBF" "pr_cloud|Cloud profession|Cloud description\n";
    const QByteArray localSkills = "\xEF\xBB\xBF" "sk_local|Local skill|1|prof=pr_local|local desc\n";
    const QByteArray remoteSkills = "\xEF\xBB\xBF" "sk_cloud|Cloud skill|1|prof=pr_cloud|cloud desc\n";
    if (!write(workspace / "meta/tasks.json", local) || !write(cloud / "meta/tasks.json", remote) ||
        !write(workspace / "meta/projects.json", localProjects) || !write(cloud / "meta/projects.json", remoteProjects) ||
        !write(workspace / "meta/banner.json", localBanner) || !write(cloud / "meta/banner.json", remoteBanner) ||
        !write(workspace / "meta/gameplay.ini", localGameplay) || !write(cloud / "meta/gameplay.ini", remoteGameplay) ||
        !write(workspace / "meta/professions.txt", localProfessions) || !write(cloud / "meta/professions.txt", remoteProfessions) ||
        !write(workspace / "skills.txt", localSkills) || !write(cloud / "skills.txt", remoteSkills) ||
        !write(workspace / "meta/pipeline.json", "{\"steps\":[]}") || !write(cloud / "meta/pipeline.json", "{\"steps\":[]}")) return false;
    CloudSyncConfig config; config.enabled = true; config.root = cloud;
    if (!SaveCloudSyncConfig(workspace, config)) return false;
    const auto applied = ApplyQtCloudWorkspaceFile(workspace, cloud / "meta/tasks.json", "meta/tasks.json", "cloud");
    const auto listedAfterApply = ListCloudWorkspaceBackups(workspace, "meta/tasks.json");
    if (!applied.ok || !applied.changed || applied.backupPath.empty() || read(workspace / "meta/tasks.json") != remote ||
        read(applied.backupPath) != local || listedAfterApply.size() < 2) {
        std::cerr << "cloudConflict detail ok=" << applied.ok << " changed=" << applied.changed
                  << " path=" << applied.backupPath.u8string() << " backups=" << listedAfterApply.size()
                  << " message=" << applied.message << '\n';
        return fail("apply cloud");
    }
    const auto backups = ListCloudWorkspaceBackups(workspace, "meta/tasks.json");
    const auto localBackup = std::find_if(backups.begin(), backups.end(), [](const auto& item) { return item.sourceKind == "local"; });
    if (localBackup == backups.end()) return fail("find local backup");
    const auto restored = ApplyQtCloudWorkspaceFile(workspace, localBackup->path, "meta/tasks.json", "restore");
    if (!restored.ok || !restored.changed || read(workspace / "meta/tasks.json") != local) return fail("restore backup");
    const auto pushed = PushQtCloudWorkspaceFile(workspace, "meta/tasks.json");
    if (!pushed.ok || !pushed.changed || pushed.backupPath.empty() || read(cloud / "meta/tasks.json") != local ||
        read(pushed.backupPath) != remote) return fail("push local");
    std::filesystem::remove(cloud / "meta/pipeline.json");
    const auto createdPush = PushQtCloudWorkspaceFile(workspace, "meta/pipeline.json");
    if (!createdPush.ok || !createdPush.changed || !std::filesystem::is_regular_file(cloud / "meta/pipeline.json"))
        return fail("push new cloud file");
    const auto pushedProjects = PushQtCloudWorkspaceFile(workspace, "meta/projects.json");
    if (!pushedProjects.ok || !pushedProjects.changed || pushedProjects.backupPath.empty() ||
        read(cloud / "meta/projects.json") != localProjects || read(pushedProjects.backupPath) != remoteProjects ||
        ListCloudWorkspaceBackups(workspace, "meta/projects.json").empty())
        return fail("push projects with cloud backup");
    const auto pushedBanner = PushQtCloudWorkspaceFile(workspace, "meta/banner.json");
    if (!pushedBanner.ok || !pushedBanner.changed || pushedBanner.backupPath.empty() ||
        read(cloud / "meta/banner.json") != localBanner || read(pushedBanner.backupPath) != remoteBanner ||
        ListCloudWorkspaceBackups(workspace, "meta/banner.json").empty())
        return fail("push banner with cloud backup");
    const auto pushedGameplay = PushQtCloudWorkspaceFile(workspace, "meta/gameplay.ini");
    if (!pushedGameplay.ok || !pushedGameplay.changed || pushedGameplay.backupPath.empty() ||
        read(cloud / "meta/gameplay.ini") != localGameplay || read(pushedGameplay.backupPath) != remoteGameplay ||
        ListCloudWorkspaceBackups(workspace, "meta/gameplay.ini").empty() ||
        pushedGameplay.backupPath.extension() != ".ini")
        return fail("push gameplay config with cloud backup");
    const auto pushedProfessions = PushQtCloudWorkspaceFile(workspace, "meta/professions.txt");
    if (!pushedProfessions.ok || !pushedProfessions.changed || pushedProfessions.backupPath.empty() ||
        read(cloud / "meta/professions.txt") != localProfessions || read(pushedProfessions.backupPath) != remoteProfessions ||
        ListCloudWorkspaceBackups(workspace, "meta/professions.txt").empty() ||
        pushedProfessions.backupPath.extension() != ".txt" || read(cloud / "skills.txt") != localSkills ||
        pushedProfessions.backupPaths.size() != 4 || ListCloudWorkspaceBackups(workspace, "skills.txt").empty()) {
        std::cerr << "profession push ok=" << pushedProfessions.ok << " changed=" << pushedProfessions.changed
                  << " backup=" << pushedProfessions.backupPath.u8string() << " listed="
                  << ListCloudWorkspaceBackups(workspace, "meta/professions.txt").size()
                  << " ext=" << pushedProfessions.backupPath.extension().u8string()
                  << " message=" << pushedProfessions.message << '\n';
        return fail("push professions with cloud backup");
    }
    const auto skillsBackups = ListCloudWorkspaceBackups(workspace, "skills.txt");
    const auto professionBackups = ListCloudWorkspaceBackups(workspace, "meta/professions.txt");
    const auto localSkillsBackup = std::find_if(skillsBackups.begin(), skillsBackups.end(), [](const auto& item) { return item.sourceKind == "local"; });
    const auto localProfessionsBackup = std::find_if(professionBackups.begin(), professionBackups.end(), [](const auto& item) { return item.sourceKind == "local"; });
    if (localSkillsBackup == skillsBackups.end() || localProfessionsBackup == professionBackups.end() ||
        localSkillsBackup->createdAt != localProfessionsBackup->createdAt) return fail("catalog source backup pair");
    if (ApplyQtCloudWorkspaceFile(workspace, localSkillsBackup->path, "skills.txt", "restore").ok)
        return fail("single catalog restore blocked");
    if (!write(workspace / "skills.txt", remoteSkills) || !write(workspace / "meta/professions.txt", remoteProfessions)) return false;
    const auto restoredPair = RestoreQtCloudCatalogPair(workspace, localSkillsBackup->path, localProfessionsBackup->path);
    if (!restoredPair.ok || !restoredPair.changed || read(workspace / "skills.txt") != localSkills ||
        read(workspace / "meta/professions.txt") != localProfessions) return fail("restore catalog pair");
    if (!write(cloud / "skills.txt", remoteSkills) || !write(cloud / "meta/professions.txt", remoteProfessions)) return false;
    const auto appliedPair = ApplyQtCloudWorkspaceFile(workspace, cloud / "meta/professions.txt", "meta/professions.txt", "cloud");
    if (!appliedPair.ok || !appliedPair.changed || read(workspace / "skills.txt") != remoteSkills ||
        read(workspace / "meta/professions.txt") != remoteProfessions || appliedPair.backupPaths.size() < 4)
        return fail("apply cloud catalog pair");
    if (!write(workspace / "skills.txt", "sk_bad||1|not a name\n") ||
        PushQtCloudWorkspaceFile(workspace, "skills.txt").ok || read(cloud / "skills.txt") != remoteSkills ||
        read(cloud / "meta/professions.txt") != remoteProfessions) return fail("malformed catalog pair blocks both");
    if (!write(workspace / "skills.txt", localSkills) || !write(workspace / "meta/professions.txt", localProfessions)) return false;
    CloudSyncConfig overlapConfig = config; overlapConfig.root = workspace;
    if (!SaveCloudSyncConfig(workspace, overlapConfig) || PushQtCloudWorkspaceFile(workspace, "meta/tasks.json").ok ||
        !SaveCloudSyncConfig(workspace, config)) return fail("push overlap guard");
    if (!write(workspace / "meta/tasks.json", "{broken")) return false;
    const auto malformedPush = PushQtCloudWorkspaceFile(workspace, "meta/tasks.json");
    if (malformedPush.ok || read(cloud / "meta/tasks.json") != local) return fail("malformed local push");
    if (!write(workspace / "meta/tasks.json", local) || PushQtCloudWorkspaceFile(workspace, "meta/unknown.json").ok)
        return fail("unsupported push");
    if (!write(workspace / "meta/projects.json", "{broken") ||
        PushQtCloudWorkspaceFile(workspace, "meta/projects.json").ok ||
        read(cloud / "meta/projects.json") != localProjects) return fail("malformed projects push");
    if (!write(workspace / "meta/projects.json", localProjects)) return false;
    if (!write(workspace / "meta/banner.json", "{broken") ||
        PushQtCloudWorkspaceFile(workspace, "meta/banner.json").ok ||
        read(cloud / "meta/banner.json") != localBanner) return fail("malformed banner push");
    if (!write(workspace / "meta/banner.json", localBanner)) return false;
    if (!write(workspace / "meta/gameplay.ini", "[leveling]\nbase=not-a-number\n") ||
        PushQtCloudWorkspaceFile(workspace, "meta/gameplay.ini").ok ||
        read(cloud / "meta/gameplay.ini") != localGameplay) return fail("malformed gameplay push");
    if (!write(workspace / "meta/gameplay.ini", localGameplay)) return false;
    if (!write(workspace / "meta/professions.txt", "missing-name\n") ||
        PushQtCloudWorkspaceFile(workspace, "meta/professions.txt").ok ||
        read(cloud / "meta/professions.txt") != remoteProfessions ||
        read(cloud / "skills.txt") != remoteSkills) return fail("malformed professions push");
    if (!write(workspace / "meta/professions.txt", localProfessions)) return false;
    if (!write(cloud / "meta/tasks.json", "{broken")) return false;
    const auto malformed = ApplyQtCloudWorkspaceFile(workspace, cloud / "meta/tasks.json", "meta/tasks.json", "cloud");
    if (malformed.ok || read(workspace / "meta/tasks.json") != local) return fail("malformed source");
    const auto foreign = workspace.parent_path() / "foreign.json";
    if (!write(foreign, remote) || ApplyQtCloudWorkspaceFile(workspace, foreign, "meta/tasks.json", "cloud").ok ||
        ApplyQtCloudWorkspaceFile(workspace, foreign, "meta/tasks.json", "restore").ok) return fail("foreign source");
#ifdef _WIN32
    if (!write(cloud / "skills.txt", remoteSkills) || !write(cloud / "meta/professions.txt", remoteProfessions) ||
        !write(workspace / "skills.txt", localSkills) || !write(workspace / "meta/professions.txt", localProfessions)) return false;
    const HANDLE localProfessionLock = CreateFileW((workspace / "meta/professions.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (localProfessionLock == INVALID_HANDLE_VALUE) return false;
    const auto lockedPairApply = ApplyQtCloudWorkspaceFile(workspace, cloud / "skills.txt", "skills.txt", "cloud");
    CloseHandle(localProfessionLock);
    if (lockedPairApply.ok || read(workspace / "skills.txt") != localSkills ||
        read(workspace / "meta/professions.txt") != localProfessions) return fail("catalog pair apply rollback");
    const HANDLE cloudProfessionLock = CreateFileW((cloud / "meta/professions.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (cloudProfessionLock == INVALID_HANDLE_VALUE) return false;
    const auto lockedPairPush = PushQtCloudWorkspaceFile(workspace, "skills.txt");
    CloseHandle(cloudProfessionLock);
    if (lockedPairPush.ok || read(cloud / "skills.txt") != remoteSkills ||
        read(cloud / "meta/professions.txt") != remoteProfessions) return fail("catalog pair push rollback");
    if (!write(cloud / "meta/tasks.json", remote)) return false;
    const HANDLE lock = CreateFileW((workspace / "meta/tasks.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    const auto locked = ApplyQtCloudWorkspaceFile(workspace, cloud / "meta/tasks.json", "meta/tasks.json", "cloud");
    CloseHandle(lock);
    if (locked.ok || read(workspace / "meta/tasks.json") != local) return fail("sharing lock");
    const HANDLE cloudLock = CreateFileW((cloud / "meta/tasks.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (cloudLock == INVALID_HANDLE_VALUE) return false;
    const auto lockedPush = PushQtCloudWorkspaceFile(workspace, "meta/tasks.json");
    CloseHandle(cloudLock);
    if (lockedPush.ok || read(cloud / "meta/tasks.json") != remote) return fail("cloud sharing lock");
#endif
    if (!write(cloud / "meta/tasks.json", remote)) return false;
    bool inspected = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog ? dialog->findChild<QTableWidget*>("tasksComparison") : nullptr;
        auto* apply = dialog ? dialog->findChild<QPushButton*>("applyCloudTasks") : nullptr;
        auto* push = dialog ? dialog->findChild<QPushButton*>("pushCloudTasks") : nullptr;
        auto* projects = dialog ? dialog->findChild<QTableWidget*>("projectsComparison") : nullptr;
        auto* projectsPush = dialog ? dialog->findChild<QPushButton*>("pushCloudProjects") : nullptr;
        auto* banner = dialog ? dialog->findChild<QTableWidget*>("bannerComparison") : nullptr;
        auto* bannerPush = dialog ? dialog->findChild<QPushButton*>("pushCloudBanner") : nullptr;
        auto* gameplay = dialog ? dialog->findChild<QTableWidget*>("gameplayComparison") : nullptr;
        auto* gameplayPush = dialog ? dialog->findChild<QPushButton*>("pushCloudGameplay") : nullptr;
        auto* professions = dialog ? dialog->findChild<QTableWidget*>("professionsComparison") : nullptr;
        auto* professionsPush = dialog ? dialog->findChild<QPushButton*>("pushCloudProfessions") : nullptr;
        auto* skills = dialog ? dialog->findChild<QTableWidget*>("skillsComparison") : nullptr;
        auto* skillsPush = dialog ? dialog->findChild<QPushButton*>("pushCloudSkills") : nullptr;
        inspected = dialog && dialog->objectName() == "cloudConflictResolver" && table && table->rowCount() == 2 &&
            apply && apply->height() >= 40 && push && push->height() >= 40 && projects &&
            projects->rowCount() == 2 && projectsPush && projectsPush->isEnabled() && banner &&
            banner->rowCount() == 2 && bannerPush && bannerPush->isEnabled() && gameplay &&
            gameplay->rowCount() == 2 && gameplayPush && gameplayPush->isEnabled() && professions &&
            professions->rowCount() == 2 && professionsPush && professionsPush->isEnabled() && skills &&
            skills->rowCount() == 2 && skillsPush && skillsPush->isEnabled();
        if (!inspected) std::cerr << "cloudConflict inspect dialog=" << bool(dialog)
            << " name=" << (dialog ? dialog->objectName().toStdString() : "") << " table=" << bool(table)
            << " rows=" << (table ? table->rowCount() : -1) << " apply=" << bool(apply)
            << " height=" << (apply ? apply->height() : -1) << " projects=" << bool(projects)
            << " projectsPush=" << bool(projectsPush) << " banner=" << bool(banner)
            << " bannerPush=" << bool(bannerPush) << " gameplay=" << bool(gameplay)
            << " gameplayPush=" << bool(gameplayPush) << " professions=" << bool(professions)
            << " professionsPush=" << bool(professionsPush) << '\n';
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (dialog && !artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/cloud-conflict.png"); }
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                if (box->defaultButton() != box->button(QMessageBox::Cancel)) return;
                box->button(QMessageBox::Yes)->click();
            }
        });
        if (apply) apply->click();
    });
    const bool dialogChanged = ShowCloudConflictResolver(nullptr, workspace);
    if (!dialogChanged || !inspected || read(workspace / "meta/tasks.json") != remote) {
        std::cerr << "cloudConflict dialog changed=" << dialogChanged << " inspected=" << inspected
                  << " local=" << read(workspace / "meta/tasks.json").toStdString() << '\n';
        return fail("dialog apply");
    }
    const QByteArray upload = "[{\"id\":\"upload\",\"title\":\"Upload\"}]";
    if (!write(workspace / "meta/tasks.json", upload)) return false;
    bool pushConfirmed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* push = dialog ? dialog->findChild<QPushButton*>("pushCloudTasks") : nullptr;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                pushConfirmed = box->defaultButton() == box->button(QMessageBox::Cancel) &&
                    box->text().contains(QString::fromUtf8("Будет заменено"));
                const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
                if (!artifacts.isEmpty()) box->grab().save(artifacts + "/cloud-push-confirm.png");
                box->button(QMessageBox::Yes)->click();
            }
        });
        if (push) push->click();
    });
    const bool pushChanged = ShowCloudConflictResolver(nullptr, workspace);
    if (!pushChanged || !pushConfirmed || read(cloud / "meta/tasks.json") != upload) return fail("dialog push");
    const QByteArray projectUpload = "[{\"id\":\"ui-project\",\"name\":\"UI project\"}]";
    if (!write(workspace / "meta/projects.json", projectUpload)) return false;
    bool projectPushConfirmed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* push = dialog ? dialog->findChild<QPushButton*>("pushCloudProjects") : nullptr;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                projectPushConfirmed = box->defaultButton() == box->button(QMessageBox::Cancel) &&
                    box->text().contains(QString::fromUtf8("meta/projects.json"));
                box->button(QMessageBox::Yes)->click();
            }
        });
        if (push) push->click();
    });
    const bool projectsChanged = ShowCloudConflictResolver(nullptr, workspace);
    if (!projectsChanged || !projectPushConfirmed || read(cloud / "meta/projects.json") != projectUpload)
        return fail("dialog projects push");
    const QByteArray bannerUpload = "{\"items\":[\"UI phrase\"]}";
    if (!write(workspace / "meta/banner.json", bannerUpload)) return false;
    bool bannerPushConfirmed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* push = dialog ? dialog->findChild<QPushButton*>("pushCloudBanner") : nullptr;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                bannerPushConfirmed = box->defaultButton() == box->button(QMessageBox::Cancel) &&
                    box->text().contains(QString::fromUtf8("meta/banner.json"));
                box->button(QMessageBox::Yes)->click();
            }
        });
        if (push) push->click();
    });
    const bool bannerChanged = ShowCloudConflictResolver(nullptr, workspace);
    if (!bannerChanged || !bannerPushConfirmed || read(cloud / "meta/banner.json") != bannerUpload)
        return fail("dialog banner push");
    const QByteArray gameplayUpload = "[leveling]\nbase=2100\nlinear=300\n";
    if (!write(workspace / "meta/gameplay.ini", gameplayUpload)) return false;
    bool gameplayPushConfirmed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* push = dialog ? dialog->findChild<QPushButton*>("pushCloudGameplay") : nullptr;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                gameplayPushConfirmed = box->defaultButton() == box->button(QMessageBox::Cancel) &&
                    box->text().contains(QString::fromUtf8("meta/gameplay.ini"));
                box->button(QMessageBox::Yes)->click();
            }
        });
        if (push) push->click();
    });
    const bool gameplayChanged = ShowCloudConflictResolver(nullptr, workspace);
    if (!gameplayChanged || !gameplayPushConfirmed || read(cloud / "meta/gameplay.ini") != gameplayUpload)
        return fail("dialog gameplay push");
    const QByteArray professionsUpload = "\xEF\xBB\xBF" "pr_ui|UI profession|UI description\n";
    const QByteArray skillsUpload = "\xEF\xBB\xBF" "sk_ui|UI skill|1|prof=pr_ui|UI description\n";
    if (!write(workspace / "meta/professions.txt", professionsUpload) || !write(workspace / "skills.txt", skillsUpload)) return false;
    bool professionsPushConfirmed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* push = dialog ? dialog->findChild<QPushButton*>("pushCloudProfessions") : nullptr;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                professionsPushConfirmed = box->defaultButton() == box->button(QMessageBox::Cancel) &&
                    box->text().contains(QString::fromUtf8("meta/professions.txt")) &&
                    box->text().contains(QString::fromUtf8("skills.txt"));
                box->button(QMessageBox::Yes)->click();
            }
        });
        if (push) push->click();
    });
    const bool professionsChanged = ShowCloudConflictResolver(nullptr, workspace);
    if (!professionsChanged || !professionsPushConfirmed || read(cloud / "meta/professions.txt") != professionsUpload ||
        read(cloud / "skills.txt") != skillsUpload)
        return fail("dialog professions push");
    QtWorkspace uiWorkspace(workspace); QtWindow window(uiWorkspace); window.show(); QApplication::processEvents();
    auto* nav = window.findChild<QListWidget*>("navigation"); nav->setCurrentRow(13);
    auto* route = window.findChild<QPushButton*>("cloudResolve");
    if (!route || !route->isVisible() || !route->isEnabled() || route->height() < 40) return fail("window route");
    bool routeOpened = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        routeOpened = dialog && dialog->objectName() == "cloudConflictResolver";
        if (dialog) dialog->reject();
    });
    route->click();
    if (!routeOpened) return fail("window route dialog");
    window.close();
    return true;
}

static bool TestStorageConflictResolver() {
    auto fail = [](const char* step) { std::cerr << "storageConflict: " << step << '\n'; return false; };
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto workspace = std::filesystem::u8path((temp.path() + "/workspace").toUtf8().toStdString());
    const auto cloud = std::filesystem::u8path((temp.path() + "/cloud").toUtf8().toStdString());
    std::filesystem::create_directories(workspace); std::filesystem::create_directories(cloud);
    CloudSyncConfig config; config.enabled = true; config.root = cloud;
    if (!SaveCloudSyncConfig(workspace, config)) return false;
    StorageVaultData local; local.currencyName = "Local coin"; local.currencyCode = "LOC"; local.balance = 10; local.log.push_back({100, 10, "local", "entry"});
    StorageVaultData remote; remote.currencyName = "Cloud coin"; remote.currencyCode = "CLD"; remote.balance = 25; remote.log.push_back({200, 25, "cloud", "entry"});
    if (!SaveStorageVault(workspace, local) || !SaveStorageVault(cloud, remote) || !HasQtStorageConflict(workspace)) return fail("fixture");
    const auto accepted = ResolveQtStorageConflict(workspace, true);
    if (!accepted.ok || !accepted.changed || std::abs(LoadStorageVault(workspace).balance - 25) > 0.001 || HasQtStorageConflict(workspace)) return fail("accept cloud");
    QDir updates(QString::fromUtf8((workspace / "meta/updates").u8string()));
    if (updates.entryList({"storage.local.*.json"}, QDir::Files).isEmpty() || updates.entryList({"storage.cloud.*.json"}, QDir::Files).isEmpty()) return fail("backups");
    local.balance = 42; local.currencyCode = "NEW"; if (!SaveStorageVault(workspace, local)) return false;
    const auto pushed = ResolveQtStorageConflict(workspace, false);
    if (!pushed.ok || !pushed.changed || std::abs(LoadStorageVault(cloud).balance - 42) > 0.001) return fail("keep local");
    QFile malformed(QString::fromUtf8((cloud / "meta/storage.json").u8string()));
    if (!malformed.open(QIODevice::WriteOnly | QIODevice::Truncate) || malformed.write("{broken") != 7) return false; malformed.close();
    const auto before = LoadStorageVault(workspace).balance;
    if (ResolveQtStorageConflict(workspace, true).ok || std::abs(LoadStorageVault(workspace).balance - before) > 0.001) return fail("malformed");
    if (!SaveStorageVault(cloud, remote)) return false;
    QFile tampered(QString::fromUtf8((cloud / "meta/storage.json").u8string()));
    if (!tampered.open(QIODevice::ReadOnly)) return false; auto tamperedBytes = tampered.readAll(); tampered.close();
    tamperedBytes.replace("Cloud coin", "False coin");
    if (!tampered.open(QIODevice::WriteOnly | QIODevice::Truncate) || tampered.write(tamperedBytes) != tamperedBytes.size()) return false; tampered.close();
    if (ResolveQtStorageConflict(workspace, true).ok) return fail("content hash");
    if (!SaveStorageVault(cloud, remote)) return false;
#ifdef _WIN32
    const auto cloudPath = (cloud / "meta/storage.json").wstring();
    const HANDLE lock = CreateFileW(cloudPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    const auto locked = ResolveQtStorageConflict(workspace, false); CloseHandle(lock);
    if (locked.ok || std::abs(LoadStorageVault(cloud).balance - 25) > 0.001) return fail("sharing lock");
#endif
    bool inspected = false; bool localChanged = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog ? dialog->findChild<QTableWidget*>("storageComparison") : nullptr;
        auto* accept = dialog ? dialog->findChild<QPushButton*>("acceptCloudStorage") : nullptr;
        inspected = dialog && table && table->rowCount() == 2 && accept && accept->height() >= 40;
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (dialog && !artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/storage-conflict.png"); }
        QTimer::singleShot(0, [&] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { inspected = inspected && box->defaultButton() == box->button(QMessageBox::Cancel) && box->button(QMessageBox::Yes)->height() >= 40; box->button(QMessageBox::Yes)->click(); } });
        if (accept) accept->click();
    });
    if (!ShowQtStorageConflictResolver(nullptr, workspace, &localChanged) || !inspected || !localChanged ||
        std::abs(LoadStorageVault(workspace).balance - 25) > 0.001) return fail("dialog accept");
    return true;
}

static bool TestProfileDialogs() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    std::vector<std::pair<AppLogLevel, std::string>> coreEvents;
    bool throwProfileEvents = false;
    workspace.profileEventLogger = [&](AppLogLevel level, const std::string& event) {
        coreEvents.emplace_back(level, event);
        if (throwProfileEvents) throw std::runtime_error("profile event observer failure");
    };
    Profile original("Original");
    original.set_total_xp(777);
    original.set_wallet_balance(42);
    original.set_login("original-login");
    original.set_password_encoded(EncodePassword("original-password"));
    original.add_skill("test-skill");
    auto created = workspace.storage->create_profile(original);
    if (!created) return false;
    const QString id = QString::fromStdString(created->id);
    auto readProfileFile = [&](const QString& path) {
        QFile file(temp.path() + "/" + path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const auto originalProfileBytes = readProfileFile(id + ".ini");
    const auto originalAuditBytes = readProfileFile("meta/profile-audit.log");
    PrepareProfileAuditRecovery(workspace.directory, created->id);
    const std::filesystem::path profileRoot(temp.path().toStdWString());
    { std::ofstream interrupted(profileRoot / std::filesystem::u8path(id.toStdString() + ".ini"), std::ios::binary | std::ios::trunc); interrupted << "interrupted"; }
    { std::ofstream interrupted(profileRoot / L"meta/profile-audit.log", std::ios::binary | std::ios::app); interrupted << "interrupted\n"; }
    workspace.reload();
    if (readProfileFile(id + ".ini") != originalProfileBytes || readProfileFile("meta/profile-audit.log") != originalAuditBytes)
        return false;
    PrepareProfileArchiveAuditRecovery(workspace.directory, created->id);
    if (!workspace.storage->set_archived(created->id, true) ||
        !AppendProfileAudit(workspace.directory, created->id, "archive")) return false;
    workspace.reload();
    if (readProfileFile(id + ".ini") != originalProfileBytes || readProfileFile("meta/profile-audit.log") != originalAuditBytes ||
        std::filesystem::exists(workspace.directory / "archive" / (created->id + ".ini"))) return false;
    workspace.data.professions.push_back({"artist", "Artist", "3D"});
    auto delegate = std::move(workspace.storage);
    auto wrapper = std::make_unique<FailingProfileStorage>(*delegate);
    auto* failures = wrapper.get();
    workspace.storage = std::move(wrapper);
    bool checks = true;
    const QString originalClipboard = QApplication::clipboard()->text();
    QStringList missingProfileDialogAccessibleNames;
    QString disposableId;
    QString resetPasswordResult;
    QTimer::singleShot(0, [&] {
        auto* manager = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!manager) { checks = false; return; }
        missingProfileDialogAccessibleNames.append(MissingAccessibleNames(manager));
        auto* table = manager->findChild<QTableWidget*>("profileRecords");
        auto createProfileThroughUi = [&](const QString& profileName) {
            QTimer::singleShot(0, [profileName] {
                if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                    input->setTextValue(profileName); input->accept();
                }
            });
            manager->findChild<QPushButton*>("createProfile")->click();
        };
        AppSetProfileAuditFailureHookForTests(true);
        createProfileThroughUi(QString::fromUtf8("Профиль без записи аудита"));
        AppSetProfileAuditFailureHookForTests(false);
        checks &= delegate->list_profiles().size() == 2;
        QString auditFailureProfileId;
        for (const auto& p : delegate->list_profiles()) if (p.id != created->id) auditFailureProfileId = QString::fromStdString(p.id);
        auto* managerStatus = manager->findChild<QLabel*>("profileNotice");
        checks &= managerStatus && managerStatus->text().contains(QString::fromUtf8("не записано в историю")) &&
            !readProfileFile("meta/profile-audit.log").contains("|" + auditFailureProfileId.toUtf8() + "|create|");
        createProfileThroughUi(QString::fromUtf8("Новый профиль"));
        checks &= delegate->list_profiles().size() == 3;
        for (const auto& p : delegate->list_profiles())
            if (p.id != created->id && p.id != auditFailureProfileId.toStdString()) disposableId = QString::fromStdString(p.id);
        auto* credentials = manager->findChild<QLineEdit*>("createdProfileCredentials");
        checks &= !credentials->text().isEmpty() && credentials->echoMode() == QLineEdit::Password;
        auto* revealCredentials = manager->findChild<QCheckBox*>("showCreatedProfileCredentials");
        auto* copyCreatedLogin = manager->findChild<QPushButton*>("copyCreatedProfileLogin");
        auto* copyCreatedPassword = manager->findChild<QPushButton*>("copyCreatedProfilePassword");
        checks &= revealCredentials && copyCreatedLogin && copyCreatedPassword && !copyCreatedPassword->isEnabled();
        const auto createdFields = credentials->text().split(QString::fromUtf8("   Пароль: "));
        const auto createdLogin = createdFields.value(0).mid(QString::fromUtf8("Логин: ").size());
        const auto createdPassword = createdFields.value(1);
        const auto createAudit = readProfileFile("meta/profile-audit.log");
        checks &= createAudit.contains("|" + disposableId.toUtf8() + "|create|login=" + createdLogin.toUtf8()) &&
            !createAudit.contains(createdPassword.toUtf8());
        copyCreatedLogin->click();
        checks &= QApplication::clipboard()->text() == createdLogin;
        revealCredentials->setChecked(true);
        copyCreatedPassword->click();
        checks &= QApplication::clipboard()->text() == createdPassword;
        revealCredentials->setChecked(false);
        checks &= !copyCreatedPassword->isEnabled();
        auto select = [&] {
            for (int r = 0; r < table->rowCount(); ++r) if (table->item(r, 0)->data(Qt::UserRole).toString() == id) table->selectRow(r);
        };
        select();
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!editor) { checks = false; return; }
            missingProfileDialogAccessibleNames.append(MissingAccessibleNames(editor));
            auto* profileName = editor->findChild<QLineEdit*>("profileName");
            profileName->clear();
            editor->findChild<QComboBox*>("profileProfession")->setCurrentIndex(1);
            editor->findChild<QComboBox*>("profileSpirit")->setCurrentIndex(1);
            editor->findChild<QCheckBox*>("profileBlocked")->setChecked(true);
            auto* save = editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            save->click();
            checks &= !editor->findChild<QLabel*>("profileNotice")->text().isEmpty();
            profileName->setText(QString::fromUtf8("Переименованный профиль"));
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) editor->grab().save(artifacts + "/profile-rename.png");
            const auto profileBytes = readProfileFile(id + ".ini");
            const auto auditBytes = readProfileFile("meta/profile-audit.log");
            AppSetProfileAuditFailureHookForTests(true);
            save->click();
            AppSetProfileAuditFailureHookForTests(false);
            delegate->set_active_profile(created->id);
            const auto auditRollbackProfile = delegate->load_profile();
            checks &= auditRollbackProfile && auditRollbackProfile->name() == original.name() &&
                auditRollbackProfile->profession_id().empty() && auditRollbackProfile->spirit() == ProfileSpirit::None &&
                !auditRollbackProfile->is_blocked() && readProfileFile(id + ".ini") == profileBytes &&
                readProfileFile("meta/profile-audit.log") == auditBytes &&
                !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction");
            profileName->setText(QString::fromUtf8("Переименованный профиль"));
            failures->writes = 0;
            failures->failAt = 1;
            save->click();
            delegate->set_active_profile(created->id);
            checks &= !delegate->load_profile()->is_blocked() && !editor->findChild<QLabel*>("profileNotice")->text().isEmpty();
            failures->failAt = 0;
            save->click();
        });
        manager->findChild<QPushButton*>("editProfile")->click();
        delegate->set_active_profile(created->id);
        auto edited = delegate->load_profile();
        checks &= edited && edited->name() == u8"Переименованный профиль" && edited->profession_id() == "artist" && edited->spirit() == ProfileSpirit::Good && edited->is_blocked();
        checks &= edited && edited->total_xp() == 777 && edited->wallet_balance() == 42 && edited->login() == original.login() &&
            edited->password_encoded() == original.password_encoded() && edited->list_skills().size() == 1;
        checks &= readProfileFile("meta/profile-audit.log").contains("|profile_edit|name,profession,spirit,blocked");
        auto* archive = manager->findChild<QPushButton*>("archiveProfile");
        const auto beforeArchiveProfile = readProfileFile(id + ".ini");
        const auto beforeArchiveAudit = readProfileFile("meta/profile-audit.log");
        AppSetProfileAuditFailureHookForTests(true);
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        archive->click();
        AppSetProfileAuditFailureHookForTests(false);
        delegate->set_active_profile(created->id);
        checks &= readProfileFile(id + ".ini") == beforeArchiveProfile &&
            !std::filesystem::exists(workspace.directory / "archive" / (created->id + ".ini")) &&
            readProfileFile("meta/profile-audit.log") == beforeArchiveAudit &&
            !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction");
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::No)->click(); });
        archive->click();
        checks &= delegate->set_active_profile(created->id);
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        archive->click();
        checks &= !delegate->set_active_profile(created->id);
        manager->findChild<QCheckBox*>("showArchivedProfiles")->setChecked(true);
        select();
        checks &= !manager->findChild<QPushButton*>("editProfile")->isEnabled();
        archive->click();
        checks &= delegate->set_active_profile(created->id);
        select();
        QTimer::singleShot(0, [&] {
            auto* password = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!password) { checks = false; return; }
            missingProfileDialogAccessibleNames.append(MissingAccessibleNames(password));
            const auto oldPassword = DecodePassword(delegate->load_profile()->password_encoded());
            const auto oldProfileBytes = readProfileFile(id + ".ini");
            const auto oldAuditBytes = readProfileFile("meta/profile-audit.log");
            auto* generatedPassword = password->findChild<QLineEdit*>("resetProfileGeneratedPassword");
            auto* reveal = password->findChild<QCheckBox*>("revealResetProfilePassword");
            auto* confirmed = password->findChild<QCheckBox*>("confirmProfilePasswordReset");
            auto* resetLogin = password->findChild<QLineEdit*>("resetProfileLogin");
            auto* copyLogin = password->findChild<QPushButton*>("copyResetProfileLogin");
            auto* copyPassword = password->findChild<QPushButton*>("copyResetProfilePassword");
            checks &= generatedPassword && reveal && confirmed && copyPassword && !copyPassword->isEnabled() &&
                resetLogin && copyLogin;
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                password->grab().save(artifacts + "/profile-reset-credentials.png");
            }
            const auto newPassword = generatedPassword->text();
            const auto resetProfileLogin = resetLogin->text();
            resetPasswordResult = newPassword;
            copyLogin->click();
            checks &= QApplication::clipboard()->text() == resetProfileLogin;
            reveal->setChecked(true);
            copyPassword->click();
            checks &= QApplication::clipboard()->text() == newPassword;
            auto* save = password->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            save->click();
            checks &= !password->findChild<QLabel*>("profileNotice")->text().isEmpty() &&
                DecodePassword(delegate->load_profile()->password_encoded()) == oldPassword;
            confirmed->setChecked(true);
            AppSetProfileAuditFailureHookForTests(true);
            save->click();
            AppSetProfileAuditFailureHookForTests(false);
            checks &= DecodePassword(delegate->load_profile()->password_encoded()) == oldPassword &&
                readProfileFile(id + ".ini") == oldProfileBytes &&
                readProfileFile("meta/profile-audit.log") == oldAuditBytes &&
                !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction");
            save->click();
            checks &= DecodePassword(delegate->load_profile()->password_encoded()) == newPassword.toUtf8().toStdString();
            checks &= password->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->text() == QString::fromUtf8("Готово");
            save->click();
        });
        manager->findChild<QPushButton*>("resetProfilePassword")->click();
        delegate->set_active_profile(created->id);
        checks &= DecodePassword(delegate->load_profile()->password_encoded()) == resetPasswordResult.toUtf8().toStdString();
        auto selectId = [&](const QString& target) {
            for (int r = 0; r < table->rowCount(); ++r)
                if (table->item(r, 0)->data(Qt::UserRole).toString() == target) table->selectRow(r);
        };
        selectId(disposableId);
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        archive->click();
        manager->findChild<QCheckBox*>("showArchivedProfiles")->setChecked(true);
        selectId(disposableId);
        auto* permanentDelete = manager->findChild<QPushButton*>("deleteArchivedProfile");
        checks &= permanentDelete && permanentDelete->isEnabled();
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            manager->grab().save(artifacts + "/profile-delete.png");
            manager->grab().save(artifacts + "/profile-manager.png");
            manager->resize(640, 440);
            QApplication::processEvents();
            manager->grab().save(artifacts + "/profile-manager-small.png");
        }
        failures->failDelete = true;
        const size_t eventsBeforeFailedDelete = coreEvents.size();
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        permanentDelete->click();
        const auto afterFailedDelete = delegate->list_profiles();
        checks &= std::find_if(afterFailedDelete.begin(), afterFailedDelete.end(),
            [&](const auto& p) { return QString::fromStdString(p.id) == disposableId; }) != afterFailedDelete.end();
        checks &= coreEvents.size() == eventsBeforeFailedDelete + 1 &&
            coreEvents.back().first == AppLogLevel::Warning &&
            coreEvents.back().second == "Profile deletion failed or was rolled back";
        failures->failDelete = false;
        selectId(disposableId);
        throwProfileEvents = true;
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        permanentDelete->click();
        throwProfileEvents = false;
        const auto afterPermanentDelete = delegate->list_profiles();
        checks &= std::none_of(afterPermanentDelete.begin(), afterPermanentDelete.end(),
            [&](const auto& p) { return QString::fromStdString(p.id) == disposableId; });
        manager->reject();
    });
    ShowProfileManager(nullptr, workspace, id);
    delegate->set_active_profile(created->id);
    auto unblocked = *delegate->load_profile();
    unblocked.set_blocked(false);
    delegate->save_profile(unblocked);
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { checks = false; return; }
        missingProfileDialogAccessibleNames.append(MissingAccessibleNames(dialog));
        dialog->findChild<QLineEdit*>("currentPassword")->setText("wrong-password");
        dialog->findChild<QLineEdit*>("newPassword")->setText("my-password");
        dialog->findChild<QLineEdit*>("confirmPassword")->setText("my-password");
        auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        save->click();
        checks &= DecodePassword(delegate->load_profile()->password_encoded()) == resetPasswordResult.toUtf8().toStdString();
        dialog->findChild<QLineEdit*>("currentPassword")->setText(resetPasswordResult);
        save->click();
    });
    checks &= ShowProfilePasswordDialog(nullptr, workspace, id, id, false);
    delegate->set_active_profile(created->id);
    checks &= DecodePassword(delegate->load_profile()->password_encoded()) == "my-password";
    missingProfileDialogAccessibleNames.removeDuplicates();
    const auto hasEvent = [&](AppLogLevel level, const std::string& message) {
        return std::any_of(coreEvents.begin(), coreEvents.end(), [&](const auto& entry) {
            return entry.first == level && entry.second == message;
        });
    };
    checks &= hasEvent(AppLogLevel::Info, "Profile update transaction committed") &&
        hasEvent(AppLogLevel::Warning, "Profile update failed or was rolled back") &&
        hasEvent(AppLogLevel::Info, "Profile archive transaction committed") &&
        hasEvent(AppLogLevel::Warning, "Profile archive transaction failed or was rolled back") &&
        hasEvent(AppLogLevel::Warning, "Profile deletion failed or was rolled back") &&
        hasEvent(AppLogLevel::Info, "Profile deletion committed") &&
        hasEvent(AppLogLevel::Info, "Profile restore transaction committed") &&
        hasEvent(AppLogLevel::Info, "Profile password transaction committed") &&
        hasEvent(AppLogLevel::Warning, "Profile password transaction failed or was rolled back");
    for (const auto& [level, event] : coreEvents) {
        (void)level;
        checks &= event.find(created->id) == std::string::npos &&
            event.find(disposableId.toStdString()) == std::string::npos &&
            event.find("Новый профиль") == std::string::npos &&
            event.find("reset-password") == std::string::npos && event.find("my-password") == std::string::npos;
        checks &= event.find(resetPasswordResult.toUtf8().toStdString()) == std::string::npos;
    }
    if (!missingProfileDialogAccessibleNames.isEmpty()) {
        std::cerr << "Visible profile dialog controls without accessible names:\n";
        for (const auto& name : missingProfileDialogAccessibleNames)
            std::cerr << "  " << name.toUtf8().constData() << '\n';
        checks = false;
    }
    if (!checks) std::cerr << "profile dialog lifecycle failed\n";
    QApplication::clipboard()->setText(originalClipboard);
    return checks;
}

static bool TestAchievements() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    QDir().mkpath(temp.path() + "/achievements/icons");
    QImage iconImage(32, 32, QImage::Format_ARGB32); iconImage.fill(QColor("#7654a8"));
    const QString iconPath = "achievements/icons/Medal.png";
    if (!iconImage.save(temp.path() + "/" + iconPath)) return false;
    workspace.catalog.add_skill("Modeling", 1, "Geometry");
    const auto skill = *workspace.catalog.id_for_name("Modeling");
    Profile profile("Achievement profile");
    profile.set_total_xp(777); profile.set_wallet_balance(42);
    const auto created = workspace.storage->create_profile(profile);
    if (!created) return false;
    const auto id = created->id;
    auto read = [&](const QString& suffix) { QFile f(temp.path() + "/" + suffix); f.open(QIODevice::ReadOnly); return f.readAll(); };
    const auto profilePath = QString::fromStdString(id) + ".ini";
    const auto achievementPath = "achievements/" + QString::fromStdString(id) + ".json";
    const auto original = read(profilePath);
    if (GrantQtAchievement(workspace, id, "", skill, 10, 1).isEmpty() ||
        GrantQtAchievement(workspace, id, "Invalid", "unknown", 10, 1).isEmpty()) return false;
    const auto beforeLock = read(achievementPath);
    std::atomic<bool> lockHolderReady{false};
    std::atomic<bool> releaseLockHolder{false};
    std::atomic<bool> lockHolderAcquired{false};
    std::thread lockHolder([&] {
        AppWorkspaceStorageWriteLock held(workspace.directory);
        lockHolderAcquired = held.acquired();
        lockHolderReady = true;
        while (!releaseLockHolder) std::this_thread::yield();
    });
    while (!lockHolderReady) std::this_thread::yield();
    const auto blockedResult = lockHolderAcquired
        ? GrantQtAchievement(workspace, id, QString::fromUtf8("Заблокированное"), skill, 25, 1)
        : QString();
    const bool blockedWithoutMutation = lockHolderAcquired && !blockedResult.isEmpty() && read(achievementPath) == beforeLock;
    releaseLockHolder = true;
    lockHolder.join();
    if (!blockedWithoutMutation) {
        std::cerr << "Achievement lock test failed: acquired=" << lockHolderAcquired
                  << " error=" << blockedResult.toStdString()
                  << " bytes-unchanged=" << (read(achievementPath) == beforeLock) << "\n";
        return false;
    }
    if (!GrantQtAchievement(workspace, id, QString::fromUtf8("Мастер геометрии"), skill, 25, 1).isEmpty()) return false;
    workspace.storage->set_active_profile(id);
    auto loaded = workspace.storage->load_profile();
    if (!loaded || loaded->achievements().size() != 1 || read(profilePath) != original ||
        loaded->total_xp() != 777 || loaded->wallet_balance() != 42) return false;
    const auto earned = loaded->achievements().front();
    if (earned.expiresAt - earned.awardedAt != 86400 ||
        loaded->skill_bonus_multiplier(skill, earned.awardedAt) != 1.25 ||
        loaded->skill_bonus_multiplier(skill, earned.expiresAt + 1) != 1.0) return false;
    const auto before = read(achievementPath);
    for (const auto& invalid : {QString("../Medal.png"), QString("achievements/icons/../Medal.png"), QString("C:/Medal.png"), QString("achievements/icons/missing.png")})
        if (GrantQtAchievement(workspace, id, "Invalid icon", skill, 10, 0, invalid).isEmpty() || read(achievementPath) != before) return false;
    { QFile badIcon(temp.path() + "/achievements/icons/bad.png"); badIcon.open(QIODevice::WriteOnly); badIcon.write("not png"); }
    if (GrantQtAchievement(workspace, id, "Invalid icon", skill, 10, 0, "achievements/icons/bad.png").isEmpty()) return false;
    const auto path = temp.path() + "/" + achievementPath;
    if (!QFile::rename(path, path + ".original") || !QDir().mkdir(path)) return false;
    const bool failed = !GrantQtAchievement(workspace, id, "Failed", skill, 10, 0).isEmpty();
    QDir().rmdir(path);
    if (!QFile::rename(path + ".original", path) || !failed || read(achievementPath) != before) return false;
    {
        QFile bad(path); if (!bad.open(QIODevice::WriteOnly)) return false; bad.write("not-json");
    }
    if (GrantQtAchievement(workspace, id, "Failed", skill, 10, 0).isEmpty() || read(achievementPath) != "not-json") return false;
    { QFile restore(path); if (!restore.open(QIODevice::WriteOnly)) return false; restore.write(before); }
    bool checks = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        checks = dialog && !dialog->findChild<QPushButton*>("grantAchievement")->isVisible() &&
            !dialog->findChild<QPushButton*>("editAchievement")->isVisible() &&
            !dialog->findChild<QPushButton*>("revokeAchievement")->isVisible() &&
            dialog->findChild<QTableWidget*>("achievementRecords")->rowCount() == 1;
        if (dialog) dialog->reject();
    });
    ShowAchievements(nullptr, workspace, id, false);
    if (!checks || read(profilePath) != original || read(achievementPath) != before) return false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QTimer::singleShot(0, [&] {
            auto* editor = QApplication::activeModalWidget();
            auto* save = editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            save->click();
            checks &= !editor->findChild<QLabel*>("achievementError")->text().isEmpty();
            editor->findChild<QLineEdit*>("achievementTitle")->setText(QString::fromUtf8("Точная работа"));
            editor->findChild<QDoubleSpinBox*>("achievementBonus")->setValue(10);
            auto* icons = editor->findChild<QComboBox*>("achievementIcon");
            checks &= icons && icons->findData(iconPath) >= 0 && icons->findData("achievements/icons/bad.png") < 0;
            icons->setCurrentIndex(icons->findData(iconPath));
            save->click();
        });
        dialog->findChild<QPushButton*>("grantAchievement")->click();
        checks &= dialog->findChild<QTableWidget*>("achievementRecords")->rowCount() == 2;
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->resize(600, 400); QApplication::processEvents(); dialog->grab().save(artifacts + "/achievements.png"); }
        dialog->reject();
    });
    ShowAchievements(nullptr, workspace, id, true);
    if (!checks || read(profilePath) != original) return false;
    workspace.storage->set_active_profile(id);
    loaded = workspace.storage->load_profile();
    if (!loaded || loaded->achievements().size() != 2 || loaded->skill_bonus_multiplier(skill, QDateTime::currentSecsSinceEpoch()) != 1.35) return false;
    if (loaded->achievements()[1].icon != iconPath.toStdString()) return false;
    const auto editBaseline = read(achievementPath);
#ifdef _WIN32
    const auto lockedPath = std::filesystem::u8path(path.toUtf8().toStdString());
    const auto editLock = CreateFileW(lockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (editLock == INVALID_HANDLE_VALUE) return false;
    const auto failedEdit = UpdateQtAchievement(workspace, id, 0, editBaseline, "Failed edit", 5);
    CloseHandle(editLock);
    if (failedEdit.isEmpty() || read(achievementPath) != editBaseline) return false;
#endif
    if (UpdateQtAchievement(workspace, id, 0, before, "Stale", 5).isEmpty() || read(achievementPath) != editBaseline) return false;
    if (!UpdateQtAchievement(workspace, id, 0, editBaseline, "Updated", 30, 2).isEmpty()) return false;
    loaded = workspace.storage->load_profile();
    if (!loaded || loaded->achievements()[0].awardedAt != earned.awardedAt ||
        loaded->achievements()[0].expiresAt != earned.awardedAt + 2 * 86400 ||
        loaded->achievements()[0].skill != skill) return false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog->findChild<QTableWidget*>("achievementRecords"); table->selectRow(0);
        QTimer::singleShot(0, [&] {
            auto* editor = QApplication::activeModalWidget();
            editor->findChild<QLineEdit*>("achievementTitle")->setText(QString::fromUtf8("Точная геометрия"));
            auto* icons = editor->findChild<QComboBox*>("achievementIcon");
            icons->setCurrentIndex(icons->findData(iconPath));
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) editor->grab().save(artifacts + "/achievement-edit.png");
            editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        dialog->findChild<QPushButton*>("editAchievement")->click();
        const auto afterEdit = workspace.storage->load_profile();
        checks &= afterEdit && afterEdit->achievements()[0].expiresAt == earned.awardedAt + 2 * 86400 &&
            afterEdit->achievements()[0].awardedAt == earned.awardedAt && afterEdit->achievements()[0].skill == skill &&
            afterEdit->achievements()[0].icon == iconPath.toStdString() && !table->item(0, 0)->icon().isNull();
        table->selectRow(0);
        QTimer::singleShot(0, [] { qobject_cast<QMessageBox*>(QApplication::activeModalWidget())->button(QMessageBox::No)->click(); });
        dialog->findChild<QPushButton*>("revokeAchievement")->click();
        checks &= table->rowCount() == 2;
        QTimer::singleShot(0, [] { qobject_cast<QMessageBox*>(QApplication::activeModalWidget())->button(QMessageBox::Yes)->click(); });
        dialog->findChild<QPushButton*>("revokeAchievement")->click();
        checks &= table->rowCount() == 1;
        dialog->reject();
    });
    ShowAchievements(nullptr, workspace, id, true);
    loaded = workspace.storage->load_profile();
    if (!checks || !loaded || loaded->achievements().size() != 1 ||
        loaded->achievements()[0].title != u8"Точная работа" || read(profilePath) != original) return false;
    // Missing legacy icons survive an unchanged edit; explicit clearing touches no XP or expiry.
    QFile::remove(temp.path() + "/" + iconPath);
    const auto retained = loaded->achievements()[0];
    if (!UpdateQtAchievement(workspace, id, 0, read(achievementPath), QString::fromUtf8(retained.title.c_str()), retained.bonusPercent,
        std::nullopt, false, iconPath).isEmpty()) return false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        dialog->findChild<QTableWidget*>("achievementRecords")->selectRow(0);
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            checks &= editor->findChild<QComboBox*>("achievementIcon")->currentData().toString() == iconPath;
            editor->reject();
        });
        dialog->findChild<QPushButton*>("editAchievement")->click(); dialog->reject();
    });
    const auto beforeCancel = read(achievementPath);
    ShowAchievements(nullptr, workspace, id, true);
    if (!checks || read(achievementPath) != beforeCancel) return false;
    if (!UpdateQtAchievement(workspace, id, 0, beforeCancel, QString::fromUtf8(retained.title.c_str()), retained.bonusPercent,
        std::nullopt, false, QString()).isEmpty()) return false;
    loaded = workspace.storage->load_profile();
    if (!loaded || !loaded->achievements()[0].icon.empty() || loaded->achievements()[0].expiresAt != retained.expiresAt ||
        loaded->achievements()[0].bonusPercent != retained.bonusPercent || read(profilePath) != original) return false;
    const auto granted = read(achievementPath);
    std::filesystem::create_directories(workspace.directory / "meta/qt-xp-transaction");
    if (GrantQtAchievement(workspace, id, "Pending", skill, 10, 0).isEmpty() || read(achievementPath) != granted) return false;
    std::filesystem::remove(workspace.directory / "meta/qt-xp-transaction");
    loaded->set_blocked(true);
    if (!workspace.storage->save_profile(*loaded) || GrantQtAchievement(workspace, id, "Blocked", skill, 10, 0).isEmpty()) return false;
    return true;
}

static bool TestAchievementFiltersUi() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    workspace.catalog.add_skill("Modeling", 1.0, "Geometry");
    const auto skill = *workspace.catalog.id_for_name("Modeling");
    Profile profile("Achievement filters");
    const auto created = workspace.storage->create_profile(profile);
    if (!created) return false;
    const auto profilePath = temp.path() + "/" + QString::fromStdString(created->id) + ".ini";
    const auto achievementPath = temp.path() + "/achievements/" + QString::fromStdString(created->id) + ".json";
    QFile profileFile(profilePath);
    if (!profileFile.open(QIODevice::ReadOnly)) return false;
    const auto profileBytes = profileFile.readAll(); profileFile.close();
    for (const auto& title : {QStringLiteral("Soon Badge"), QStringLiteral("Permanent Badge"), QStringLiteral("Expired Badge")})
        if (!GrantQtAchievement(workspace, created->id, title, skill, 10, title == "Soon Badge" ? 2 : 0).isEmpty()) return false;
    QFile achievements(achievementPath);
    if (!achievements.open(QIODevice::ReadOnly)) return false;
    const auto originalBytes = achievements.readAll(); achievements.close();
    const auto json = QJsonDocument::fromJson(originalBytes.startsWith("\xEF\xBB\xBF") ? originalBytes.mid(3) : originalBytes);
    if (!json.isArray() || json.array().size() != 3) return false;
    auto records = json.array();
    auto expired = records[2].toObject();
    const auto now = QDateTime::currentSecsSinceEpoch();
    expired["awarded"] = qint64(now - 2 * 86400);
    expired["expires"] = qint64(now - 60);
    expired["durationDays"] = 1;
    records[2] = expired;
    QSaveFile corrected(achievementPath);
    const auto correctedBytes = QByteArray("\xEF\xBB\xBF") + QJsonDocument(records).toJson();
    if (!corrected.open(QIODevice::WriteOnly) || corrected.write(correctedBytes) != correctedBytes.size() || !corrected.commit()) return false;
    QFile saved(achievementPath);
    if (!saved.open(QIODevice::ReadOnly)) return false;
    const auto storedAchievements = saved.readAll(); saved.close();
    bool checks = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog ? dialog->findChild<QTableWidget*>("achievementRecords") : nullptr;
        auto* search = dialog ? dialog->findChild<QLineEdit*>("achievementSearch") : nullptr;
        auto* showExpired = dialog ? dialog->findChild<QCheckBox*>("showExpiredAchievements") : nullptr;
        auto* reset = dialog ? dialog->findChild<QPushButton*>("resetAchievementFilters") : nullptr;
        auto* notice = dialog ? dialog->findChild<QLabel*>("achievementsNotice") : nullptr;
        auto* expiring = dialog ? dialog->findChild<QLabel*>("achievementsExpiringSoon") : nullptr;
        checks = dialog && table && search && showExpired && reset && notice && expiring && table->rowCount() == 3 &&
            showExpired->isChecked() && !table->isRowHidden(0) && !table->isRowHidden(1) && !table->isRowHidden(2) &&
            notice->text().contains(QString::fromUtf8("активных: 2")) && notice->text().contains(QString::fromUtf8("истекло: 1")) &&
            expiring->isVisible() && expiring->text().contains(QString::fromUtf8("Soon Badge")) &&
            !search->accessibleName().isEmpty() && !showExpired->accessibleName().isEmpty() && !reset->accessibleName().isEmpty();
        if (!dialog) return;
        auto* edit = dialog->findChild<QPushButton*>("editAchievement");
        auto* revoke = dialog->findChild<QPushButton*>("revokeAchievement");
        table->selectRow(2); QApplication::processEvents();
        checks &= edit && revoke && edit->isEnabled() && revoke->isEnabled();
        search->setText("Expired"); QApplication::processEvents();
        checks &= table->isRowHidden(0) && table->isRowHidden(1) && !table->isRowHidden(2) && notice->text().contains("Показано: 1");
        showExpired->setChecked(false); QApplication::processEvents();
        checks &= table->isRowHidden(0) && table->isRowHidden(1) && table->isRowHidden(2) && notice->text().contains("Показано: 0") &&
            table->currentRow() == -1 && !edit->isEnabled() && !revoke->isEnabled();
        search->clear(); QApplication::processEvents();
        checks &= !table->isRowHidden(0) && !table->isRowHidden(1) && table->isRowHidden(2);
        reset->click(); QApplication::processEvents();
        checks &= showExpired->isChecked() && search->text().isEmpty() &&
            !table->isRowHidden(0) && !table->isRowHidden(1) && !table->isRowHidden(2);
        search->setText("no matching achievement"); QApplication::processEvents();
        checks &= table->isRowHidden(0) && table->isRowHidden(1) && table->isRowHidden(2) && notice->text().contains("Показано: 0");
        reset->click(); QApplication::processEvents();
        dialog->reject();
    });
    ShowAchievements(nullptr, workspace, created->id, true);
    QFile afterProfile(profilePath); if (!afterProfile.open(QIODevice::ReadOnly)) return false;
    QFile afterAchievements(achievementPath); if (!afterAchievements.open(QIODevice::ReadOnly)) return false;
    return checks && afterProfile.readAll() == profileBytes && afterAchievements.readAll() == storedAchievements;
}

static bool TestProfileSession() {
    const char* phase = "setup";
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    Profile profile("Session profile");
    profile.set_password_encoded(EncodePassword("secret"));
    const auto created = workspace.storage->create_profile(profile);
    if (!created) return false;
    QtProfileSession session(workspace.directory);
    AppSetProfileAuditFailureHookForTests(true);
    const bool auditBlockedUnlock = session.unlock(*workspace.storage, created->id, "secret");
    AppSetProfileAuditFailureHookForTests(false);
    if (auditBlockedUnlock || session.isUnlocked(*workspace.storage, created->id)) { std::cerr << "session: auditBlockedUnlock\n"; return false; }
    if (session.unlock(*workspace.storage, created->id, "wrong") ||
        !session.unlock(*workspace.storage, created->id, "secret") ||
        !session.isUnlocked(*workspace.storage, created->id)) { std::cerr << "session: basic unlock\n"; return false; }
    QtProfileSession fresh(workspace.directory);
    if (fresh.isUnlocked(*workspace.storage, created->id)) return false;
    QFile ui(temp.path() + "/meta/ui.ini");
    if (!ui.open(QIODevice::ReadOnly) && !ui.open(QIODevice::WriteOnly)) { std::cerr << "session: ui before trusted\n"; return false; }
    if (ui.isOpen() && ui.size() == 0 && (ui.openMode() & QIODevice::WriteOnly)) ui.close();
    if (!ui.open(QIODevice::ReadOnly)) { std::cerr << "session: ui read before trusted\n"; return false; }
    const auto beforeTrustedUnlock = ui.readAll(); ui.close();
    QFile audit(temp.path() + "/meta/profile-audit.log"); if (!audit.open(QIODevice::ReadOnly)) { std::cerr << "session: audit before trusted\n"; return false; }
    const auto beforeTrustedAudit = audit.readAll(); audit.close();
    AppSetProfileAuditFailureHookForTests(true);
    const bool auditBlockedTrustedUnlock = session.unlock(*workspace.storage, created->id, "secret", 30);
    AppSetProfileAuditFailureHookForTests(false);
    if (auditBlockedTrustedUnlock || session.isUnlocked(*workspace.storage, created->id)) { std::cerr << "session: trusted unlock failure\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) { std::cerr << "session: ui after trusted fail\n"; return false; }
    const auto afterTrustedUnlock = ui.readAll(); ui.close();
    if (afterTrustedUnlock != beforeTrustedUnlock || !audit.open(QIODevice::ReadOnly)) return false;
    const auto afterTrustedAudit = audit.readAll(); audit.close();
    if (afterTrustedAudit != beforeTrustedAudit || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) { std::cerr << "session: trusted rollback equal=" << (afterTrustedAudit == beforeTrustedAudit) << " journal=" << std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction") << "\n"; return false; }
    phase = "trusted login";
    if (!session.unlock(*workspace.storage, created->id, "secret", 30) || !session.isTrusted() || session.trustedUntil() <= QDateTime::currentSecsSinceEpoch()) { std::cerr << "session phase: " << phase << "\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto trustBytes = ui.readAll(); ui.close();
    if (!trustBytes.contains("trusted=" + QByteArray::fromStdString(created->id) + ":")) return false;
    auto externallyExpiredBytes = trustBytes;
    const QByteArray trustToken = QByteArray::fromStdString(created->id) + ':';
    const auto expiryStart = externallyExpiredBytes.indexOf(trustToken);
    if (expiryStart < 0) { std::cerr << "session: active trust entry missing before simulated external expiry\n"; return false; }
    const auto valueStart = expiryStart + trustToken.size();
    auto valueEnd = valueStart;
    while (valueEnd < externallyExpiredBytes.size() && externallyExpiredBytes[valueEnd] >= '0' && externallyExpiredBytes[valueEnd] <= '9') ++valueEnd;
    if (valueEnd == valueStart) { std::cerr << "session: malformed active trust expiry\n"; return false; }
    externallyExpiredBytes.replace(valueStart, valueEnd - valueStart, "1");
    QSaveFile externalTrustChange(temp.path() + "/meta/ui.ini");
    if (!externalTrustChange.open(QIODevice::WriteOnly) || externalTrustChange.write(externallyExpiredBytes) != externallyExpiredBytes.size() ||
        !externalTrustChange.commit()) { std::cerr << "session: could not write simulated external expiry\n"; return false; }
    if (!audit.open(QIODevice::ReadOnly)) { std::cerr << "session: could not read audit before external expiry\n"; return false; }
    const auto auditBeforeExternalExpiry = audit.readAll(); audit.close();
    AppSetProfileAuditFailureHookForTests(true);
    const bool remainedUnlockedAfterExternalExpiry = session.isUnlocked(*workspace.storage, created->id);
    AppSetProfileAuditFailureHookForTests(false);
    if (remainedUnlockedAfterExternalExpiry || session.isTrusted()) { std::cerr << "session: active trusted session survived external expiry\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) { std::cerr << "session: could not read ui after failed expiry audit\n"; return false; }
    const auto trustAfterFailedExternalExpiry = ui.readAll(); ui.close();
    if (trustAfterFailedExternalExpiry != externallyExpiredBytes || !audit.open(QIODevice::ReadOnly)) { std::cerr << "session: failed external expiry was not rolled back exactly\n"; return false; }
    const auto auditAfterFailedExternalExpiry = audit.readAll(); audit.close();
    if (auditAfterFailedExternalExpiry != auditBeforeExternalExpiry) { std::cerr << "session: failed external expiry changed audit bytes\n"; return false; }
    if (session.isUnlocked(*workspace.storage, created->id)) { std::cerr << "session: externally expired trust restored\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) { std::cerr << "session: could not read ui after retrying expiry\n"; return false; }
    const auto trustAfterExternalExpiry = ui.readAll(); ui.close();
    if (trustAfterExternalExpiry.contains(trustToken + "1") || !audit.open(QIODevice::ReadOnly)) { std::cerr << "session: expired persistent trust was not pruned\n"; return false; }
    const auto auditAfterExternalExpiry = audit.readAll(); audit.close();
    if (!auditAfterExternalExpiry.contains("|trust_expired")) { std::cerr << "session: external expiry audit missing\n"; return false; }
    if (!session.unlock(*workspace.storage, created->id, "secret", 30)) { std::cerr << "session: relogin after external expiry failed\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) return false;
    auto externallyRevokedBytes = ui.readAll(); ui.close();
    const auto revokeStart = externallyRevokedBytes.indexOf(trustToken);
    if (revokeStart < 0) { std::cerr << "session: active trust entry missing before simulated external revocation\n"; return false; }
    auto revokeEnd = revokeStart + trustToken.size();
    while (revokeEnd < externallyRevokedBytes.size() && externallyRevokedBytes[revokeEnd] >= '0' && externallyRevokedBytes[revokeEnd] <= '9') ++revokeEnd;
    if (revokeEnd == revokeStart + trustToken.size()) return false;
    externallyRevokedBytes.remove(revokeStart, revokeEnd - revokeStart);
    QSaveFile externalRevokeChange(temp.path() + "/meta/ui.ini");
    if (!externalRevokeChange.open(QIODevice::WriteOnly) || externalRevokeChange.write(externallyRevokedBytes) != externallyRevokedBytes.size() ||
        !externalRevokeChange.commit()) { std::cerr << "session: could not write simulated external revocation\n"; return false; }
    if (session.isUnlocked(*workspace.storage, created->id) || session.isTrusted()) {
        std::cerr << "session: active trusted session survived external revocation\n"; return false;
    }
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto trustAfterExternalRevocation = ui.readAll(); ui.close();
    if (trustAfterExternalRevocation != externallyRevokedBytes || !audit.open(QIODevice::ReadOnly)) {
        std::cerr << "session: external revocation was overwritten\n"; return false;
    }
    const auto auditAfterExternalRevocation = audit.readAll(); audit.close();
    if (!auditAfterExternalRevocation.contains("|trust_session_invalidated|persistent_trust_changed")) {
        std::cerr << "session: external revocation audit missing\n"; return false;
    }
    QtProfileSession afterExternalRevocation(workspace.directory);
    if (afterExternalRevocation.isUnlocked(*workspace.storage, created->id) ||
        !session.unlock(*workspace.storage, created->id, "secret", 30)) return false;
    const auto profileBeforeRevocation = profile;
    profile.set_blocked(true);
    if (!workspace.storage->save_profile(profile)) return false;
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto trustBeforeRevocation = ui.readAll(); ui.close();
    if (!audit.open(QIODevice::ReadOnly)) return false;
    const auto auditBeforeRevocation = audit.readAll(); audit.close();
    QtProfileSession unavailable(workspace.directory);
    AppSetProfileAuditFailureHookForTests(true);
    const bool incorrectlyRestored = unavailable.isUnlocked(*workspace.storage, created->id);
    AppSetProfileAuditFailureHookForTests(false);
    if (incorrectlyRestored || unavailable.isTrusted()) return false;
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto trustAfterFailedRevocation = ui.readAll(); ui.close();
    if (!audit.open(QIODevice::ReadOnly)) return false;
    const auto auditAfterFailedRevocation = audit.readAll(); audit.close();
    if (trustAfterFailedRevocation != trustBeforeRevocation || auditAfterFailedRevocation != auditBeforeRevocation) return false;
    if (unavailable.isUnlocked(*workspace.storage, created->id)) return false;
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto trustAfterRevocation = ui.readAll(); ui.close();
    if (trustAfterRevocation.contains("trusted=" + QByteArray::fromStdString(created->id) + ":")) return false;
    if (!audit.open(QIODevice::ReadOnly)) return false;
    const auto auditAfterRevocation = audit.readAll(); audit.close();
    if (!auditAfterRevocation.contains("|trust_revoked|profile_unavailable")) return false;
    profile = profileBeforeRevocation;
    if (!workspace.storage->save_profile(profile) || !session.unlock(*workspace.storage, created->id, "secret", 30)) return false;
    QtProfileSession restored(workspace.directory);
    AppSetProfileAuditFailureHookForTests(true);
    const bool auditBlockedTrustedRestore = restored.isUnlocked(*workspace.storage, created->id);
    AppSetProfileAuditFailureHookForTests(false);
    phase = "audit failed trusted restore";
    if (auditBlockedTrustedRestore || restored.isTrusted()) { std::cerr << "session phase: " << phase << "\n"; return false; }
    phase = "trusted restore";
    if (!restored.isUnlocked(*workspace.storage, created->id) || !restored.isTrusted()) { std::cerr << "session phase: " << phase << "\n"; return false; }
    AppSetProfileAuditFailureHookForTests(true);
    const bool auditedLogout = restored.lock(true);
    AppSetProfileAuditFailureHookForTests(false);
    phase = "audited logout";
    if (auditedLogout || restored.isTrusted() || restored.isUnlocked(*workspace.storage, created->id)) { std::cerr << "session phase: " << phase << "\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto loggedOutBytes = ui.readAll(); ui.close();
    if (loggedOutBytes.contains("trusted=" + QByteArray::fromStdString(created->id) + ":")) return false;
    phase = "second trusted login";
    if (!session.unlock(*workspace.storage, created->id, "secret", 30)) { std::cerr << "session phase: " << phase << "\n"; return false; }
    QtProfileSession restoredNormally(workspace.directory);
    phase = "normal trusted logout";
    if (!restoredNormally.isUnlocked(*workspace.storage, created->id) || !restoredNormally.isTrusted() || !restoredNormally.lock(true)) { std::cerr << "session phase: " << phase << "\n"; return false; }
    QtProfileSession forgotten(workspace.directory);
    if (forgotten.isUnlocked(*workspace.storage, created->id)) return false;
    if (!ui.open(QIODevice::ReadWrite)) return false;
    auto expiredBytes = ui.readAll();
    expiredBytes.replace("trusted=", "trusted=" + QByteArray::fromStdString(created->id) + ":1");
    if (!ui.resize(0) || !ui.seek(0) || ui.write(expiredBytes) != expiredBytes.size()) return false;
    ui.close();
    QtProfileSession expired(workspace.directory);
    const auto expiryBeforeFailure = expiredBytes;
    phase = "expiry restore";
    AppSetProfileAuditFailureHookForTests(true);
    const bool blockedExpiryRestore = expired.isUnlocked(*workspace.storage, created->id);
    AppSetProfileAuditFailureHookForTests(false);
    if (blockedExpiryRestore) { std::cerr << "session phase: " << phase << "\n"; return false; }
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto unchangedExpiryBytes = ui.readAll(); ui.close();
    if (unchangedExpiryBytes != expiryBeforeFailure) return false;
    QtProfileSession expiredRetry(workspace.directory);
    if (expiredRetry.isUnlocked(*workspace.storage, created->id)) return false;
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto prunedBytes = ui.readAll(); ui.close();
    if (prunedBytes.contains(QByteArray::fromStdString(created->id) + ":1")) return false;
#ifdef _WIN32
    const auto trustPath = (workspace.directory / "meta/ui.ini").wstring();
    const auto trustLock = CreateFileW(trustPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (trustLock == INVALID_HANDLE_VALUE) return false;
    QtProfileSession writeBlocked(workspace.directory);
    const bool blockedUnlock = writeBlocked.unlock(*workspace.storage, created->id, "secret", 30);
    CloseHandle(trustLock);
    if (blockedUnlock || writeBlocked.isUnlocked(*workspace.storage, created->id)) return false;
    if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto afterBlockedBytes = ui.readAll(); ui.close();
    if (afterBlockedBytes != prunedBytes) return false;
#endif
    if (!audit.open(QIODevice::ReadOnly)) return false;
    const auto auditBytes = audit.readAll();
    if (!auditBytes.contains("|unlock|trust_days=30") || !auditBytes.contains("|trusted_unlock") || !auditBytes.contains("|lock")) return false;
    if (session.isUnlocked(*workspace.storage, "other") || session.isUnlocked(*workspace.storage, created->id)) return false;
    if (!session.unlock(*workspace.storage, created->id, "secret")) { std::cerr << "session: relogin before password change\n"; return false; }
    profile.set_password_encoded(EncodePassword("changed"));
    if (!workspace.storage->save_profile(profile) || session.isUnlocked(*workspace.storage, created->id)) { std::cerr << "session: password invalidation\n"; return false; }
    if (!session.unlock(*workspace.storage, created->id, "changed")) { std::cerr << "session: relogin after password change\n"; return false; }
    profile.set_blocked(true);
    if (!workspace.storage->save_profile(profile) || session.isUnlocked(*workspace.storage, created->id) ||
        session.unlock(*workspace.storage, created->id, "changed")) { std::cerr << "session: blocked invalidation\n"; return false; }
    profile.set_blocked(false);
    if (!workspace.storage->save_profile(profile) || !session.unlock(*workspace.storage, created->id, "changed")) { std::cerr << "session: unblock login\n"; return false; }
    if (!workspace.storage->set_archived(created->id, true) || session.isUnlocked(*workspace.storage, created->id) ||
        session.unlock(*workspace.storage, created->id, "changed")) { std::cerr << "session: archived invalidation\n"; return false; }
    if (!workspace.storage->set_archived(created->id, false) || !workspace.storage->set_active_profile(created->id)) return false;
    profile.set_password_encoded("");
    if (!workspace.storage->save_profile(profile) || session.unlock(*workspace.storage, created->id, "")) return false;
    return true;
}

static bool TestPipelineTransition() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    PipelineStep first, second, third;
    first.id = "first"; first.title = "First"; first.nextIds = {"second", "missing", "second", "first"};
    first.doneCriteria = "Check geometry";
    second.id = "second"; second.title = "Second";
    third.id = "third"; third.title = "Unlinked";
    workspace.data.pipelineSteps = {first, second, third};
    TaskEntry task;
    task.id = "transition-task"; task.title = "Pipeline task"; task.description = "Description";
    task.pipelineStepId = first.id; task.pipelineStep = first.title;
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, task, "test", &workspace.data.taskAudit).ok) return false;
    auto advance = [&](const std::string& from, const std::string& to) {
        return AdvanceTaskPipeline(workspace.directory, workspace.data.tasks, workspace.data.taskAudit,
            workspace.data.pipelineSteps, task.id, from, to, "test");
    };
    if (advance("first", "third").ok || advance("first", "missing").ok ||
        advance("first", "first").ok || advance("stale", "second").ok) return false;
    auto bytes = [&](const char* name) { QFile f(temp.path() + "/meta/" + name); f.open(QIODevice::ReadOnly); return f.readAll(); };
    const auto taskBytes = bytes("tasks.json"), auditBytes = bytes("task-audit.log");
    QTimer::singleShot(0, [] { qobject_cast<QDialog*>(QApplication::activeModalWidget())->reject(); });
    if (ShowPipelineTransition(nullptr, workspace, task.id) || bytes("tasks.json") != taskBytes) return false;
    AppSetRecoveryPrimaryWriteFailureForTests(true);
    auto failed = advance("first", "second");
    AppSetRecoveryPrimaryWriteFailureForTests(false);
    if (failed.ok || bytes("tasks.json") != taskBytes || bytes("task-audit.log") != auditBytes) return false;
    bool checks = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* choices = dialog->findChild<QComboBox*>("nextStage");
        auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        checks = choices->count() == 1 && choices->currentData().toString() == "second" && !save->isEnabled();
        dialog->findChild<QCheckBox*>("stageReady")->setChecked(true);
        checks &= save->isEnabled();
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts); dialog->resize(480, 360); QApplication::processEvents();
            dialog->grab().save(artifacts + "/pipeline-transition.png");
        }
        save->click();
    });
    if (!ShowPipelineTransition(nullptr, workspace, task.id) || !checks) return false;
    const auto disk = LoadTasksData(workspace.directory).front();
    if (disk.pipelineStepId != "second" || disk.status != task.status || disk.id != task.id ||
        workspace.data.taskAudit.back().field != "pipeline") return false;
    if (advance("first", "second").ok || advance("second", "third").ok) return false;
    workspace.data.tasks.front().pipelineStepId = "first";
    workspace.data.tasks.front().status = 2;
    if (advance("first", "second").ok) return false;
    return true;
}

static bool TestPipelineEditor() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    PipelineStep step;
    step.id = "custom-editor-fixture";
    step.title = "Original stage";
    step.nextIds = {"missing-stage"};
    step.hints = {"First hint", "Second hint"};
    step.legacyNotes = "Historical note";
    workspace.data.pipelineSteps = {step};
    if (!AppSavePipelineData(workspace.directory, workspace.data.pipelineSteps)) return false;
    auto read = [&] { QFile file(temp.path() + "/meta/pipeline.json"); file.open(QIODevice::ReadOnly); return file.readAll(); };
    const auto before = read();
    QTimer::singleShot(0, [] { qobject_cast<QDialog*>(QApplication::activeModalWidget())->reject(); });
    if (ShowPipelineEditor(nullptr, workspace) || read() != before) return false;
    bool checked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* title = dialog->findChild<QLineEdit*>("stageTitle");
        auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        title->clear();
        save->click();
        checked = !dialog->findChild<QLabel*>("pipelineNotice")->text().isEmpty() && read() == before;
        title->setText(QString::fromUtf8("Проверка геометрии"));
        dialog->findChild<QPlainTextEdit*>("stageDone")->setPlainText(QString::fromUtf8("Нет самопересечений\nМасштаб проверен"));
        AppSetRecoveryPrimaryWriteFailureForTests(true);
        save->click();
        AppSetRecoveryPrimaryWriteFailureForTests(false);
        checked &= read() == before && workspace.data.pipelineSteps.front().title == step.title;
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            dialog->findChild<QLabel*>("pipelineNotice")->clear();
            dialog->resize(520, 440);
            for (int i = 0; i < 4; ++i) {
                dialog->findChild<QTabWidget*>("pipelineTabs")->setCurrentIndex(i);
                QApplication::processEvents();
                dialog->grab().save(artifacts + QString("/pipeline-tab-%1.png").arg(i));
            }
        }
        save->click();
    });
    if (!ShowPipelineEditor(nullptr, workspace, step.id) || !checked) return false;
    workspace.reload();
    const auto updated = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& value) { return value.id == step.id; });
    if (updated == workspace.data.pipelineSteps.end() || updated->title != u8"Проверка геометрии" ||
        updated->nextIds != step.nextIds || updated->hints != step.hints || updated->legacyNotes != step.legacyNotes ||
        updated->doneCriteria != u8"Нет самопересечений\nМасштаб проверен") return false;
    const auto count = workspace.data.pipelineSteps.size();
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        dialog->findChild<QLineEdit*>("stageTitle")->setText("Created stage");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    if (!ShowPipelineEditor(nullptr, workspace) || workspace.data.pipelineSteps.size() != count + 1) return false;
    return true;
}

static bool TestTaskEditorTransaction() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    TaskEntry original;
    original.id = "edit-fixture";
    original.title = "Original";
    original.description = "Original description";
    original.createdAt = 123;
    original.assignees = {"legacy-profile"};
    original.skillIds = {"legacy-skill"};
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, original, "test", &workspace.data.taskAudit).ok) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    std::vector<std::pair<AppLogLevel, std::string>> taskEvents;
    const auto observeTaskEvent = [&](AppLogLevel level, const std::string& message) { taskEvents.emplace_back(level, message); };
    const auto hasTaskEvent = [&](AppLogLevel level, const std::string& message) {
        return std::any_of(taskEvents.begin(), taskEvents.end(), [&](const auto& event) {
            return event.first == level && event.second == message;
        });
    };
    const std::vector<std::string> files = {"meta/tasks.json", "meta/task-audit.log", "meta/updates/tasks.last-good.json"};
    auto read = [&](const std::string& name) {
        QFile f(temp.path() + "/" + QString::fromStdString(name));
        f.open(QIODevice::ReadOnly); return f.readAll();
    };
    std::vector<QByteArray> before;
    for (const auto& file : files) before.push_back(read(file));
    TaskEntry createCandidate;
    createCandidate.id = "create-rollback-fixture";
    createCandidate.title = "Must roll back";
    AppSetTaskAuditFailureHookForTests(true);
    const auto failedCreate = CreateTaskWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, createCandidate, "test", observeTaskEvent);
    AppSetTaskAuditFailureHookForTests(false);
    if (failedCreate.ok || !hasTaskEvent(AppLogLevel::Warning, "Task creation failed or rolled back") || workspace.data.tasks.size() != 1 ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) return false;
    AppSetTaskAuditFailureHookForTests(true);
    AppLogLevel failedStatusLogLevel = AppLogLevel::Info;
    std::string failedStatusLogMessage;
    const auto failedStatus = UpdateTaskStatusWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, original.id, 1, "test",
        [&](AppLogLevel level, const std::string& message) { failedStatusLogLevel = level; failedStatusLogMessage = message; });
    AppSetTaskAuditFailureHookForTests(false);
    if (failedStatus.ok || workspace.data.tasks.front().status != 0 ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction") ||
        failedStatusLogLevel != AppLogLevel::Warning ||
        failedStatusLogMessage != "Task status update failed or was rolled back") return false;
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) return false;
    AppSetTaskAuditFailureHookForTests(true);
    const auto failedDelete = DeleteTaskWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, original.id, "test", observeTaskEvent);
    AppSetTaskAuditFailureHookForTests(false);
    if (failedDelete.ok || !hasTaskEvent(AppLogLevel::Warning, "Task deletion failed or was rolled back") ||
        workspace.data.tasks.size() != 1 || workspace.data.tasks.front().id != original.id ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) return false;
    auto duplicateTasks = std::vector<TaskEntry>{original, original};
    auto duplicateAudit = workspace.data.taskAudit;
    if (DeleteTaskWithRecovery(workspace.directory, duplicateTasks, duplicateAudit, original.id, "test").ok ||
        duplicateTasks.size() != 2 || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    TaskEntry draft = original;
    draft.title = "Updated";
    draft.description = "Updated description";
    draft.priority = 2;
    draft.pipelineStepId = "stage-new";
    draft.pipelineStep = "New stage";
    draft.deadlineAt = 1900000000;
    auto edit = [&] { return EditTaskDetails(workspace.directory, workspace.data.tasks, workspace.data.taskAudit, draft, "test", observeTaskEvent); };
    AppSetRecoveryPrimaryWriteFailureForTests(true);
    const auto failedWrite = edit();
    AppSetRecoveryPrimaryWriteFailureForTests(false);
    if (failedWrite.ok || !hasTaskEvent(AppLogLevel::Warning, "Task edit failed or rolled back")) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
#ifdef _WIN32
    // Allow the journal to read the audit, but deny actual append and rollback writes.
    const auto auditPath = workspace.directory / "meta/task-audit.log";
    const auto lock = CreateFileW(auditPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    const auto failedAudit = edit();
    CloseHandle(lock);
    if (failedAudit.ok || workspace.data.tasks.front().title != original.title) return false;
    workspace.reload(); // Retry the interrupted rollback now that the file is unlocked.
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) return false;
#endif
    // Fail after several successful mutations, not just at the first write.
    draft.assignees.clear();
    if (edit().ok) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    draft.assignees = original.assignees;
    if (!edit().ok || workspace.data.tasks.front().createdAt != 123 ||
        workspace.data.tasks.front().pipelineStepId != draft.pipelineStepId) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    const auto successfulStatus = UpdateTaskStatusWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, original.id, 1, "test",
        [](AppLogLevel, const std::string&) { throw std::runtime_error("observer must be isolated"); });
    if (!successfulStatus.ok || workspace.data.tasks.front().status != 1) return false;
    // Simulate interrupted metadata editing and exercise the startup recovery format.
    const auto journal = workspace.directory / "meta/qt-xp-transaction";
    std::filesystem::create_directories(journal);
    std::vector<QByteArray> interruptedImages;
    {
        for (const auto& file : files) interruptedImages.push_back(read(file));
        std::ofstream manifest(journal / "manifest");
        manifest << "FORGEMIRROR_QT_TASK_EDIT_1 3\n";
        for (size_t i = 0; i < files.size(); ++i) {
            std::filesystem::create_directories((journal / files[i]).parent_path());
            std::ofstream out(journal / files[i], std::ios::binary);
            out.write(before[i].constData(), before[i].size());
            manifest << std::quoted(files[i]) << " 1\n";
        }
    }
    workspace.reload();
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    if (!workspace.transactionRecoveryNotice) return false;
    if (workspace.transactionRecoveryPreservedFiles.empty()) return false;
    bool preservedInterruptedBytes = false;
    for (size_t i = 0; i < files.size(); ++i) {
        if (interruptedImages[i] == before[i]) continue;
        QFile preserved(QString::fromStdWString((workspace.transactionRecoveryPreservedFiles / files[i]).wstring()));
        if (!preserved.open(QIODevice::ReadOnly) || preserved.readAll() != interruptedImages[i]) return false;
        preservedInterruptedBytes = true;
    }
    if (!preservedInterruptedBytes) return false;
    QtWindow recoveryWindow(workspace);
    QFile recoveryLog(temp.path() + "/meta/qt-application-log.json");
    if (workspace.transactionRecoveryNotice || !recoveryLog.open(QIODevice::ReadOnly)) return false;
    const auto recoveryBytes = recoveryLog.readAll();
    if (!recoveryBytes.contains("CoreTransactionRecovery") || !recoveryBytes.contains("interrupted local transaction") ||
        !recoveryBytes.contains(workspace.transactionRecoveryPreservedFiles.filename().string().c_str())) return false;
    recoveryLog.close();
    {
        PrepareProjectDeletionRecovery(workspace.directory);
        { std::ofstream interrupted(workspace.directory / "meta/tasks.json", std::ios::binary | std::ios::trunc);
          interrupted << "interrupted-project-transaction"; }
        QtWorkspace constructorRecovery(workspace.directory);
        const auto preservedDirectory = constructorRecovery.transactionRecoveryPreservedFiles;
        QFile preservedTask(QString::fromStdWString((preservedDirectory / "meta/tasks.json").wstring()));
        if (!constructorRecovery.transactionRecoveryNotice || preservedDirectory.empty() ||
            !preservedTask.open(QIODevice::ReadOnly) || preservedTask.readAll() != "interrupted-project-transaction") return false;
        QtWindow constructorRecoveryWindow(constructorRecovery);
        QFile constructorRecoveryLog(temp.path() + "/meta/qt-application-log.json");
        if (!constructorRecoveryLog.open(QIODevice::ReadOnly) ||
            !constructorRecoveryLog.readAll().contains(preservedDirectory.filename().string().c_str())) return false;
    }
    auto& awarded = workspace.data.tasks.front();
    awarded.participants.push_back({"legacy-profile", 100, 77, 22, "snapshot"});
    awarded.status = 2;
    awarded.score = 9;
    if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    const auto blockedDelete = DeleteTaskWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, awarded.id, "test");
    if (blockedDelete.ok || blockedDelete.errorMessage.find(u8"контекстом профилей") == std::string::npos ||
        workspace.data.tasks.size() != 1 ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    draft = awarded;
    draft.category = 2;
    if (edit().ok) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    draft = awarded;
    draft.title = "Corrected title";
    // The service ignores caller-supplied protected fields even outside the UI.
    draft.status = 0;
    draft.score = 0;
    draft.participants.clear();
    if (!edit().ok) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    const auto disk = LoadTasksData(workspace.directory).front();
    if (disk.status != 2 || disk.score != 9 || disk.participants.size() != 1 ||
        disk.participants.front().rollbackSnapshot != "snapshot" || disk.participants.front().globalXp != 77 ||
        !hasTaskEvent(AppLogLevel::Info, "Task edit committed")) return false;
    TaskEntry removable;
    removable.id = "delete-telemetry-fixture";
    removable.title = "Disposable delete telemetry task";
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, removable, "test", &workspace.data.taskAudit).ok) return false;
    std::string committedDeleteEvent;
    const auto deleted = DeleteTaskWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, removable.id, "test",
        [&](AppLogLevel level, const std::string& event) {
            if (level == AppLogLevel::Info) committedDeleteEvent = event;
        });
    return deleted.ok && committedDeleteEvent == "Task deletion committed" &&
        std::none_of(workspace.data.tasks.begin(), workspace.data.tasks.end(), [&](const auto& item) { return item.id == removable.id; });
}

static bool TestReportExport() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    TeamValueReport report;
    report.totalTasks = 1;
    TeamValueProjectMetric project; project.id = "project"; project.name = u8"Проект, «Фарос»"; project.totalTasks = 1;
    report.projects.push_back(project);
    const auto path = temp.path() + "/report.csv";
    QString error;
    if (!ExportTeamValueReportCsv(path, report, &error) || !error.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto bytes = file.readAll();
    if (!bytes.startsWith("\xEF\xBB\xBF") || !bytes.contains(QString::fromUtf8("Проект, «Фарос»").toUtf8())) return false;
    TeamValueReport previous; previous.totalTasks = 7;
    TeamValueProjectMetric previousProject; previousProject.id = "old"; previousProject.name = u8"Старый проект"; previousProject.totalTasks = 7;
    previous.projects.push_back(previousProject);
    const auto comparisonPath = temp.path() + "/comparison.csv";
    if (!ExportTeamValueReportComparisonCsv(comparisonPath, report, QString::fromUtf8("Текущий диапазон"),
        previous, QString::fromUtf8("Предыдущий диапазон"), &error) || !error.isEmpty()) return false;
    QFile comparison(comparisonPath);
    if (!comparison.open(QIODevice::ReadOnly)) return false;
    const auto comparisonBytes = comparison.readAll();
    if (!comparisonBytes.contains(QString::fromUtf8("ComparisonPeriod,Текущий период,Текущий диапазон").toUtf8()) ||
        !comparisonBytes.contains(QString::fromUtf8("ComparisonPeriod,Предыдущий период,Предыдущий диапазон").toUtf8()) ||
        !comparisonBytes.contains(QString::fromUtf8("Старый проект").toUtf8())) return false;
    if (ExportTeamValueReportCsv(QString(), report, &error) || error.isEmpty() ||
        ExportTeamValueReportCsv(temp.path(), report, &error)) return false;
#ifdef _WIN32
    const auto wide = std::filesystem::u8path(path.toStdString()).wstring();
    const auto lock = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    const bool replaced = ExportTeamValueReportCsv(path, report, &error);
    CloseHandle(lock);
    if (replaced) return false;
#endif
    return true;
}

static bool TestProfileReportExport() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    SkillCatalog catalog(std::filesystem::path(temp.path().toStdWString()));
    Profile profile;
    profile.set_name(u8"Анна, тест");
    profile.grant_global_xp(1250);
    profile.set_last_task_timestamp(1700000000);
    profile.start_penalty_recovery(3);
    profile.set_category_best_scores({1, 2, 3, 4, 5});
    Skill skill("cpp", 3);
    skill.xp = 17;
    skill.xpToNext = 83;
    skill.weight = 1.25;
    profile.set_skills({skill});
    QString error;
    const auto csvPath = temp.path() + "/profile.csv";
    if (!ExportProfileReport(csvPath, profile, catalog, QStringLiteral("profile-1"), true, 1700001000, &error) || !error.isEmpty()) return false;
    QFile csvFile(csvPath);
    if (!csvFile.open(QIODevice::ReadOnly)) return false;
    const QByteArray csvBytes = csvFile.readAll();
    if (!csvBytes.startsWith("\xEF\xBB\xBF") || !csvBytes.contains(QString::fromUtf8("\"Анна, тест\"").toUtf8()) ||
        !csvBytes.contains("Summary,TotalXP,") || !csvBytes.contains("Categories,") ||
        !csvBytes.contains("cpp,cpp,3,17,83,") || !csvBytes.contains("1.25")) return false;
    const auto txtPath = temp.path() + "/profile.txt";
    if (!ExportProfileReport(txtPath, profile, catalog, QStringLiteral("profile-1"), false, 1700001000, &error) || !error.isEmpty()) return false;
    QFile txtFile(txtPath);
    if (!txtFile.open(QIODevice::ReadOnly)) return false;
    const QByteArray txtBytes = txtFile.readAll();
    if (!txtBytes.startsWith("\xEF\xBB\xBF") || !txtBytes.contains(QString::fromUtf8("Отчёт профиля").toUtf8()) ||
        !txtBytes.contains(QString::fromUtf8("Имя: Анна, тест").toUtf8()) || !txtBytes.contains("Прогрев:") ||
        !txtBytes.contains("cpp\tcpp\t3\t17/83")) return false;
    if (ExportProfileReport(QString(), profile, catalog, QStringLiteral("profile-1"), true, 1700001000, &error) || error.isEmpty()) return false;
    return true;
}

static bool TestQtStorageHealthReport() {
    auto fail = [](const char* where) { std::cerr << "storageHealth: " << where << '\n'; return false; };
    QTemporaryDir temp;
    if (!temp.isValid()) return fail("temporary directory");
    const auto root = std::filesystem::path(temp.path().toStdWString());
    const auto workspacePath = root / "workspace";
    const auto cloudPath = root / "cloud-copy";
    std::filesystem::create_directories(workspacePath);
    QtWorkspace workspace(workspacePath);
    if (!SetAdminPassword(workspacePath, "report-password")) return fail("password fixture");
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* reportAction = window.findChild<QAction*>("storageHealthReport");
    auto* cleanupAction = window.findChild<QAction*>("storageCleanup");
    if (!reportAction || !cleanupAction || reportAction->isVisible() || cleanupAction->isVisible()) return fail("admin-only actions missing or visible without admin");
    QAction* adminAction = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return fail("admin action missing");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "adminLoginDialog") return;
        auto* input = dialog->findChild<QLineEdit*>("adminLoginPassword");
        auto* remember = dialog->findChild<QCheckBox*>("adminRememberSession");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!input || !remember || !buttons) return;
        input->setText(QString::fromUtf8("report-password"));
        remember->setChecked(false);
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    adminAction->trigger();
    if (!reportAction->isVisible() || !cleanupAction->isVisible()) return fail("admin authentication");
    std::filesystem::create_directories(workspacePath / "meta");
    std::filesystem::create_directories(cloudPath / "meta");
    const auto localTasks = workspacePath / "meta" / "tasks.json";
    const std::string invalidLocal = "not-json-private-content";
    QString report, error;
    { std::ofstream out(localTasks, std::ios::binary | std::ios::trunc); out << invalidLocal; }
    { std::ofstream out(cloudPath / "meta" / "tasks.json", std::ios::binary | std::ios::trunc); out << "[]"; }
    { std::ofstream out(workspacePath / "stray-report-test.bin", std::ios::binary | std::ios::trunc); out << "not included"; }
    std::vector<QtStorageStrayEntry> staleInventory;
    if (!BuildQtStorageStrayInventory(workspacePath, &staleInventory, &error) || staleInventory.empty()) return fail("stray inventory");
    { std::ofstream out(workspacePath / "appeared-after-preview.bin", std::ios::binary | std::ios::trunc); out << "keep"; }
    int removed = -1;
    if (RemoveQtStorageStrayEntries(workspacePath, staleInventory, {"stray-report-test.bin"}, &removed, &error) ||
        removed != 0 || !std::filesystem::exists(workspacePath / "stray-report-test.bin")) return fail("stale inventory guard");
    CloudSyncConfig config;
    config.enabled = true;
    config.root = cloudPath;
    if (!SaveCloudSyncConfig(workspacePath, config)) return fail("save cloud config");
    if (!BuildQtStorageHealthReport(workspacePath, workspace.modules, 1700000000, &report, &error) || !error.isEmpty()) return fail("build: ");
    if (!report.contains(QString::fromUtf8("РАСХОЖДЕНИЯ ЛОКАЛЬНОЙ И ОБЛАЧНОЙ КОПИИ")) ||
        !report.contains(QString::fromUtf8("meta/tasks.json")) || !report.contains(QString::fromUtf8("JSON не распознан")) ||
        !report.contains(QString::fromUtf8("stray-report-test.bin")) ||
        report.contains(QString::fromUtf8("qt-application-log.json")) ||
        report.contains(QString::fromUtf8("not-json-private-content")) || report.contains(QString::fromUtf8("not included")) ||
        !report.contains(QString::fromUtf8("не изменялись и не удалялись"))) {
        std::cerr << report.toUtf8().constData() << '\n';
        return fail("content");
    }
    std::ifstream verifyInput(localTasks, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(verifyInput)), std::istreambuf_iterator<char>());
    if (after != invalidLocal) return fail("source changed");
    const auto outputPath = temp.path() + "/storage-health.txt";
    if (!ExportQtStorageHealthReport(outputPath, report, &error) || !error.isEmpty()) return fail("direct export");
    QFile output(outputPath);
    if (!output.open(QIODevice::ReadOnly)) return fail("output open");
    const auto bytes = output.readAll();
    if (!bytes.startsWith("\xEF\xBB\xBF") || !bytes.contains("stray-report-test.bin")) return fail("output bytes");
    const auto uiOutputPath = temp.path() + "/storage-health-ui.txt";
    QTimer::singleShot(0, [uiOutputPath] {
        if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            picker->selectFile(uiOutputPath);
            static_cast<QDialog*>(picker)->accept();
        }
    });
    reportAction->trigger();
    QFile uiOutput(uiOutputPath);
    if (!uiOutput.open(QIODevice::ReadOnly) || !uiOutput.readAll().contains("stray-report-test.bin")) return fail("UI export");

    std::cerr << "storageHealth: before cancel preview" << std::endl;
    bool cancelWasSafeDefault = false;
    QTimer::singleShot(0, [&cancelWasSafeDefault] {
        auto* preview = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        std::cerr << "storageHealth: cancel timer modal=" << (preview ? preview->objectName().toUtf8().constData() : "none") << std::endl;
        if (!preview || preview->objectName() != "storageCleanupDialog") return;
        auto* cancel = preview->findChild<QPushButton*>("storageCleanupCancel");
        auto* removeButton = preview->findChild<QPushButton*>("storageCleanupConfirm");
        cancelWasSafeDefault = cancel && cancel->isDefault() && removeButton && !removeButton->isDefault();
        preview->reject();
    });
    cleanupAction->trigger();
    std::cerr << "storageHealth: after cancel preview" << std::endl;
    if (!cancelWasSafeDefault || !std::filesystem::exists(workspacePath / "stray-report-test.bin")) return fail("cleanup cancel did not preserve files");

    const auto targetStray = std::string("stray-report-test.bin");
    QTimer::singleShot(0, [targetStray] {
        auto* preview = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        std::cerr << "storageHealth: delete timer modal=" << (preview ? preview->objectName().toUtf8().constData() : "none") << std::endl;
        auto* list = preview ? preview->findChild<QListWidget*>("storageCleanupInventory") : nullptr;
        auto* removeButton = preview ? preview->findChild<QPushButton*>("storageCleanupConfirm") : nullptr;
        if (!list || !removeButton) return;
        if (auto* none = preview->findChild<QPushButton*>("storageCleanupSelectNone")) none->click();
        for (int row = 0; row < list->count(); ++row) {
            auto* item = list->item(row);
            if (item->data(Qt::UserRole).toString().toUtf8().toStdString() == targetStray)
                item->setCheckState(Qt::Checked);
        }
        if (removeButton->isEnabled()) {
            QTimer::singleShot(0, [] {
                if (auto* notice = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    std::cerr << "storageHealth: cleanup notice: " << notice->text().toUtf8().constData() << std::endl;
                    notice->accept();
                }
            });
            removeButton->click();
        }
    });
    std::cerr << "storageHealth: before delete preview" << std::endl;
    cleanupAction->trigger();
    std::cerr << "storageHealth: after delete preview" << std::endl;
    if (std::filesystem::exists(workspacePath / "stray-report-test.bin") ||
        !std::filesystem::exists(workspacePath / "appeared-after-preview.bin")) return fail("selected cleanup scope");
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* auditSource = window.findChild<QComboBox*>("auditSourceFilter");
    auto* auditTable = window.findChild<QTableWidget*>("records");
    if (!navigation || !auditSource || !auditTable) return fail("audit controls unavailable after cleanup");
    navigation->setCurrentRow(7);
    auditSource->setCurrentIndex(5);
    bool cleanupAuditVisible = false;
    for (int row = 0; row < auditTable->rowCount(); ++row) {
        if (auditTable->item(row, 3)->text() == QString::fromUtf8("Проверка и очистка хранилища") &&
            auditTable->item(row, 6)->text() == QStringLiteral("Administrator removed explicitly approved stray workspace entries")) {
            cleanupAuditVisible = !auditTable->item(row, 6)->text().contains(QString::fromUtf8("stray-report-test.bin"));
        }
    }
    if (!cleanupAuditVisible) return fail("privacy-safe storage cleanup missing from core audit");

    const auto treeRoot = root / "tree-cleanup";
    std::filesystem::create_directories(treeRoot / "unknown-dir" / "nested");
    { std::ofstream out(treeRoot / "unknown-dir" / "nested" / "payload.bin", std::ios::binary); out << "payload"; }
    std::vector<QtStorageStrayEntry> treeInventory;
    if (!BuildQtStorageStrayInventory(treeRoot, &treeInventory, &error) || treeInventory.size() != 3) return fail("recursive directory inventory");
    removed = 0;
    if (RemoveQtStorageStrayEntries(treeRoot, treeInventory, {"unknown-dir"}, &removed, &error) || removed != 0 ||
        !std::filesystem::exists(treeRoot / "unknown-dir" / "nested" / "payload.bin")) return fail("partial directory approval guard");
    std::vector<std::string> allTreePaths;
    for (const auto& entry : treeInventory) allTreePaths.push_back(entry.relativePath);
    if (!RemoveQtStorageStrayEntries(treeRoot, treeInventory, allTreePaths, &removed, &error) || removed != 3 ||
        std::filesystem::exists(treeRoot / "unknown-dir")) return fail("approved recursive directory cleanup");
    if (ExportQtStorageHealthReport(QString(), report, &error) || error.isEmpty() ||
        ExportQtStorageHealthReport(temp.path(), report, &error)) return fail("invalid destination");
    return true;
}

static bool TestReportPeriodComparison() {
    auto fail = [](const char* why) { std::cerr << "reportCompare: " << why << '\n'; return false; };
    QTemporaryDir temp;
    if (!temp.isValid()) return fail("temp");
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    auto profile = workspace.storage->create_profile(Profile("Comparison worker"));
    if (!profile) return fail("profile");
    workspace.reload();
    ProjectEntry currentProject; currentProject.id = "current-project"; currentProject.name = "Current project";
    ProjectEntry previousProject; previousProject.id = "previous-project"; previousProject.name = "Previous project";
    workspace.data.projects = {currentProject, previousProject};
    PipelineStep currentStage; currentStage.id = "current-stage"; currentStage.stageCode = "A"; currentStage.title = "Current stage";
    PipelineStep previousStage; previousStage.id = "previous-stage"; previousStage.stageCode = "B"; previousStage.title = "Previous stage";
    workspace.data.pipelineSteps = {currentStage, previousStage};
    TaskEntry current; current.id = "comparison-current"; current.title = "Current task";
    current.createdAt = QDateTime::currentSecsSinceEpoch(); current.assignees = {profile->id};
    current.projectId = currentProject.id; current.project = currentProject.name; current.pipelineStepId = currentStage.id;
    const auto reportNow = QDateTime::currentSecsSinceEpoch();
    TaskEntry overdue = current; overdue.id = "comparison-overdue"; overdue.title = "Overdue task";
    overdue.deadlineAt = reportNow - 60;
    TaskEntry upcoming = current; upcoming.id = "comparison-upcoming"; upcoming.title = "Upcoming task";
    upcoming.deadlineAt = reportNow + 86400;
    TaskEntry previous; previous.id = "comparison-previous"; previous.title = "Previous task"; previous.status = 2;
    previous.priority = 3;
    previous.createdAt = QDateTime(QDate::currentDate().addDays(-40), QTime(12, 0), Qt::LocalTime).toSecsSinceEpoch();
    previous.deadlineAt = reportNow - 40 * 86400;
    previous.category = 1;
    previous.assignees = {profile->id}; previous.projectId = previousProject.id; previous.project = previousProject.name;
    previous.pipelineStepId = previousStage.id; previous.pipelineStep = previousStage.title;
    previous.participants.push_back({profile->id, 100, 45, 15, "comparison"});
    TaskEntry noCreationDate; noCreationDate.id = "comparison-no-date"; noCreationDate.title = "Legacy task without date";
    workspace.data.tasks = {current, overdue, upcoming, previous, noCreationDate};
    if (!AppSaveProjects(workspace.directory, workspace.data.projects) ||
        !AppSavePipelineData(workspace.directory, workspace.data.pipelineSteps) ||
        !AppSaveTasks(workspace.directory, workspace.data.tasks)) return fail("save fixtures");
    QtWindow window(workspace);
    auto* nav = window.findChild<QListWidget*>("navigation");
    auto* table = window.findChild<QTableWidget*>("records");
    auto* range = window.findChild<QComboBox*>("reportDateRange");
    auto* compare = window.findChild<QCheckBox*>("reportComparePrevious");
    auto* view = window.findChild<QComboBox*>("reportView");
    if (!nav || !table || !range || !compare || !view || compare->accessibleName().isEmpty()) return fail("controls");
    if (nav->count() != 18 || nav->item(0)->text() != QString::fromUtf8("Профиль") ||
        nav->item(0)->icon().isNull() || nav->item(0)->data(Qt::AccessibleTextRole).toString() != nav->item(0)->text() ||
        !nav->item(0)->toolTip().contains(QStringLiteral("F1")) || nav->item(9)->icon().isNull())
        return fail("Reference-style navigation labels, icons, keyboard hint or accessible text missing");
    QAction* adminAction = nullptr;
    for (auto* menu : window.findChildren<QMenu*>()) for (auto* action : menu->actions())
        if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return fail("admin action");
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("admin123"); });
    adminAction->trigger();
    nav->setCurrentRow(6);
    range->setCurrentIndex(1);
    if (!compare->isEnabled()) return fail("comparison disabled");
    compare->setChecked(true);
    if (table->rowCount() != 2 || table->columnCount() != 9) { std::cerr << "rows=" << table->rowCount() << " cols=" << table->columnCount() << '\n'; return fail("project rows"); }
    int oldProjectRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString() == QStringLiteral("previous-project")) oldProjectRow = row;
    if (oldProjectRow < 0 || table->item(oldProjectRow, 1)->text() != "0" ||
        table->item(oldProjectRow, 6)->text() != "1") return fail("previous-only project metrics");
    table->selectRow(oldProjectRow);
    auto* details = window.findChild<QTextBrowser*>("details");
    if (!details || !details->toPlainText().contains("Previous task") ||
        !details->toPlainText().contains(QString::fromUtf8("Предыдущий период"))) return fail("project drill-down");
    view->setCurrentIndex(1);
    if (table->rowCount() != 1 || table->columnCount() != 14 ||
        table->item(0, 9)->text() != "1" || table->item(0, 12)->text() != "45" || table->item(0, 13)->text() != "15") return fail("employee previous metrics");
    view->setCurrentIndex(2);
    if (view->count() != 8 || table->rowCount() != 2 || table->columnCount() != 15) return fail("stage report layout");
    int previousStageRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString() == QStringLiteral("previous-stage")) previousStageRow = row;
    if (previousStageRow < 0 || table->item(previousStageRow, 1)->text() != "0" ||
        table->item(previousStageRow, 8)->text() != "1") return fail("previous-only stage metrics");
    table->selectRow(previousStageRow);
    if (!details->toPlainText().contains("Previous task") ||
        !details->toPlainText().contains(QString::fromUtf8("Этап пайплайна"))) return fail("stage drill-down");
    auto* exportButton = window.findChild<QPushButton*>("exportReport");
    const auto stageCsvPath = temp.path() + "/stage-report.csv";
    if (!exportButton || !exportButton->isEnabled()) return fail("stage report export action");
    QTimer::singleShot(0, [stageCsvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(stageCsvPath); static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportButton->click();
    QFile stageCsv(stageCsvPath);
    if (!stageCsv.open(QIODevice::ReadOnly)) return fail("stage report CSV missing");
    const auto stageCsvBytes = stageCsv.readAll();
    if (!stageCsvBytes.startsWith("\xEF\xBB\xBF") || !stageCsvBytes.contains(QString::fromUtf8("Этап").toUtf8()) ||
        !stageCsvBytes.contains(QString::fromUtf8("Previous stage").toUtf8()) ||
        !stageCsvBytes.contains(QString::fromUtf8("Задач · пред.").toUtf8())) return fail("stage report CSV content");
    view->setCurrentIndex(3);
    if (table->rowCount() != 2 || table->columnCount() != 15) return fail("category report layout");
    int previousCategoryRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString() == QStringLiteral("category:1")) previousCategoryRow = row;
    if (previousCategoryRow < 0 || table->item(previousCategoryRow, 1)->text() != "0" ||
        table->item(previousCategoryRow, 8)->text() != "1") return fail("previous-only category metrics");
    table->selectRow(previousCategoryRow);
    if (!details->toPlainText().contains("Previous task") ||
        !details->toPlainText().contains(QString::fromUtf8("Категория"))) return fail("category drill-down");
    const auto categoryCsvPath = temp.path() + "/category-report.csv";
    QTimer::singleShot(0, [categoryCsvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(categoryCsvPath); static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportButton->click();
    QFile categoryCsv(categoryCsvPath);
    if (!categoryCsv.open(QIODevice::ReadOnly)) return fail("category report CSV missing");
    const auto categoryCsvBytes = categoryCsv.readAll();
    if (!categoryCsvBytes.startsWith("\xEF\xBB\xBF") || !categoryCsvBytes.contains("Категория") ||
        !categoryCsvBytes.contains("D") || !categoryCsvBytes.contains(QString::fromUtf8("Задач · пред.").toUtf8()))
        return fail("category report CSV content");
    view->setCurrentIndex(5);
    if (table->rowCount() != 4 || table->columnCount() != 15) return fail("priority report layout");
    int previousPriorityRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString() == QStringLiteral("__priority_3")) previousPriorityRow = row;
    if (previousPriorityRow < 0 || table->item(previousPriorityRow, 1)->text() != "0" ||
        table->item(previousPriorityRow, 8)->text() != "1") return fail("previous-only priority metrics");
    table->selectRow(previousPriorityRow);
    if (!details->toPlainText().contains("Previous task") ||
        !details->toPlainText().contains(QString::fromUtf8("Приоритет"))) return fail("priority drill-down");
    const auto priorityCsvPath = temp.path() + "/priority-report.csv";
    QTimer::singleShot(0, [priorityCsvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(priorityCsvPath); static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportButton->click();
    QFile priorityCsv(priorityCsvPath);
    if (!priorityCsv.open(QIODevice::ReadOnly)) return fail("priority report CSV missing");
    const auto priorityCsvBytes = priorityCsv.readAll();
    if (!priorityCsvBytes.startsWith("\xEF\xBB\xBF") ||
        !priorityCsvBytes.contains(QString::fromUtf8("Приоритет").toUtf8()) ||
        !priorityCsvBytes.contains(QString::fromUtf8("Критический").toUtf8()) ||
        !priorityCsvBytes.contains(QString::fromUtf8("Задач · пред.").toUtf8())) return fail("priority report CSV content");
    view->setCurrentIndex(6);
    if (table->rowCount() != 4 || table->columnCount() != 15) return fail("deadline report layout");
    auto deadlineRow = [table](const QString& key) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toString() == key) return row;
        return -1;
    };
    const int completedDeadline = deadlineRow(QStringLiteral("__deadline_0"));
    const int overdueDeadline = deadlineRow(QStringLiteral("__deadline_1"));
    const int upcomingDeadline = deadlineRow(QStringLiteral("__deadline_2"));
    const int noDeadline = deadlineRow(QStringLiteral("__deadline_3"));
    if (completedDeadline < 0 || table->item(completedDeadline, 1)->text() != "0" ||
        table->item(completedDeadline, 8)->text() != "1" || overdueDeadline < 0 ||
        table->item(overdueDeadline, 1)->text() != "1" || upcomingDeadline < 0 ||
        table->item(upcomingDeadline, 1)->text() != "1" || noDeadline < 0 ||
        table->item(noDeadline, 1)->text() != "1") return fail("deadline-state report counts or prior-only row");
    table->selectRow(completedDeadline);
    if (!details->toPlainText().contains("Previous task") ||
        !details->toPlainText().contains(QString::fromUtf8("Состояние срока"))) return fail("deadline report drill-down");
    const auto deadlineCsvPath = temp.path() + "/deadline-report.csv";
    QTimer::singleShot(0, [deadlineCsvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(deadlineCsvPath); static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportButton->click();
    QFile deadlineCsv(deadlineCsvPath);
    if (!deadlineCsv.open(QIODevice::ReadOnly)) return fail("deadline report CSV missing");
    const auto deadlineCsvBytes = deadlineCsv.readAll();
    if (!deadlineCsvBytes.startsWith("\xEF\xBB\xBF") ||
        !deadlineCsvBytes.contains(QString::fromUtf8("Состояние срока").toUtf8()) ||
        !deadlineCsvBytes.contains(QString::fromUtf8("Просрочена").toUtf8()) ||
        !deadlineCsvBytes.contains(QString::fromUtf8("Без срока").toUtf8())) return fail("deadline report CSV content");
    view->setCurrentIndex(7);
    if (table->rowCount() != 2 || table->columnCount() != 15) return fail("creation-month report layout");
    const auto monthRow = [table](const QString& key) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toString() == key) return row;
        return -1;
    };
    const auto currentMonth = QDate::currentDate().toString("yyyy-MM");
    const auto previousMonth = QDate::currentDate().addDays(-40).toString("yyyy-MM");
    const int currentMonthRow = monthRow(currentMonth);
    const int previousMonthRow = monthRow(previousMonth);
    if (currentMonthRow < 0 || previousMonthRow < 0 ||
        table->item(currentMonthRow, 1)->text() != "3" || table->item(currentMonthRow, 8)->text() != "0" ||
        table->item(previousMonthRow, 1)->text() != "0" || table->item(previousMonthRow, 8)->text() != "1")
        return fail("creation-month current and previous cohort metrics");
    table->selectRow(previousMonthRow);
    if (!details->toPlainText().contains("Previous task") ||
        !details->toPlainText().contains(QString::fromUtf8("Месяц создания")))
        return fail("creation-month drill-down");
    const auto monthCsvPath = temp.path() + "/creation-month-report.csv";
    QTimer::singleShot(0, [monthCsvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(monthCsvPath); static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportButton->click();
    QFile monthCsv(monthCsvPath);
    if (!monthCsv.open(QIODevice::ReadOnly)) return fail("creation-month report CSV missing");
    const auto monthCsvBytes = monthCsv.readAll();
    if (!monthCsvBytes.startsWith("\xEF\xBB\xBF") ||
        !monthCsvBytes.contains(QString::fromUtf8("Месяц создания").toUtf8()) ||
        !monthCsvBytes.contains(previousMonth.toUtf8()) ||
        !monthCsvBytes.contains(QString::fromUtf8("Задач · пред.").toUtf8())) return fail("creation-month report CSV content");
    range->setCurrentIndex(0);
    if (compare->isEnabled() || table->columnCount() != 8 || !compare->isChecked()) return fail("all-time guard");
    view->setCurrentIndex(7);
    if (table->columnCount() != 8 || monthRow(QStringLiteral("__created_month_unknown")) < 0 ||
        table->item(monthRow(QStringLiteral("__created_month_unknown")), 0)->text() != QString::fromUtf8("Без даты"))
        return fail("legacy tasks without creation date are not grouped");
    const auto settings = LoadQtDisplaySettings(workspace.directory);
    return (settings.reportComparePrevious && settings.reportView == 7) || fail("setting persistence");
}

static bool TestAuditExport() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto path = temp.path() + "/audit.csv";
    const QStringList headers = {QString::fromUtf8("Источник"), QString::fromUtf8("Поле"), QString::fromUtf8("Детали")};
    const QVector<QStringList> rows = {{QString::fromUtf8("Задача"), QString::fromUtf8("поле, старое"),
        QString::fromUtf8("Текст \"с кавычками\"\nследующая строка")}};
    QString error;
    if (!ExportQtAuditCsv(path, headers, rows, &error) || !error.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto original = file.readAll();
    if (!original.startsWith("\xEF\xBB\xBF") ||
        !original.contains("\"поле, старое\"") ||
        !original.contains("\"Текст \"\"с кавычками\"\"\nследующая строка\"")) return false;
    if (ExportQtAuditCsv(path, headers, {{QString::fromUtf8("только одна колонка")}}, &error) || error.isEmpty()) return false;
    file.close();
    if (!file.open(QIODevice::ReadOnly) || file.readAll() != original) return false;
    return !ExportQtAuditCsv(temp.path(), headers, rows, &error) && !error.isEmpty();
}

static bool TestProjectDeletionRecovery() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toStdString());
    QtWorkspace workspace(directory);
    ProjectEntry project; project.id = "journal-project"; project.name = "Journal project"; project.createdAt = 123;
    TaskEntry task; task.id = "journal-task"; task.title = "Journal task"; task.projectId = project.id; task.project = project.name;
    workspace.data.projects = {project}; workspace.data.tasks = {task};
    if (!AppSaveProjects(directory, workspace.data.projects) || !AppSaveTasks(directory, workspace.data.tasks)) return false;

    PrepareProjectDeletionRecovery(directory);
    auto interrupted = AppDeleteProjectAndDetachTasks(directory, workspace.data.projects, workspace.data.tasks,
        project.id, "test", &workspace.data.taskAudit);
    if (!interrupted.ok || !std::filesystem::exists(directory / "meta/qt-xp-transaction")) return false;
    workspace.reload(); // Startup/reload must undo an operation that never reached the commit rename.
    if (workspace.data.projects.size() != 1 || workspace.data.tasks.size() != 1 ||
        workspace.data.tasks.front().projectId != project.id || !workspace.data.taskAudit.empty()) return false;

    PrepareProjectDeletionRecovery(directory);
    auto committed = AppDeleteProjectAndDetachTasks(directory, workspace.data.projects, workspace.data.tasks,
        project.id, "test", &workspace.data.taskAudit);
    if (!committed.ok) return false;
    CommitQtRecoveryTransaction(directory);
    workspace.reload();
    return workspace.data.projects.empty() && workspace.data.tasks.size() == 1 &&
        workspace.data.tasks.front().projectId.empty() && workspace.data.taskAudit.size() == 1 &&
        !std::filesystem::exists(directory / "meta/qt-xp-transaction");
}

static bool TestSkillEditor() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    auto read = [&] { QFile file(temp.path() + "/skills.txt"); file.open(QIODevice::ReadOnly); return file.readAll(); };
    auto save = [&](const std::string& id, const QString& name, const QString& desc) {
        return SaveQtSkill(workspace, id, name, 1.25, desc, "");
    };
    const auto before = read();
    if (save({}, "", "Description").isEmpty() || read() != before) return false;
    if (save({}, "Test", "line\nbreak").isEmpty() || read() != before) return false;
    if (save({}, "Test|invalid", "Description").isEmpty() || read() != before) return false;
    bool formChecked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::Save)->click();
        formChecked = !dialog->findChild<QLabel*>("editorNotice")->text().isEmpty();
        dialog->findChild<QLineEdit*>("skillName")->setText(QString::fromUtf8("Новый навык"));
        dialog->findChild<QLineEdit*>("skillDescription")->setText(QString::fromUtf8("Описание нового навыка"));
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/skill-editor.png"); }
        buttons->button(QDialogButtonBox::Save)->click();
    });
    if (!ShowSkillEditor(nullptr, workspace) || !formChecked) return false;
    const auto id = workspace.catalog.id_for_name(u8"Новый навык");
    if (!id) return false;
    Profile profile("Skill owner");
    profile.add_skill(*id);
    const auto owner = workspace.storage->create_profile(profile);
    if (!owner) return false;
    const auto ownerPath = temp.path() + "/" + QString::fromStdString(owner->id) + ".ini";
    auto readOwner = [&] { QFile file(ownerPath); file.open(QIODevice::ReadOnly); return file.readAll(); };
    const auto ownerBytes = readOwner();
    const auto created = read();
    if (save({}, QString::fromUtf8("Новый навык"), "Duplicate").isEmpty() || read() != created) return false;
    // A failed atomic replacement must not modify memory or report success.
    QFile locked(temp.path() + "/skills.txt");
    // A directory at the target path is a deterministic I/O failure on all platforms.
    if (!QFile::rename(locked.fileName(), locked.fileName() + ".original")) return false;
    if (!QDir().mkdir(locked.fileName())) return false;
    const bool failed = !save(*id, "Renamed", "Description").isEmpty();
    QDir().rmdir(locked.fileName());
    if (!QFile::rename(locked.fileName() + ".original", locked.fileName())) return false;
    if (!failed || read() != created || workspace.catalog.display_name(*id) != u8"Новый навык") return false;
    if (!save(*id, "Renamed", "Description").isEmpty()) return false;
    SkillCatalog disk(workspace.directory);
    if (disk.id_for_name("Renamed") != id || disk.weight(*id) != 1.25) return false;
    // Unchanged submission is valid, not a false failure from legacy update_skill.
    if (!save(*id, "Renamed", "Description").isEmpty()) return false;
    if (readOwner() != ownerBytes) return false;
    const auto edited = read();
    std::filesystem::create_directories(workspace.directory / "meta/qt-xp-transaction");
    if (save(*id, "Blocked", "Description").isEmpty() || read() != edited) return false;
    std::filesystem::remove(workspace.directory / "meta/qt-xp-transaction");
    QTimer::singleShot(0, [] { qobject_cast<QDialog*>(QApplication::activeModalWidget())->reject(); });
    if (ShowSkillEditor(nullptr, workspace, *id) || read() != edited) return false;
    workspace.catalog.add_skill("Linked", 1.0, "Linked description", "", {"unknown-profession"});
    workspace.catalog.reload();
    const auto linked = workspace.catalog.id_for_name("Linked");
    if (!linked || !save(*linked, "Linked renamed", "Changed description").isEmpty() ||
        workspace.catalog.professions(*linked) != std::vector<std::string>{"unknown-profession"}) return false;
    // Both metadata tokens now survive save/reload, with unknown links preserved.
    if (!SaveQtSkill(workspace, *linked, "Linked renamed", 1.25, "Changed description", "New category").isEmpty()) return false;
    workspace.catalog.reload();
    if (workspace.catalog.category(*linked) != "New category" ||
        workspace.catalog.professions(*linked) != std::vector<std::string>{"unknown-profession"} ||
        workspace.catalog.description(*linked) != "Changed description") return false;
    workspace.data.professions = {{"artist", "Artist", "Art"}};
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* list = dialog->findChild<QListWidget*>("skillProfessions");
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == "artist") list->item(i)->setCheckState(Qt::Checked);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/skill-professions.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    if (!ShowSkillEditor(nullptr, workspace, *linked)) return false;
    workspace.catalog.reload();
    if (workspace.catalog.professions(*linked) != std::vector<std::string>{"unknown-profession", "artist"} || readOwner() != ownerBytes) return false;
    const auto boundBytes = read();
    if (SaveQtSkill(workspace, *linked, "Linked renamed", 1.25, "Changed description", "New category",
        std::vector<std::string>{"missing-new"}).isEmpty() || read() != boundBytes) return false;
    if (!SaveQtSkill(workspace, *linked, "Linked renamed", 1.25, "Changed description", "New category",
        std::vector<std::string>{}).isEmpty() || !workspace.catalog.professions(*linked).empty()) return false;
    QTemporaryDir parserFixture;
    QFile parserFile(parserFixture.path() + "/skills.txt");
    if (!parserFile.open(QIODevice::WriteOnly)) return false;
    parserFile.write("reverse|Reverse|1.2|prof=one,two|cat=Art|Keep|pipe\nplain|Plain|1|Text only\n");
    parserFile.close();
    SkillCatalog parsed(std::filesystem::u8path(parserFixture.path().toStdString()));
    if (parsed.category("reverse") != "Art" || parsed.professions("reverse") != std::vector<std::string>{"one", "two"} ||
        parsed.description("reverse") != "Keep|pipe" || parsed.description("plain") != "Text only") return false;
    return true;
}

static bool TestProfessionEditor() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    bool checked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        save->click();
        checked = !dialog->findChild<QLabel*>("professionNotice")->text().isEmpty();
        dialog->findChild<QLineEdit*>("professionName")->setText(QString::fromUtf8("Художник"));
        dialog->findChild<QLineEdit*>("professionDescription")->setText(QString::fromUtf8("Геометрия и материалы"));
        save->click();
    });
    if (!ShowProfessionEditor(nullptr, workspace) || !checked || workspace.data.professions.size() != 1) return false;
    const auto id = workspace.data.professions.front().id;
    workspace.reload();
    if (workspace.data.professions.size() != 1 || workspace.data.professions.front().id != id) return false; // BOM regression
    auto read = [&] { QFile f(temp.path() + "/meta/professions.txt"); f.open(QIODevice::ReadOnly); return f.readAll(); };
    const auto before = read();
    QTimer::singleShot(0, [] { qobject_cast<QDialog*>(QApplication::activeModalWidget())->reject(); });
    if (ShowProfessionEditor(nullptr, workspace, id) || read() != before) return false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* name = dialog->findChild<QLineEdit*>("professionName");
        auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        name->setText("Invalid|name"); save->click();
        checked &= read() == before;
        name->setText(QString::fromUtf8("3D-художник"));
        const auto file = temp.path() + "/meta/professions.txt";
        checked &= QFile::rename(file, file + ".original") && QDir().mkdir(file);
        save->click();
        checked &= workspace.data.professions.front().name == u8"Художник";
        QDir().rmdir(file);
        checked &= QFile::rename(file + ".original", file) && read() == before;
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { dialog->findChild<QLabel*>("professionNotice")->clear(); dialog->grab().save(artifacts + "/profession-editor.png"); }
        save->click();
    });
    if (!ShowProfessionEditor(nullptr, workspace, id) || !checked) return false;
    workspace.reload();
    return workspace.data.professions.size() == 1 && workspace.data.professions.front().id == id &&
        workspace.data.professions.front().name == u8"3D-художник";
}

static bool TestProfessionDeletionRecovery() {
    auto fail = [](const char* text) { std::cerr << "professionDelete: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    workspace.data.professions = {{"artist", "Artist", "3D"}};
    if (!AppSaveProfessionsData(workspace.directory, workspace.data.professions)) return fail("profession fixture");
    workspace.catalog.add_skill("Modeling", 1.0, "Modeling", "Art", {"artist"});
    const auto skillId = workspace.catalog.id_for_name("Modeling");
    if (!skillId) return fail("skill fixture");
    auto created = workspace.storage->create_profile(Profile("Alice"));
    if (!created || !workspace.storage->set_active_profile(created->id)) return fail("profile fixture");
    auto profile = workspace.storage->load_profile();
    profile->set_profession_id("artist");
    if (!workspace.storage->save_profile(*profile)) return fail("profile binding");
    auto archivedCreated = workspace.storage->create_profile(Profile("Archived Alice"));
    if (!archivedCreated || !workspace.storage->set_active_profile(archivedCreated->id)) return fail("archived profile fixture");
    auto archivedProfile = workspace.storage->load_profile();
    if (!archivedProfile) return fail("archived profile load");
    archivedProfile->set_profession_id("artist");
    if (!workspace.storage->save_profile(*archivedProfile) ||
        !workspace.storage->set_archived(archivedCreated->id, true)) return fail("archived profession binding");
    workspace.reload();

    auto bytes = [&](const QString& relative) {
        QFile file(temp.path() + "/" + relative);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    const auto professionBytes = bytes("meta/professions.txt");
    const auto skillBytes = bytes("skills.txt");
    const auto profileBytes = bytes(QString::fromStdString(created->id) + ".ini");
    const auto archivedProfileBytes = bytes("archive/" + QString::fromStdString(archivedCreated->id) + ".ini");
    const std::vector<std::string> profilePaths = {created->id, "archive/" + archivedCreated->id};

    PrepareProfessionDeletionRecovery(workspace.directory, profilePaths);
    if (!workspace.storage->set_archived(archivedCreated->id, false)) return fail("archived move interruption fixture");
    workspace.reload();
    if (bytes("archive/" + QString::fromStdString(archivedCreated->id) + ".ini") != archivedProfileBytes ||
        std::filesystem::exists(workspace.directory / (archivedCreated->id + ".ini")))
        return fail("archive move recovery left a duplicate active profile");

    PrepareProfessionDeletionRecovery(workspace.directory, profilePaths);
    const auto interrupted = AppDeleteProfessionEntry(workspace.directory, workspace.data.professions, *workspace.storage,
        workspace.profiles, workspace.catalog, created->id, "artist", true);
    if (!interrupted.ok || !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("interrupted transaction fixture");
    workspace.reload();
    if (bytes("meta/professions.txt") != professionBytes || bytes("skills.txt") != skillBytes ||
        bytes(QString::fromStdString(created->id) + ".ini") != profileBytes ||
        bytes("archive/" + QString::fromStdString(archivedCreated->id) + ".ini") != archivedProfileBytes ||
        workspace.data.professions.size() != 1 || !workspace.catalog.has_profession(*skillId, "artist"))
        return fail("restart recovery");

    PrepareProfessionDeletionRecovery(workspace.directory, profilePaths);
    const auto removed = AppDeleteProfessionEntry(workspace.directory, workspace.data.professions, *workspace.storage,
        workspace.profiles, workspace.catalog, created->id, "artist", true);
    if (!removed.ok || removed.affectedProfiles != 2 || removed.affectedSkills != 1) return fail("delete result");
    CommitQtRecoveryTransaction(workspace.directory);
    workspace.reload();
    if (!workspace.data.professions.empty() || workspace.catalog.has_profession(*skillId, "artist") ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("commit state");
    if (!workspace.storage->set_archived(archivedCreated->id, false) ||
        !workspace.storage->set_active_profile(archivedCreated->id)) return fail("archived profile restore");
    const auto clearedArchived = workspace.storage->load_profile();
    if (!clearedArchived || !clearedArchived->profession_id().empty() ||
        !workspace.storage->set_archived(archivedCreated->id, true)) return fail("archived profile was not cleared");
    if (!workspace.storage->set_active_profile(created->id)) return fail("profile reload");
    const auto cleared = workspace.storage->load_profile();
    return cleared && cleared->profession_id().empty();
}

static bool TestProfessionMergeRecovery() {
    auto fail = [](const char* text) { std::cerr << "professionMerge: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    workspace.data.professions = {{"source", "Source", "Old"}, {"target", "Target", "Keep"}};
    if (!AppSaveProfessionsData(workspace.directory, workspace.data.professions)) return fail("profession fixture");
    if (!workspace.catalog.add_skill("Source skill", 1.0, "Source", "", {"source"}) ||
        !workspace.catalog.add_skill("Target skill", 1.0, "Target", "", {"target"}) ||
        !workspace.catalog.add_skill("Shared skill", 1.0, "Shared", "", {"source", "target"}))
        return fail("skill fixtures");
    auto active = workspace.storage->create_profile(Profile("Active"));
    if (!active || !workspace.storage->set_active_profile(active->id)) return fail("active profile fixture");
    auto profile = workspace.storage->load_profile();
    if (!profile) return fail("active profile load");
    profile->set_profession_id("source");
    if (!workspace.storage->save_profile(*profile)) return fail("active profession binding");
    auto archived = workspace.storage->create_profile(Profile("Archived"));
    if (!archived || !workspace.storage->set_active_profile(archived->id)) return fail("archived profile fixture");
    profile = workspace.storage->load_profile();
    if (!profile) return fail("archived profile load");
    profile->set_profession_id("source");
    if (!workspace.storage->save_profile(*profile) || !workspace.storage->set_archived(archived->id, true) ||
        !workspace.storage->set_active_profile(active->id)) return fail("archive profile binding");
    workspace.reload();

    bool confirmed = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        if (auto* modal = QApplication::activeModalWidget()) {
            if (auto* box = qobject_cast<QMessageBox*>(modal)) box->button(QMessageBox::Cancel)->click();
            else if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
        }
    });
    watchdog.start(8000);
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* name = dialog ? dialog->findChild<QLineEdit*>("professionName") : nullptr;
        auto* description = dialog ? dialog->findChild<QLineEdit*>("professionDescription") : nullptr;
        auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
        if (!dialog || !name || !description || !buttons) {
            std::cerr << "professionMerge editor controls missing modal=" << (QApplication::activeModalWidget() ? QApplication::activeModalWidget()->objectName().toStdString() : "none") << '\n';
            return;
        }
        name->setText(QString::fromUtf8("Target"));
        description->setText(QString::fromUtf8("Merged"));
        QTimer::singleShot(50, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) for (auto* widget : QApplication::topLevelWidgets())
                if (widget->isVisible() && (box = qobject_cast<QMessageBox*>(widget))) break;
            confirmed = box && box->objectName() == "professionMergeConfirm" &&
                box->defaultButton() == box->button(QMessageBox::Cancel) && box->text().contains(QString::fromUtf8("архивных"));
            if (box) box->button(QMessageBox::Yes)->click();
            else {
                auto* owner = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto* notice = owner ? owner->findChild<QLabel*>("professionNotice") : nullptr;
                std::cerr << "professionMerge confirmation missing modal=" << (owner ? owner->objectName().toStdString() : "none")
                          << " notice=" << (notice ? notice->text().toStdString() : "none") << '\n';
            }
        });
        buttons->button(QDialogButtonBox::Save)->click();
    });
    const bool accepted = ShowProfessionEditor(nullptr, workspace, "source", active->id);
    watchdog.stop();
    if (!accepted || !confirmed) {
        std::cerr << "accepted=" << accepted << " confirmed=" << confirmed << " professions=" << workspace.data.professions.size() << '\n';
        return fail("explicit merge confirmation");
    }
    if (workspace.data.professions.size() != 1 || workspace.data.professions.front().id != "target" ||
        workspace.data.professions.front().description != "Merged") return fail("catalog merge");
    if (workspace.catalog.professions("Shared skill") != std::vector<std::string>{"target"} ||
        workspace.catalog.professions("Source skill") != std::vector<std::string>{"target"} ||
        workspace.catalog.professions("Target skill") != std::vector<std::string>{"target"}) return fail("skill binding merge");
    if (!workspace.storage->set_active_profile(active->id)) return fail("active profile select");
    profile = workspace.storage->load_profile();
    if (!profile || profile->profession_id() != "target") return fail("active profile reassignment");
    const auto info = workspace.storage->list_profiles();
    const auto archivedInfo = std::find_if(info.begin(), info.end(), [&](const auto& item) { return item.id == archived->id; });
    if (archivedInfo == info.end() || !archivedInfo->archived || !workspace.storage->set_archived(archived->id, false) ||
        !workspace.storage->set_active_profile(archived->id)) return fail("archived state");
    profile = workspace.storage->load_profile();
    if (!profile || profile->profession_id() != "target" || !workspace.storage->set_archived(archived->id, true) ||
        !workspace.storage->set_active_profile(active->id)) return fail("archived profile reassignment");

    auto readBytes = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    const auto originalProfessions = readBytes(temp.path() + "/meta/professions.txt");
    const auto originalSkills = readBytes(temp.path() + "/skills.txt");
    const auto originalArchive = readBytes(temp.path() + "/archive/" + QString::fromStdString(archived->id) + ".ini");
    if (originalProfessions.isEmpty() || originalSkills.isEmpty() || originalArchive.isEmpty()) return fail("original recovery bytes");
    PrepareProfessionDeletionRecovery(workspace.directory, {active->id, "archive/" + archived->id});
    QFile pf(temp.path() + "/meta/professions.txt"); if (!pf.open(QIODevice::WriteOnly | QIODevice::Truncate) || pf.write("broken") < 0) return fail("journal mutation professions");
    QFile sf(temp.path() + "/skills.txt"); if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate) || sf.write("broken") < 0) return fail("journal mutation skills");
    QFile af(temp.path() + "/archive/" + QString::fromStdString(archived->id) + ".ini");
    if (!af.open(QIODevice::WriteOnly | QIODevice::Truncate) || af.write("broken") < 0) return fail("journal mutation archive");
    pf.close(); sf.close(); af.close();
    if (!RecoverTaskCompletion(workspace.directory)) return fail("journal recovery");
    QFile restoredProfessions(temp.path() + "/meta/professions.txt"), restoredSkills(temp.path() + "/skills.txt"), restoredArchive(temp.path() + "/archive/" + QString::fromStdString(archived->id) + ".ini");
    return restoredProfessions.open(QIODevice::ReadOnly) && restoredProfessions.readAll() == originalProfessions &&
        restoredSkills.open(QIODevice::ReadOnly) && restoredSkills.readAll() == originalSkills &&
        restoredArchive.open(QIODevice::ReadOnly) && restoredArchive.readAll() == originalArchive;
}

static bool TestSkillDeletionRecovery() {
    auto fail = [](const char* text) { std::cerr << "skillDelete: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    if (!workspace.catalog.add_skill("Keeper", 1.0, "Retained")) return fail("keeper fixture");
    if (!workspace.catalog.add_skill("Disposable", 1.2, "Unused", "Test", {"legacy-prof"})) return fail("fixture");
    workspace.catalog.reload(); // Canonicalize the catalog's mandatory defaults before byte-level recovery checks.
    const auto disposable = workspace.catalog.id_for_name("Disposable");
    if (!disposable) return fail("fixture id");
    QFile source(temp.path() + "/skills.txt");
    if (!source.open(QIODevice::ReadOnly)) return fail("fixture bytes");
    const auto original = source.readAll();
    source.close();

    PrepareSkillDeletionRecovery(workspace.directory);
    const auto interrupted = AppDeleteUnusedSkill(workspace.directory, workspace.catalog, *workspace.storage,
        workspace.profiles, workspace.data.tasks, {}, *disposable);
    if (!interrupted.ok || !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("interrupted fixture");
    workspace.reload();
    QFile restored(temp.path() + "/skills.txt");
    if (!restored.open(QIODevice::ReadOnly) || restored.readAll() != original || !workspace.catalog.contains_id(*disposable))
        return fail("restart recovery");
    restored.close();

    QFile locked(temp.path() + "/skills.txt");
    if (!locked.open(QIODevice::ReadOnly)) return fail("lock fixture");
    PrepareSkillDeletionRecovery(workspace.directory);
    const auto lockedDelete = AppDeleteUnusedSkill(workspace.directory, workspace.catalog, *workspace.storage,
        workspace.profiles, workspace.data.tasks, {}, *disposable);
    if (lockedDelete.ok) return fail("locked destination accepted");
    locked.close();
    if (!RecoverTaskCompletion(workspace.directory)) return fail("locked rollback");
    workspace.reload();
    if (!workspace.catalog.contains_id(*disposable)) return fail("locked rollback state");

    auto created = workspace.storage->create_profile(Profile("Linked"));
    if (!created || !workspace.storage->set_active_profile(created->id)) return fail("profile fixture");
    auto profile = workspace.storage->load_profile();
    profile->add_skill(*disposable);
    Achievement achievement; achievement.title = "Linked"; achievement.skill = *disposable;
    profile->add_achievement(achievement);
    if (!workspace.storage->save_profile(*profile)) return fail("profile save");
    workspace.reload();
    TaskEntry task; task.id = "linked-skill-task"; task.title = "Linked"; task.skillIds = {*disposable};
    workspace.data.tasks.push_back(task);
    const auto blocked = AppDeleteUnusedSkill(workspace.directory, workspace.catalog, *workspace.storage,
        workspace.profiles, workspace.data.tasks, created->id, *disposable);
    if (blocked.ok || blocked.linkedTasks != 1 || blocked.linkedProfiles != 1 || blocked.linkedAchievements != 1 ||
        !workspace.catalog.contains_id(*disposable)) return fail("relationship guard");

    profile->set_skills({}); profile->set_achievements({});
    if (!workspace.storage->save_profile(*profile)) return fail("unlink profile");
    workspace.data.tasks.clear();
    PrepareSkillDeletionRecovery(workspace.directory);
    const auto removed = AppDeleteUnusedSkill(workspace.directory, workspace.catalog, *workspace.storage,
        workspace.profiles, workspace.data.tasks, created->id, *disposable);
    if (!removed.ok) { std::cerr << removed.errorMessage << "\n"; return fail("delete"); }
    CommitQtRecoveryTransaction(workspace.directory);
    workspace.reload();
    return !workspace.catalog.contains_id(*disposable) &&
        !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction");
}

static bool TestSkillMergeRecovery() {
    auto fail = [](const char* text) { std::cerr << "skillMerge: " << text << '\n'; return false; };
    QTemporaryDir temp;
    if (!temp.isValid()) return fail("temporary directory");
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    if (!workspace.catalog.add_skill("Source", 0.8, "Old source", "Legacy") ||
        !workspace.catalog.add_skill("Target", 1.0, "Old target", "Current")) return fail("catalog fixture");
    workspace.catalog.reload();
    const auto sourceId = workspace.catalog.id_for_name("Source");
    const auto targetId = workspace.catalog.id_for_name("Target");
    if (!sourceId || !targetId) return fail("catalog ids");

    auto active = workspace.storage->create_profile(Profile("Active"));
    if (!active || !workspace.storage->set_active_profile(active->id)) return fail("active profile fixture");
    Skill source(*sourceId); source.xp = 200; source.xpToNext = Skill::required_xp_for(2);
    Skill target(*targetId); target.xp = 100; target.xpToNext = Skill::required_xp_for(2);
    auto profile = workspace.storage->load_profile();
    if (!profile) return fail("active profile load");
    profile->set_skills({source, target});
    Achievement activeAchievement; activeAchievement.title = "Source badge"; activeAchievement.skill = *sourceId;
    profile->add_achievement(activeAchievement);
    if (!workspace.storage->save_profile(*profile)) return fail("active profile save");

    auto archived = workspace.storage->create_profile(Profile("Archived"));
    if (!archived || !workspace.storage->set_active_profile(archived->id)) return fail("archived profile fixture");
    Skill archivedSource(*sourceId, 2); archivedSource.xp = 25;
    profile = workspace.storage->load_profile();
    if (!profile) return fail("archived profile load");
    profile->set_skills({archivedSource});
    Achievement archivedAchievement; archivedAchievement.title = "Archived badge"; archivedAchievement.skill = *sourceId;
    profile->add_achievement(archivedAchievement);
    if (!workspace.storage->save_profile(*profile) || !workspace.storage->set_archived(archived->id, true) ||
        !workspace.storage->set_active_profile(active->id)) return fail("archive profile setup");

    TaskEntry task; task.id = "skill-merge-task"; task.title = "Skill merge"; task.skillIds = {*sourceId, *targetId};
    if (!AppSaveTasks(workspace.directory, {task})) return fail("task fixture");
    workspace.reload();
    bool confirmed = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        if (auto* modal = QApplication::activeModalWidget()) {
            if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
            else if (auto* box = qobject_cast<QMessageBox*>(modal)) box->button(QMessageBox::Cancel)->click();
        }
    });
    watchdog.start(8000);
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* name = dialog ? dialog->findChild<QLineEdit*>("skillName") : nullptr;
        auto* description = dialog ? dialog->findChild<QLineEdit*>("skillDescription") : nullptr;
        auto* category = dialog ? dialog->findChild<QLineEdit*>("skillCategory") : nullptr;
        auto* weight = dialog ? dialog->findChild<QDoubleSpinBox*>("skillWeight") : nullptr;
        auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
        if (!dialog || !name || !description || !category || !weight || !buttons) {
            std::cerr << "skillMerge: editor controls missing active="
                      << (QApplication::activeModalWidget() ? QApplication::activeModalWidget()->objectName().toStdString() : "none") << '\n';
            return;
        }
        name->setText(QString::fromUtf8("Target"));
        description->setText(QString::fromUtf8("Merged description"));
        category->setText(QString::fromUtf8("Merged category"));
        weight->setValue(1.25);
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            confirmed = box && box->objectName() == "skillMergeConfirm" &&
                box->defaultButton() == box->button(QMessageBox::Cancel) &&
                box->text().contains(QString::fromUtf8("архивные")) && box->text().contains(QString::fromUtf8("задач"));
            if (box) box->button(QMessageBox::Yes)->click();
            else std::cerr << "skillMerge: confirmation widget missing\n";
        });
        buttons->button(QDialogButtonBox::Save)->click();
    });
    const bool editorAccepted = ShowSkillEditor(nullptr, workspace, *sourceId, active->id);
    watchdog.stop();
    if (!editorAccepted || !confirmed)
        return fail("explicit merge confirmation");
    if (workspace.catalog.contains_id(*sourceId) || !workspace.catalog.contains_id(*targetId) ||
        workspace.catalog.description(*targetId) != "Merged description" ||
        workspace.catalog.category(*targetId) != "Merged category" ||
        std::abs(workspace.catalog.weight(*targetId) - 1.25) > 0.001) return fail("catalog result");
    if (!workspace.storage->set_active_profile(active->id)) return fail("active profile select");
    profile = workspace.storage->load_profile();
    if (!profile) return fail("merged active profile load");
    auto activeSkills = profile->list_skills();
    if (activeSkills.size() != 1 || activeSkills.front().name != *targetId || activeSkills.front().level != 2 ||
        activeSkills.front().xp != 54 || profile->achievements().size() != 1 ||
        profile->achievements().front().skill != *targetId) return fail("active XP and achievement");
    const auto profiles = workspace.storage->list_profiles();
    const auto archivedInfo = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == archived->id; });
    if (archivedInfo == profiles.end() || !archivedInfo->archived ||
        !workspace.storage->set_archived(archived->id, false) ||
        !workspace.storage->set_active_profile(archived->id)) {
        return fail("archive state");
    }
    profile = workspace.storage->load_profile();
    if (!profile || profile->list_skills().size() != 1 || profile->list_skills().front().name != *targetId ||
        profile->list_skills().front().level != 2 || profile->list_skills().front().xp != 25 ||
        profile->achievements().size() != 1 || profile->achievements().front().skill != *targetId ||
        !workspace.storage->set_archived(archived->id, true) ||
        !workspace.storage->set_active_profile(active->id))
        return fail("archived XP and achievement");
    const auto mergedTasks = LoadTasksData(workspace.directory);
    const auto mergedTask = std::find_if(mergedTasks.begin(), mergedTasks.end(), [](const auto& item) { return item.id == "skill-merge-task"; });
    if (mergedTask == mergedTasks.end() || mergedTask->skillIds != std::vector<std::string>{*targetId} ||
        LoadTaskAuditData(workspace.directory).empty()) return fail("task binding and audit");

    auto readBytes = [&](const std::filesystem::path& path) {
        QFile file(QString::fromStdWString(path.wstring()));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    auto writeBytes = [&](const std::filesystem::path& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(QString::fromStdWString(path.wstring())).absolutePath());
        QFile file(QString::fromStdWString(path.wstring()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    const auto skillBytes = readBytes(workspace.directory / "skills.txt");
    const auto taskBytes = readBytes(workspace.directory / "meta/tasks.json");
    const auto auditBytes = readBytes(workspace.directory / "meta/task-audit.log");
    const auto profileBytes = readBytes(workspace.directory / (active->id + ".ini"));
    const auto archivedBytes = readBytes(workspace.directory / "archive" / (archived->id + ".ini"));
    const auto achievementBytes = readBytes(workspace.directory / "achievements" / (archived->id + ".json"));
    PrepareSkillMergeRecovery(workspace.directory, {active->id, archived->id});
    if (!writeBytes(workspace.directory / "skills.txt", "broken\n") ||
        !writeBytes(workspace.directory / "meta/tasks.json", "[]") ||
        !writeBytes(workspace.directory / (active->id + ".ini"), "broken\n")) return fail("recovery interruption fixture");
    if (!RecoverTaskCompletion(workspace.directory)) return fail("recovery did not run");
    workspace.reload();
    if (readBytes(workspace.directory / "skills.txt") != skillBytes ||
        readBytes(workspace.directory / "meta/tasks.json") != taskBytes ||
        readBytes(workspace.directory / "meta/task-audit.log") != auditBytes ||
        readBytes(workspace.directory / (active->id + ".ini")) != profileBytes ||
        readBytes(workspace.directory / "archive" / (archived->id + ".ini")) != archivedBytes ||
        readBytes(workspace.directory / "achievements" / (archived->id + ".json")) != achievementBytes)
        return fail("journal rollback");
    return true;
}

static bool TestImmediateRollbackPreservesChangedFiles() {
    auto fail = [](const char* text) {
        std::cerr << "immediateRollbackPreservation: " << text << '\n';
        return false;
    };
    QTemporaryDir temp;
    if (!temp.isValid()) return fail("temporary directory");
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    const auto target = directory / "skills.txt";
    const QByteArray original = "original catalog\n";
    const QByteArray external = "external catalog edit\n";
    auto writeBytes = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(QString::fromStdWString(path.wstring())).absolutePath());
        QFile file(QString::fromStdWString(path.wstring()));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    auto readBytes = [](const std::filesystem::path& path) {
        QFile file(QString::fromStdWString(path.wstring()));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    if (!writeBytes(target, original)) return fail("initial bytes");
    PrepareSkillDeletionRecovery(directory);
    if (!writeBytes(target, external)) return fail("external in-flight edit");

    const auto notice = RecoverTaskCompletionWithNotice(directory);
    if (readBytes(target) != original) return fail("transaction pre-image was not restored");
    std::filesystem::path preservedDirectory;
    for (const auto& item : std::filesystem::directory_iterator(directory / "meta" / "updates")) {
        if (item.is_directory() && item.path().filename().u8string().rfind("qt-xp-recovery-", 0) == 0) {
            preservedDirectory = item.path();
            break;
        }
    }
    if (preservedDirectory.empty()) return fail("preservation directory was not created");
    const auto preservedBytes = readBytes(preservedDirectory / "skills.txt");
    if (preservedBytes != external) {
        std::cerr << "preserved skill bytes=" << preservedBytes.toHex().constData()
                  << " expected=" << external.toHex().constData() << '\n';
        return fail("external bytes were not copied before rollback");
    }
    auto displayPath = preservedDirectory;
    displayPath.make_preferred();
    const auto preservedPath = displayPath.u8string();
    if (notice.find(preservedPath) == std::string::npos) {
        std::cerr << "notice=" << notice << " path=" << preservedPath << '\n';
        return fail("recovery copy path missing from user message");
    }
    const auto manifest = readBytes(preservedDirectory / "manifest.txt");
    if (!manifest.contains("skills.txt") || !manifest.contains("saved"))
        return fail("preservation manifest missing changed target");
    return true;
}

static bool TestProfileDeletionRecovery() {
    auto fail = [](const char* text) { std::cerr << "profileDelete: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    auto created = workspace.storage->create_profile(Profile("Disposable"));
    if (!created || !workspace.storage->set_archived(created->id, true)) return fail("fixture");
    workspace.reload();
    TaskEntry linked; linked.id = "profile-link"; linked.title = "Linked"; linked.assignees = {created->id};
    const auto blocked = AppDeleteEmptyArchivedProfile(*workspace.storage, workspace.profiles, {linked}, created->id);
    if (blocked.ok || blocked.linkedTasks != 1) return fail("task guard");

    PrepareProfileDeletionRecovery(workspace.directory, created->id);
    const auto interrupted = AppDeleteEmptyArchivedProfile(*workspace.storage, workspace.profiles, {}, created->id);
    if (!interrupted.ok || !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("interrupted fixture");
    workspace.reload();
    const auto restored = std::find_if(workspace.profiles.begin(), workspace.profiles.end(),
        [&](const auto& p) { return p.id == created->id && p.archived; });
    if (restored == workspace.profiles.end()) return fail("restart recovery");

    PrepareProfileDeletionRecovery(workspace.directory, created->id);
    const auto removed = AppDeleteEmptyArchivedProfile(*workspace.storage, workspace.profiles, {}, created->id);
    if (!removed.ok) { std::cerr << removed.errorMessage << '\n'; return fail("delete"); }
    CommitQtRecoveryTransaction(workspace.directory);
    workspace.reload();
    return std::none_of(workspace.profiles.begin(), workspace.profiles.end(),
               [&](const auto& p) { return p.id == created->id; }) &&
        !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction");
}

static bool TestPersonalWallet() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    Profile profile("Wallet profile");
    profile.set_password_encoded(EncodePassword("wallet-password"));
    profile.set_wallet_balance(250.0);
    profile.set_spirit(ProfileSpirit::Evil);
    const auto created = workspace.storage->create_profile(profile);
    if (!created) return false;
    const auto now = QDateTime::currentDateTime();
    const int minute = now.time().hour() * 60 + now.time().minute();
    workspace.data.vault.pomodoroDaysMask = 0x7f;
    workspace.data.vault.pomodoroStartMinutes = (minute + 1439) % 1440;
    workspace.data.vault.pomodoroEndMinutes = (minute + 2) % 1440;
    workspace.data.vault.pomodoroMinMinutes = 20;
    workspace.data.vault.pomodoroCoinsPerCycle = 1;
    if (!SaveStorageVault(workspace.directory, workspace.data.vault)) return false;
    TaskEntry awardedHistory; awardedHistory.id = "history-awarded"; awardedHistory.title = "Awarded task";
    awardedHistory.project = "History project"; awardedHistory.createdAt = QDateTime::currentSecsSinceEpoch() - 3600;
    awardedHistory.status = 2; awardedHistory.assignees = {created->id};
    awardedHistory.participants.push_back({created->id, 40, 17, 8, {}});
    TaskEntry pendingHistory; pendingHistory.id = "history-pending"; pendingHistory.title = "Pending task";
    pendingHistory.createdAt = QDateTime::currentSecsSinceEpoch() - 1800; pendingHistory.status = 2;
    pendingHistory.assignees = {created->id};
    if (!AppSaveTasks(workspace.directory, {awardedHistory, pendingHistory})) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* remove = window.findChild<QPushButton*>("removeEvilSpirit");
    auto* history = window.findChild<QPushButton*>("profileWalletHistory");
    auto* profileHistory = window.findChild<QPushButton*>("profileActivityHistory");
    auto* access = window.findChild<QAction*>("profileAccess");
    if (!remove || !history || !profileHistory || !access || remove->isVisible() || history->isVisible() || profileHistory->isVisible()) return false;
    auto* pomodoro = static_cast<QtPomodoro*>(window.findChild<QWidget*>("pomodoroPanel"));
    pomodoro->findChild<QPushButton*>("pomodoroStart")->click();
    pomodoro->advanceSecondsForTest(25 * 60);
    workspace.storage->set_active_profile(created->id);
    if (workspace.storage->load_profile()->wallet_balance() != 250.0 ||
        !pomodoro->findChild<QLabel*>("pomodoroStatus")->text().contains(QString::fromUtf8("личный вход"))) return false;
    pomodoro->findChild<QPushButton*>("pomodoroReset")->click();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        dialog->findChild<QLineEdit*>("profileLoginPassword")->setText("wallet-password");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    access->trigger();
    if (!remove->isVisible() || !remove->isEnabled() || !history->isVisible() || !profileHistory->isVisible()) return false;
    pomodoro->findChild<QPushButton*>("pomodoroStart")->click();
    pomodoro->advanceSecondsForTest(25 * 60);
    workspace.storage->set_active_profile(created->id);
    auto rewarded = workspace.storage->load_profile();
    if (!rewarded || rewarded->wallet_balance() != 251.0 ||
        !pomodoro->findChild<QLabel*>("pomodoroStatus")->text().contains("+1")) return false;
    QTimer::singleShot(0, [] { qobject_cast<QMessageBox*>(QApplication::activeModalWidget())->button(QMessageBox::No)->click(); });
    remove->click();
    workspace.storage->set_active_profile(created->id);
    auto unchanged = workspace.storage->load_profile();
    if (!unchanged || unchanged->spirit() != ProfileSpirit::Evil || unchanged->wallet_balance() != 251.0 || workspace.data.vault.balance != 0.0) return false;
    AppSetProfileAuditFailureHookForTests(true);
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            confirm->button(QMessageBox::Yes)->click();
        QTimer::singleShot(0, [] {
            if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) warning->accept();
        });
    });
    remove->click();
    AppSetProfileAuditFailureHookForTests(false);
    workspace.storage->set_active_profile(created->id);
    const auto spiritAuditRollback = workspace.storage->load_profile();
    const auto vaultAuditRollback = LoadStorageVault(workspace.directory);
    if (!spiritAuditRollback || spiritAuditRollback->spirit() != ProfileSpirit::Evil ||
        spiritAuditRollback->wallet_balance() != 251.0 || vaultAuditRollback.balance != 0.0 ||
        !vaultAuditRollback.log.empty() || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); window.grab().save(artifacts + "/profile-wallet.png"); }
    QTimer::singleShot(0, [] { qobject_cast<QMessageBox*>(QApplication::activeModalWidget())->button(QMessageBox::Yes)->click(); });
    remove->click();
    workspace.storage->set_active_profile(created->id);
    const auto changed = workspace.storage->load_profile();
    const auto vault = LoadStorageVault(workspace.directory);
    if (!changed || changed->spirit() != ProfileSpirit::None || changed->wallet_balance() != 51.0 ||
        vault.balance != 200.0 || vault.log.empty() || vault.log.back().action != "spirit_cleanup" || remove->isEnabled()) return false;
    if (!AppendProfileAudit(workspace.directory, created->id, "wallet_adjustment", "credit|12.50|проверка истории")) return false;
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog ? dialog->findChild<QTableWidget*>("profileWalletHistoryTable") : nullptr;
        if (!table || table->rowCount() < 3 || table->columnCount() != 4) return;
        bool foundPomodoro = false, foundSpirit = false, foundReason = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            foundPomodoro |= table->item(row, 1)->text().contains(QString::fromUtf8("Pomodoro"));
            foundSpirit |= table->item(row, 1)->text().contains(QString::fromUtf8("Злого духа"));
            foundReason |= table->item(row, 3)->text().contains(QString::fromUtf8("проверка истории"));
        }
        if (foundPomodoro && foundSpirit && foundReason) dialog->accept();
    });
    history->click();
    if (!AppendProfileAudit(workspace.directory, created->id, "password_reset") ||
        !AppendProfileAudit(workspace.directory, created->id, "trust_expired") ||
        !AppendProfileAudit(workspace.directory, created->id, "trust_revoked", "profile_unavailable") ||
        !AppendProfileAudit(workspace.directory, created->id, "trust_revoke_failed", "local_session_closed") ||
        !AppendProfileAudit(workspace.directory, created->id, "test_profile_event", "profile history marker")) return false;
    QTimer::singleShot(0, [eventsCsvPath = temp.path() + "/profile-events.csv",
                           tasksCsvPath = temp.path() + "/profile-task-history.csv"] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog ? dialog->findChild<QTableWidget*>("profileActivityHistoryTable") : nullptr;
        if (!table || table->rowCount() < 6 || table->columnCount() != 3) return;
        bool foundWallet = false, foundPassword = false, foundUnknown = false, foundSpirit = false;
        bool foundTrustExpired = false, foundTrustRevoked = false, foundTrustFailure = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            foundWallet |= table->item(row, 1)->text().contains(QString::fromUtf8("Изменение кошелька")) &&
                table->item(row, 2)->text().contains(QString::fromUtf8("проверка истории"));
            foundPassword |= table->item(row, 1)->text() == QString::fromUtf8("Сброс пароля");
            foundUnknown |= table->item(row, 1)->text() == "test_profile_event" &&
                table->item(row, 2)->text() == "profile history marker";
            foundSpirit |= table->item(row, 1)->text().contains(QString::fromUtf8("Злого духа"));
            foundTrustExpired |= table->item(row, 1)->text() == QString::fromUtf8("Срок доверенного входа истёк");
            foundTrustRevoked |= table->item(row, 1)->text() == QString::fromUtf8("Доверенный вход отозван");
            foundTrustFailure |= table->item(row, 1)->text() == QString::fromUtf8("Не удалось отозвать доверенный вход");
        }
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/profile-activity-history.png"); }
        auto* tabs = dialog ? dialog->findChild<QTabWidget*>("profileHistoryTabs") : nullptr;
        auto* exportEvents = dialog ? dialog->findChild<QPushButton*>("exportProfileEvents") : nullptr;
        auto* exportTasks = dialog ? dialog->findChild<QPushButton*>("exportProfileTaskHistory") : nullptr;
        auto* taskTable = dialog ? dialog->findChild<QTableWidget*>("profileTaskXpHistoryTable") : nullptr;
        auto* taskFilter = dialog ? dialog->findChild<QLineEdit*>("profileTaskXpHistoryFilter") : nullptr;
        auto* taskSummary = dialog ? dialog->findChild<QLabel*>("profileTaskXpHistorySummary") : nullptr;
        bool foundAwarded = false, foundPending = false;
        bool eventExportPassed = false, taskExportPassed = false;
        if (tabs && exportEvents && exportTasks && taskTable && taskFilter && taskSummary &&
            !exportEvents->accessibleName().isEmpty() && !exportTasks->accessibleDescription().isEmpty()) {
            QTimer::singleShot(0, [eventsCsvPath] {
                if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    picker->selectFile(eventsCsvPath);
                    static_cast<QDialog*>(picker)->accept();
                }
            });
            exportEvents->click();
            QFile eventsFile(eventsCsvPath);
            if (eventsFile.open(QIODevice::ReadOnly)) {
                const auto bytes = eventsFile.readAll();
                eventExportPassed = bytes.startsWith("\xEF\xBB\xBF") &&
                    bytes.contains(QString::fromUtf8("profile history marker").toUtf8()) &&
                    bytes.contains(QString::fromUtf8("Событие").toUtf8());
            }
            tabs->setCurrentIndex(1);
            if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/profile-task-xp-history.png");
            for (int row = 0; row < taskTable->rowCount(); ++row) {
                if (taskTable->item(row, 2)->text().contains(QString::fromUtf8("Awarded task")))
                    foundAwarded = taskTable->item(row, 5)->text() == "17" && taskTable->item(row, 6)->text() == "8";
                if (taskTable->item(row, 2)->text().contains(QString::fromUtf8("Pending task")))
                    foundPending = taskTable->item(row, 3)->text().contains(QString::fromUtf8("ждёт XP")) &&
                        taskTable->item(row, 5)->text() == QString::fromUtf8("—");
            }
            taskFilter->setText(QString::fromUtf8("Awarded"));
            const bool filters = taskTable->rowCount() == 1;
            QTimer::singleShot(0, [tasksCsvPath] {
                if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    picker->selectFile(tasksCsvPath);
                    static_cast<QDialog*>(picker)->accept();
                }
            });
            exportTasks->click();
            QFile tasksFile(tasksCsvPath);
            if (tasksFile.open(QIODevice::ReadOnly)) {
                const auto bytes = tasksFile.readAll();
                taskExportPassed = bytes.startsWith("\xEF\xBB\xBF") &&
                    bytes.contains(QString::fromUtf8("Awarded task").toUtf8()) &&
                    !bytes.contains(QString::fromUtf8("Pending task").toUtf8());
            }
            taskFilter->clear();
            foundAwarded = foundAwarded && filters && taskTable->rowCount() == 2;
            foundAwarded = foundAwarded && taskSummary->text().contains("17") && taskSummary->text().contains("8");
        }
        if (foundWallet && foundPassword && foundUnknown && foundSpirit && foundTrustExpired && foundTrustRevoked &&
            foundTrustFailure && foundAwarded && foundPending && eventExportPassed && taskExportPassed) dialog->accept();
    });
    profileHistory->click();
    QAction* adminAction = nullptr;
    for (auto* menu : window.findChildren<QMenu*>())
        for (auto* action : menu->actions())
            if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("admin123"); });
    adminAction->trigger();
    auto* adjust = window.findChild<QPushButton*>("adjustProfileWallet");
    if (!adjust || !adjust->isVisible()) return false;
    QFile auditFile(QString::fromStdWString((workspace.directory / "meta/profile-audit.log").wstring()));
    if (!auditFile.open(QIODevice::ReadOnly)) return false;
    const auto auditBefore = auditFile.readAll();
    auditFile.close();
    AppSetProfileAuditFailureHookForTests(true);
    bool auditFailureShown = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
    });
    watchdog.start(8000);
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* amount = dialog ? dialog->findChild<QDoubleSpinBox*>("walletAmount") : nullptr;
        auto* reason = dialog ? dialog->findChild<QLineEdit*>("walletReason") : nullptr;
        auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
        if (!dialog || !amount || !reason || !buttons) return;
        amount->setValue(12.5);
        reason->setText(QString::fromUtf8("Тест отката аудита"));
        QTimer::singleShot(0, [] {
            if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                confirm->button(QMessageBox::Yes)->click();
        });
        buttons->button(QDialogButtonBox::Save)->click();
        QTimer::singleShot(0, [&] {
            auto* current = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            auto* notice = current ? current->findChild<QLabel*>("walletNotice") : nullptr;
            auditFailureShown = current && current->objectName() == "walletAdjustmentDialog" && notice &&
                notice->text().contains(QString::fromUtf8("Все изменения отменены"));
            if (current) current->reject();
        });
    });
    adjust->click();
    watchdog.stop();
    AppSetProfileAuditFailureHookForTests(false);
    if (!auditFailureShown || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    if (!workspace.storage->set_active_profile(created->id)) return false;
    const auto afterAuditFailure = workspace.storage->load_profile();
    QFile auditAfterFile(QString::fromStdWString((workspace.directory / "meta/profile-audit.log").wstring()));
    if (!auditAfterFile.open(QIODevice::ReadOnly)) return false;
    const auto auditAfter = auditAfterFile.readAll();
    auditAfterFile.close();
    if (!afterAuditFailure || std::abs(afterAuditFailure->wallet_balance() - 51.0) > 0.001 || auditAfter != auditBefore) return false;
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* auditSource = window.findChild<QComboBox*>("auditSourceFilter");
    auto* auditTable = window.findChild<QTableWidget*>();
    if (!navigation || !auditSource || !auditTable) return false;
    navigation->setCurrentRow(7); auditSource->setCurrentIndex(5);
    bool committedTelemetry = false, rollbackTelemetry = false;
    for (int row = 0; row < auditTable->rowCount(); ++row) {
        const auto sourceLabel = auditTable->item(row, 0)->text();
        const auto event = auditTable->item(row, 6)->text();
        committedTelemetry |= sourceLabel == QString::fromUtf8("Core-событие") &&
            auditTable->item(row, 3)->text() == QString::fromUtf8("Операция кошелька") &&
            event == QString::fromUtf8("Wallet mutation committed with audit");
        rollbackTelemetry |= sourceLabel == QString::fromUtf8("Core-событие") &&
            auditTable->item(row, 3)->text() == QString::fromUtf8("Операция кошелька") &&
            event == QString::fromUtf8("Wallet mutation rolled back after failure");
    }
    if (!committedTelemetry || !rollbackTelemetry) return false;
    return true;
}

static bool TestMonthlyCompletionTrend() {
    const QDate current(2026, 9, 24);
    const QDate firstMonth(2025, 10, 1);
    auto entry = [](const QDate& date, const std::string& field, const std::string& status) {
        TaskAuditEntry item;
        item.timestamp = QDateTime(date, QTime(12, 0), Qt::LocalTime).toSecsSinceEpoch();
        item.field = field;
        item.newValue = status;
        return item;
    };
    std::vector<TaskAuditEntry> audit{
        entry(firstMonth, "status", u8"Выполнена"),
        entry(firstMonth.addDays(12), "status", u8"Выполнена"),
        entry(firstMonth.addDays(4), "status", u8"В работе"),
        entry(firstMonth.addMonths(11), "status", u8"Выполнена"),
        entry(firstMonth.addDays(-1), "status", u8"Выполнена"),
        entry(firstMonth.addDays(2), "priority", u8"Выполнена")};
    const auto counts = QtReportChart::BuildMonthlyCompletionTrend(audit, current);
    return counts[0] == 2 && counts[1] == 0 && counts[11] == 1 &&
        std::accumulate(counts.begin(), counts.end(), 0) == 3;
}

static bool TestStatisticsTrendBeyondAuditPageLimit() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    const auto today = QDate::currentDate();
    const auto firstMonth = QDate(today.year(), today.month(), 1).addMonths(-11);
    const auto markerDate = firstMonth.addDays(3);
    QFile audit(temp.path() + "/meta/task-audit.log");
    if (!audit.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream output(&audit); output.setEncoding(QStringConverter::Utf8);
    auto addCompletion = [&](const QDate& date, int index) {
        output << QDateTime(date, QTime(12, 0), Qt::LocalTime).toSecsSinceEpoch()
            << "|test|trend-" << index << "|status|В работе|Выполнена\n";
    };
    addCompletion(markerDate, 0);
    for (int index = 1; index <= 205; ++index) addCompletion(today, index);
    audit.close();
    const auto recentPage = LoadTaskAuditData(directory);
    if (recentPage.size() != 200 || std::any_of(recentPage.begin(), recentPage.end(), [](const auto& entry) { return entry.taskId == "trend-0"; }))
        return false;
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    QListWidget* navigation = window.findChild<QListWidget*>("navigation");
    QAction* adminAction = nullptr;
    for (auto* menu : window.findChildren<QMenu*>())
        for (auto* action : menu->actions())
            if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!navigation || !adminAction) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("admin123"); });
    adminAction->trigger();
    navigation->setCurrentRow(6);
    auto* chart = static_cast<QtReportChart*>(window.findChild<QWidget*>("statisticsStatusChart"));
    QApplication::processEvents();
    if (!chart || !chart->isVisible()) return false;
    const auto* chartAccessible = QAccessible::queryAccessibleInterface(chart);
    if (!chartAccessible) return false;
    const auto chartDescription = chartAccessible->text(QAccessible::Description);
    const int markerIndex = (markerDate.year() - firstMonth.year()) * 12 + markerDate.month() - firstMonth.month();
    if (chart->completionTrend()[size_t(markerIndex)] != 1 || chart->completionTrend()[11] != 205) return false;
    const auto markerLabel = markerDate.toString("yyyy-MM") + QStringLiteral(": 1");
    const auto currentLabel = today.toString("yyyy-MM") + QStringLiteral(": 205");
    if (!chartDescription.contains(markerLabel) || !chartDescription.contains(currentLabel)) return false;
    chart->setCompletionTrend(chart->completionTrend());
    return chart->accessibleDescription().count(QString::fromUtf8("Завершения по месяцам")) == 1;
}

static bool TestPipelineMap() {
    PipelineStep start; start.id = "start"; start.stageCode = "A"; start.title = "Start"; start.branch = "Main"; start.description = "Entry point"; start.nextIds = {"left", "right"};
    PipelineStep left; left.id = "left"; left.stageCode = "B1"; left.title = "Left branch"; left.branch = "Left"; left.owner = "Artist"; left.nextIds = {"removed-step"};
    PipelineStep right; right.id = "right"; right.stageCode = "B2"; right.title = "Right branch"; right.branch = "Right";
    bool inspected = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* view = dialog ? dialog->findChild<QGraphicsView*>("pipelineMapView") : nullptr;
        auto* summary = dialog ? dialog->findChild<QLabel*>("pipelineMapSummary") : nullptr;
        auto* details = dialog ? dialog->findChild<QTextBrowser*>("pipelineMapDetails") : nullptr;
        if (!dialog || !view || !view->scene() || !summary || !details) { if (dialog) dialog->reject(); return; }
        view->scene()->clearSelection();
        for (auto* item : view->scene()->items()) if (item->data(Qt::UserRole).toString() == "left") item->setSelected(true);
        inspected = summary->text().contains(QString::fromUtf8("Этапов: 3 · переходов: 3 · недоступных связей: 1")) &&
            details->toPlainText().contains("Left branch") && details->toPlainText().contains("removed-step");
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/pipeline-map.png"); }
        dialog->reject();
    });
    if (!ShowQtPipelineMap(nullptr, {start, left, right}) || !inspected) {
        std::cerr << "pipelineMap: graph, summary or node details failed\n";
        return false;
    }
    return true;
}

static bool TestCatalogProfessionFilter() {
    auto fail = [](const char* step) { std::cerr << "catalogProfessionFilter: " << step << '\n'; return false; };
    QTemporaryDir temp;
    if (!temp.isValid()) return fail("temp");
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    workspace.data.professions = {{"artist", "Artist", "Art"}};
    if (!AppSaveProfessionsData(workspace.directory, workspace.data.professions) ||
        !workspace.catalog.add_skill("Bound", 1.0, "Known profession", {}, {"artist"}) ||
        !workspace.catalog.add_skill("Unbound", 1.0, "No profession") ||
        !workspace.catalog.add_skill("Orphan", 1.0, "Missing profession", {}, {"removed-profession"})) return fail("fixture");
    QtWindow window(workspace);
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* filter = window.findChild<QComboBox*>("catalogProfessionFilter");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !filter || !table) return fail("widgets");
    int catalogPage = -1;
    for (int i = 0; i < navigation->count(); ++i)
        if (navigation->item(i)->text().contains(QString::fromUtf8("Навык"), Qt::CaseInsensitive)) catalogPage = i;
    if (catalogPage < 0) return fail("page");
    navigation->setCurrentRow(catalogPage);
    if (table->rowCount() != 3 || filter->findData("artist") < 0 || filter->findData("removed-profession") < 0) return fail("all choices");
    filter->setCurrentIndex(filter->findData("artist"));
    if (table->item(0, 0)->text() != "Bound") return fail("known filter");
    filter->setCurrentIndex(filter->findData("__none__"));
    if (table->item(0, 0)->text() != "Unbound") return fail("unbound filter");
    filter->setCurrentIndex(filter->findData("removed-profession"));
    if (table->item(0, 0)->text() != "Orphan") return fail("orphan filter");
    const auto saved = LoadQtDisplaySettings(workspace.directory);
    if (saved.catalogProfessionId != "removed-profession") return fail("persist");
    QtWindow reopened(workspace);
    auto* restoredFilter = reopened.findChild<QComboBox*>("catalogProfessionFilter");
    auto* restoredNavigation = reopened.findChild<QListWidget*>("navigation");
    auto* restoredTable = reopened.findChild<QTableWidget*>("records");
    if (!restoredFilter || !restoredNavigation || !restoredTable) return fail("restore widgets");
    restoredNavigation->setCurrentRow(catalogPage);
    if (restoredFilter->currentData().toString() != "removed-profession") return fail("restore selection");
    if (restoredTable->rowCount() != 1) return fail("restore rows");
    return true;
}

static bool TestDeadlineReminders() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
    auto displaySettings = LoadQtDisplaySettings(workspace.directory);
    displaySettings.minimizeToTray = trayAvailable;
    if (!SaveQtDisplaySettings(workspace.directory, displaySettings)) return false;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (!QDir().mkpath(temp.path() + "/meta")) return false;
    QFile reminderState(temp.path() + "/meta/qt-reminder-state.json");
    const auto reminderStateBytes = QJsonDocument(QJsonObject{{"version", 1}, {"lastCheckAt", qlonglong(now - 3600)}})
        .toJson(QJsonDocument::Compact);
    if (!reminderState.open(QIODevice::WriteOnly) || reminderState.write(reminderStateBytes) != reminderStateBytes.size()) { std::cerr << "deadline reminder state setup failed\n"; return false; }
    reminderState.close();
    TaskEntry upcoming;
    upcoming.id = "deadline-upcoming";
    upcoming.title = u8"Проверить сборку";
    upcoming.deadlineAt = now + 2 * 60 * 60;
    TaskEntry upcomingSecond;
    upcomingSecond.id = "deadline-upcoming-second";
    upcomingSecond.title = u8"Проверить релиз";
    upcomingSecond.deadlineAt = now + 3 * 60 * 60;
    TaskEntry overdue;
    overdue.id = "deadline-overdue";
    overdue.title = "Old deadline";
    overdue.deadlineAt = now - 60;
    TaskEntry missedWhileClosed;
    missedWhileClosed.id = "deadline-missed-while-closed";
    missedWhileClosed.title = u8"Срок прошёл во время перерыва";
    missedWhileClosed.deadlineAt = now - 1800;
    TaskEntry completed;
    completed.id = "deadline-completed";
    completed.title = "Already done";
    completed.deadlineAt = now - 30;
    completed.status = 2;
    workspace.data.tasks = {upcoming, overdue, missedWhileClosed, completed};
    if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) return false;
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* timer = window.findChild<QTimer*>("deadlineReminderTimer");
    auto* autoSyncTimer = window.findChild<QTimer*>("cloudAutoSyncTimer");
    if (!autoSyncTimer || !autoSyncTimer->isActive() || autoSyncTimer->interval() != 60000) return false;
    if (!timer || !QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) return false;
    const auto first = window.statusBar()->currentMessage();
    if (!first.contains(QString::fromUtf8("Проверить сборку")) || !first.contains(QString::fromUtf8("Срок задачи"))) return false;
    QFile reminderLog(temp.path() + "/meta/qt-application-log.json");
    if (!reminderLog.open(QIODevice::ReadOnly)) return false;
    const auto reminderEntries = QJsonDocument::fromJson(reminderLog.readAll()).array();
    int missedNoticeCount = 0;
    bool missedTaskIncluded = false;
    bool completedTaskIncluded = false;
    for (const auto& entry : reminderEntries) {
        const auto text = entry.toObject().value("message").toString();
        if (text.contains(QString::fromUtf8("С прошлого запуска срок прошёл"))) ++missedNoticeCount;
        missedTaskIncluded |= text.contains(QString::fromUtf8("Срок прошёл во время перерыва"));
        completedTaskIncluded |= text.contains(QString::fromUtf8("Already done"));
    }
    if (missedNoticeCount < 1 || !missedTaskIncluded || completedTaskIncluded) { std::cerr << "missed notice count=" << missedNoticeCount << " task=" << missedTaskIncluded << " completed=" << completedTaskIncluded << "\n"; return false; }
    const auto noticesAfterFirstCheck = missedNoticeCount;
    QFile updatedReminderState(temp.path() + "/meta/qt-reminder-state.json");
    if (!updatedReminderState.open(QIODevice::ReadOnly)) return false;
    const auto updatedWatermark = QJsonDocument::fromJson(updatedReminderState.readAll()).object().value("lastCheckAt").toVariant().toLongLong();
    if (updatedWatermark < now) return false;
    window.statusBar()->clearMessage();
    if (!QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) return false;
    if (!window.statusBar()->currentMessage().isEmpty()) return false;
    reminderLog.seek(0);
    const auto repeatedReminderEntries = QJsonDocument::fromJson(reminderLog.readAll()).array();
    missedNoticeCount = 0;
    for (const auto& entry : repeatedReminderEntries)
        missedNoticeCount += entry.toObject().value("message").toString().contains(QString::fromUtf8("С прошлого запуска срок прошёл"));
    if (missedNoticeCount != noticesAfterFirstCheck) { std::cerr << "missed notice count changed from " << noticesAfterFirstCheck << " to " << missedNoticeCount << "\n"; return false; }
    if (trayAvailable) {
        auto* tray = window.findChild<QSystemTrayIcon*>();
        if (!tray || !tray->isVisible()) return false;
        workspace.data.tasks.push_back(upcomingSecond);
        if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) return false;
        window.close();
        if (window.isVisible() || !QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) return false;
        QFile log(temp.path() + "/meta/qt-application-log.json");
        if (!log.open(QIODevice::ReadOnly)) return false;
        const auto document = QJsonDocument::fromJson(log.readAll());
        bool trayReminderLogged = false;
        for (const auto& entry : document.array())
            trayReminderLogged |= entry.toObject().value("message").toString().contains(QString::fromUtf8("Проверить релиз"));
        if (!trayReminderLogged) return false;
        auto* trayMenu = tray->contextMenu();
        if (!trayMenu || trayMenu->actions().isEmpty()) return false;
        trayMenu->actions().front()->trigger();
        QApplication::processEvents();
        if (!window.isVisible()) return false;
    }
    QAction* about = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->objectName() == QStringLiteral("aboutApplicationAction")) { about = action; break; }
    if (!about) return false;
    QString aboutText;
    QTimer::singleShot(0, [&aboutText] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (!box || box->objectName() != QStringLiteral("aboutApplicationDialog")) continue;
            aboutText = box->text();
            box->accept();
        }
    });
    about->trigger();
    if (!aboutText.contains(QString::fromUtf8("облачные операции")) ||
        !aboutText.contains(QString::fromUtf8("загрузка установщика новой версии")) ||
        !aboutText.contains(QString::fromUtf8(APP_VERSION)) ||
        !aboutText.contains(QString::fromUtf8("Роман Рощин")) ||
        !aboutText.contains(QString::fromUtf8("геймифицированный трекер навыков и задач"))) return false;
    return true;
}

static bool TestTaskActionNeededQuickFilter() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    PipelineStep active; active.id = "active"; active.stageCode = "01"; active.title = "In progress"; active.nextIds = {"final"};
    PipelineStep final; final.id = "final"; final.stageCode = "02"; final.title = "Handoff";
    workspace.data.pipelineSteps = {active, final};
    const auto now = QDateTime::currentSecsSinceEpoch();
    TaskEntry pendingXp; pendingXp.id = "pending-xp"; pendingXp.title = "Pending XP"; pendingXp.status = 2; pendingXp.pipelineStepId = "active";
    TaskEntry overdue; overdue.id = "overdue"; overdue.title = "Overdue"; overdue.deadlineAt = now - 3600; overdue.pipelineStepId = "active";
    TaskEntry missingStage; missingStage.id = "missing-stage"; missingStage.title = "Missing stage";
    TaskEntry unknownStage; unknownStage.id = "unknown-stage"; unknownStage.title = "Unknown stage"; unknownStage.pipelineStepId = "deleted-stage";
    TaskEntry openHandoff; openHandoff.id = "open-handoff"; openHandoff.title = "Open handoff"; openHandoff.pipelineStepId = "final";
    TaskEntry normal; normal.id = "normal"; normal.title = "Normal"; normal.deadlineAt = now + 3600; normal.pipelineStepId = "active";
    TaskEntry awarded; awarded.id = "awarded"; awarded.title = "XP already awarded"; awarded.status = 2;
    awarded.pipelineStepId = "active"; awarded.participants.push_back({"someone", 100, 10, 0, {}});
    workspace.data.tasks = {pendingXp, overdue, missingStage, unknownStage, openHandoff, normal, awarded};
    if (!AppSavePipelineData(directory, workspace.data.pipelineSteps) || !AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* filter = window.findChild<QComboBox*>("quickTaskFilter");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !filter || !table || filter->count() != 14 ||
        filter->itemText(8) != QString::fromUtf8("Требуют внимания") ||
        filter->itemText(9) != QString::fromUtf8("Сигналы пайплайна") ||
        filter->itemText(13) != QString::fromUtf8("Пайплайн: финал открыт")) return false;
    navigation->setCurrentRow(1);
    filter->setCurrentIndex(8);
    const std::set<std::string> expected{"pending-xp", "overdue", "missing-stage", "unknown-stage", "open-handoff"};
    std::set<std::string> actual;
    for (int row = 0; row < table->rowCount(); ++row)
        actual.insert(table->item(row, 0)->data(Qt::UserRole).toString().toUtf8().toStdString());
    if (actual != expected || LoadQtDisplaySettings(directory).taskQuickFilter != 8) return false;
    QtWorkspace reopenedWorkspace(directory);
    QtWindow reopened(reopenedWorkspace);
    auto* reopenedFilter = reopened.findChild<QComboBox*>("quickTaskFilter");
    return reopenedFilter && reopenedFilter->currentIndex() == 8;
}

static bool TestQtTaskCreationRangeAndSorting() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    const auto now = QDateTime::currentSecsSinceEpoch();
    TaskEntry old; old.id = "old"; old.title = "Old"; old.createdAt = now - 8 * 86400; old.priority = 3;
    TaskEntry recentLow; recentLow.id = "recent-low"; recentLow.title = "Recent low"; recentLow.createdAt = now - 6 * 86400; recentLow.priority = 0; recentLow.deadlineAt = now + 3600;
    TaskEntry recentHigh; recentHigh.id = "recent-high"; recentHigh.title = "Recent high"; recentHigh.createdAt = now - 86400; recentHigh.priority = 3; recentHigh.deadlineAt = now + 7200;
    TaskEntry noTimestamp; noTimestamp.id = "no-timestamp"; noTimestamp.title = "No timestamp"; noTimestamp.createdAt = 0; noTimestamp.priority = 2;
    workspace.data.tasks = {old, recentLow, recentHigh, noTimestamp};
    if (!AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* range = window.findChild<QComboBox*>("taskCreatedRange");
    auto* sort = window.findChild<QComboBox*>("taskSortMode");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !range || !sort || !table || range->count() != 5 || sort->count() != 3) return false;
    navigation->setCurrentRow(1);
    range->setCurrentIndex(1);
    if (table->rowCount() != 2) return false;
    sort->setCurrentIndex(1);
    if (table->rowCount() != 2 || table->item(0, 0)->data(Qt::UserRole).toString() != "recent-low") return false;
    sort->setCurrentIndex(2);
    if (table->rowCount() != 2 || table->item(0, 0)->data(Qt::UserRole).toString() != "recent-high" ||
        table->item(1, 0)->data(Qt::UserRole).toString() != "recent-low") return false;
    const auto saved = LoadQtDisplaySettings(directory);
    if (saved.taskCreatedRange != 1 || saved.taskSortMode != 2) return false;
    QtWorkspace reopenedWorkspace(directory);
    QtWindow reopened(reopenedWorkspace);
    auto* restoredRange = reopened.findChild<QComboBox*>("taskCreatedRange");
    auto* restoredSort = reopened.findChild<QComboBox*>("taskSortMode");
    if (!restoredRange || !restoredSort || restoredRange->currentIndex() != 1 || restoredSort->currentIndex() != 2) return false;
    auto clamped = saved; clamped.taskCreatedRange = 99; clamped.taskSortMode = -1;
    return SaveQtDisplaySettings(directory, clamped) && LoadQtDisplaySettings(directory).taskCreatedRange == 4 &&
        LoadQtDisplaySettings(directory).taskSortMode == 0;
}

static bool TestQtTaskAssigneeProfileFilter() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    const auto assigned = workspace.storage->create_profile(Profile("Assigned profile"));
    const auto participant = workspace.storage->create_profile(Profile("Participant profile"));
    if (!assigned || !participant) return false;
    TaskEntry assignedTask; assignedTask.id = "assigned"; assignedTask.title = "Assigned"; assignedTask.assignees = {assigned->id};
    TaskEntry participantTask; participantTask.id = "participant"; participantTask.title = "Participant";
    participantTask.participants.push_back({participant->id, 0, 0, 0, {}});
    TaskEntry unrelated; unrelated.id = "unrelated"; unrelated.title = "Unrelated";
    workspace.data.tasks = {assignedTask, participantTask, unrelated};
    if (!AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* filter = window.findChild<QComboBox*>("taskAssigneeFilter");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !filter || !table || filter->count() != 3) return false;
    navigation->setCurrentRow(1);
    if (table->rowCount() != 3 || filter->itemText(0) != QString::fromUtf8("Все профили")) return false;
    filter->setCurrentIndex(filter->findData(QString::fromStdString(participant->id)));
    if (table->rowCount() != 1 || table->item(0, 0)->data(Qt::UserRole).toString() != "participant" ||
        LoadQtDisplaySettings(directory).taskAssigneeProfileId != QString::fromStdString(participant->id)) return false;
    QtWorkspace reopenedWorkspace(directory);
    QtWindow reopened(reopenedWorkspace);
    auto* restored = reopened.findChild<QComboBox*>("taskAssigneeFilter");
    return restored && restored->currentData().toString() == QString::fromStdString(participant->id);
}

static bool TestQtVisibleTaskExports() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    TaskEntry visible; visible.id = "visible-task"; visible.title = "Visible, \"quoted\" task";
    visible.description = "first line\nsecond line"; visible.createdAt = QDateTime::currentSecsSinceEpoch(); visible.score = 8;
    TaskEntry hidden; hidden.id = "hidden-task"; hidden.title = "Hidden unrelated task";
    workspace.data.tasks = {visible, hidden};
    if (!AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* search = window.findChild<QLineEdit*>("search");
    auto* table = window.findChild<QTableWidget*>("records");
    auto* exportButton = window.findChild<QToolButton*>("exportTasks");
    auto* csvAction = window.findChild<QAction*>("exportTasksCsv");
    auto* txtAction = window.findChild<QAction*>("exportTasksTxt");
    if (!navigation || !search || !table || !exportButton || !csvAction || !txtAction) return false;
    navigation->setCurrentRow(1);
    if (!exportButton->isVisible() || table->rowCount() != 2) return false;
    search->setText(QString::fromUtf8("visible"));
    if (table->rowCount() != 1 || table->item(0, 0)->data(Qt::UserRole).toString() != "visible-task") return false;
    const auto csvPath = temp.path() + "/visible-tasks.csv";
    QTimer::singleShot(0, [csvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(csvPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    csvAction->trigger();
    QFile csv(csvPath);
    if (!csv.open(QIODevice::ReadOnly)) return false;
    const auto csvBytes = csv.readAll();
    const auto csvText = QString::fromUtf8(csvBytes.mid(3));
    if (!csvBytes.startsWith("\xEF\xBB\xBF") || !csvText.contains("\"Visible, \"\"quoted\"\" task\"") ||
        !csvText.contains("\"first line\nsecond line\"") || csvText.contains("Hidden unrelated task") ||
        !window.statusBar()->currentMessage().contains(QString::fromUtf8("Экспортировано видимых задач: 1"))) return false;
    const auto txtPath = temp.path() + "/visible-tasks.txt";
    QTimer::singleShot(0, [txtPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(txtPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    txtAction->trigger();
    QFile txt(txtPath);
    if (!txt.open(QIODevice::ReadOnly)) return false;
    const auto txtBytes = txt.readAll();
    return txtBytes.startsWith("\xEF\xBB\xBF") && txtBytes.contains("Visible, \"quoted\" task") &&
        txtBytes.contains("first line second line") && !txtBytes.contains("Hidden unrelated task");
}

static bool TestQtTaskFilterReset() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    ProjectEntry project; project.id = "reset-project"; project.name = "Reset project";
    PipelineStep step; step.id = "reset-step"; step.title = "Reset step";
    workspace.data.projects = {project}; workspace.data.pipelineSteps = {step};
    TaskEntry task; task.id = "reset-task"; task.title = "Visible after reset"; task.projectId = project.id;
    task.pipelineStepId = step.id; task.createdAt = QDateTime::currentSecsSinceEpoch();
    workspace.data.tasks = {task};
    if (!AppSaveProjects(directory, workspace.data.projects) || !AppSavePipelineData(directory, workspace.data.pipelineSteps) ||
        !AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* search = window.findChild<QLineEdit*>("search");
    auto* reset = window.findChild<QPushButton*>("taskFilterReset");
    auto* status = window.findChild<QComboBox*>("statusFilter");
    auto* priority = window.findChild<QComboBox*>("priorityFilter");
    auto* quick = window.findChild<QComboBox*>("quickTaskFilter");
    auto* age = window.findChild<QComboBox*>("taskCreatedRange");
    auto* sort = window.findChild<QComboBox*>("taskSortMode");
    auto* projectFilter = window.findChild<QComboBox*>("taskProjectFilter");
    auto* pipelineFilter = window.findChild<QComboBox*>("taskPipelineFilter");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !search || !reset || !status || !priority || !quick || !age || !sort || !projectFilter || !pipelineFilter || !table) return false;
    navigation->setCurrentRow(1);
    status->setCurrentIndex(1); priority->setCurrentIndex(1); quick->setCurrentIndex(6);
    age->setCurrentIndex(1); sort->setCurrentIndex(2); projectFilter->setCurrentIndex(1); pipelineFilter->setCurrentIndex(1);
    search->setText(QString::fromUtf8("no match"));
    if (table->rowCount() != 0 || !reset->isVisible()) return false;
    reset->click();
    const auto settings = LoadQtDisplaySettings(directory);
    return search->text().isEmpty() && table->rowCount() == 1 && status->currentIndex() == 0 &&
        priority->currentIndex() == 0 && quick->currentIndex() == 0 && age->currentIndex() == 0 &&
        sort->currentIndex() == 0 && projectFilter->currentIndex() == 0 && pipelineFilter->currentIndex() == 0 &&
        settings.taskStatusFilter == 0 && settings.taskPriorityFilter == 0 && settings.taskQuickFilter == 0 &&
        settings.taskCreatedRange == 0 && settings.taskSortMode == 0 && settings.taskProjectId.isEmpty() &&
        settings.taskPipelineStepId.isEmpty() && settings.taskAssigneeProfileId.isEmpty();
}

static bool TestQtVisibleTaskSelectionTools() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    TaskEntry first; first.id = "visible-first"; first.title = "Visible first";
    TaskEntry second; second.id = "visible-second"; second.title = "Visible second";
    TaskEntry hidden; hidden.id = "hidden-third"; hidden.title = "Hidden third";
    workspace.data.tasks = {first, second, hidden};
    if (!AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* search = window.findChild<QLineEdit*>("search");
    auto* selectionTools = window.findChild<QToolButton*>("taskSelectionTools");
    auto* selectVisible = window.findChild<QAction*>("selectVisibleTasks");
    auto* clearSelection = window.findChild<QAction*>("clearTaskSelection");
    auto* table = window.findChild<QTableWidget*>("records");
    auto* bulkEdit = window.findChild<QPushButton*>("bulkTaskEdit");
    if (!navigation || !search || !selectionTools || !selectVisible || !clearSelection || !table || !bulkEdit) return false;
    navigation->setCurrentRow(1);
    if (selectionTools->isVisible()) return false;
    QAction* adminAction = nullptr;
    for (auto* menu : window.findChildren<QMenu*>()) for (auto* action : menu->actions())
        if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("admin123"); });
    adminAction->trigger();
    if (!selectionTools->isVisible()) return false;
    search->setText(QString::fromUtf8("Visible"));
    if (table->rowCount() != 2) return false;
    selectVisible->trigger();
    if (table->selectionModel()->selectedRows().size() != 2 || !bulkEdit->isEnabled()) return false;
    for (const auto& index : table->selectionModel()->selectedRows())
        if (table->item(index.row(), 0)->data(Qt::UserRole).toString() == "hidden-third") return false;
    clearSelection->trigger();
    return table->selectionModel()->selectedRows().empty() && !bulkEdit->isEnabled();
}

static bool TestQtTaskTableDateAndAssignees() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    const auto profile = workspace.storage->create_profile(Profile("Visible assignee"));
    if (!profile) return false;
    TaskEntry task; task.id = "table-metadata"; task.title = "Table metadata"; task.createdAt = 1700000000;
    task.assignees = {profile->id};
    workspace.data.tasks = {task};
    if (!AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* table = window.findChild<QTableWidget*>("records");
    auto* search = window.findChild<QLineEdit*>("search");
    if (!navigation || !table || !search) return false;
    navigation->setCurrentRow(1);
    if (table->columnCount() != 8 || table->horizontalHeaderItem(1)->text() != QString::fromUtf8("Дата") ||
        table->horizontalHeaderItem(3)->text() != QString::fromUtf8("Исполнители") || table->rowCount() != 1 ||
        table->item(0, 1)->text() != QDateTime::fromSecsSinceEpoch(task.createdAt).toString("dd.MM.yyyy HH:mm") ||
        table->item(0, 3)->text() != QString::fromUtf8("Visible assignee")) return false;
    search->setText(QString::fromUtf8("Visible assignee"));
    return table->rowCount() == 1 && table->item(0, 0)->data(Qt::UserRole).toString() == "table-metadata";
}

static bool TestQtTaskAttentionBadges() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    PipelineStep active; active.id = "active"; active.title = "Active"; active.nextIds = {"final"};
    PipelineStep final; final.id = "final"; final.title = "Final";
    PipelineStep branch; branch.id = "branch"; branch.title = "Branch"; branch.nextIds = {"final", "other"};
    PipelineStep other; other.id = "other"; other.title = "Other";
    PipelineStep brokenEdge; brokenEdge.id = "broken-edge"; brokenEdge.title = "Broken edge"; brokenEdge.nextIds = {"deleted-next"};
    workspace.data.pipelineSteps = {active, final, branch, other, brokenEdge};
    const auto now = QDateTime::currentSecsSinceEpoch();
    TaskEntry xp; xp.id = "xp-pending"; xp.title = "XP pending"; xp.status = 2; xp.pipelineStepId = active.id;
    TaskEntry overdue; overdue.id = "overdue-task"; overdue.title = "Overdue task"; overdue.deadlineAt = now - 60; overdue.pipelineStepId = active.id;
    TaskEntry missing; missing.id = "missing-stage"; missing.title = "Missing stage";
    TaskEntry unknown; unknown.id = "unknown-stage"; unknown.title = "Unknown stage"; unknown.pipelineStepId = "deleted";
    TaskEntry handoff; handoff.id = "open-handoff"; handoff.title = "Open handoff"; handoff.pipelineStepId = final.id;
    TaskEntry normal; normal.id = "normal-task"; normal.title = "Normal task"; normal.deadlineAt = now + 3600; normal.pipelineStepId = active.id;
    TaskEntry awarded; awarded.id = "awarded-task"; awarded.title = "Awarded task"; awarded.status = 2;
    awarded.pipelineStepId = active.id; awarded.participants.push_back({"done", 100, 20, 0, {}});
    TaskEntry branching; branching.id = "branching-task"; branching.title = "Branching task"; branching.pipelineStepId = branch.id;
    TaskEntry brokenNext; brokenNext.id = "broken-next-task"; brokenNext.title = "Broken next task"; brokenNext.pipelineStepId = brokenEdge.id;
    workspace.data.tasks = {xp, overdue, missing, unknown, handoff, normal, awarded, branching, brokenNext};
    if (!AppSavePipelineData(directory, workspace.data.pipelineSteps) || !AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !table) return false;
    navigation->setCurrentRow(1);
    auto cellFor = [table](const QString& id) -> QTableWidgetItem* {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toString() == id) return table->item(row, 0);
        return nullptr;
    };
    const auto* xpCell = cellFor("xp-pending");
    const auto* overdueCell = cellFor("overdue-task");
    const auto* missingCell = cellFor("missing-stage");
    const auto* unknownCell = cellFor("unknown-stage");
    const auto* handoffCell = cellFor("open-handoff");
    const auto* normalCell = cellFor("normal-task");
    const auto* awardedCell = cellFor("awarded-task");
    const auto* branchingCell = cellFor("branching-task");
    const auto* brokenNextCell = cellFor("broken-next-task");
    const bool badgesPass = xpCell && xpCell->text().contains("XP") && xpCell->text().contains('!') && xpCell->toolTip().contains(QString::fromUtf8("Ожидает выдачи XP")) &&
        overdueCell && overdueCell->text().contains('!') && overdueCell->toolTip().contains(QString::fromUtf8("Просрочен срок")) &&
        missingCell && missingCell->toolTip().contains(QString::fromUtf8("Не указан этап")) &&
        unknownCell && unknownCell->toolTip().contains(QString::fromUtf8("не найден")) &&
        handoffCell && handoffCell->toolTip().contains(QString::fromUtf8("handoff")) &&
        branchingCell && branchingCell->text().contains('!') && branchingCell->toolTip().contains(QString::fromUtf8("Ветвящийся этап")) &&
        brokenNextCell && brokenNextCell->text().contains('!') && brokenNextCell->toolTip().contains(QString::fromUtf8("Следующий этап пайплайна не найден")) &&
        normalCell && !normalCell->text().contains('!') && !normalCell->text().contains("XP") &&
        awardedCell && !awardedCell->text().contains("XP");
    auto* quick = window.findChild<QComboBox*>("quickTaskFilter");
    if (!badgesPass || !quick) return false;
    quick->setCurrentIndex(9);
    const auto savedSettings = LoadQtDisplaySettings(directory);
    auto* summary = window.findChild<QLabel*>("summary");
    auto* pipelineSummary = window.findChild<QLabel*>("taskPipelineSummary");
    auto* search = window.findChild<QLineEdit*>("search");
    if (table->rowCount() != 5 || savedSettings.taskQuickFilter != 9 || !summary || !pipelineSummary || !search ||
        !pipelineSummary->text().contains(QString::fromUtf8("href=\"risk:branching\""))) return false;
    const std::vector<std::pair<int, std::set<std::string>>> riskFilters{
        {10, {"missing-stage"}}, {11, {"unknown-stage", "broken-next-task"}},
        {12, {"branching-task"}}, {13, {"open-handoff"}}};
    for (const auto& [index, expectedIds] : riskFilters) {
        quick->setCurrentIndex(index);
        std::set<std::string> actualIds;
        for (int row = 0; row < table->rowCount(); ++row)
            actualIds.insert(table->item(row, 0)->data(Qt::UserRole).toString().toStdString());
        if (actualIds != expectedIds) return false;
    }
    if (!QMetaObject::invokeMethod(pipelineSummary, "linkActivated", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("risk:branching"))) || quick->currentIndex() != 12 || table->rowCount() != 1 ||
        LoadQtDisplaySettings(directory).taskQuickFilter != 12) return false;
    search->setText(QString::fromUtf8("Branching task"));
    return table->rowCount() == 1 && pipelineSummary->text().contains(QString::fromUtf8("ветвление 1"));
}

static bool TestQtTaskFocusAndOverdueTint() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    const auto now = QDateTime::currentSecsSinceEpoch();
    TaskEntry overdueLow; overdueLow.id = "focus-overdue-low"; overdueLow.title = "Overdue low";
    overdueLow.deadlineAt = now - 7200; overdueLow.priority = 0;
    TaskEntry overdueCritical; overdueCritical.id = "focus-overdue-critical"; overdueCritical.title = "Overdue critical";
    overdueCritical.deadlineAt = now - 3600; overdueCritical.priority = 3;
    TaskEntry futureCritical; futureCritical.id = "focus-future-critical"; futureCritical.title = "Future critical";
    futureCritical.deadlineAt = now + 3600; futureCritical.priority = 3;
    workspace.data.tasks = {overdueLow, overdueCritical, futureCritical};
    if (!AppSaveTasks(directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* table = window.findChild<QTableWidget*>("records");
    if (!navigation || !table) return false;
    navigation->setCurrentRow(1);
    auto rowFor = [table](const QString& id) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toString() == id) return row;
        return -1;
    };
    const int focusRow = rowFor("focus-overdue-critical");
    const int overdueRow = rowFor("focus-overdue-low");
    const int futureRow = rowFor("focus-future-critical");
    if (focusRow < 0 || overdueRow < 0 || futureRow < 0) return false;
    const auto focusTint = table->item(focusRow, 0)->background().color();
    const auto overdueTint = table->item(overdueRow, 0)->background().color();
    const bool hasFocusBadge = table->item(focusRow, 0)->text().contains(QString::fromUtf8("Фокус"));
    const bool colorsMatch = focusTint.isValid() && focusTint != overdueTint && overdueTint.isValid() &&
        table->item(futureRow, 0)->background().style() == Qt::NoBrush;
    if (!hasFocusBadge || !colorsMatch)
        std::cerr << "focus badge=" << hasFocusBadge << " colors=" << colorsMatch
                  << " focus=" << focusTint.name().toStdString() << " overdue=" << overdueTint.name().toStdString()
                  << "\n";
    return hasFocusBadge && colorsMatch;
}

static bool TestBulkTaskEditsUi() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    const auto executor = workspace.storage->create_profile(Profile(u8"Активный исполнитель"));
    const auto archivedExecutor = workspace.storage->create_profile(Profile(u8"Архивный исполнитель"));
    if (!executor || !archivedExecutor || !workspace.storage->set_archived(archivedExecutor->id, true)) return false;
    ProjectEntry bulkProject;
    bulkProject.id = "bulk-project";
    bulkProject.name = "Bulk project";
    PipelineStep bulkStep;
    bulkStep.id = "bulk-step";
    bulkStep.title = "Bulk step";
    workspace.data.projects = {bulkProject};
    workspace.data.pipelineSteps = {bulkStep};
    if (!AppSaveProjects(workspace.directory, workspace.data.projects) ||
        !AppSavePipelineData(workspace.directory, workspace.data.pipelineSteps)) return false;
    TaskEntry first;
    first.id = "bulk-first";
    first.title = "First bulk task";
    first.status = 0;
    TaskEntry second;
    second.id = "bulk-second";
    second.title = "Second bulk task";
    second.status = 0;
    TaskEntry completed;
    completed.id = "bulk-completed";
    completed.title = "Completed task";
    completed.status = 2;
    workspace.data.tasks = {first, second, completed};
    if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) return false;
    workspace.reload();
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* table = window.findChild<QTableWidget*>("records");
    auto* bulk = window.findChild<QPushButton*>("bulkTaskEdit");
    if (!navigation || !table || !bulk) return false;
    navigation->setCurrentRow(1);
    if (table->selectionMode() != QAbstractItemView::ExtendedSelection || bulk->isVisible()) return false;

    QAction* adminAction = nullptr;
    for (auto* menu : window.findChildren<QMenu*>())
        for (auto* action : menu->actions())
            if (action->objectName() == QStringLiteral("adminLoginAction")) adminAction = action;
    if (!adminAction) return false;
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("admin123"); });
    adminAction->trigger();
    if (!bulk->isVisible()) return false;
    auto select = [table](int row) {
        table->selectionModel()->select(table->model()->index(row, 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    };
    auto rowFor = [table](const QString& id) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toString() == id) return row;
        return -1;
    };
    table->setCurrentCell(0, 0);
    table->clearSelection();
    select(rowFor("bulk-first")); select(rowFor("bulk-second"));
    if (!bulk->isEnabled()) return false;
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "bulkTaskEditDialog") return;
        dialog->findChild<QComboBox*>("bulkTaskOperation")->setCurrentIndex(
            dialog->findChild<QComboBox*>("bulkTaskOperation")->findData(QStringLiteral("status")));
        auto* target = dialog->findChild<QComboBox*>("bulkTaskTarget");
        target->setCurrentIndex(target->findData(1));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    bulk->click();
    const auto saved = LoadTasksData(workspace.directory);
    const auto firstSaved = std::find_if(saved.begin(), saved.end(), [](const auto& task) { return task.id == "bulk-first"; });
    const auto secondSaved = std::find_if(saved.begin(), saved.end(), [](const auto& task) { return task.id == "bulk-second"; });
    const auto completedSaved = std::find_if(saved.begin(), saved.end(), [](const auto& task) { return task.id == "bulk-completed"; });
    if (firstSaved == saved.end() || secondSaved == saved.end() || completedSaved == saved.end() ||
        firstSaved->status != 1 || secondSaved->status != 1 || completedSaved->status != 2 || workspace.data.taskAudit.size() != 2)
        return false;
    QFile bulkTelemetry(temp.path() + "/meta/qt-application-log.json");
    if (!bulkTelemetry.open(QIODevice::ReadOnly)) return false;
    const auto bulkEvents = QJsonDocument::fromJson(bulkTelemetry.readAll()).array();
    const auto bulkCommitted = std::find_if(bulkEvents.begin(), bulkEvents.end(), [](const auto& value) {
        const auto entry = value.toObject();
        return entry.value("source").toString() == QStringLiteral("CoreTaskMutation") &&
            entry.value("message").toString() == QStringLiteral("Bulk task update committed: changed=2 skipped=0");
    });
    if (bulkCommitted == bulkEvents.end()) return false;
    for (const auto& value : bulkEvents) if (value.toObject().value("source").toString() == QStringLiteral("CoreTaskMutation")) {
        const auto text = value.toObject().value("message").toString();
        if (text.contains(QStringLiteral("First bulk task")) || text.contains(QStringLiteral("bulk-first")) ||
            text.contains(QString::fromStdString(executor->id))) return false;
    }
    table->clearSelection();
    select(rowFor("bulk-first")); select(rowFor("bulk-second"));
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "bulkTaskEditDialog") return;
        auto* operation = dialog->findChild<QComboBox*>("bulkTaskOperation");
        operation->setCurrentIndex(operation->findData(QStringLiteral("priority")));
        auto* target = dialog->findChild<QComboBox*>("bulkTaskTarget");
        target->setCurrentIndex(target->findData(2));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    bulk->click();
    const auto reprioritized = LoadTasksData(workspace.directory);
    for (const auto& task : reprioritized)
        if ((task.id == "bulk-first" || task.id == "bulk-second") && task.priority != 2) return false;
    if (workspace.data.taskAudit.size() != 4) return false;
    auto applyBulkValue = [&](const QString& mode, const QString& targetValue) {
        table->clearSelection();
        select(rowFor("bulk-first")); select(rowFor("bulk-second"));
        bool applied = false;
        QTimer::singleShot(0, [mode, targetValue, &applied] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "bulkTaskEditDialog") return;
            auto* operation = dialog->findChild<QComboBox*>("bulkTaskOperation");
            operation->setCurrentIndex(operation->findData(mode));
            if (mode == QStringLiteral("deadline")) {
                dialog->findChild<QDateTimeEdit*>("bulkTaskDeadline")->setDateTime(QDateTime::fromSecsSinceEpoch(1900000000));
            } else {
                auto* target = dialog->findChild<QComboBox*>("bulkTaskTarget");
                target->setCurrentIndex(target->findData(targetValue));
            }
            applied = true;
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        bulk->click();
        return applied;
    };
    if (!applyBulkValue(QStringLiteral("project"), QStringLiteral("bulk-project"))) return false;
    if (!applyBulkValue(QStringLiteral("pipeline"), QStringLiteral("bulk-step"))) return false;
    if (!applyBulkValue(QStringLiteral("deadline"), QString())) return false;
    const auto bulkRefs = LoadTasksData(workspace.directory);
    for (const auto& task : bulkRefs) {
        if (task.id != "bulk-first" && task.id != "bulk-second") continue;
        if (task.projectId != "bulk-project" || task.project != "Bulk project" ||
            task.pipelineStepId != "bulk-step" || task.pipelineStep != "Bulk step" || task.deadlineAt != 1900000000) return false;
    }
    if (workspace.data.taskAudit.size() != 10) return false;
    auto firstInMemory = std::find_if(workspace.data.tasks.begin(), workspace.data.tasks.end(),
        [](const auto& task) { return task.id == "bulk-first"; });
    if (firstInMemory == workspace.data.tasks.end()) return false;
    firstInMemory->participants.push_back({executor->id, 100, 100, 0, "xp-awarded"});
    if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) return false;
    table->clearSelection();
    select(rowFor("bulk-first")); select(rowFor("bulk-second"));
    bool assigneePickerChecked = false;
    QTimer::singleShot(0, [executorId = executor->id, archivedId = archivedExecutor->id, &assigneePickerChecked] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "bulkTaskEditDialog") return;
        auto* operation = dialog->findChild<QComboBox*>("bulkTaskOperation");
        operation->setCurrentIndex(operation->findData(QStringLiteral("assignees")));
        auto* list = dialog->findChild<QListWidget*>("bulkTaskAssignees");
        auto* apply = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        const int activeIndex = list->findItems(QString::fromUtf8("Активный исполнитель"), Qt::MatchExactly).empty()
            ? -1 : list->row(list->findItems(QString::fromUtf8("Активный исполнитель"), Qt::MatchExactly).front());
        int archivedIndex = -1;
        for (int index = 0; index < list->count(); ++index)
            if (list->item(index)->data(Qt::UserRole).toString() == QString::fromStdString(archivedId)) archivedIndex = index;
        if (activeIndex >= 0 && archivedIndex >= 0 && !apply->isEnabled() &&
            !list->item(archivedIndex)->flags().testFlag(Qt::ItemIsUserCheckable)) {
            list->item(activeIndex)->setCheckState(Qt::Checked);
            assigneePickerChecked = apply->isEnabled() &&
                list->item(activeIndex)->data(Qt::UserRole).toString() == QString::fromStdString(executorId);
        }
        if (assigneePickerChecked) apply->click(); else dialog->reject();
    });
    bulk->click();
    if (!assigneePickerChecked) return false;
    const auto assigned = LoadTasksData(workspace.directory);
    const auto assignedFirst = std::find_if(assigned.begin(), assigned.end(), [](const auto& task) { return task.id == "bulk-first"; });
    const auto assignedSecond = std::find_if(assigned.begin(), assigned.end(), [](const auto& task) { return task.id == "bulk-second"; });
    if (assignedFirst == assigned.end() || assignedSecond == assigned.end() ||
        assignedFirst->assignees != std::vector<std::string>{executor->id} ||
        assignedSecond->assignees != std::vector<std::string>{executor->id} || workspace.data.taskAudit.size() != 11 ||
        workspace.data.taskAudit.back().taskId != "bulk-second" || workspace.data.taskAudit.back().field != "assignees" ||
        !window.statusBar()->currentMessage().contains(QString::fromUtf8("пропущено: 1"))) return false;
    table->setCurrentCell(0, 0);
    table->clearSelection();
    select(rowFor("bulk-first")); select(rowFor("bulk-completed"));
    if (!bulk->isEnabled()) return false;
    bool completedStatusUnavailable = false;
    QTimer::singleShot(0, [&completedStatusUnavailable] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* operation = dialog ? dialog->findChild<QComboBox*>("bulkTaskOperation") : nullptr;
        completedStatusUnavailable = operation && operation->count() == 5 &&
            operation->findData(QStringLiteral("status")) < 0 &&
            operation->findData(QStringLiteral("assignees")) >= 0 &&
            operation->currentData().toString() == QStringLiteral("priority");
        if (dialog) dialog->reject();
    });
    bulk->click();
    auto* bulkDelete = window.findChild<QPushButton*>("bulkTaskDelete");
    table->clearSelection();
    select(rowFor("bulk-second")); select(rowFor("bulk-completed"));
    if (!completedStatusUnavailable || !bulkDelete || !bulkDelete->isVisible() || !bulkDelete->isEnabled()) return false;
    QTimer::singleShot(0, [] {
        auto* modal = QApplication::activeModalWidget();
        if (auto* confirm = qobject_cast<QMessageBox*>(modal)) {
            QTimer::singleShot(0, [] {
                if (auto* notice = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    notice->accept();
                }
            });
            confirm->button(QMessageBox::Yes)->click();
        }
    });
    bulkDelete->click();
    const auto afterDelete = LoadTasksData(workspace.directory);
    return afterDelete.size() == 1 && afterDelete.front().id == "bulk-first" &&
        std::count_if(workspace.data.taskAudit.begin(), workspace.data.taskAudit.end(), [](const auto& entry) {
            return entry.field == "delete";
        }) == 2;
}

static bool TestPomodoro() {
    auto fail = [](int step) { std::cerr << "Pomodoro step " << step << " failed\n"; return false; };
    QTemporaryDir temp;
    QDir().mkpath(temp.path() + "/meta");
    QDir().mkpath(temp.path() + "/music");
    { QFile music(temp.path() + "/music/focus.mp3"); if (!music.open(QIODevice::WriteOnly)) return false; music.write("test"); }
    { QFile settings(temp.path() + "/meta/ui.ini"); if (!settings.open(QIODevice::WriteOnly)) return false;
      settings.write("[style]\nunknown=kept\n[pomodoro]\nsoundEnabled=0\nsoundFocus=../outside.mp3\n"); }
    QtPomodoro panel(nullptr, std::filesystem::u8path(temp.path().toUtf8().constData()), 2, 1, 1, 2);
    int rewards = 0;
    panel.setRewardHandler([&](int minutes, std::int64_t started) {
        if (minutes == 0 && started > 0) ++rewards;
        return QString::fromUtf8("Тестовая награда");
    });
    panel.resize(720, 580); panel.show(); QApplication::processEvents();
    auto* start = panel.findChild<QPushButton*>("pomodoroStart");
    auto* pause = panel.findChild<QPushButton*>("pomodoroPause");
    auto* next = panel.findChild<QPushButton*>("pomodoroNext");
    auto* reset = panel.findChild<QPushButton*>("pomodoroReset");
    auto* time = panel.findChild<QLabel*>("pomodoroTime");
    auto* phase = panel.findChild<QLabel*>("pomodoroPhase");
    auto* cycles = panel.findChild<QLabel*>("pomodoroCycles");
    if (!start || !pause || !next || !reset || !time || !phase || !cycles || time->text() != "00:02") return fail(1);
    panel.setAdministrator(true);
    auto* focusSound = panel.findChild<QComboBox*>("pomodoroFocusSound");
    if (!panel.findChild<QWidget*>("pomodoroSoundEnabled")->isVisible() || focusSound->findData("music/focus.mp3") < 0 ||
        focusSound->findData("../outside.mp3") >= 0) return fail(12);
    focusSound->setCurrentIndex(focusSound->findData("music/focus.mp3"));
    start->click(); panel.advanceSecondsForTest(1);
    if (time->text() != "00:01" || !pause->isVisible()) return fail(2);
    pause->click(); panel.advanceSecondsForTest(2);
    if (time->text() != "00:01" || start->text() != QString::fromUtf8("Продолжить")) return fail(3);
    start->click(); panel.advanceSecondsForTest(1);
    if (!next->isVisible() || !cycles->text().contains("1 / 2") || rewards != 1) return fail(4);
    next->click();
    if (phase->text() != QString::fromUtf8("Перерыв")) return fail(5);
    panel.advanceSecondsForTest(1); next->click(); panel.advanceSecondsForTest(2);
    if (!next->isVisible() || !cycles->text().contains("2 / 2") || rewards != 2) return fail(6);
    next->click();
    if (phase->text() != QString::fromUtf8("Длинный перерыв")) return fail(7);
    const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); panel.grab().save(artifacts + "/pomodoro.png"); }
    panel.findChild<QSpinBox*>("pomodoroWorkMinutes")->setValue(30);
    panel.findChild<QCheckBox*>("pomodoroAutoAdvance")->setChecked(true);
    panel.findChild<QPushButton*>("pomodoroSaveSettings")->click();
    QFile settings(temp.path() + "/meta/ui.ini"); if (!settings.open(QIODevice::ReadOnly)) return false;
    const auto saved = settings.readAll();
    if (!saved.contains("unknown=kept") || !saved.contains("workMinutes=30") || !saved.contains("autoAdvance=1") ||
        !saved.contains("soundFocus=music/focus.mp3") || saved.contains("../outside.mp3")) { std::cerr << saved.constData() << '\n'; return fail(8); }
    settings.close();
#ifdef _WIN32
    const auto settingsPath = std::filesystem::u8path((temp.path() + "/meta/ui.ini").toUtf8().toStdString());
    const auto lock = CreateFileW(settingsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return fail(10);
    panel.findChild<QSpinBox*>("pomodoroWorkMinutes")->setValue(31);
    panel.findChild<QPushButton*>("pomodoroSaveSettings")->click();
    CloseHandle(lock);
    QFile unchanged(temp.path() + "/meta/ui.ini"); unchanged.open(QIODevice::ReadOnly);
    if (unchanged.readAll() != saved || !panel.findChild<QLabel*>("pomodoroStatus")->text().contains(QString::fromUtf8("Не удалось"))) return fail(11);
#endif
    panel.advanceSecondsForTest(1);
    if (phase->text() != QString::fromUtf8("Фокус") || !pause->isVisible()) return fail(9);
    reset->click();
    if (phase->text() != QString::fromUtf8("Фокус") || time->text() != "30:00" || pause->isVisible()) return fail(13);
    const int rewardsBeforeSkippedFocus = rewards;
    panel.quickToggle(); panel.advanceSecondsForTest(1); panel.quickNext();
    if (rewards != rewardsBeforeSkippedFocus || phase->text() != QString::fromUtf8("Перерыв") || !pause->isVisible()) return fail(14);
    panel.quickReset();
    return phase->text() == QString::fromUtf8("Фокус") && time->text() == "30:00" && !pause->isVisible();
}

static bool TestPomodoroQuickHeader() {
    auto fail = [](int step) { std::cerr << "Pomodoro quick header step " << step << " failed\n"; return false; };
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    QtWindow window(workspace);
    window.show(); QApplication::processEvents();
    auto* button = window.findChild<QToolButton*>("pomodoroQuickButton");
    auto* menu = window.findChild<QMenu*>("pomodoroQuickMenu");
    auto* panel = static_cast<QtPomodoro*>(window.findChild<QWidget*>("pomodoroPanel"));
    auto* navigation = window.findChild<QListWidget*>("navigation");
    if (!button || !menu || !panel || !navigation || !button->isVisible() ||
        button->accessibleName() != QString::fromUtf8("Быстрое управление Pomodoro")) return fail(1);
    QAction* toggle = nullptr; QAction* next = nullptr; QAction* reset = nullptr; QAction* openPage = nullptr;
    for (auto* action : menu->actions()) {
        if (action->objectName() == "pomodoroQuickToggle") toggle = action;
        else if (action->objectName() == "pomodoroQuickNext") next = action;
        else if (action->objectName() == "pomodoroQuickReset") reset = action;
        else if (action->objectName() == "pomodoroOpenPage") openPage = action;
    }
    if (!toggle || !next || !reset || !openPage || button->text() != panel->quickSummary()) return fail(2);
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    if (toggle->text() != QString::fromUtf8("Старт фокуса") || !next->isEnabled()) return fail(3);
    toggle->trigger();
    if (panel->quickToggleText() != QString::fromUtf8("Пауза") || button->text() != panel->quickSummary()) return fail(4);
    panel->advanceSecondsForTest(1);
    toggle->trigger();
    if (panel->quickToggleText() != QString::fromUtf8("Продолжить")) return fail(5);
    next->trigger();
    if (panel->quickSummary().section(QStringLiteral(" · "), 0, 0) != QString::fromUtf8("Перерыв") ||
        panel->quickToggleText() != QString::fromUtf8("Пауза")) return fail(6);
    reset->trigger();
    if (panel->quickSummary().section(QStringLiteral(" · "), 0, 0) != QString::fromUtf8("Фокус")) return fail(7);
    openPage->trigger();
    return navigation->currentRow() == 8 || fail(8);
}

static bool TestCloudQuickHeader() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    QtWindow window(workspace);
    window.show(); QApplication::processEvents();
    auto* button = window.findChild<QToolButton*>("quickCloudSync");
    auto* menu = window.findChild<QMenu*>("quickCloudSyncMenu");
    auto* navigation = window.findChild<QListWidget*>("navigation");
    if (!button || !menu || !navigation || !button->isVisible() || button->icon().isNull() ||
        button->accessibleName() != QString::fromUtf8("Быстрая синхронизация с облаком") ||
        !button->toolTip().contains(QString::fromUtf8("Облако отключено"))) return false;
    QAction* sync = nullptr; QAction* openCloud = nullptr;
    for (auto* action : menu->actions()) {
        if (action->objectName() == "quickCloudSyncNow") sync = action;
        else if (action->objectName() == "quickCloudOpenPage") openCloud = action;
    }
    if (!sync || !openCloud) return false;
    openCloud->trigger(); QApplication::processEvents();
    if (navigation->currentRow() != 13) return false;
    button->click(); QApplication::processEvents();
    return window.statusBar()->currentMessage() == QString::fromUtf8("Облако отключено в настройках.");
}

static bool TestNavigationClock() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    QtWindow window(workspace);
    window.show(); QApplication::processEvents();
    auto* clock = window.findChild<QWidget*>("localAnalogClock");
    auto* digital = window.findChild<QLabel*>("localClockDigital");
    auto* timer = window.findChild<QTimer*>("localClockTimer");
    if (!clock || !digital || !timer || !clock->isVisible() || !timer->isActive() || timer->interval() != 1000 ||
        digital->accessibleName() != QString::fromUtf8("Текущее местное время") ||
        !QTime::fromString(digital->text(), "HH:mm:ss").isValid()) return false;
    const auto pixels = clock->grab().toImage();
    if (pixels.size() != QSize(76, 76) || pixels.pixelColor(pixels.rect().center()) != window.palette().color(QPalette::Highlight)) return false;
    auto* accessible = QAccessible::queryAccessibleInterface(clock);
    return accessible && accessible->text(QAccessible::Name) == QString::fromUtf8("Аналоговые часы");
}

static bool TestRulesEditor() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    bool saved = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* base = dialog ? dialog->findChild<QSpinBox*>("rulesLevelBase") : nullptr;
        auto* repeat = dialog ? dialog->findChild<QDoubleSpinBox*>("rulesRepeat") : nullptr;
        auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
        auto* savePreset = dialog ? dialog->findChild<QPushButton*>("rulesSavePreset") : nullptr;
        if (!base || !repeat || !buttons || !savePreset) { if (auto* modal = qobject_cast<QDialog*>(dialog)) modal->reject(); return; }
        base->setValue(2345); repeat->setValue(0.55);
        QTimer::singleShot(0, [] {
            auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!input) return;
            input->findChild<QLineEdit*>()->setText(QStringLiteral("Migration baseline"));
            input->accept();
        });
        savePreset->click();
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/rules-editor.png"); }
        buttons->button(QDialogButtonBox::Save)->click(); saved = true;
    });
    if (!ShowRulesEditor(nullptr, workspace) || !saved) return false;
    const auto loaded = LoadGameplayConfig(workspace.directory);
    if (loaded.levelBaseXp != 2345 || std::abs(loaded.repeatRewardFactor - 0.55f) > 0.0001f || GetGameplayConfig().levelBaseXp != 2345) return false;
    QFile file(temp.path() + "/meta/gameplay.ini"); if (!file.open(QIODevice::ReadOnly)) return false;
    const auto before = file.readAll(); file.close(); if (!before.startsWith("\xEF\xBB\xBF")) return false;
    QFile presetFile(temp.path() + "/meta/qt-rules-presets.json");
    if (!presetFile.open(QIODevice::ReadOnly)) return false;
    const auto presets = QJsonDocument::fromJson(presetFile.readAll()).array(); presetFile.close();
    if (presets.size() != 1 || presets.first().toObject().value("name").toString() != QStringLiteral("Migration baseline") ||
        presets.first().toObject().value("rules").toObject().value("levelBaseXp").toInt() != 2345) return false;
    QFile historyFile(temp.path() + "/meta/qt-rules-history.json");
    if (!historyFile.open(QIODevice::ReadOnly)) return false;
    const auto history = QJsonDocument::fromJson(historyFile.readAll()).array(); historyFile.close();
    if (history.size() != 1 || !history.first().toObject().value("changes").toString().contains(QString::fromUtf8("База уровня")) ||
        history.first().toObject().value("before").toObject().value("levelBaseXp").toInt() != 1500 ||
        history.first().toObject().value("after").toObject().value("levelBaseXp").toInt() != 2345) return false;
    bool restoredPreset = false, historyViewed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* combo = dialog ? dialog->findChild<QComboBox*>("rulesPresetCombo") : nullptr;
        auto* base = dialog ? dialog->findChild<QSpinBox*>("rulesLevelBase") : nullptr;
        auto* apply = dialog ? dialog->findChild<QPushButton*>("rulesApplyPreset") : nullptr;
        auto* historyButton = dialog ? dialog->findChild<QPushButton*>("rulesHistory") : nullptr;
        if (!combo || !base || !apply || !historyButton || combo->findData(QStringLiteral("Migration baseline")) < 0) {
            if (auto* modal = qobject_cast<QDialog*>(dialog)) modal->reject(); return;
        }
        combo->setCurrentIndex(combo->findData(QStringLiteral("Migration baseline")));
        base->setValue(1234); apply->click(); restoredPreset = base->value() == 2345;
        QTimer::singleShot(0, [&] {
            auto* historyDialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            auto* table = historyDialog ? historyDialog->findChild<QTableWidget*>("rulesHistoryTable") : nullptr;
            historyViewed = table && table->rowCount() == 1 && table->item(0, 1)->text().contains(QString::fromUtf8("База уровня"));
            if (historyDialog) historyDialog->accept();
        });
        historyButton->click();
        qobject_cast<QDialog*>(dialog)->reject();
    });
    if (ShowRulesEditor(nullptr, workspace) || !restoredPreset || !historyViewed || LoadGameplayConfig(workspace.directory).levelBaseXp != 2345) return false;
    QTemporaryDir corruptTemp; if (!corruptTemp.isValid()) return false;
    QtWorkspace corruptWorkspace(std::filesystem::u8path(corruptTemp.path().toUtf8().constData()));
    const QByteArray corruptPresetBytes("{broken preset data");
    QFile corruptPreset(corruptTemp.path() + "/meta/qt-rules-presets.json");
    if (!corruptPreset.open(QIODevice::WriteOnly) || corruptPreset.write(corruptPresetBytes) != corruptPresetBytes.size()) return false;
    corruptPreset.close();
    bool corruptRejected = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* save = dialog ? dialog->findChild<QPushButton*>("rulesSavePreset") : nullptr;
        auto* apply = dialog ? dialog->findChild<QPushButton*>("rulesApplyPreset") : nullptr;
        auto* notice = dialog ? dialog->findChild<QLabel*>("rulesNotice") : nullptr;
        corruptRejected = save && apply && !save->isEnabled() && !apply->isEnabled() && notice && !notice->text().isEmpty();
        if (auto* modal = qobject_cast<QDialog*>(dialog)) modal->reject();
    });
    if (ShowRulesEditor(nullptr, corruptWorkspace) || !corruptRejected || !corruptPreset.open(QIODevice::ReadOnly) ||
        corruptPreset.readAll() != corruptPresetBytes) return false;
#ifdef _WIN32
    const auto path = (workspace.directory / "meta/gameplay.ini").wstring();
    const auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    bool failureShown = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        dialog->findChild<QSpinBox*>("rulesLevelBase")->setValue(3456);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        failureShown = !dialog->findChild<QLabel*>("rulesNotice")->text().isEmpty();
        qobject_cast<QDialog*>(dialog)->reject();
    });
    const bool blockedAccepted = ShowRulesEditor(nullptr, workspace);
    CloseHandle(lock);
    if (blockedAccepted || !failureShown) return false;
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto after = file.readAll(); file.close(); if (after != before) return false;
#endif
    return true;
}

static bool TestDisplaySettings(QApplication& app) {
    const auto originalAppFont = app.font();
    const auto originalAppStyleSheet = app.styleSheet();
    const auto originalBasePointSize = app.property("forgeBasePointSize");
    QTemporaryDir accessibilityTemp;
    if (!accessibilityTemp.isValid()) return false;
    QtDisplaySettings largeTextSettings;
    largeTextSettings.scalePercent = 200;
    const auto accessibilityDirectory = std::filesystem::u8path(accessibilityTemp.path().toUtf8().constData());
    if (!SaveQtDisplaySettings(accessibilityDirectory, largeTextSettings) ||
        LoadQtDisplaySettings(accessibilityDirectory).scalePercent != 200) {
        std::cerr << "200% accessibility text scale did not survive save and reload\n";
        return false;
    }
    QtLayoutPreset largeTextPreset;
    largeTextPreset.name = QString::fromUtf8("Увеличенный текст");
    largeTextPreset.scalePercent = 200;
    QtLayoutPreset restoredLargeTextPreset;
    QString accessibilityError;
    if (!SaveQtLayoutPreset(accessibilityDirectory, largeTextPreset, &accessibilityError) ||
        !LoadQtLayoutPreset(accessibilityDirectory, largeTextPreset.name, &restoredLargeTextPreset) ||
        restoredLargeTextPreset.scalePercent != 200) {
        std::cerr << "200% accessibility text scale did not survive layout preset save and reload: "
                  << accessibilityError.toUtf8().constData() << '\n';
        return false;
    }
    {
        QtWorkspace largeTextWorkspace(accessibilityDirectory);
        QtWindow largeTextWindow(largeTextWorkspace);
        if (!(largeTextWindow.windowState() & Qt::WindowMaximized) ||
            !largeTextWindow.findChild<QScrollArea*>("pageContentScrollArea") ||
            !largeTextWindow.findChild<QScrollArea*>("headerScrollArea")) {
            std::cerr << "large text did not enable the expanded, scrollable Qt layout\n";
            return false;
        }
    }
    app.setFont(originalAppFont);
    app.setStyleSheet(originalAppStyleSheet);
    app.setProperty("forgeBasePointSize", originalBasePointSize);
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QDir().mkpath(temp.path() + "/meta"); QFile seed(temp.path() + "/meta/ui.ini");
    if (!seed.open(QIODevice::WriteOnly) || seed.write("\xEF\xBB\xBF[other]\nunknown=kept\n\n[style]\nwindowRounding=14\nframeRounding=10\nscrollbarRounding=12\ngrabRounding=8\nwindowPadding=12 14\nframePadding=7 5\nitemSpacing=9 4\n\n[projects]\nfilter=legacy-project-query\nsortMode=3\noverdueOnly=1\nxpPendingOnly=1\n") < 0) return false; seed.close();
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtLayoutPreset quickPreset;
    quickPreset.scalePercent = 110;
    quickPreset.windowOpacityPercent = 84;
    quickPreset.spacingPercent = 120;
    quickPreset.cornerRadius = 12;
    quickPreset.compactRows = true;
    quickPreset.fullscreen = true;
    quickPreset.decorated = true;
    quickPreset.windowBackgrounds[0] = QStringLiteral("ui/backgrounds/keep.png");
    quickPreset.backgroundAlpha = 0.7;
    quickPreset.backgroundTiled = true;
    quickPreset.backgroundTileScale = 1.5;
    if (!ApplyQtBuiltInLayoutPreset(QString::fromUtf8("Минимализм"), &quickPreset) ||
        quickPreset.scalePercent != 100 || quickPreset.windowOpacityPercent != 98 || quickPreset.spacingPercent != 110 ||
        quickPreset.cornerRadius != 8 || quickPreset.compactRows || !quickPreset.fullscreen || quickPreset.decorated ||
        quickPreset.backgroundAlpha != 0.18 || quickPreset.backgroundTiled ||
        quickPreset.windowBackgrounds[0] != QStringLiteral("ui/backgrounds/keep.png") || quickPreset.backgroundTileScale != 1.5)
        return false;
    quickPreset.decorated = true;
    if (!ApplyQtBuiltInLayoutPreset(QString::fromUtf8("Презентация"), &quickPreset) ||
        quickPreset.scalePercent != 125 || quickPreset.windowOpacityPercent != 100 || quickPreset.spacingPercent != 110 ||
        quickPreset.cornerRadius != 8 || quickPreset.compactRows || !quickPreset.fullscreen || !quickPreset.decorated ||
        quickPreset.backgroundAlpha != 0.18 || quickPreset.backgroundTiled) return false;
    if (!ApplyQtBuiltInLayoutPreset(QString::fromUtf8("Компактный"), &quickPreset) ||
        quickPreset.scalePercent != 90 || quickPreset.windowOpacityPercent != 95 || quickPreset.spacingPercent != 80 ||
        quickPreset.cornerRadius != 4 || !quickPreset.compactRows || !quickPreset.fullscreen || !quickPreset.decorated)
        return false;
    const auto unsupportedPresetBefore = quickPreset;
    if (ApplyQtBuiltInLayoutPreset(QString::fromUtf8("Техно-стекло"), &quickPreset) ||
        quickPreset.scalePercent != unsupportedPresetBefore.scalePercent ||
        quickPreset.windowOpacityPercent != unsupportedPresetBefore.windowOpacityPercent ||
        quickPreset.windowBackgrounds != unsupportedPresetBefore.windowBackgrounds) return false;
    QDir().mkpath(temp.path() + "/ui/backgrounds");
    QImage backgroundImage(16, 12, QImage::Format_ARGB32_Premultiplied);
    backgroundImage.fill(QColor(210, 40, 80, 220));
    const bool imageSaved = backgroundImage.save(temp.path() + "/ui/backgrounds/reference.png", "PNG");
    const auto listedBackgrounds = ListQtBackgroundImages(directory);
    const auto loadedBackground = LoadQtBackgroundImage(directory, "ui/backgrounds/reference.png");
    if (!imageSaved || !listedBackgrounds.contains("ui/backgrounds/reference.png") || loadedBackground.size() != QSize(16, 12) ||
        !LoadQtBackgroundImage(directory, "ui/backgrounds/../reference.png").isNull() ||
        !LoadQtBackgroundImage(directory, "C:/outside.png").isNull()) { QImageReader probe(temp.path() + "/ui/backgrounds/reference.png", "png"); std::cerr << "Qt background image validation/listing failed saved=" << imageSaved << " listed=" << listedBackgrounds.join(',').toUtf8().constData() << " size=" << loadedBackground.width() << 'x' << loadedBackground.height() << " reader=" << probe.canRead() << " readerSize=" << probe.size().width() << 'x' << probe.size().height() << " error=" << probe.errorString().toUtf8().constData() << " base=" << QFileInfo(temp.path() + "/ui/backgrounds").canonicalFilePath().toUtf8().constData() << " file=" << QFileInfo(temp.path() + "/ui/backgrounds/reference.png").canonicalFilePath().toUtf8().constData() << '\n'; return false; }
    auto settings = LoadQtDisplaySettings(directory);
    if (settings.projectFilter != QStringLiteral("legacy-project-query") || settings.projectSortMode != 3 ||
        settings.windowRounding != 14 || settings.frameRounding != 10 || settings.scrollbarRounding != 12 ||
        settings.grabRounding != 8 || settings.windowPaddingX != 12 || settings.windowPaddingY != 14 ||
        settings.framePaddingX != 7 || settings.framePaddingY != 5 || settings.itemSpacingX != 9 || settings.itemSpacingY != 4 ||
        !settings.projectsOverdueOnly || !settings.projectsXpPendingOnly) return false;
    settings.auditSourceFilter = 5; settings.lastPage = 16; settings.taskQuickFilter = 13;
    settings.projectFilter = QString::fromUtf8("remember project query");
    QDir().mkpath(temp.path() + "/meta/ui-presets");
    QFile legacyPreset(temp.path() + "/meta/ui-presets/LegacyLayout.ini");
    if (!legacyPreset.open(QIODevice::WriteOnly) ||
        legacyPreset.write("[style]\nfontScale=1.25\nalpha=0.78\nwindowRounding=15\nframeRounding=9\nscrollbarRounding=11\ngrabRounding=7\nwindowPadding=13 15\nframePadding=6 3\nitemSpacing=6 4\nwindowFullscreen=1\nwindowDecorated=0\ncustomColors=1\nbackgroundAlpha=0.4\nbackgroundTiled=1\nbackgroundTileScale=1.5\n[backgrounds]\nПрофиль=ui/backgrounds/reference.png\n[profile]\ntrusted=secret-value\n") < 0)
        return false;
    legacyPreset.close();
    QtLayoutPreset migratedPreset;
    if (!LoadQtLayoutPreset(directory, "LegacyLayout", &migratedPreset) ||
        migratedPreset.scalePercent != 125 || migratedPreset.windowOpacityPercent != 78 || migratedPreset.spacingPercent != 80 ||
        migratedPreset.cornerRadius != 8 || !migratedPreset.compactRows ||
        migratedPreset.windowRounding != 15 || migratedPreset.frameRounding != 9 || migratedPreset.scrollbarRounding != 11 ||
        migratedPreset.grabRounding != 7 || migratedPreset.windowPaddingX != 13 || migratedPreset.windowPaddingY != 15 ||
        migratedPreset.framePaddingX != 6 || migratedPreset.framePaddingY != 3 || migratedPreset.itemSpacingX != 6 || migratedPreset.itemSpacingY != 4 ||
        !migratedPreset.fullscreen || migratedPreset.decorated ||
        migratedPreset.windowBackgrounds[0] != "ui/backgrounds/reference.png" ||
        qAbs(migratedPreset.backgroundAlpha - 0.4) > 0.001 || !migratedPreset.backgroundTiled ||
        qAbs(migratedPreset.backgroundTileScale - 1.5) > 0.001 ||
        IsQtLayoutPresetDeletable(directory, "LegacyLayout")) { std::cerr << "Qt background legacy preset import failed path=" << migratedPreset.windowBackgrounds[0].toUtf8().constData() << " alpha=" << migratedPreset.backgroundAlpha << " tile=" << migratedPreset.backgroundTiled << " scale=" << migratedPreset.backgroundTileScale << '\n'; return false; }
    migratedPreset.name = QString::fromUtf8("Моя компоновка / 1");
    migratedPreset.fullscreen = false; migratedPreset.decorated = true;
    QString presetError;
    if (!SaveQtLayoutPreset(directory, migratedPreset, &presetError) || !presetError.isEmpty() ||
        !ListQtLayoutPresets(directory).contains(QString::fromUtf8("Моя компоновка 1")) ||
        !IsQtLayoutPresetDeletable(directory, QString::fromUtf8("Моя компоновка 1"))) return false;
    QtLayoutPreset roundTrip;
    if (!LoadQtLayoutPreset(directory, QString::fromUtf8("Моя компоновка 1"), &roundTrip) ||
        roundTrip.scalePercent != 125 || roundTrip.windowOpacityPercent != 78 || roundTrip.cornerRadius != 8 || roundTrip.decorated != migratedPreset.decorated)
        { std::cerr << "Qt layout preset round-trip failed\n"; return false; }
    if (roundTrip.windowBackgrounds[0] != "ui/backgrounds/reference.png" ||
        qAbs(roundTrip.backgroundAlpha - 0.4) > 0.001 || !roundTrip.backgroundTiled ||
        qAbs(roundTrip.backgroundTileScale - 1.5) > 0.001) return false;
    if (!DeleteQtLayoutPreset(directory, roundTrip.name, &presetError) ||
        !QFileInfo::exists(temp.path() + "/meta/ui-presets/LegacyLayout.ini") ||
        IsQtLayoutPresetDeletable(directory, "LegacyLayout")) return false;
    QFile corruptPresets(temp.path() + "/meta/qt-layout-presets.json");
    if (!corruptPresets.open(QIODevice::WriteOnly) || corruptPresets.write("{broken") != 7) return false;
    corruptPresets.close();
    QtLayoutPreset rejectedPreset; rejectedPreset.name = "Must not overwrite";
    if (SaveQtLayoutPreset(directory, rejectedPreset, &presetError) ||
        !corruptPresets.open(QIODevice::ReadOnly) || corruptPresets.readAll() != "{broken") return false;
    corruptPresets.close();
    if (!corruptPresets.remove()) return false;
    QFile legacyBeforeFile(temp.path() + "/meta/ui-presets/LegacyLayout.ini");
    if (!legacyBeforeFile.open(QIODevice::ReadOnly)) return false;
    const auto legacyBytesBefore = legacyBeforeFile.readAll();
    legacyBeforeFile.close();
    settings.profileViewMode = 2;
    settings.profileSkillSort = 3; settings.profileSkillWeightCategory = 4;
    settings.profileSkillWeightMin = 0.7; settings.profileSkillWeightMax = 1.4;
    settings.reportComparePrevious = true; settings.reportView = 3;
    settings.windowOpacityPercent = 100;
    settings.windowRounding = 13; settings.frameRounding = 11; settings.scrollbarRounding = 12; settings.grabRounding = 7;
    settings.windowPaddingX = 10; settings.windowPaddingY = 11; settings.framePaddingX = 6; settings.framePaddingY = 5;
    settings.itemSpacingX = 9; settings.itemSpacingY = 4;
    settings.deadlineNotificationsWhenClosed = true;
    if (!SaveQtDisplaySettings(directory, settings) || !LoadQtDisplaySettings(directory).deadlineNotificationsWhenClosed) return false;
    settings.deadlineNotificationsWhenClosed = false;
    if (!SaveQtDisplaySettings(directory, settings)) return false;
    settings.logShowInfo = false; settings.logShowWarning = true; settings.logShowError = false;
    settings.logSourceFilter = QStringLiteral("Qt");
    settings.logFilter = QStringLiteral("remember this log query");
    settings.logAutoScroll = false; settings.logCompactView = true; bool saved = false;
    QStringList missingDialogAccessibleNames;
    const auto paletteBeforeQuickPresets = app.palette().color(QPalette::Window);
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget(); auto* scale = dialog->findChild<QComboBox*>("qtScale");
        scale->setCurrentIndex(scale->findData(125)); dialog->findChild<QCheckBox*>("qtCompactRows")->setChecked(true);
        auto* opacity = dialog->findChild<QSlider*>("qtWindowOpacity");
        auto* spacing = dialog->findChild<QComboBox*>("qtSpacing");
        auto* rounding = dialog->findChild<QComboBox*>("qtCornerRadius");
        auto* presetList = dialog->findChild<QComboBox*>("qtLayoutPresetList");
        auto* applyPreset = dialog->findChild<QPushButton*>("qtLayoutPresetApply");
        auto* builtInPreset = dialog->findChild<QComboBox*>("qtBuiltInLayoutPreset");
        auto* applyBuiltInPreset = dialog->findChild<QPushButton*>("qtBuiltInLayoutPresetApply");
        auto* presetName = dialog->findChild<QLineEdit*>("qtLayoutPresetName");
        auto* savePreset = dialog->findChild<QPushButton*>("qtLayoutPresetSave");
        auto* geometryToggle = dialog->findChild<QToolButton*>("qtAdvancedGeometryToggle");
        if (!opacity || !spacing || !rounding || !presetList || !applyPreset || !presetName || !savePreset ||
            !builtInPreset || !applyBuiltInPreset || !geometryToggle) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        geometryToggle->click();
        QApplication::processEvents();
        missingDialogAccessibleNames.append(MissingAccessibleNames(dialog));
        auto* windowRounding = dialog->findChild<QDoubleSpinBox*>("qtWindowRounding");
        auto* frameRounding = dialog->findChild<QDoubleSpinBox*>("qtFrameRounding");
        auto* scrollbarRounding = dialog->findChild<QDoubleSpinBox*>("qtScrollbarRounding");
        auto* grabRounding = dialog->findChild<QDoubleSpinBox*>("qtGrabRounding");
        auto* windowPaddingX = dialog->findChild<QDoubleSpinBox*>("qtWindowPaddingX");
        auto* windowPaddingY = dialog->findChild<QDoubleSpinBox*>("qtWindowPaddingY");
        auto* framePaddingX = dialog->findChild<QDoubleSpinBox*>("qtFramePaddingX");
        auto* framePaddingY = dialog->findChild<QDoubleSpinBox*>("qtFramePaddingY");
        auto* itemSpacingX = dialog->findChild<QDoubleSpinBox*>("qtItemSpacingX");
        auto* itemSpacingY = dialog->findChild<QDoubleSpinBox*>("qtItemSpacingY");
        if (!windowRounding || !frameRounding || !scrollbarRounding || !grabRounding || !windowPaddingX ||
            !windowPaddingY || !framePaddingX || !framePaddingY || !itemSpacingX || !itemSpacingY) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        builtInPreset->setCurrentText(QString::fromUtf8("Компактный"));
        applyBuiltInPreset->click();
        if (scale->currentData().toInt() != 90 || opacity->value() != 95 || spacing->currentData().toInt() != 80 ||
            rounding->currentData().toInt() != 4 || !dialog->findChild<QCheckBox*>("qtCompactRows")->isChecked()) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        builtInPreset->setCurrentText(QString::fromUtf8("Презентация"));
        applyBuiltInPreset->click();
        if (scale->currentData().toInt() != 125 || opacity->value() != 100 || spacing->currentData().toInt() != 110 ||
            rounding->currentData().toInt() != 8 || dialog->findChild<QCheckBox*>("qtCompactRows")->isChecked()) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        builtInPreset->setCurrentText(QString::fromUtf8("Минимализм"));
        applyBuiltInPreset->click();
        if (scale->currentData().toInt() != 100 || opacity->value() != 98 || spacing->currentData().toInt() != 110 ||
            rounding->currentData().toInt() != 8 || dialog->findChild<QCheckBox*>("qtDecorated")->isChecked() ||
            app.palette().color(QPalette::Window) != paletteBeforeQuickPresets) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        rounding->setCurrentIndex(rounding->findData(4));
        if (windowRounding->value() != 4 || frameRounding->value() != 4) { qobject_cast<QDialog*>(dialog)->reject(); return; }
        rounding->setCurrentIndex(rounding->findData(8));
        windowRounding->setValue(13); frameRounding->setValue(11);
        if (windowRounding->value() != 13 || frameRounding->value() != 11 || itemSpacingX->value() != 9 || itemSpacingY->value() != 4) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        windowRounding->setValue(13); frameRounding->setValue(11); scrollbarRounding->setValue(12); grabRounding->setValue(7);
        windowPaddingX->setValue(10); windowPaddingY->setValue(11); framePaddingX->setValue(6); framePaddingY->setValue(5);
        itemSpacingX->setValue(9); itemSpacingY->setValue(4);
        opacity->setValue(86);
        presetName->setText(QString::fromUtf8("Интерфейс Qt"));
        savePreset->click();
        if (!IsQtLayoutPresetDeletable(directory, QString::fromUtf8("Интерфейс Qt"))) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        presetList->setCurrentIndex(presetList->findData(QString::fromUtf8("Интерфейс Qt")));
        applyPreset->click();
        if (windowRounding->value() != 13 || frameRounding->value() != 11 || scrollbarRounding->value() != 12 ||
            grabRounding->value() != 7 || windowPaddingX->value() != 10 || windowPaddingY->value() != 11 ||
            framePaddingX->value() != 6 || framePaddingY->value() != 5 || itemSpacingX->value() != 9 || itemSpacingY->value() != 4) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        presetList->setCurrentIndex(presetList->findData("LegacyLayout"));
        applyPreset->click();
        if (scale->currentData().toInt() != 125 || spacing->currentData().toInt() != 80 ||
            opacity->value() != 78 ||
            rounding->currentData().toInt() != 8 || !dialog->findChild<QCheckBox*>("qtFullscreen")->isChecked() ||
            dialog->findChild<QCheckBox*>("qtDecorated")->isChecked() || windowRounding->value() != 15 ||
            frameRounding->value() != 9 || scrollbarRounding->value() != 11 || grabRounding->value() != 7 ||
            windowPaddingX->value() != 13 || windowPaddingY->value() != 15 || framePaddingX->value() != 6 ||
            framePaddingY->value() != 3 || itemSpacingX->value() != 6 || itemSpacingY->value() != 4) {
            qobject_cast<QDialog*>(dialog)->reject(); return;
        }
        builtInPreset->setCurrentText(QString::fromUtf8("Минимализм"));
        applyBuiltInPreset->click();
        if (scale->currentData().toInt() != 100 || opacity->value() != 98 || spacing->currentData().toInt() != 110 ||
            rounding->currentData().toInt() != 8) { qobject_cast<QDialog*>(dialog)->reject(); return; }
        // Applying an imported preset changes layout controls only. Keep this test's requested final values.
        scale->setCurrentIndex(scale->findData(125));
        opacity->setValue(86);
        spacing->setCurrentIndex(spacing->findData(120));
        rounding->setCurrentIndex(rounding->findData(8));
        windowRounding->setValue(13); frameRounding->setValue(11); scrollbarRounding->setValue(12); grabRounding->setValue(7);
        windowPaddingX->setValue(10); windowPaddingY->setValue(11); framePaddingX->setValue(6); framePaddingY->setValue(5);
        itemSpacingX->setValue(9); itemSpacingY->setValue(4);
        dialog->findChild<QCheckBox*>("qtCompactRows")->setChecked(true);
        dialog->findChild<QCheckBox*>("qtFullscreen")->setChecked(false);
        dialog->findChild<QCheckBox*>("qtDecorated")->setChecked(true);
        auto* tray = dialog->findChild<QCheckBox*>("qtMinimizeToTray");
        auto* background = dialog->findChild<QCheckBox*>("qtDeadlineNotificationsWhenClosed");
        const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
        if (!tray || tray->isEnabled() != trayAvailable || !background || background->isEnabled()) { qobject_cast<QDialog*>(dialog)->reject(); return; }
        tray->setChecked(trayAvailable);
        QTimer::singleShot(0, [directory, &missingDialogAccessibleNames] {
            auto* backgroundsDialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!backgroundsDialog || backgroundsDialog->objectName() != "qtBackgroundSettings") {
                std::cerr << "Qt background dialog did not open\n";
                if (backgroundsDialog) backgroundsDialog->reject();
                return;
            }
            missingDialogAccessibleNames.append(MissingAccessibleNames(backgroundsDialog));
            auto* page = backgroundsDialog->findChild<QComboBox*>("qtBackgroundPage0");
            auto* alpha = backgroundsDialog->findChild<QSlider*>("qtBackgroundAlpha");
            auto* tiled = backgroundsDialog->findChild<QCheckBox*>("qtBackgroundTiled");
            auto* tileScale = backgroundsDialog->findChild<QComboBox*>("qtBackgroundTileScale");
            auto* buttons = backgroundsDialog->findChild<QDialogButtonBox*>();
            if (!page || !alpha || !tiled || !tileScale || !buttons) { std::cerr << "Qt background controls missing\n"; backgroundsDialog->reject(); return; }
            if (alpha->value() != 18 || tiled->isChecked()) { std::cerr << "Minimal layout did not apply its background defaults\n"; backgroundsDialog->reject(); return; }
            page->setCurrentIndex(page->findData("ui/backgrounds/reference.png"));
            alpha->setValue(55); tiled->setChecked(true); tileScale->setCurrentIndex(tileScale->findData(1.5));
            std::cerr << "Nested background selection index=" << page->currentIndex() << " data=" << page->currentData().toString().toUtf8().constData() << " alpha=" << alpha->value() << '\n';
            buttons->button(QDialogButtonBox::Save)->click();
        });
        auto* backgroundsButton = dialog->findChild<QPushButton*>("qtBackgroundSettingsButton");
        if (!backgroundsButton) { qobject_cast<QDialog*>(dialog)->reject(); return; }
        backgroundsButton->click();
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS"); if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/display-settings.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); saved = true;
    });
    if (!ShowQtDisplaySettings(nullptr, directory, settings) || !saved || settings.scalePercent != 125 ||
        settings.windowOpacityPercent != 86 ||
        settings.spacingPercent != 120 || settings.cornerRadius != 8 || !settings.compactRows ||
        settings.windowRounding != 13 || settings.frameRounding != 11 || settings.scrollbarRounding != 12 || settings.grabRounding != 7 ||
        settings.windowPaddingX != 10 || settings.windowPaddingY != 11 || settings.framePaddingX != 6 || settings.framePaddingY != 5 ||
        settings.itemSpacingX != 9 || settings.itemSpacingY != 4 ||
        settings.minimizeToTray != (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages())) {
        std::cerr << "Qt display settings dialog/save failed saved=" << saved << " scale=" << settings.scalePercent
                  << " opacity=" << settings.windowOpacityPercent << " spacing=" << settings.spacingPercent
                  << " radius=" << settings.cornerRadius << " geometry=" << settings.windowRounding << ',' << settings.frameRounding
                  << ',' << settings.scrollbarRounding << ',' << settings.grabRounding << " padding=" << settings.windowPaddingX << ','
                  << settings.windowPaddingY << ',' << settings.framePaddingX << ',' << settings.framePaddingY << " item="
                  << settings.itemSpacingX << ',' << settings.itemSpacingY << " tray=" << settings.minimizeToTray << '\n';
        return false;
    }
    missingDialogAccessibleNames.removeDuplicates();
    if (!missingDialogAccessibleNames.isEmpty()) {
        std::cerr << "Visible settings controls without accessible names:\n";
        for (const auto& name : missingDialogAccessibleNames)
            std::cerr << "  " << name.toUtf8().constData() << '\n';
        return false;
    }
    QFile file(temp.path() + "/meta/ui.ini"); if (!file.open(QIODevice::ReadOnly)) return false; const auto before = file.readAll(); file.close();
    if (!before.contains("unknown=kept") || !before.contains("[qt]") || !before.contains("scalePercent=125") || !before.startsWith("\xEF\xBB\xBF")) return false;
    const auto loaded = LoadQtDisplaySettings(directory); if (loaded.scalePercent != 125 || loaded.windowOpacityPercent != 86 || loaded.spacingPercent != 120 ||
        loaded.cornerRadius != 8 || !loaded.compactRows ||
        loaded.windowRounding != 13 || loaded.frameRounding != 11 || loaded.scrollbarRounding != 12 || loaded.grabRounding != 7 ||
        loaded.windowPaddingX != 10 || loaded.windowPaddingY != 11 || loaded.framePaddingX != 6 || loaded.framePaddingY != 5 ||
        loaded.itemSpacingX != 9 || loaded.itemSpacingY != 4 ||
        loaded.auditSourceFilter != 5 || loaded.lastPage != 16 || loaded.profileViewMode != 2 || loaded.profileSkillSort != 3 ||
        loaded.windowBackgrounds[0] != "ui/backgrounds/reference.png" || qAbs(loaded.backgroundAlpha - 0.55) > 0.001 ||
        !loaded.backgroundTiled || qAbs(loaded.backgroundTileScale - 1.5) > 0.001 ||
        loaded.profileSkillWeightCategory != 4 || qAbs(loaded.profileSkillWeightMin - 0.7) > 0.001 ||
        qAbs(loaded.profileSkillWeightMax - 1.4) > 0.001 || loaded.taskQuickFilter != 13 || !loaded.reportComparePrevious || loaded.reportView != 3 ||
        loaded.logShowInfo || !loaded.logShowWarning || loaded.logShowError || loaded.logSourceFilter != QStringLiteral("Qt") ||
        loaded.logFilter != QStringLiteral("remember this log query") ||
        loaded.projectFilter != QString::fromUtf8("remember project query") ||
        loaded.logAutoScroll || !loaded.logCompactView || loaded.minimizeToTray !=
            (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()) || loaded.deadlineNotificationsWhenClosed) {
        std::cerr << "Qt display/background settings persistence mismatch path=" << loaded.windowBackgrounds[0].toUtf8().constData()
                  << " alpha=" << loaded.backgroundAlpha << " tiled=" << loaded.backgroundTiled << " scale=" << loaded.backgroundTileScale
                  << " logFilter=" << loaded.logFilter.toUtf8().constData() << " source=" << loaded.logSourceFilter.toUtf8().constData()
                  << " levels=" << loaded.logShowInfo << loaded.logShowWarning << loaded.logShowError << '\n';
        return false;
    }
    if (!SetAdminStayLoggedIn(directory, true)) return false;
    QtWorkspace restoredWorkspace(directory);
    QtWindow restoredWindow(restoredWorkspace);
    if (qAbs(restoredWindow.windowOpacity() - 0.86) > 0.01) return false;
    auto* restoredNavigation = restoredWindow.findChild<QListWidget*>("navigation");
    auto* accessibleSearch = restoredWindow.findChild<QLineEdit*>("search");
    auto* accessibleTable = restoredWindow.findChild<QTableWidget*>();
    auto* backgroundSurface = restoredWindow.findChild<QWidget*>("qtBackgroundSurface");
    auto* focusSearch = restoredWindow.findChild<QShortcut*>("shortcutFocusSearch");
    auto* clearSearch = restoredWindow.findChild<QShortcut*>("shortcutClearSearch");
    if (!restoredNavigation || restoredNavigation->currentRow() != 16 ||
        restoredNavigation->accessibleName().isEmpty() || !accessibleSearch || !backgroundSurface ||
        accessibleSearch->accessibleName().isEmpty() || accessibleSearch->text() != QStringLiteral("remember this log query") ||
        !accessibleTable || !focusSearch || !clearSearch) {
        std::cerr << "Restored display controls navigation=" << (restoredNavigation ? restoredNavigation->currentRow() : -99)
                  << " query=" << (accessibleSearch ? accessibleSearch->text().toUtf8().constData() : "<null>")
                  << " navName=" << (restoredNavigation ? restoredNavigation->accessibleName().toUtf8().constData() : "<null>") << '\n';
        return false;
    }
    auto hasAccessibleName = [&restoredWindow](const char* objectName) {
        const auto* widget = restoredWindow.findChild<QWidget*>(QString::fromLatin1(objectName));
        return widget && !widget->accessibleName().trimmed().isEmpty();
    };
    if (!hasAccessibleName("profiles")) return false;
    restoredNavigation->setCurrentRow(0); QApplication::processEvents();
    if (backgroundSurface->property("backgroundPath").toString() != "ui/backgrounds/reference.png") { std::cerr << "Qt page background binding failed: " << backgroundSurface->property("backgroundPath").toString().toUtf8().constData() << '\n'; return false; }
    const auto backgroundArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!backgroundArtifacts.isEmpty()) {
        restoredWindow.resize(1120, 720); restoredWindow.show(); QApplication::processEvents();
        restoredWindow.grab().save(backgroundArtifacts + "/background-render.png");
    }
    auto* profileFocusMode = restoredWindow.findChild<QPushButton*>("profileViewMode2");
    auto* profileTable = restoredWindow.findChild<QTableWidget*>("records");
    auto* profileAchievements = restoredWindow.findChild<QPushButton*>("showAchievements");
    if (!profileFocusMode || !profileFocusMode->isChecked() || !profileTable || !profileTable->isHidden() || !profileAchievements || !profileAchievements->isHidden()) return false;
    auto* profileOverviewMode = restoredWindow.findChild<QPushButton*>("profileViewMode0");
    if (!profileOverviewMode) return false;
    profileOverviewMode->click(); QApplication::processEvents();
    if (profileTable->isHidden() || LoadQtDisplaySettings(directory).profileViewMode != 0) return false;
    restoredNavigation->setCurrentRow(2); QApplication::processEvents();
    if (!hasAccessibleName("projectSort")) return false;
    auto* projectReset = restoredWindow.findChild<QPushButton*>("projectFilterReset");
    auto* projectsOverdue = restoredWindow.findChild<QCheckBox*>("projectsOverdueOnly");
    auto* projectsXpPending = restoredWindow.findChild<QCheckBox*>("projectsXpPendingOnly");
    auto* projectSort = restoredWindow.findChild<QComboBox*>("projectSort");
    if (!projectReset || !projectsOverdue || !projectsXpPending || !projectSort ||
        accessibleSearch->text() != QString::fromUtf8("remember project query")) return false;
    accessibleSearch->setText(QString::fromUtf8("temporary project query"));
    if (LoadQtDisplaySettings(directory).projectFilter != QString::fromUtf8("temporary project query")) return false;
    restoredNavigation->setCurrentRow(0); QApplication::processEvents();
    if (!accessibleSearch->text().isEmpty()) return false;
    restoredNavigation->setCurrentRow(2); QApplication::processEvents();
    if (accessibleSearch->text() != QString::fromUtf8("temporary project query")) return false;
    projectsOverdue->setChecked(true); projectsXpPending->setChecked(true); projectSort->setCurrentIndex(2);
    projectReset->click(); QApplication::processEvents();
    const auto resetProjectFilters = LoadQtDisplaySettings(directory);
    if (!accessibleSearch->text().isEmpty() || projectsOverdue->isChecked() || projectsXpPending->isChecked() ||
        projectSort->currentIndex() != 2 || !resetProjectFilters.projectFilter.isEmpty() ||
        resetProjectFilters.projectsOverdueOnly || resetProjectFilters.projectsXpPendingOnly || resetProjectFilters.projectSortMode != 2)
        return false;
    restoredNavigation->setCurrentRow(3); QApplication::processEvents();
    if (!hasAccessibleName("catalogProfessionFilter") || !hasAccessibleName("projectFilterReset")) return false;
    restoredNavigation->setCurrentRow(1); QApplication::processEvents();
    for (const auto* name : {"statusFilter", "priorityFilter", "quickTaskFilter", "taskCreatedRange",
            "taskSortMode", "taskAssigneeFilter", "taskProjectFilter", "taskPipelineFilter", "taskFilterReset"})
        if (!hasAccessibleName(name)) { std::cerr << "Missing task filter accessible name: " << name << '\n'; return false; }
    restoredNavigation->setCurrentRow(6); QApplication::processEvents();
    if (!hasAccessibleName("reportView") || !hasAccessibleName("reportDateRange") || !hasAccessibleName("reportComparePrevious") ||
        !hasAccessibleName("reportDateFrom") || !hasAccessibleName("reportDateTo")) { std::cerr << "Missing report filter accessible name\n"; return false; }
    restoredNavigation->setCurrentRow(7); QApplication::processEvents();
    if (!hasAccessibleName("auditSourceFilter") || !hasAccessibleName("auditActorFilter") ||
        !hasAccessibleName("auditObjectFilter") || !hasAccessibleName("auditFieldFilter")) { std::cerr << "Missing audit filter accessible name\n"; return false; }
    restoredNavigation->setCurrentRow(16); QApplication::processEvents();
    if (!hasAccessibleName("logSourceFilter")) return false;
    restoredWindow.show(); restoredWindow.activateWindow(); QApplication::processEvents();
    QTest::keyClick(&restoredWindow, Qt::Key_K, Qt::ControlModifier); QApplication::processEvents();
    if (QApplication::focusWidget() != accessibleSearch) return false;
    accessibleSearch->setText(QString::fromUtf8("needle"));
    QTest::keyClick(accessibleSearch, Qt::Key_Escape); QApplication::processEvents();
    if (!accessibleSearch->text().isEmpty() || accessibleTable->accessibleName().isEmpty() ||
        !accessibleTable->accessibleDescription().contains(QString::fromUtf8("Строк:"))) return false;
    QSaveFile restoreSettings(temp.path() + "/meta/ui.ini");
    if (!restoreSettings.open(QIODevice::WriteOnly) || restoreSettings.write(before) != before.size() || !restoreSettings.commit()) return false;
    const auto fixedWindowColor = app.palette().color(QPalette::Window);
    ApplyQtDisplaySettings(app, loaded);
    if (app.font().pointSizeF() <= app.property("forgeBasePointSize").toDouble() ||
        !app.styleSheet().contains(QStringLiteral("min-height: 42px")) ||
        !app.styleSheet().contains(QStringLiteral("border-radius: 11px")) ||
        app.palette().color(QPalette::Window) != fixedWindowColor) return false;
    ApplyQtDisplaySettings(app, QtDisplaySettings{});
#ifdef _WIN32
    const auto path = (directory / "meta/ui.ini").wstring(); const auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false; settings.scalePercent = 90; const bool blocked = SaveQtDisplaySettings(directory, settings); CloseHandle(lock);
    if (blocked || !file.open(QIODevice::ReadOnly)) return false; const auto after = file.readAll(); file.close(); if (after != before) return false;
#endif
    QFile legacyAfterFile(temp.path() + "/meta/ui-presets/LegacyLayout.ini");
    return legacyAfterFile.open(QIODevice::ReadOnly) && legacyAfterFile.readAll() == legacyBytesBefore;
}

static bool TestWindowDecorationHotkey() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    QtWorkspace workspace(directory);
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* fullscreenShortcut = window.findChild<QShortcut*>("shortcutFullscreen");
    auto* fullscreenAction = window.findChild<QAction*>("windowFullscreenAction");
    auto* shortcut = window.findChild<QShortcut*>("shortcutToggleWindowDecoration");
    auto* decoratedAction = window.findChild<QAction*>("windowDecoratedAction");
    auto* dragHandle = window.findChild<QToolButton*>("windowDragHandle");
    auto* windowMenu = window.findChild<QMenu*>("windowMenu");
    if (!fullscreenShortcut || !fullscreenAction || !shortcut || !decoratedAction || !dragHandle || !windowMenu ||
        !fullscreenAction->isCheckable() || !decoratedAction->isCheckable() ||
        !windowMenu->actions().contains(fullscreenAction) || !windowMenu->actions().contains(decoratedAction) ||
        fullscreenAction->isChecked() || decoratedAction->isChecked() ||
        window.windowFlags().testFlag(Qt::FramelessWindowHint) || dragHandle->isVisible()) return false;
    fullscreenShortcut->activated();
    QApplication::processEvents();
    auto settings = LoadQtDisplaySettings(directory);
    if (!settings.fullscreen || !fullscreenAction->isChecked() || !window.isFullScreen()) return false;
    fullscreenAction->trigger();
    QApplication::processEvents();
    settings = LoadQtDisplaySettings(directory);
    if (settings.fullscreen || fullscreenAction->isChecked() || window.isFullScreen()) return false;
    shortcut->activated();
    QApplication::processEvents();
    settings = LoadQtDisplaySettings(directory);
    if (settings.decorated || !decoratedAction->isChecked() || !window.windowFlags().testFlag(Qt::FramelessWindowHint) || !dragHandle->isVisible()) return false;
    window.close();
    QtWindow restored(workspace);
    restored.show(); QApplication::processEvents();
    dragHandle = restored.findChild<QToolButton*>("windowDragHandle");
    if (!dragHandle || restored.windowFlags().testFlag(Qt::FramelessWindowHint) == false || !dragHandle->isVisible()) return false;
    shortcut = restored.findChild<QShortcut*>("shortcutToggleWindowDecoration");
    decoratedAction = restored.findChild<QAction*>("windowDecoratedAction");
    if (!shortcut || !decoratedAction || !decoratedAction->isChecked()) return false;
    decoratedAction->trigger(); QApplication::processEvents();
    settings = LoadQtDisplaySettings(directory);
    const bool restoredDecorated = settings.decorated && !restored.windowFlags().testFlag(Qt::FramelessWindowHint) && !dragHandle->isVisible();
    fullscreenAction = restored.findChild<QAction*>("windowFullscreenAction");
    auto* displayAction = restored.findChild<QAction*>("qtDisplaySettingsAction");
    if (!restoredDecorated || !fullscreenAction || !displayAction) { restored.close(); return false; }
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->findChild<QCheckBox*>("qtFullscreen")->setChecked(true);
        dialog->findChild<QCheckBox*>("qtDecorated")->setChecked(false);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    displayAction->trigger();
    QApplication::processEvents();
    settings = LoadQtDisplaySettings(directory);
    if (!settings.fullscreen || settings.decorated || !fullscreenAction->isChecked() || !decoratedAction->isChecked() ||
        !restored.isFullScreen() || !restored.windowFlags().testFlag(Qt::FramelessWindowHint)) {
        restored.close();
        return false;
    }
    fullscreenAction->trigger();
    decoratedAction->trigger();
    QApplication::processEvents();
    settings = LoadQtDisplaySettings(directory);
    const bool settingsMenuRoundTrip = !settings.fullscreen && settings.decorated && !fullscreenAction->isChecked() &&
        !decoratedAction->isChecked() && !restored.isFullScreen() && !restored.windowFlags().testFlag(Qt::FramelessWindowHint);
    restored.close();
    return settingsMenuRoundTrip;
}

static bool TestQtUiSettingsReset() {
    auto failAt = [](int line) { std::cerr << "Qt reset test failed at line " << line << '\n'; return false; };
    QTemporaryDir temp;
    if (!temp.isValid()) return failAt(__LINE__);
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    auto settings = QtDisplaySettings{};
    settings.scalePercent = 125; settings.windowOpacityPercent = 75; settings.spacingPercent = 120;
    settings.cornerRadius = 12; settings.compactRows = true; settings.fullscreen = true; settings.decorated = false;
    settings.minimizeToTray = true; settings.deadlineNotificationsWhenClosed = true;
    settings.lastProfileId = QStringLiteral("profile-keep"); settings.lastPage = 13;
    settings.projectFilter = QStringLiteral("reset-me-project-query");
    settings.taskQuickFilter = 8; settings.logFilter = QStringLiteral("keep-independent-state");
    settings.windowBackgrounds[0] = QStringLiteral("ui/backgrounds/keep.png");
    if (!SaveQtDisplaySettings(directory, settings)) return failAt(__LINE__);
    QtModelSettings model;
    model.modelPath = QStringLiteral("models/keep.obj"); model.yaw = 2.0f; model.pitch = 0.4f;
    model.zoom = 2.3f; model.autoRotate = false; model.autoSpeed = 2.0f; model.lineColor = QColor(Qt::red);
    if (!SaveQtModelSettings(directory, model)) return failAt(__LINE__);

    QFile ui(QString::fromUtf8((directory / "meta/ui.ini").u8string()));
    if (!ui.open(QIODevice::Append)) return failAt(__LINE__);
    const QByteArray preservedSections = "\n[profile]\nlastProfileId=profile-keep\ntrusted=profile-keep:4102444800\nrecent=profile-keep\n"
        "\n[cloud]\nroot=keep-cloud-location\nautoSync=1\n"
        "\n[pomodoro]\nworkMinutes=50\nbreakMinutes=20\nlongBreakMinutes=45\ncyclesBeforeLong=8\n"
        "autoAdvance=1\nsoundEnabled=0\nsoundFocus=focus.wav\nsoundBreak=break.wav\nsoundVolume=12\n";
    if (ui.write(preservedSections) != preservedSections.size()) return failAt(__LINE__);
    ui.close();

    if (!SetAdminPassword(directory, "reset-test-admin-secret") || !SetAdminStayLoggedIn(directory, true)) return failAt(__LINE__);
    QFile adminFile(QString::fromUtf8((directory / "meta/admin.ini").u8string()));
    if (!adminFile.open(QIODevice::ReadOnly)) return failAt(__LINE__);
    const auto adminBytes = adminFile.readAll(); adminFile.close();
    if (!std::filesystem::create_directories(directory / "models") && !std::filesystem::exists(directory / "models")) return failAt(__LINE__);
    if (!std::filesystem::create_directories(directory / "music") && !std::filesystem::exists(directory / "music")) return failAt(__LINE__);
    if (!std::filesystem::create_directories(directory / "ui/backgrounds") && !std::filesystem::exists(directory / "ui/backgrounds")) return failAt(__LINE__);
    auto writeFixture = [](const std::filesystem::path& path, const QByteArray& bytes) {
        QFile file(QString::fromUtf8(path.u8string()));
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    if (!writeFixture(directory / "models/keep.obj", "model-data") ||
        !writeFixture(directory / "music/keep.wav", "audio-data") ||
        !writeFixture(directory / "ui/backgrounds/keep.png", "background-data")) return failAt(__LINE__);
    QtLayoutPreset preset; preset.name = QStringLiteral("Keep this preset"); preset.scalePercent = 125;
    QString error;
    if (!SaveQtLayoutPreset(directory, preset, &error)) return failAt(__LINE__);
    QFile presetFile(QString::fromUtf8((directory / "meta/qt-layout-presets.json").u8string()));
    if (!presetFile.open(QIODevice::ReadOnly)) return failAt(__LINE__);
    const auto presetBytes = presetFile.readAll(); presetFile.close();

    if (!ResetQtUiSettings(directory)) return failAt(__LINE__);
    const auto reset = LoadQtDisplaySettings(directory);
    if (reset.scalePercent != 100 || reset.windowOpacityPercent != 100 || reset.spacingPercent != 100 ||
        reset.cornerRadius != 4 || reset.compactRows || reset.fullscreen || !reset.decorated || reset.minimizeToTray ||
        reset.deadlineNotificationsWhenClosed || reset.lastProfileId != QStringLiteral("profile-keep") ||
        reset.lastPage != 0 || reset.taskQuickFilter != 0 || !reset.logFilter.isEmpty() || !reset.projectFilter.isEmpty() ||
        !reset.windowBackgrounds[0].isEmpty()) return failAt(__LINE__);
    const auto resetModel = LoadQtModelSettings(directory);
    if (!resetModel.modelPath.isEmpty() || resetModel.yaw != 0.0f || resetModel.pitch != 0.0f ||
        resetModel.zoom != 1.0f || !resetModel.autoRotate || resetModel.autoSpeed != 0.6f ||
        resetModel.lineColor != QColor(153, 217, 255)) return failAt(__LINE__);
    QFile afterUi(QString::fromUtf8((directory / "meta/ui.ini").u8string()));
    if (!afterUi.open(QIODevice::ReadOnly)) return failAt(__LINE__);
    const auto afterBytes = afterUi.readAll(); afterUi.close();
    const auto text = QString::fromUtf8(afterBytes);
    if (!text.contains(QStringLiteral("trusted=profile-keep:4102444800")) ||
        !text.contains(QStringLiteral("recent=profile-keep")) || !text.contains(QStringLiteral("root=keep-cloud-location")) ||
        !text.contains(QStringLiteral("workMinutes=25")) || !text.contains(QStringLiteral("breakMinutes=5")) ||
        !text.contains(QStringLiteral("longBreakMinutes=15")) || !text.contains(QStringLiteral("cyclesBeforeLong=4")) ||
        !text.contains(QStringLiteral("autoAdvance=0")) || !text.contains(QStringLiteral("soundEnabled=1")) ||
        !text.contains(QStringLiteral("soundVolume=80")) || text.contains(QStringLiteral("soundFocus=focus.wav")) ||
        text.contains(QStringLiteral("scalePercent=125")) || text.contains(QStringLiteral("modelPath=models/keep.obj"))) return failAt(__LINE__);
    QFile adminAfter(QString::fromUtf8((directory / "meta/admin.ini").u8string()));
    if (!adminAfter.open(QIODevice::ReadOnly) || adminAfter.readAll() != adminBytes) return failAt(__LINE__);
    QFile presetsAfter(QString::fromUtf8((directory / "meta/qt-layout-presets.json").u8string()));
    if (!presetsAfter.open(QIODevice::ReadOnly) || presetsAfter.readAll() != presetBytes) return failAt(__LINE__);
    QFile cloud(QString::fromUtf8((directory / "models/keep.obj").u8string()));
    if (!cloud.open(QIODevice::ReadOnly) || cloud.readAll() != QByteArray("model-data")) return failAt(__LINE__);
    QFile music(QString::fromUtf8((directory / "music/keep.wav").u8string()));
    if (!music.open(QIODevice::ReadOnly) || music.readAll() != QByteArray("audio-data")) return failAt(__LINE__);
    QFile background(QString::fromUtf8((directory / "ui/backgrounds/keep.png").u8string()));
    return background.open(QIODevice::ReadOnly) && background.readAll() == QByteArray("background-data");
}

static QStringList MissingAccessibleNames(QWidget* root) {
    QStringList missing;
    if (!root) return {QStringLiteral("<missing root>")};
    auto widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    for (auto* widget : widgets) {
        if (!widget->isVisible()) continue;
        // These are sub-controls of their owning accessible widget, not separate actions.
        if (qobject_cast<QHeaderView*>(widget) || qobject_cast<QScrollBar*>(widget) ||
            QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("QTableCornerButton") ||
            ((qobject_cast<QLineEdit*>(widget)) &&
                (qobject_cast<QAbstractSpinBox*>(widget->parentWidget()) || qobject_cast<QComboBox*>(widget->parentWidget())))) continue;
        const bool interactive = qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget) ||
            qobject_cast<QLineEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) ||
            qobject_cast<QAbstractSlider*>(widget) || qobject_cast<QAbstractItemView*>(widget);
        if (!interactive) continue;
        auto* accessible = QAccessible::queryAccessibleInterface(widget);
        const auto accessibleName = widget->accessibleName().trimmed().isEmpty() && accessible
            ? accessible->text(QAccessible::Name).trimmed() : widget->accessibleName().trimmed();
        if (accessibleName.isEmpty()) {
            const auto description = widget->accessibleDescription().trimmed();
            missing.push_back(QStringLiteral("%1 <%2>%3")
                .arg(widget->objectName().isEmpty() ? QStringLiteral("(unnamed object)") : widget->objectName(),
                    QString::fromLatin1(widget->metaObject()->className()),
                    description.isEmpty() ? QString() : QStringLiteral(" — ") + description));
        }
    }
    missing.removeDuplicates();
    return missing;
}

static QStringList g_dialogAccessibilityFailures;
static int g_dialogAccessibilityAudits = 0;

class DialogAccessibilityAuditor final : public QObject {
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (auto* dialog = qobject_cast<QDialog*>(watched)) {
                QPointer<QDialog> guarded(dialog);
                QTimer::singleShot(0, dialog, [guarded] {
                    if (!guarded || !guarded->isVisible()) return;
                    ++g_dialogAccessibilityAudits;
                    const auto missing = MissingAccessibleNames(guarded);
                    if (missing.isEmpty()) return;
                    const auto identity = guarded->objectName().isEmpty()
                        ? guarded->windowTitle() : guarded->objectName();
                    for (const auto& control : missing)
                        g_dialogAccessibilityFailures.append(identity + QStringLiteral(": ") + control);
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

static bool TestVisibleQtAccessibleNames() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    if (!SetAdminStayLoggedIn(directory, true)) return false;
    QtWorkspace workspace(directory);
    QtWindow window(workspace);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    if (!navigation) return false;
    QStringList missing;
    for (int page = 0; page < navigation->count(); ++page) {
        auto* item = navigation->item(page);
        if (!item || item->isHidden()) continue;
        navigation->setCurrentRow(page);
        QApplication::processEvents();
        missing.append(MissingAccessibleNames(&window));
    }
    window.close();
    missing.removeDuplicates();
    if (!missing.isEmpty()) {
        std::cerr << "Visible Qt controls without accessible names:\n";
        for (const auto& name : missing) std::cerr << "  " << name.toUtf8().constData() << '\n';
        return false;
    }
    return true;
}

static bool TestQtModuleToggleParity() {
    struct RestoreModuleEnvironment {
        bool wasSet = qEnvironmentVariableIsSet("FORGEMIRROR_DISABLE_MODULES");
        QByteArray previous = qgetenv("FORGEMIRROR_DISABLE_MODULES");
        ~RestoreModuleEnvironment() {
            if (wasSet) qputenv("FORGEMIRROR_DISABLE_MODULES", previous);
            else qunsetenv("FORGEMIRROR_DISABLE_MODULES");
        }
    } restore;
    if (!qputenv("FORGEMIRROR_DISABLE_MODULES",
        "tasks,pipeline,achievements,shortcuts,pomodoro,cloud,view3d,professions")) return false;
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    auto settings = LoadQtDisplaySettings(directory);
    settings.lastPage = 13; // Cloud was selected before its module was disabled.
    if (!SaveQtDisplaySettings(directory, settings)) return false;
    QtWorkspace workspace(directory);
    if (workspace.modules.tasks || workspace.modules.pipeline || workspace.modules.achievements ||
        workspace.modules.shortcuts || workspace.modules.pomodoro || workspace.modules.cloud ||
        workspace.modules.view3d || workspace.modules.professions) return false;
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    auto* title = window.findChild<QLabel*>("title");
    auto* achievementButton = window.findChild<QPushButton*>("showAchievements");
    auto* shortcutLauncher = window.findChild<QToolButton*>("quickShortcutLauncher");
    auto* pomodoroQuick = window.findChild<QToolButton*>("pomodoroQuickButton");
    if (!navigation || !title || !achievementButton || !shortcutLauncher || !pomodoroQuick) return false;
    for (int page : {1, 4, 5, 7, 8, 11, 13, 14, 15})
        if (!navigation->item(page)->isHidden()) return false;
    if (!achievementButton->isHidden()) return false;
    if (!shortcutLauncher->isHidden()) return false;
    if (!pomodoroQuick->isHidden()) return false;
    navigation->setCurrentRow(13);
    QApplication::processEvents();
    if (navigation->currentRow() != 0 || title->text() != QString::fromUtf8("Профиль")) return false;
    window.close();
    return true;
}

static bool TestWorkspaceImportSnapshot() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const auto root = std::filesystem::u8path(temp.path().toUtf8().constData());
    const auto source = root / "legacy";
    const auto destination = root / "qt" / "workspace";
    std::filesystem::create_directories(source / "meta/nested");
    std::filesystem::create_directories(destination.parent_path());
    {
        std::ofstream tasks(source / "meta/tasks.json", std::ios::binary);
        tasks << "[{\"id\":\"task-1\",\"title\":\"preserved\"}]";
        std::ofstream profile(source / "0001.ini", std::ios::binary);
        profile << "[profile]\nid=0001\n";
    }
    const auto external = root / "outside.txt";
    { std::ofstream file(external, std::ios::binary); file << "outside"; }
    std::error_code symlinkError;
    std::filesystem::create_symlink(external, source / "linked-outside.txt", symlinkError);
    const bool hasSymlink = !symlinkError;
    QString error;
    if (!ImportQtWorkspaceSnapshot(source, destination, &error) || !error.isEmpty()) return false;
    auto read = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    if (read(source / "meta/tasks.json") != read(destination / "meta/tasks.json") ||
        read(source / "0001.ini") != read(destination / "0001.ini") ||
        (hasSymlink && std::filesystem::exists(destination / "linked-outside.txt")) ||
        !std::filesystem::is_directory(destination / "meta/nested")) return false;
    {
        std::ofstream marker(destination / "user-data.txt", std::ios::binary);
        marker << "keep";
    }
    if (ImportQtWorkspaceSnapshot(source, destination, &error) || error.isEmpty() ||
        read(destination / "user-data.txt") != "keep") return false;
    const auto overlapping = source / "child";
    if (ImportQtWorkspaceSnapshot(source, overlapping, &error) || error.isEmpty() ||
        std::filesystem::exists(overlapping)) return false;
    const auto missingSource = root / "missing";
    const auto failedDestination = root / "qt" / "failed";
    if (ImportQtWorkspaceSnapshot(missingSource, failedDestination, &error) || error.isEmpty() ||
        std::filesystem::exists(failedDestination)) return false;
    return true;
}

static bool TestQtDeadlineEvaluation() {
    const std::int64_t now = 1800000000;
    TaskEntry overdue; overdue.id = "private-overdue-id"; overdue.deadlineAt = now - 1;
    TaskEntry upcoming; upcoming.id = "private-upcoming-id"; upcoming.deadlineAt = now + 3600;
    TaskEntry later; later.id = "later"; later.deadlineAt = now + 2 * 86400;
    TaskEntry done; done.id = "done"; done.deadlineAt = now - 60; done.status = 2;
    TaskEntry noDeadline; noDeadline.id = "none";
    const std::vector<TaskEntry> tasks{overdue, upcoming, later, done, noDeadline};
    const auto summary = EvaluateQtDeadlines(tasks, now);
    auto reversed = tasks; std::reverse(reversed.begin(), reversed.end());
    if (summary.overdue != 1 || summary.upcoming != 1 || summary.signature.isEmpty() ||
        summary.signature != EvaluateQtDeadlines(reversed, now).signature) return false;
    upcoming.deadlineAt += 60;
    if (summary.signature == EvaluateQtDeadlines({overdue, upcoming, later, done, noDeadline}, now).signature ||
        summary.signature.contains("private")) return false;
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    QDir().mkpath(temp.path() + "/meta/updates");
    QFile primary(temp.path() + "/meta/tasks.json");
    if (!primary.open(QIODevice::WriteOnly) || primary.write("invalid primary") < 0) return false;
    primary.close();
    QFile backup(temp.path() + "/meta/updates/tasks.last-good.json");
    if (!backup.open(QIODevice::WriteOnly) || backup.write("[{\"id\":\"backup-task\",\"title\":\"backup\"}]") < 0) return false;
    backup.close();
    if (!LoadTasksDataReadOnly(std::filesystem::u8path(temp.path().toUtf8().constData())).empty() ||
        !primary.open(QIODevice::ReadOnly) || primary.readAll() != "invalid primary") return false;
    return true;
}

static bool TestShortcutPersistence() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QFile first(temp.path() + "/first tool.exe"), second(temp.path() + "/second.txt");
    if (!first.open(QIODevice::WriteOnly) || first.write("one") != 3) return false; first.close();
    if (!second.open(QIODevice::WriteOnly) || second.write("two") != 3) return false; second.close();
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    std::vector<ShortcutEntry> shortcuts;
    if (!AppAddShortcut(directory, shortcuts, u8"Первый", first.fileName().toUtf8().toStdString()).ok ||
        !AppAddShortcut(directory, shortcuts, u8"Второй", second.fileName().toUtf8().toStdString()).ok || shortcuts.size() != 2) return false;
    if (!AppMoveShortcut(directory, shortcuts, 1, 0).ok || shortcuts.front().label != u8"Второй") return false;
    const auto loaded = LoadShortcutsData(directory);
    if (loaded.size() != 2 || loaded.front().label != u8"Второй") return false;
    QFile stored(temp.path() + "/meta/shortcuts.json"); if (!stored.open(QIODevice::ReadOnly)) return false;
    const auto before = stored.readAll(); stored.close();
    AppSetRecoveryPrimaryWriteFailureForTests(true);
    const auto failed = AppDeleteShortcut(directory, shortcuts, 0);
    AppSetRecoveryPrimaryWriteFailureForTests(false);
    if (failed.ok || shortcuts.size() != 2 || shortcuts.front().label != u8"Второй" || !stored.open(QIODevice::ReadOnly)) return false;
    const auto after = stored.readAll(); stored.close();
    return before == after;
}

static bool TestQuickShortcutLauncher() {
    QTemporaryDir temp; if (!temp.isValid()) return false;
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    std::vector<ShortcutEntry> shortcuts;
    const QString runningPath = QCoreApplication::applicationFilePath();
    const QString missingPath = temp.path() + "/not-running.exe";
    const QString unknownPath = temp.path() + "/document.txt";
    QFile missingFile(missingPath), unknownFile(unknownPath);
    if (!missingFile.open(QIODevice::WriteOnly) || missingFile.write("fixture") != 7) return false;
    missingFile.close();
    if (!unknownFile.open(QIODevice::WriteOnly) || unknownFile.write("fixture") != 7) return false;
    unknownFile.close();
    const std::vector<QPair<QString, QString>> entries{{"Running", runningPath}, {"Stopped", missingPath}, {"Unknown", unknownPath}};
    for (const auto& item : entries)
        if (!AppAddShortcut(directory, shortcuts, item.first.toUtf8().toStdString(), item.second.toUtf8().toStdString()).ok) return false;
    QtWorkspace workspace(directory);
    QtWindow window(workspace);
    auto* launcher = window.findChild<QToolButton*>("quickShortcutLauncher");
    auto* menu = window.findChild<QMenu*>("quickShortcutMenu");
    if (!launcher || !menu || launcher->accessibleName() != QString::fromUtf8("Быстрый запуск ярлыков")) return false;
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    int seen = 0;
    for (auto* action : menu->actions()) {
        if (action->objectName() != "quickShortcutAction") continue;
        ++seen;
        const auto id = action->data().toString();
        const int expected = id == QString::fromStdString(shortcuts[0].id) ? 1 :
            id == QString::fromStdString(shortcuts[1].id) ? 2 :
            id == QString::fromStdString(shortcuts[2].id) ? 0 : -1;
        if (expected < 0 || action->property("shortcutRunState").toInt() != expected || action->icon().isNull()) return false;
    }
    if (seen != 3) return false;
    QAction* manage = nullptr;
    for (auto* action : menu->actions()) if (action->objectName() == "manageShortcutsAction") manage = action;
    if (!manage) return false;
    menu->close();
    manage->trigger();
    QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    return navigation && navigation->currentRow() == 11;
}

static bool TestLogActivityHistogram() {
    const std::vector<AppLogEntry> entries{
        {100, AppLogLevel::Info, "Qt", "first"},
        {100, AppLogLevel::Error, "Qt", "same time"},
        {150, AppLogLevel::Warning, "Qt", "middle"},
        {200, AppLogLevel::Info, "Qt", "last"}
    };
    const auto histogram = QtLogActivityChart::BuildHistogram(entries);
    if (histogram[0] != 2 || histogram[7] != 1 || histogram[15] != 1 ||
        std::accumulate(histogram.begin(), histogram.end(), 0) != int(entries.size())) return false;
    QtLogActivityChart chart;
    chart.setEntries(entries);
    const auto dateRange = QString::fromUtf8("%1 — %2")
        .arg(QDateTime::fromSecsSinceEpoch(100).toString("yyyy-MM-dd HH:mm"),
             QDateTime::fromSecsSinceEpoch(200).toString("yyyy-MM-dd HH:mm"));
    if (!chart.accessibleDescription().contains(QString::fromUtf8("Интервал 1: 2")) ||
        !chart.accessibleDescription().contains(QString::fromUtf8("Интервал 8: 1")) ||
        !chart.accessibleDescription().contains(QString::fromUtf8("Интервал 16: 1")) ||
        !chart.accessibleDescription().contains(dateRange)) return false;
    const std::vector<AppLogEntry> identicalTimes{
        {500, AppLogLevel::Info, "Qt", "one"},
        {500, AppLogLevel::Info, "Qt", "two"},
        {500, AppLogLevel::Info, "Qt", "three"}
    };
    const auto fallback = QtLogActivityChart::BuildHistogram(identicalTimes);
    if (fallback[0] != 1 || fallback[5] != 1 || fallback[10] != 1 ||
        std::accumulate(fallback.begin(), fallback.end(), 0) != int(identicalTimes.size())) return false;
    chart.setEntries(identicalTimes);
    if (!chart.accessibleDescription().contains(QString::fromUtf8("Временной диапазон неразличим"))) return false;
    const std::vector<AppLogEntry> missingTimes{
        {100, AppLogLevel::Info, "Qt", "first"},
        {0, AppLogLevel::Warning, "Qt", "unknown time"},
        {200, AppLogLevel::Error, "Qt", "last"}
    };
    const auto withMissingTime = QtLogActivityChart::BuildHistogram(missingTimes);
    chart.setEntries(missingTimes);
    return std::accumulate(withMissingTime.begin(), withMissingTime.end(), 0) == int(missingTimes.size()) &&
        chart.accessibleDescription().contains(QString::fromUtf8("Записи без временной метки распределены"));
}

static bool TestQtLogSourceSanitizationAndRetention() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>("navigation");
    if (!navigation) return false;
    navigation->setCurrentRow(1);
    window.statusBar()->showMessage(QStringLiteral("Task event password=leak token=other https://user:secret@example.test"));
    QFile file(temp.path() + "/meta/qt-application-log.json");
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto entries = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    bool sanitizedTaskEvent = false;
    for (const auto& value : entries) {
        const auto item = value.toObject();
        const auto message = item.value("message").toString();
        if (!message.startsWith("Task event")) continue;
        sanitizedTaskEvent = item.value("source").toString() == QString::fromUtf8("Задачи") &&
            message.contains("[REDACTED]") && !message.contains("leak") && !message.contains("other") &&
            !message.contains("user:secret");
    }
    if (!sanitizedTaskEvent) return false;
    window.statusBar()->showMessage(QString::fromUtf8("Не удалось сохранить тестовую задачу"));
    if (!file.open(QIODevice::ReadOnly)) return false;
    entries = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    bool errorClassified = false;
    for (const auto& value : entries) {
        const auto item = value.toObject();
        if (item.value("message").toString().contains(QString::fromUtf8("Не удалось сохранить тестовую задачу")))
            errorClassified = item.value("level").toInt(-1) == int(AppLogLevel::Error);
    }
    if (!errorClassified) return false;
    for (int index = 0; index < 205; ++index)
        window.statusBar()->showMessage(QStringLiteral("retention-%1").arg(index));
    if (!file.open(QIODevice::ReadOnly)) return false;
    entries = QJsonDocument::fromJson(file.readAll()).array();
    if (entries.size() != 200 || entries.first().toObject().value("message").toString() != "retention-5" ||
        entries.last().toObject().value("message").toString() != "retention-204") return false;
    return true;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ApplyQtTheme(app);
    DialogAccessibilityAuditor dialogAccessibilityAuditor;
    app.installEventFilter(&dialogAccessibilityAuditor);
    if (!TestQtLogSourceSanitizationAndRetention()) { std::cerr << "Qt log source, sanitization, or retention failed\n"; return 1; }
    qunsetenv("FORGEMIRROR_ADMIN_PASSWORD");
    qunsetenv("FORGEMIRROR_DISABLE_MODULES");
    if (!TestTaskCompletion()) return 1;
    if (!TestRulesReapplyRecovery()) return 1;
    if (!TestDirectXpRecovery()) return 1;
    if (!TestVaultEditor()) { std::cerr << "Vault editor failed\n"; return 1; }
    if (!TestBannerEditor()) { std::cerr << "Banner editor failed\n"; return 1; }
    if (!TestCloudSettings()) { std::cerr << "Cloud settings failed\n"; return 1; }
    if (!TestCloudPullTransaction()) { std::cerr << "Cloud pull transaction failed\n"; return 1; }
    if (!TestCloudAutoSync()) { std::cerr << "Cloud automatic sync failed\n"; return 1; }
    if (!TestCloudReleaseUpdate()) { std::cerr << "Cloud release update failed\n"; return 1; }
    if (!TestQtAdminAuthParity()) { std::cerr << "Administrator authentication parity failed\n"; return 1; }
    if (!TestQtAdminAuthAcrossProcesses()) { std::cerr << "Administrator process-restart persistence failed\n"; return 1; }
    if (!TestCatalogMutationCoreAudit()) { std::cerr << "Catalog mutation core audit failed\n"; return 1; }
    if (!TestCloudPushPreview()) { std::cerr << "Cloud push preview failed\n"; return 1; }
    if (!TestCloudConflictResolver()) { std::cerr << "Cloud conflict resolver failed\n"; return 1; }
    if (!TestStorageConflictResolver()) { std::cerr << "Storage conflict resolver failed\n"; return 1; }
    if (!TestAchievements()) { std::cerr << "Achievements failed\n"; return 1; }
    if (!TestAchievementFiltersUi()) { std::cerr << "Achievement filters UI failed\n"; return 1; }
    if (!TestProfileSession()) { std::cerr << "Profile session failed\n"; return 1; }
    if (!TestPipelineTransition()) { std::cerr << "Pipeline transition failed\n"; return 1; }
    if (!TestPipelineEditor()) { std::cerr << "Pipeline editor failed\n"; return 1; }
    if (!TestTaskEditorTransaction()) { std::cerr << "Task editor transaction failed\n"; return 1; }
    if (!TestBulkAwardedTaskDeletion()) { std::cerr << "Bulk awarded task deletion failed\n"; return 1; }
    if (!TestProjectDeletionRecovery()) { std::cerr << "Project deletion recovery failed\n"; return 1; }
    if (!TestProfileDialogs()) return 1;
    if (!TestSkillEditor()) { std::cerr << "Skill editor failed\n"; return 1; }
    if (!TestProfessionEditor()) { std::cerr << "Profession editor failed\n"; return 1; }
    if (!TestProfessionDeletionRecovery()) { std::cerr << "Profession deletion recovery failed\n"; return 1; }
    if (!TestProfessionMergeRecovery()) { std::cerr << "Profession merge recovery failed\n"; return 1; }
    if (!TestSkillDeletionRecovery()) { std::cerr << "Skill deletion recovery failed\n"; return 1; }
    if (!TestSkillMergeRecovery()) { std::cerr << "Skill merge recovery failed\n"; return 1; }
    if (!TestImmediateRollbackPreservesChangedFiles()) { std::cerr << "Immediate rollback preservation failed\n"; return 1; }
    if (!TestProfileDeletionRecovery()) { std::cerr << "Profile deletion recovery failed\n"; return 1; }
    if (!TestPersonalWallet()) { std::cerr << "Personal wallet failed\n"; return 1; }
    if (!TestPomodoro()) { std::cerr << "Pomodoro failed\n"; return 1; }
    if (!TestPomodoroQuickHeader()) { std::cerr << "Pomodoro quick header failed\n"; return 1; }
    if (!TestCloudQuickHeader()) { std::cerr << "Cloud quick header failed\n"; return 1; }
    if (!TestNavigationClock()) { std::cerr << "Navigation clock failed\n"; return 1; }
    if (!TestRulesEditor()) { std::cerr << "Rules editor failed\n"; return 1; }
    if (!TestDisplaySettings(app)) { std::cerr << "Display settings failed\n"; return 1; }
    if (!TestWindowDecorationHotkey()) { std::cerr << "Window decoration hotkey failed\n"; return 1; }
    if (!TestQtUiSettingsReset()) { std::cerr << "Qt UI settings reset failed\n"; return 1; }
    if (!TestVisibleQtAccessibleNames()) { std::cerr << "Qt accessible-name audit failed\n"; return 1; }
    if (!TestQtModuleToggleParity()) { std::cerr << "Qt module toggle parity failed\n"; return 1; }
    if (!TestWorkspaceImportSnapshot()) { std::cerr << "Workspace import snapshot failed\n"; return 1; }
    if (!TestQtDeadlineEvaluation()) { std::cerr << "Qt deadline evaluation failed\n"; return 1; }
    if (!TestShortcutPersistence()) { std::cerr << "Shortcut persistence failed\n"; return 1; }
    if (!TestQuickShortcutLauncher()) { std::cerr << "Quick shortcut launcher failed\n"; return 1; }
    if (!TestReportExport()) { std::cerr << "Report export failed\n"; return 1; }
    if (!TestProfileReportExport()) { std::cerr << "Profile report export failed\n"; return 1; }
    if (!TestQtStorageHealthReport()) { std::cerr << "Qt storage health report failed\n"; return 1; }
    if (!TestReportPeriodComparison()) { std::cerr << "Report period comparison failed\n"; return 1; }
    if (!TestMonthlyCompletionTrend()) { std::cerr << "Monthly completion trend failed\n"; return 1; }
    if (!TestStatisticsTrendBeyondAuditPageLimit()) { std::cerr << "Statistics trend audit history failed\n"; return 1; }
    if (!TestPipelineMap()) { std::cerr << "Pipeline map failed\n"; return 1; }
    if (!TestDeadlineReminders()) { std::cerr << "Deadline reminders failed\n"; return 1; }
    if (!TestTaskActionNeededQuickFilter()) { std::cerr << "Task action-needed quick filter failed\n"; return 1; }
    if (!TestQtTaskCreationRangeAndSorting()) { std::cerr << "Qt task creation range and sorting failed\n"; return 1; }
    if (!TestQtTaskAssigneeProfileFilter()) { std::cerr << "Qt task assignee profile filter failed\n"; return 1; }
    if (!TestQtVisibleTaskExports()) { std::cerr << "Qt visible task export failed\n"; return 1; }
    if (!TestQtTaskFilterReset()) { std::cerr << "Qt task filter reset failed\n"; return 1; }
    if (!TestQtVisibleTaskSelectionTools()) { std::cerr << "Qt visible task selection tools failed\n"; return 1; }
    if (!TestQtTaskTableDateAndAssignees()) { std::cerr << "Qt task table date and assignees failed\n"; return 1; }
    if (!TestQtTaskAttentionBadges()) { std::cerr << "Qt task attention badges failed\n"; return 1; }
    if (!TestQtTaskFocusAndOverdueTint()) { std::cerr << "Qt task focus and overdue tint failed\n"; return 1; }
    if (!TestCatalogProfessionFilter()) { std::cerr << "Catalog profession filter failed\n"; return 1; }
    if (!TestBulkTaskEditsUi()) { std::cerr << "Bulk task edits UI failed\n"; return 1; }
    if (!TestAuditExport()) { std::cerr << "Audit export failed\n"; return 1; }
    if (!TestLogActivityHistogram()) { std::cerr << "Log activity histogram failed\n"; return 1; }
    QTemporaryDir temp;
    auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
    if (!temp.isValid()) return fail("Temporary directory unavailable");
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    Profile profile(u8"Тестовый профиль");
    profile.set_password_encoded(EncodePassword("profile-test-password"));
    profile.set_last_task_timestamp(QDateTime::currentSecsSinceEpoch() - 5 * 86400);
    workspace.catalog.add_skill(u8"Моделирование", 1.5, u8"Создание геометрии");
    workspace.catalog.add_skill(u8"Текстурирование", 1.2, u8"Подготовка материалов");
    workspace.catalog.add_skill(u8"Анимация", 0.95, u8"Движение персонажа");
    workspace.catalog.add_skill(u8"Концепт-арт", 0.6, u8"Визуальные идеи");
    profile.add_skill(*workspace.catalog.id_for_name(u8"Моделирование"), 1, 1.5);
    profile.add_skill(*workspace.catalog.id_for_name(u8"Текстурирование"), 1, 1.2);
    profile.add_skill(*workspace.catalog.id_for_name(u8"Анимация"), 1, 0.95);
    profile.add_skill(*workspace.catalog.id_for_name(u8"Концепт-арт"), 1, 0.6);
    auto createdProfile = workspace.storage->create_profile(profile);
    if (!createdProfile) return fail("Profile creation failed");
    Profile archivedProfile(u8"Архивный профиль статистики");
    archivedProfile.set_overall_level(10);
    archivedProfile.set_last_task_timestamp(QDateTime::currentSecsSinceEpoch() - 40 * 86400);
    archivedProfile.start_penalty_recovery(4);
    archivedProfile.set_category_best_scores({10, 8, 6, 4, 2});
    Achievement activeAchievement; activeAchievement.title = "Active"; activeAchievement.awardedAt = 100;
    archivedProfile.add_achievement(activeAchievement);
    Achievement expiredAchievement; expiredAchievement.title = "Expired";
    expiredAchievement.awardedAt = QDateTime::currentSecsSinceEpoch() - 2 * 86400;
    expiredAchievement.expiresAt = QDateTime::currentSecsSinceEpoch() - 1;
    archivedProfile.add_achievement(expiredAchievement);
    const auto archivedInfo = workspace.storage->create_profile(archivedProfile);
    if (!archivedInfo || !workspace.storage->set_archived(archivedInfo->id, true) ||
        !workspace.storage->set_active_profile(createdProfile->id)) return fail("Archived stats fixture creation failed");
    QFile archivedProfileFile(temp.path() + "/archive/" + QString::fromStdString(archivedInfo->id) + ".ini");
    if (!archivedProfileFile.open(QIODevice::ReadOnly)) return fail("Archived profile fixture missing");
    const auto archivedBytesBeforeSnapshot = archivedProfileFile.readAll(); archivedProfileFile.close();
    auto archivedSnapshot = workspace.storage->load_profile_snapshot(archivedInfo->id, true);
    auto activeSnapshot = workspace.storage->load_profile();
    if (!archivedSnapshot || archivedSnapshot->name() != u8"Архивный профиль статистики" ||
        !activeSnapshot || activeSnapshot->name() != profile.name() ||
        workspace.storage->load_profile_snapshot(archivedInfo->id, false)) return fail("Profile snapshot read changed active selection or archive access");
    if (!archivedProfileFile.open(QIODevice::ReadOnly) || archivedProfileFile.readAll() != archivedBytesBeforeSnapshot)
        return fail("Profile snapshot read wrote normalized archive data");
    archivedProfileFile.close();
    QFile profileFile(temp.path() + "/" + QString::fromStdString(createdProfile->id) + ".ini");
    if (!profileFile.open(QIODevice::ReadOnly)) return fail("Cannot read profile fixture");
    const auto profileBytes = profileFile.readAll();
    profileFile.close();
    TaskEntry task;
    task.id = "qt-smoke-task";
    task.title = u8"Проверка Qt <без HTML>";
    task.description = u8"Описание задачи";
    task.createdAt = QDateTime::currentSecsSinceEpoch();
    task.assignees = {createdProfile->id};
    PipelineStep stage;
    stage.id = "custom-ui-stage";
    stage.title = "Old stage";
    PipelineStep unusedStage;
    unusedStage.id = "unused-ui-stage";
    unusedStage.title = "Unused stage";
    workspace.data.pipelineSteps = {stage, unusedStage};
    if (!AppSavePipelineData(workspace.directory, workspace.data.pipelineSteps)) return fail("Pipeline fixture failed");
    task.pipelineStepId = stage.id;
    task.pipelineStep = stage.title;
    if (!AppCreateTaskEntry(workspace.directory, workspace.data.tasks, task, "test").ok) return fail("Task fixture failed");
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* nav = window.findChild<QListWidget*>("navigation");
    auto* table = window.findChild<QTableWidget*>("records");
    auto* search = window.findChild<QLineEdit*>("search");
    auto* primary = window.findChild<QPushButton*>("primary");
    auto* status = window.findChild<QComboBox*>("statusFilter");
    if (!nav || !table || !search || !primary || !status) return fail("Missing UI controls");
    nav->setCurrentRow(0); QApplication::processEvents();
    auto* modeOverview = window.findChild<QPushButton*>("profileViewMode0");
    auto* modeAnalytics = window.findChild<QPushButton*>("profileViewMode1");
    auto* modeFocus = window.findChild<QPushButton*>("profileViewMode2");
    auto* modeTasks = window.findChild<QPushButton*>("profileViewMode3");
    auto* showAchievements = window.findChild<QPushButton*>("showAchievements");
    if (!modeOverview || !modeAnalytics || !modeFocus || !modeTasks || !showAchievements || table->rowCount() != 4 ||
        !modeAnalytics->isChecked() || table->isRowHidden(0) || table->isRowHidden(1) || table->isRowHidden(2) || table->isRowHidden(3))
        return fail("Profile analytics mode did not show full skill list");
    auto* profileCharts = static_cast<QtProfileAnalytics*>(window.findChild<QWidget*>("profileAnalyticsCharts"));
    if (!profileCharts || !profileCharts->isVisible() || !profileCharts->axisControl()->isEnabled() ||
        profileCharts->axisControl()->maximum() != 4 ||
        !profileCharts->accessibleDescription().contains(QString::fromUtf8(Profile::kCategoryLabels[0])) ||
        !profileCharts->accessibleDescription().contains(QString::fromUtf8("Топ навыков по общему XP")) ||
        !profileCharts->accessibleDescription().contains(QString::fromUtf8("Радар:")) ||
        !profileCharts->accessibleDescription().contains(QString::fromUtf8("уровень 1 +")) ||
        !profileCharts->accessibleDescription().contains(QString::fromUtf8("Моделирование")))
        return fail("Profile analytics category, skill chart, or accessible radar controls missing");
    const auto* profileChartsAccessible = QAccessible::queryAccessibleInterface(profileCharts);
    if (!profileChartsAccessible || !profileChartsAccessible->text(QAccessible::Description)
            .contains(QString::fromUtf8("уровень 1 +")))
        return fail("Profile chart values were not exposed through QAccessible");
    profileCharts->axisControl()->setValue(3);
    if (!profileCharts->accessibleDescription().contains(QString::fromUtf8("Радар: 3 навыков")))
        return fail("Profile radar accessible data did not follow the selected axis count");
    profileCharts->axisControl()->setValue(4);
    const auto rankedCharts = QtProfileAnalytics::TopSkills({
        {"Низкий", 1, 0, 100, 12, 1.0}, {"Высокий", 2, 0, 100, 240, 1.0},
        {"Средний", 1, 0, 100, 81, 1.0}}, 2);
    if (rankedCharts.size() != 2 || rankedCharts[0].name != "Высокий" || rankedCharts[1].name != "Средний")
        return fail("Profile analytics top-skill ranking failed");
    auto* sortSkills = window.findChild<QComboBox*>("profileSkillSort");
    auto* categorySkills = window.findChild<QComboBox*>("profileSkillWeightCategory");
    auto* minWeight = window.findChild<QDoubleSpinBox*>("profileSkillWeightMin");
    auto* maxWeight = window.findChild<QDoubleSpinBox*>("profileSkillWeightMax");
    auto* resetSkillFilters = window.findChild<QPushButton*>("profileSkillFilterReset");
    if (!sortSkills || !categorySkills || !minWeight || !maxWeight || !resetSkillFilters)
        return fail("Profile skill filters missing");
    sortSkills->setCurrentIndex(3); QApplication::processEvents();
    if (table->item(0, 0)->text() != QString::fromUtf8(workspace.catalog.display_name(u8"Моделирование").c_str()))
        return fail("Profile skills weight sorting failed");
    categorySkills->setCurrentIndex(5); QApplication::processEvents();
    if (table->rowCount() != 1 || table->item(0, 0)->text() != QString::fromUtf8(workspace.catalog.display_name(u8"Концепт-арт").c_str()))
        return fail("Profile skill weight category filter failed");
    categorySkills->setCurrentIndex(0);
    minWeight->setValue(1.3); QApplication::processEvents();
    if (table->rowCount() != 1 || table->item(0, 0)->text() != QString::fromUtf8(workspace.catalog.display_name(u8"Моделирование").c_str()))
        return fail("Profile skill weight range filter failed");
    resetSkillFilters->click(); QApplication::processEvents();
    if (table->rowCount() != 4 || sortSkills->currentIndex() != 0 || categorySkills->currentIndex() != 0 ||
        minWeight->value() != 0.0 || maxWeight->value() != 2.0)
        return fail("Profile skill filter reset failed");
    modeOverview->click(); QApplication::processEvents();
    if (table->isRowHidden(0) || table->isRowHidden(1) || table->isRowHidden(2) || !table->isRowHidden(3) || showAchievements->isHidden())
        return fail("Profile overview mode did not keep only leading skills and achievement action");
    modeFocus->click(); QApplication::processEvents();
    if (!table->isHidden() || !showAchievements->isHidden()) return fail("Profile focus mode did not hide details");
    modeAnalytics->click(); QApplication::processEvents();
    if (table->isHidden() || table->isRowHidden(1) || table->isRowHidden(2) || table->isRowHidden(3)) return fail("Profile analytics mode did not restore details");
    modeTasks->click(); QApplication::processEvents();
    if (!modeTasks->isChecked() || table->columnCount() != 5 || table->rowCount() != 1 ||
        table->item(0, 0)->text() != QString::fromUtf8("Проверка Qt <без HTML>") ||
        LoadQtDisplaySettings(workspace.directory).profileViewMode != 3 ||
        !window.findChild<QLabel*>("summary")->text().contains(QString::fromUtf8("1 активных")) ||
        !window.findChild<QPushButton*>("profileTasksFilter1"))
        return fail("Profile task dashboard did not show the assigned active task and summary");
    window.findChild<QPushButton*>("profileTasksFilter1")->click(); QApplication::processEvents();
    if (nav->currentRow() != 1 || table->rowCount() != 1 ||
        window.findChild<QComboBox*>("quickTaskFilter")->currentIndex() != 7 ||
        window.findChild<QComboBox*>("taskAssigneeFilter")->currentData().toString() != QString::fromStdString(createdProfile->id))
        return fail("Profile task dashboard did not open the active task list scoped to the profile");
    window.findChild<QPushButton*>("taskFilterReset")->click(); QApplication::processEvents();
    modeAnalytics->click(); QApplication::processEvents();
    nav->setCurrentRow(0);
    auto* displayAction = window.findChild<QAction*>("qtDisplaySettingsAction");
    if (!displayAction) return fail("Display settings action missing");
    auto* shortcutHelpAction = window.findChild<QAction*>("shortcutHelpAction");
    const std::vector<std::pair<const char*, QKeySequence>> shortcuts = {
        {"shortcutCreate", QKeySequence::New}, {"shortcutEdit", QKeySequence("Ctrl+E")},
        {"shortcutDelete", QKeySequence::Delete}, {"shortcutRefresh", QKeySequence::Refresh},
        {"shortcutDetails", QKeySequence("Ctrl+I")}, {"shortcutHelp", QKeySequence("Ctrl+/")},
        {"shortcutToggleWindowDecoration", QKeySequence(Qt::Key_F10)},
        {"shortcutResetUiSettings", QKeySequence("Ctrl+F10")}
    };
    if (!shortcutHelpAction) return fail("Shortcut help action missing");
    for (const auto& expected : shortcuts) {
        const auto* shortcut = window.findChild<QShortcut*>(expected.first);
        if (!shortcut || shortcut->key() != expected.second) return fail("Context shortcut missing or incorrect");
    }
    bool shortcutHelpChecked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* helpTable = dialog ? dialog->findChild<QTableWidget*>("shortcutHelpTable") : nullptr;
        shortcutHelpChecked = dialog && dialog->objectName() == "shortcutHelp" && helpTable && helpTable->rowCount() == 17 &&
            helpTable->item(8, 0)->text() == "Ctrl+N" && helpTable->item(13, 0)->text() == "Ctrl+/" &&
            helpTable->item(14, 0)->text() == "F10" && helpTable->item(15, 0)->text() == "Ctrl+F10" &&
            helpTable->item(16, 0)->text() == "F11";
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (dialog && !artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/shortcuts.png"); }
        if (dialog) dialog->accept();
    });
    shortcutHelpAction->trigger();
    if (!shortcutHelpChecked) return fail("Shortcut help content failed");
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget(); auto* scale = dialog->findChild<QComboBox*>("qtScale");
        scale->setCurrentIndex(scale->findData(100)); dialog->findChild<QCheckBox*>("qtCompactRows")->setChecked(true);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    displayAction->trigger();
    if (table->verticalHeader()->defaultSectionSize() != 24) return fail("Compact table setting not applied");
    auto* fullscreenMenuAction = window.findChild<QAction*>("windowFullscreenAction");
    auto* framelessMenuAction = window.findChild<QAction*>("windowDecoratedAction");
    if (!fullscreenMenuAction || !framelessMenuAction) return fail("Window mode menu actions missing");
    QFile settingsEventLog(QString::fromStdWString((workspace.directory / "meta/qt-application-log.json").wstring()));
    if (!settingsEventLog.open(QIODevice::ReadOnly)) return fail("Qt display settings event log missing");
    const auto settingsEventDocument = QJsonDocument::fromJson(settingsEventLog.readAll());
    settingsEventLog.close();
    const auto settingsEvents = settingsEventDocument.array();
    const bool displaySettingsEventLogged = settingsEventDocument.isArray() && std::any_of(
        settingsEvents.begin(), settingsEvents.end(), [](const QJsonValue& value) {
            const auto entry = value.toObject();
            return entry.value("source").toString() == QStringLiteral("InterfaceSettings") &&
                entry.value("message").toString() == QStringLiteral("Qt display settings saved");
        });
    if (!displaySettingsEventLogged) return fail("Qt display settings save was not logged");
    QFile shortcutTarget(temp.path() + "/launch target.txt");
    if (!shortcutTarget.open(QIODevice::WriteOnly) || shortcutTarget.write("target") != 6) return fail("Shortcut target fixture failed");
    shortcutTarget.close();
    nav->setCurrentRow(11);
    if (nav->item(11)->isHidden() || !primary->isVisible() || primary->text() != QString::fromUtf8("Добавить ярлык"))
        return fail("Shortcut page unavailable without admin access");
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "shortcutEditor") return;
        dialog->findChild<QLineEdit*>("shortcutLabel")->setText(QString::fromUtf8("Тестовый запуск"));
        dialog->findChild<QLineEdit*>("shortcutPath")->setText(QDir::toNativeSeparators(shortcutTarget.fileName()));
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/shortcut-editor.png"); }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    primary->click();
    if (workspace.data.shortcuts.size() != 1 || table->rowCount() != 1 ||
        table->item(0, 0)->text() != QString::fromUtf8("Тестовый запуск")) return fail("Shortcut UI add failed");
    table->selectRow(0);
    auto* openShortcut = window.findChild<QPushButton*>("openShortcut");
    if (!openShortcut || !openShortcut->isEnabled()) return fail("Shortcut open action unavailable");
    const auto shortcutArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!shortcutArtifacts.isEmpty()) window.grab().save(shortcutArtifacts + "/shortcut-page.png");
    QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
    window.findChild<QPushButton*>("deleteEntry")->click();
    if (!workspace.data.shortcuts.empty() || table->rowCount() != 0 || !QFileInfo::exists(shortcutTarget.fileName()))
        return fail("Shortcut UI delete removed data incorrectly");
    CloudSyncConfig displayCloudConfig;
    displayCloudConfig.enabled = false;
    displayCloudConfig.root = std::filesystem::u8path((temp.path() + "/manifest-cloud").toUtf8().constData());
    displayCloudConfig.manifest = displayCloudConfig.root / "meta/manifest.ini";
    CloudManifest displayCloudManifest;
    displayCloudManifest.appVersion = "0.6.57";
    displayCloudManifest.dataUpdatedAt = 1790609509;
    if (!SaveCloudSyncConfig(workspace.directory, displayCloudConfig) ||
        !SaveCloudManifest(displayCloudConfig, workspace.directory, displayCloudManifest)) return fail("Cloud manifest fixture failed");
    nav->setCurrentRow(13);
    auto* cloudPull = window.findChild<QPushButton*>("cloudPull");
    auto* storageResolve = window.findChild<QPushButton*>("storageResolve");
    if (nav->item(13)->isHidden() || !primary->isVisible() || primary->text() != QString::fromUtf8("Настроить облако") ||
        !cloudPull || !cloudPull->isVisible() || cloudPull->isEnabled() || !storageResolve || !storageResolve->isVisible() || storageResolve->isEnabled() ||
        !window.findChild<QLabel*>("summary")->text().contains(QString::fromUtf8("Ручные pull"))) return fail("Cloud guarded pull page unavailable");
    const auto expectedCloudUpdate = QDateTime::fromSecsSinceEpoch(displayCloudManifest.dataUpdatedAt).toString("yyyy-MM-dd HH:mm");
    bool cloudUpdateTimestampVisible = false;
    for (int row = 0; row < table->rowCount(); ++row)
        cloudUpdateTimestampVisible |= table->item(row, 0)->text() == QString::fromUtf8("Данные обновлены") &&
            table->item(row, 1)->text() == expectedCloudUpdate;
    if (!cloudUpdateTimestampVisible) return fail("Cloud data update timestamp was not shown from the manifest");
    const auto cloudArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!cloudArtifacts.isEmpty()) window.grab().save(cloudArtifacts + "/cloud-page.png");
    QTimer::singleShot(0, [] { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject(); });
    primary->click();
    nav->setCurrentRow(8);
    if (!window.findChild<QWidget*>("pomodoroPanel")->isVisible() || table->isVisible() || search->isVisible())
        return fail("Pomodoro page layout failed");
    nav->setCurrentRow(1);
    if (table->rowCount() != 1 || !table->item(0, 0)->text().contains(QString::fromUtf8("Проверка Qt"))) return fail("Task loading failed");
    if (primary->isVisible() || !nav->item(9)->isHidden() || !nav->item(12)->isHidden()) return fail("Unauthenticated user can mutate data");
    if (window.findChild<QPushButton*>("advanceStage")->isVisible()) return fail("Unauthenticated pipeline transition visible");
    search->setText("no-matches");
    if (table->rowCount() != 0) return fail("Search did not filter");
    search->clear();
    status->setCurrentIndex(2);
    if (table->rowCount() != 0) return fail("Status filter failed");
    status->setCurrentIndex(0);
    if (table->rowCount() != 1) return fail("Reset filter failed");
    table->selectRow(0);
    auto* details = window.findChild<QTextBrowser*>("details");
    if (!details->toPlainText().contains("<без HTML>")) return fail("HTML escaping failed");
    auto* detailsShortcut = window.findChild<QShortcut*>("shortcutDetails");
    if (!detailsShortcut || details->isVisible() || !QMetaObject::invokeMethod(detailsShortcut, "activated") || !details->isVisible())
        return fail("Details shortcut failed");
    auto* access = window.findChild<QAction*>("profileAccess");
    auto* ownPassword = window.findChild<QAction*>("changeOwnProfilePassword");
    if (!access || !ownPassword || ownPassword->isEnabled()) return fail("Profile access not locked initially");
    bool loginChecked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* password = dialog->findChild<QLineEdit*>("profileLoginPassword");
        auto* trust = dialog->findChild<QComboBox*>("profileTrust");
        auto* submit = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        loginChecked = trust && trust->count() == 3 && trust->itemData(1).toInt() == 30 && trust->itemData(2).toInt() == 90;
        password->setText("wrong"); submit->click();
        loginChecked &= !dialog->findChild<QLabel*>("profileLoginNotice")->text().isEmpty() && password->text().isEmpty();
        password->setText("profile-test-password");
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/profile-login.png"); }
        submit->click();
    });
    access->trigger();
    if (!loginChecked || !ownPassword->isEnabled() || primary->isVisible() || !nav->item(2)->isHidden())
        return fail("Profile login failed or granted admin privileges");
    access->trigger();
    if (ownPassword->isEnabled()) return fail("Profile logout failed");
    // Drive the actual modal forms: authenticate, create, persist, and change status.
    if (!SetAdminPassword(workspace.directory, "qt-test-password")) return fail("Admin fixture failed");
    QAction* login = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->objectName() == QStringLiteral("adminLoginAction")) login = action;
    if (!login) return fail("Admin action missing");
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("qt-test-password"); });
    login->trigger();
    if (!primary->isVisible()) return fail("Admin login failed");
    workspace.modules.view3d = true;
    nav->item(15)->setHidden(false);
    if (nav->item(15)->isHidden()) return fail("3D viewer settings page unavailable to administrator");
    nav->setCurrentRow(15);
    auto* modelYaw = window.findChild<QSlider*>("modelYaw");
    if (!modelYaw || !primary->isVisible() || primary->text() != QString::fromUtf8("Сохранить настройки"))
        return fail("3D viewer settings controls unavailable");
    modelYaw->setValue(37);
    QTimer::singleShot(0, [] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) message->accept();
    });
    primary->click();
    if (std::abs(LoadQtModelSettings(workspace.directory).yaw - 0.37f) > 0.001f)
        return fail("3D viewer settings did not persist through the real page");
    QFile modelSettingsEventLog(QString::fromStdWString((workspace.directory / "meta/qt-application-log.json").wstring()));
    if (!modelSettingsEventLog.open(QIODevice::ReadOnly)) return fail("3D viewer settings event log unavailable");
    const auto modelSettingsEvents = QJsonDocument::fromJson(modelSettingsEventLog.readAll()).array();
    modelSettingsEventLog.close();
    const bool modelSettingsEventLogged = std::any_of(modelSettingsEvents.begin(), modelSettingsEvents.end(),
        [](const QJsonValue& value) {
            const auto entry = value.toObject();
            return entry.value("source").toString() == QStringLiteral("ModelSettings") &&
                entry.value("message").toString() == QStringLiteral("3D viewer settings saved");
        });
    if (!modelSettingsEventLogged) return fail("3D viewer settings save was not logged");
    nav->setCurrentRow(0);
    nav->setCurrentRow(17);
    QApplication::processEvents();
    auto* profileStatsSummary = window.findChild<QLabel*>("summary");
    auto* statsArchived = window.findChild<QCheckBox*>("adminStatsIncludeArchived");
    auto* statsView = window.findChild<QComboBox*>("adminStatsView");
    auto* statsRank = window.findChild<QComboBox*>("adminStatsRankFilter");
    auto* statsSearch = window.findChild<QLineEdit*>("adminProfileStatsSearch");
    auto* statsDays = window.findChild<QSpinBox*>("adminStatsInactivityDays");
    auto* statsAutoRefresh = window.findChild<QCheckBox*>("adminStatsAutoRefresh");
    auto* statsRefreshSeconds = window.findChild<QSpinBox*>("adminStatsRefreshSeconds");
    auto* statsRefreshButton = window.findChild<QPushButton*>("adminStatsRefresh");
    auto* statsResetButton = window.findChild<QPushButton*>("adminStatsReset");
    if (nav->item(17)->isHidden() || !profileStatsSummary || !profileStatsSummary->text().contains(QString::fromUtf8("Профилей: 2")) ||
        !statsArchived || !statsArchived->isChecked() || !statsView || !statsRank || !statsSearch || !statsDays ||
        !statsAutoRefresh || !statsRefreshSeconds || !statsRefreshButton || !statsResetButton ||
        statsArchived->accessibleName().isEmpty() || statsRank->accessibleName().isEmpty() || statsView->accessibleName().isEmpty() ||
        statsSearch->accessibleName().isEmpty() || statsAutoRefresh->accessibleName().isEmpty() ||
        statsRefreshSeconds->accessibleName().isEmpty() || statsDays->accessibleName().isEmpty() ||
        table->rowCount() != 2 || table->columnCount() != 9)
        return fail("Administrator profile statistics page or archived profile data unavailable");
    const auto statsArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!statsArtifacts.isEmpty()) {
        QDir().mkpath(statsArtifacts);
        window.grab().save(statsArtifacts + "/admin-profile-stats.png");
    }
    bool archivedRowFound = false;
    for (int index = 0; index < table->rowCount(); ++index) {
        const auto* idCell = table->item(index, 0);
        const auto* nameCell = table->item(index, 1);
        if (idCell && idCell->data(Qt::UserRole).toString() == QString::fromStdString(archivedInfo->id)) {
            archivedRowFound = nameCell && nameCell->text().contains(QString::fromUtf8("Архив")) &&
                table->item(index, 7)->text() == "4" && table->item(index, 8)->text() == "1 / 2";
        }
    }
    if (!archivedRowFound) return fail("Profile statistics missed archived status, recovery, or achievement metrics");
    statsArchived->setChecked(false);
    if (table->rowCount() != 1) return fail("Archive filter did not exclude archived profile");
    statsArchived->setChecked(true);
    statsRank->setCurrentIndex(2);
    if (table->rowCount() != 1 || table->item(0, 2)->text() != QString::fromUtf8("Джуниор I"))
        return fail("Profile statistics rank filter failed");
    statsRank->setCurrentIndex(0);
    statsSearch->setText(QString::fromUtf8("Архивный профиль статистики"));
    if (table->rowCount() != 1 || table->item(0, 0)->data(Qt::UserRole).toString() != QString::fromStdString(archivedInfo->id))
        return fail("Profile statistics ID/name search failed");
    statsSearch->clear();
    statsView->setCurrentIndex(4); statsDays->setValue(30);
    if (table->rowCount() != 1 || table->item(0, 0)->data(Qt::UserRole).toString() != QString::fromStdString(archivedInfo->id))
        return fail("Profile inactivity ranking or threshold failed");
    statsView->setCurrentIndex(5);
    if (table->rowCount() != 1 || table->item(0, 7)->text() != "4") return fail("Profile recovery list failed");
    statsView->setCurrentIndex(6);
    if (table->rowCount() != 16 || table->item(1, 0)->text() != QString::fromUtf8("Джуниор I") || table->item(1, 1)->text() != "1")
        return fail("Profile rank distribution failed");
    statsView->setCurrentIndex(7);
    if (table->rowCount() != 5 || table->item(0, 1)->text() != "5.0/10") return fail("Profile category average report failed");
    statsView->setCurrentIndex(0);
    const auto statsCsvPath = temp.path() + "/profile-stats.csv";
    QTimer::singleShot(0, [statsCsvPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(statsCsvPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    auto* statsExportReport = window.findChild<QPushButton*>("exportReport");
    if (!statsExportReport || !statsExportReport->isVisible()) return fail("Profile statistics export action unavailable");
    statsExportReport->click();
    QFile statsCsv(statsCsvPath);
    if (!statsCsv.open(QIODevice::ReadOnly)) return fail("Profile statistics CSV was not created");
    const auto statsCsvBytes = statsCsv.readAll();
    if (!statsCsvBytes.startsWith("\xEF\xBB\xBF") || !statsCsvBytes.contains("AchievementsActive") ||
        !statsCsvBytes.contains(QString::fromUtf8("Архивный профиль статистики").toUtf8()) ||
        !statsCsvBytes.contains("TeamValueReport")) return fail("Profile statistics export omitted filtered profile or team-value report data");
    int activeStatsRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString() == QString::fromStdString(createdProfile->id)) activeStatsRow = row;
    if (activeStatsRow < 0) return fail("Active profile missing from click-through statistics");
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
        table->visualItemRect(table->item(activeStatsRow, 0)).center());
    QApplication::processEvents();
    auto* profileSelector = window.findChild<QComboBox*>("profiles");
    if (nav->currentRow() != 0 || !profileSelector || profileSelector->currentData().toString() != QString::fromStdString(createdProfile->id))
        return fail("Single click in profile statistics did not open the selected profile");
    const auto savedStatsSettings = LoadQtDisplaySettings(workspace.directory);
    if (savedStatsSettings.adminStatsView != 0 || !savedStatsSettings.adminStatsIncludeArchived ||
        savedStatsSettings.adminStatsRankFilter != 0 || savedStatsSettings.adminStatsInactivityDays != 30 ||
        !savedStatsSettings.adminStatsAutoRefresh || savedStatsSettings.adminStatsRefreshSeconds != 30 ||
        !savedStatsSettings.adminStatsSearch.isEmpty())
        return fail("Profile statistics filters were not persisted");
    nav->setCurrentRow(0);
    auto* profileReportMenu = window.findChild<QToolButton*>("profileReportExport");
    if (!profileReportMenu || !profileReportMenu->isVisible() || !window.findChild<QAction*>("profileReportTxt") ||
        !window.findChild<QAction*>("profileReportCsv")) return fail("Personal profile report export actions unavailable");
    auto* directXp = window.findChild<QPushButton*>("directXp");
    if (!directXp || !directXp->isVisible() || !directXp->isEnabled()) return fail("Direct XP action unavailable");
    workspace.storage->set_active_profile(createdProfile->id);
    const auto directXpOriginal = workspace.storage->load_profile();
    if (!directXpOriginal) return fail("Direct XP original profile unavailable");
    const int directXpBefore = directXpOriginal->total_xp();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "directXpDialog") return;
        dialog->findChild<QSpinBox*>("directXpAmount")->setValue(200);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/direct-xp.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    directXp->click();
    workspace.storage->set_active_profile(createdProfile->id);
    const auto directXpProfile = workspace.storage->load_profile();
    if (!directXpProfile || directXpProfile->total_xp() != directXpBefore + 200 ||
        !window.statusBar()->currentMessage().contains(QString::fromUtf8("Начислено"))) return fail("Direct XP UI failed");
    QFile coreLog(QString::fromStdWString((workspace.directory / "meta/qt-application-log.json").wstring()));
    if (!coreLog.open(QIODevice::ReadOnly)) return fail("Direct XP telemetry file unavailable");
    const auto directXpLogBytes = coreLog.readAll();
    const auto directXpEntries = QJsonDocument::fromJson(directXpLogBytes).array(); coreLog.close();
    bool directXpTelemetryFound = false;
    for (const auto& value : directXpEntries) {
        const auto entry = value.toObject();
        if (entry.value("source").toString() != QStringLiteral("CoreProfileMutation") ||
            entry.value("message").toString() != QStringLiteral("Direct skill XP transaction committed")) continue;
        directXpTelemetryFound = true;
        if (QJsonDocument(entry).toJson(QJsonDocument::Compact).contains(createdProfile->id.c_str()))
            return fail("Direct XP telemetry contains the profile ID");
    }
    if (!directXpTelemetryFound) return fail("Direct XP telemetry event missing");
    auto hasAdminCoreEvent = [&] (const QString& expected) {
        const int priorPage = nav->currentRow();
        auto* filter = window.findChild<QComboBox*>("auditSourceFilter");
        if (!filter) return false;
        const int priorSource = filter->currentIndex();
        nav->setCurrentRow(7); filter->setCurrentIndex(5); QApplication::processEvents();
        bool found = false;
        for (int row = 0; row < table->rowCount(); ++row)
            found |= table->item(row, 3)->text() == QString::fromUtf8("Операция профиля") &&
                table->item(row, 6)->text() == expected;
        nav->setCurrentRow(priorPage); filter->setCurrentIndex(priorSource); QApplication::processEvents();
        return found;
    };
    if (!hasAdminCoreEvent(QString::fromUtf8("Direct skill XP transaction committed")))
        return fail("Direct skill XP missing from admin core audit");
    if (!workspace.storage->save_profile(*directXpOriginal)) return fail("Direct XP fixture restore failed");
    nav->setCurrentRow(10);
    if (!primary->isVisible() || primary->text() != QString::fromUtf8("Настройки хранилища") ||
        !window.findChild<QLabel*>("summary")->text().contains(QString::fromUtf8("Баланс хранилища")))
        return fail("Vault page unavailable");
    QTimer::singleShot(0, [] { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject(); });
    primary->click();
    nav->setCurrentRow(12);
    if (nav->item(12)->isHidden() || !primary->isVisible() || primary->text() != QString::fromUtf8("Добавить фразу"))
        return fail("Banner page unavailable");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "bannerEditor") return;
        dialog->findChild<QPlainTextEdit*>("bannerText")->setPlainText(QString::fromUtf8("Фраза из Qt"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    primary->click();
    auto* bannerStrip = window.findChild<QLabel*>("bannerStrip");
    if (!bannerStrip || !bannerStrip->isVisible() || bannerStrip->text() != QString::fromUtf8("Фраза из Qt") || table->rowCount() != 1)
        return fail("Banner strip did not refresh");
    table->selectRow(0);
    const auto bannerArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!bannerArtifacts.isEmpty()) window.grab().save(bannerArtifacts + "/banner-page.png");
    QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
    window.findChild<QPushButton*>("deleteEntry")->click();
    if (!workspace.data.bannerTexts.empty() || bannerStrip->isVisible() || table->rowCount() != 0)
        return fail("Banner deletion did not refresh");
    nav->setCurrentRow(6);
    auto* exportReport = window.findChild<QPushButton*>("exportReport");
    if (!exportReport || !exportReport->isVisible()) return fail("Report export action unavailable");
    const auto uiReportPath = temp.path() + "/ui-report.csv";
    QTimer::singleShot(0, [uiReportPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(uiReportPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportReport->click();
    QFile uiReport(uiReportPath);
    if (!uiReport.open(QIODevice::ReadOnly) || !uiReport.readAll().startsWith("\xEF\xBB\xBF"))
        return fail("Report export UI failed");
    auto* reportView = window.findChild<QComboBox*>("reportView");
    if (!reportView || reportView->count() != 8) return fail("Report view selector unavailable");
    reportView->setCurrentIndex(1);
    auto* reportRange = window.findChild<QComboBox*>("reportDateRange");
    auto* reportCompare = window.findChild<QCheckBox*>("reportComparePrevious");
    if (!reportRange || !reportCompare) return fail("Report comparison controls unavailable");
    reportRange->setCurrentIndex(1);
    if (!reportCompare->isEnabled()) return fail("Report comparison not enabled for bounded period");
    reportCompare->setChecked(true);
    bool assigneeVisible = table->columnCount() == 14;
    for (int i = 0; i < table->rowCount(); ++i)
        assigneeVisible = assigneeVisible && table->item(i, 0)->text() == QString::fromUtf8("Тестовый профиль") &&
            table->item(i, 1)->text() == QString::fromStdString(createdProfile->id);
    if (!assigneeVisible || table->columnCount() != 14 || table->rowCount() != 1 ||
        table->horizontalHeaderItem(8)->text() != QString::fromUtf8("Активно · пред.") ||
        table->item(0, 8)->text() != "0" || table->item(0, 13)->text() != "0" ||
        !window.findChild<QLabel*>("summary")->text().contains(QString::fromUtf8("сравнение с")))
        return fail("Assignee report comparison failed");
    table->selectRow(0);
    auto* reportDetails = window.findChild<QTextBrowser*>("details");
    const auto reportDetailText = reportDetails ? reportDetails->toPlainText() : QString();
    if (!reportDetails || !reportDetailText.contains(QString::fromUtf8("Статусы: новые / в работе / завершены")) ||
        !reportDetailText.contains(QString::fromUtf8("Просрочено / ожидают XP")) ||
        !reportDetailText.contains(QString::fromUtf8("Проверка Qt <без HTML>")) ||
        !reportDetailText.contains(QString::fromUtf8("Old stage")) ||
        !reportDetailText.contains(QString::fromUtf8("Предыдущий период")) ||
        !reportDetailText.contains(QString::fromUtf8("Исполнители и участники"))) return fail("Report group drill-down incomplete");
    reportView->setCurrentIndex(4);
    if (table->rowCount() != 3 || table->columnCount() != 15 ||
        table->horizontalHeaderItem(8)->text() != QString::fromUtf8("Задач · пред."))
        return fail("Status report comparison layout failed");
    int createdStatusRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->text() == QString::fromUtf8(AppTaskStatusLabel(0))) createdStatusRow = row;
    }
    if (createdStatusRow < 0 || table->item(createdStatusRow, 1)->text() != "1")
        return fail("Status report did not group the active task");
    table->selectRow(createdStatusRow);
    reportDetails = window.findChild<QTextBrowser*>("details");
    if (!reportDetails || !reportDetails->toPlainText().contains(QString::fromUtf8("Статус задачи")) ||
        !reportDetails->toPlainText().contains(QString::fromUtf8("Проверка Qt <без HTML>")))
        return fail("Status report drill-down failed");
    if (LoadQtDisplaySettings(workspace.directory).reportView != 4)
        return fail("Status report grouping did not persist");
    reportView->setCurrentIndex(7);
    const auto currentMonth = QDate::currentDate().toString("yyyy-MM");
    if (table->rowCount() != 1 || table->columnCount() != 15 ||
        table->horizontalHeaderItem(0)->text() != QString::fromUtf8("Месяц создания") ||
        table->item(0, 0)->text() != currentMonth || table->item(0, 1)->text() != "1" ||
        LoadQtDisplaySettings(workspace.directory).reportView != 7)
        return fail("Creation-month report grouping or persistence failed");
    table->selectRow(0);
    reportDetails = window.findChild<QTextBrowser*>("details");
    if (!reportDetails || !reportDetails->toPlainText().contains(QString::fromUtf8("Месяц создания")) ||
        !reportDetails->toPlainText().contains(QString::fromUtf8("Проверка Qt <без HTML>")))
        return fail("Creation-month report drill-down failed");
    reportView->setCurrentIndex(1);
    reportRange->setCurrentIndex(0);
    if (reportCompare->isEnabled() || table->columnCount() != 8 || !reportCompare->isChecked())
        return fail("All-time comparison guard failed");
    reportRange->setCurrentIndex(1);
    const auto reportArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!reportArtifacts.isEmpty()) window.grab().save(reportArtifacts + "/statistics-export.png");
    nav->setCurrentRow(9);
    if (!primary->isVisible() || primary->text() != QString::fromUtf8("Изменить правила") || table->rowCount() != 13)
        return fail("Rules page unavailable");
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        dialog->findChild<QSpinBox*>("rulesLevelBase")->setValue(2468);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    primary->click();
    if (workspace.data.rulesConfig.levelBaseXp != 2468 || GetGameplayConfig().levelBaseXp != 2468)
        return fail("Rules page did not apply config");
    bool rulesValueVisible = false;
    for (int i = 0; i < table->rowCount(); ++i)
        rulesValueVisible |= table->item(i, 0)->text() == QString::fromUtf8("Базовый XP уровня") && table->item(i, 1)->text() == "2468";
    if (!rulesValueVisible) return fail("Rules page did not refresh");
    auto* reapplyRules = window.findChild<QPushButton*>("reapplyRules");
    if (!reapplyRules || !reapplyRules->isVisible() || !reapplyRules->isEnabled()) return fail("Rules reapply action unavailable");
    QTimer::singleShot(0, [] {
        auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (confirm) {
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) confirm->grab().save(artifacts + "/rules-reapply-confirm.png");
            confirm->button(QMessageBox::Yes)->click();
        }
    });
    reapplyRules->click();
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction") ||
        !window.statusBar()->currentMessage().contains(QString::fromUtf8("Профили пересчитаны")))
        return fail("Rules reapply UI failed");
    if (!coreLog.open(QIODevice::ReadOnly)) return fail("Rules telemetry file unavailable");
    const auto rulesTelemetry = coreLog.readAll(); coreLog.close();
    if (!rulesTelemetry.contains("Rules reapply transaction committed") ||
        rulesTelemetry.contains(createdProfile->id.c_str())) return fail("Rules telemetry privacy or outcome failed");
    if (!hasAdminCoreEvent(QString::fromUtf8("Rules reapply transaction committed")))
        return fail("Rules transaction missing from admin core audit");
    const auto rulesArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!rulesArtifacts.isEmpty()) window.grab().save(rulesArtifacts + "/rules-page.png");
    window.statusBar()->showMessage(QString::fromUtf8("audit-app-log-unique-token"));
    workspace.data.vault.log.push_back({QDateTime::currentSecsSinceEpoch(), 12.5, "audit_test", "audit-vault-unique-token"});
    nav->setCurrentRow(7);
    bool profileAuditVisible = false;
    QDateTime previousAuditTime;
    bool auditChronological = true;
    for (int i = 0; i < table->rowCount(); ++i) {
        profileAuditVisible |= table->item(i, 0)->text() == QString::fromUtf8("Профиль");
        const auto currentAuditTime = QDateTime::fromString(table->item(i, 1)->text(), "dd.MM.yyyy HH:mm");
        if (currentAuditTime.isValid()) {
            if (previousAuditTime.isValid() && currentAuditTime > previousAuditTime) auditChronological = false;
            previousAuditTime = currentAuditTime;
        }
    }
    if (!auditChronological) return fail("Audit rows are not newest first");
    if (!profileAuditVisible) return fail("Profile audit is not visible");
    auto* exportAudit = window.findChild<QPushButton*>("exportAudit");
    if (!exportAudit || !exportAudit->isVisible() || table->rowCount() == 0) return fail("Audit export action unavailable");
    auto* auditSourceFilter = window.findChild<QComboBox*>("auditSourceFilter");
    if (!auditSourceFilter || !auditSourceFilter->isVisible() || auditSourceFilter->count() != 6 || auditSourceFilter->currentIndex() != 0)
        return fail("Audit source filter unavailable");
    bool taskAuditVisible = false;
    for (int index = 0; index < table->rowCount(); ++index)
        taskAuditVisible |= table->item(index, 0)->text() == QString::fromUtf8("Задача");
    if (!taskAuditVisible) return fail("Task audit source is missing");
    auditSourceFilter->setCurrentIndex(1);
    if (table->rowCount() == 0) return fail("Task audit filter returned no rows");
    for (int index = 0; index < table->rowCount(); ++index)
        if (table->item(index, 0)->text() != QString::fromUtf8("Задача")) return fail("Task audit filter leaked another source");
    if (LoadQtDisplaySettings(workspace.directory).auditSourceFilter != 1) return fail("Audit source filter did not persist");
    auto* auditActorFilter = window.findChild<QLineEdit*>("auditActorFilter");
    auto* auditObjectFilter = window.findChild<QLineEdit*>("auditObjectFilter");
    auto* auditFieldFilter = window.findChild<QLineEdit*>("auditFieldFilter");
    auto* auditFilterReset = window.findChild<QPushButton*>("auditFilterReset");
    if (!auditActorFilter || !auditObjectFilter || !auditFieldFilter || !auditFilterReset)
        return fail("Focused audit filters unavailable");
    const auto actorToken = table->item(0, 2)->text();
    auditActorFilter->setText(actorToken);
    if (table->rowCount() == 0) return fail("Audit actor filter returned no rows");
    for (int index = 0; index < table->rowCount(); ++index)
        if (!table->item(index, 2)->text().contains(actorToken, Qt::CaseInsensitive)) return fail("Audit actor filter leaked a row");
    const auto taskToken = table->item(0, 3)->text();
    auditObjectFilter->setText(taskToken);
    if (table->rowCount() == 0) return fail("Audit object filter returned no rows");
    for (int index = 0; index < table->rowCount(); ++index) {
        const auto searchable = table->item(index, 3)->text() + QLatin1Char(' ') + table->item(index, 5)->text() + QLatin1Char(' ') + table->item(index, 6)->text();
        if (!searchable.contains(taskToken, Qt::CaseInsensitive)) return fail("Audit object filter leaked a row");
    }
    const auto fieldToken = table->item(0, 4)->text();
    auditFieldFilter->setText(fieldToken);
    if (table->rowCount() == 0) return fail("Audit field filter returned no rows");
    for (int index = 0; index < table->rowCount(); ++index)
        if (!table->item(index, 4)->text().contains(fieldToken, Qt::CaseInsensitive)) return fail("Audit field filter leaked a row");
    auditFieldFilter->setText(QString::fromUtf8("audit-field-that-does-not-exist"));
    if (table->rowCount() != 0) return fail("Audit filters did not intersect");
    auditFilterReset->click();
    if (auditSourceFilter->currentIndex() != 0 || !search->text().isEmpty() || table->rowCount() == 0)
        return fail("Audit filter reset failed");
    auditSourceFilter->setCurrentIndex(2);
    if (table->rowCount() == 0) return fail("Profile audit filter returned no rows");
    for (int index = 0; index < table->rowCount(); ++index)
        if (table->item(index, 0)->text() != QString::fromUtf8("Профиль")) return fail("Profile audit filter leaked another source");
    auditSourceFilter->setCurrentIndex(3);
    if (table->rowCount() == 0) return fail("Application audit filter returned no rows");
    bool applicationEventVisible = false;
    for (int index = 0; index < table->rowCount(); ++index) {
        if (table->item(index, 0)->text() != QString::fromUtf8("Приложение") ||
            table->item(index, 3)->text() != QString::fromUtf8("Журнал Qt")) return fail("Application audit filter leaked another source");
        applicationEventVisible |= table->item(index, 6)->text().contains(QString::fromUtf8("audit-app-log-unique-token"));
    }
    if (!applicationEventVisible) return fail("Application audit did not expose the Qt activity log entry");
    auditSourceFilter->setCurrentIndex(4);
    if (table->rowCount() == 0) return fail("Vault audit filter returned no rows");
    bool vaultEventVisible = false;
    for (int index = 0; index < table->rowCount(); ++index) {
        if (table->item(index, 0)->text() != QString::fromUtf8("Хранилище")) return fail("Vault audit filter leaked another source");
        vaultEventVisible |= table->item(index, 3)->text() == QStringLiteral("audit_test") &&
            table->item(index, 6)->text().contains(QStringLiteral("audit-vault-unique-token")) &&
            table->item(index, 6)->text().contains(QStringLiteral("12.50"));
    }
    if (!vaultEventVisible) return fail("Vault audit filter did not show the local storage log entry");
    auditSourceFilter->setCurrentIndex(2);
    const int auditRowsBeforeExport = table->rowCount();
    const auto uiAuditPath = temp.path() + "/ui-audit.csv";
    QTimer::singleShot(0, [uiAuditPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(uiAuditPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportAudit->click();
    QFile uiAudit(uiAuditPath);
    if (!uiAudit.open(QIODevice::ReadOnly)) return fail("Audit export UI did not create a file");
    const auto uiAuditBytes = uiAudit.readAll();
    if (!uiAuditBytes.startsWith("\xEF\xBB\xBF") ||
        !uiAuditBytes.contains(QString::fromUtf8("Источник,Время,Автор,Объект,Поле,Было,Стало").toUtf8()) ||
        !window.statusBar()->currentMessage().contains(QString::fromUtf8("Экспортировано событий: %1").arg(auditRowsBeforeExport)))
        return fail("Audit export UI content or visible row count failed");
    nav->setCurrentRow(16);
    auto* logInfo = window.findChild<QCheckBox*>("logInfo");
    auto* logWarnings = window.findChild<QCheckBox*>("logWarnings");
    auto* logErrors = window.findChild<QCheckBox*>("logErrors");
    auto* logSourceFilter = window.findChild<QComboBox*>("logSourceFilter");
    auto* logPresetAll = window.findChild<QPushButton*>("logPresetAll");
    auto* logPresetWarningsErrors = window.findChild<QPushButton*>("logPresetWarningsErrors");
    auto* logPresetErrors = window.findChild<QPushButton*>("logPresetErrors");
    auto* exportLogs = window.findChild<QPushButton*>("exportLogs");
    auto* clearLogs = window.findChild<QPushButton*>("clearLogs");
    if (!logInfo || !logWarnings || !logErrors || !logSourceFilter || !logPresetAll || !logPresetWarningsErrors || !logPresetErrors ||
        !exportLogs || !clearLogs || !logInfo->isVisible() || !logPresetAll->isVisible() || !logPresetWarningsErrors->isVisible() ||
        !logPresetErrors->isVisible() || !logSourceFilter->isVisible() || !exportLogs->isVisible() || !clearLogs->isVisible())
        return fail("Qt application log controls unavailable");
    if (logInfo->accessibleName().isEmpty() || logWarnings->accessibleName().isEmpty() || logErrors->accessibleName().isEmpty() ||
        logSourceFilter->accessibleName().isEmpty() || logPresetAll->accessibleName().isEmpty() ||
        logPresetWarningsErrors->accessibleName().isEmpty() || logPresetErrors->accessibleName().isEmpty() ||
        exportLogs->accessibleName().isEmpty() || clearLogs->accessibleName().isEmpty() || clearLogs->accessibleDescription().isEmpty())
        return fail("Qt application log controls are missing accessible names or clear confirmation description");
    const auto savedLogQuery = QString::fromUtf8("автопрокрутки Qt-журнала");
    search->setText(savedLogQuery);
    if (LoadQtDisplaySettings(workspace.directory).logFilter != savedLogQuery)
        return fail("Qt application log query was not saved to workspace settings");
    nav->setCurrentRow(1);
    if (!search->text().isEmpty()) return fail("Qt log query leaked into another page");
    nav->setCurrentRow(16);
    if (search->text() != savedLogQuery) return fail("Qt log query did not restore when returning to logs");
    search->clear();
    auto* logSummary = window.findChild<QLabel*>("summary");
    QFile appLogFile(temp.path() + "/meta/qt-application-log.json");
    if (!logSummary || !appLogFile.open(QIODevice::ReadOnly)) return fail("Qt application log summary unavailable");
    auto* logActivityChartWidget = window.findChild<QWidget*>("logActivityChart");
    auto* logActivityChart = static_cast<QtLogActivityChart*>(logActivityChartWidget);
    if (!logActivityChart || !logActivityChart->isVisible() ||
        !logActivityChart->accessibleDescription().contains(QString::fromUtf8("по 16 интервалам")) ||
        !logActivityChart->accessibleDescription().contains(QString::fromUtf8("Число записей по интервалам")))
        return fail("Qt application log activity chart unavailable");
    const auto* logActivityAccessible = QAccessible::queryAccessibleInterface(logActivityChart);
    if (!logActivityAccessible || !logActivityAccessible->text(QAccessible::Description)
            .contains(QString::fromUtf8("Интервал 1:")))
        return fail("Qt application log chart values were not exposed through QAccessible");
    const auto appLogBytes = appLogFile.readAll();
    appLogFile.close();
    const auto appLogEntries = QJsonDocument::fromJson(appLogBytes).array();
    if (std::accumulate(logActivityChart->values().begin(), logActivityChart->values().end(), 0) != appLogEntries.size())
        return fail("Qt application log activity chart omitted entries or followed active filters");
    std::array<int, 3> appLogCounts{};
    for (const auto& value : appLogEntries) {
        const auto entry = value.toObject();
        const int level = entry.value("level").toInt(-1);
        if (level >= 0 && level < int(appLogCounts.size())) ++appLogCounts[size_t(level)];
    }
    if (!logSummary->text().contains(QString::fromUtf8("Инфо: %1 · Предупреждения: %2 · Ошибки: %3")
            .arg(appLogCounts[0]).arg(appLogCounts[1]).arg(appLogCounts[2])))
        return fail("Qt application log level summary does not match persisted entries");
    const auto logArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!logArtifacts.isEmpty()) logActivityChart->grab().save(logArtifacts + "/log-activity.png");
    logPresetWarningsErrors->click();
    if (logInfo->isChecked() || !logWarnings->isChecked() || !logErrors->isChecked())
        return fail("Qt log warning/error preset did not select the expected levels");
    logPresetErrors->click();
    if (logInfo->isChecked() || logWarnings->isChecked() || !logErrors->isChecked())
        return fail("Qt log errors-only preset did not select the expected level");
    logPresetAll->click();
    search->clear();
    if (!logInfo->isChecked() || !logWarnings->isChecked() || !logErrors->isChecked())
        return fail("Qt log all-levels preset did not restore all levels");
    const int qtSourceIndex = logSourceFilter->findData(QStringLiteral("Qt"));
    if (qtSourceIndex < 0) return fail("Qt application log source filter did not list the Qt source");
    logSourceFilter->setCurrentIndex(qtSourceIndex);
    if (table->rowCount() == 0) return fail("Qt application log source filter returned no Qt entries");
    for (int index = 0; index < table->rowCount(); ++index)
        if (table->item(index, 3)->text() != QStringLiteral("Qt")) return fail("Qt application log source filter leaked another source");
    if (LoadQtDisplaySettings(workspace.directory).logSourceFilter != QStringLiteral("Qt"))
        return fail("Qt application log source filter was not saved to workspace settings");
    logSourceFilter->setCurrentIndex(0);
    auto* logAutoScroll = window.findChild<QCheckBox*>("logAutoScroll");
    auto* logCompactView = window.findChild<QCheckBox*>("logCompactView");
    if (!logAutoScroll || !logCompactView || !logAutoScroll->isVisible() || !logCompactView->isVisible() ||
        logAutoScroll->accessibleName().isEmpty() || logCompactView->accessibleName().isEmpty())
        return fail("Qt log display options unavailable");
    logAutoScroll->setChecked(true);
    logCompactView->setChecked(true);
    if (!table->isColumnHidden(1) || !table->isColumnHidden(3))
        return fail("Qt compact log view did not hide timestamp and source columns");
    const int logRowsBeforeAutoscroll = table->rowCount();
    for (int index = 0; index < 20; ++index)
        window.statusBar()->showMessage(QString::fromUtf8("Проверка автопрокрутки Qt-журнала %1").arg(index));
    QApplication::processEvents();
    const int expectedLogRows = std::min(logRowsBeforeAutoscroll + 20, 200);
    if (table->rowCount() != expectedLogRows || table->verticalScrollBar()->maximum() == 0 ||
        table->verticalScrollBar()->value() != table->verticalScrollBar()->minimum() ||
        !table->item(0, 4)->text().contains(QString::fromUtf8("автопрокрутки Qt-журнала 19")))
        return fail("Qt application log did not append and autoscroll to a new entry");
    logAutoScroll->setChecked(false);
    logWarnings->setChecked(false);
    logSourceFilter->setCurrentIndex(0);
    const auto startupLogToken = QString::fromUtf8("рабочее пространство загружено");
    search->setText(startupLogToken);
    if (table->rowCount() == 0 || !table->item(0, 4)->text().contains(startupLogToken, Qt::CaseInsensitive))
        return fail("Qt startup event was not captured in the application log");
    logInfo->setChecked(false);
    if (table->rowCount() != 0) return fail("Qt log level filter did not hide info entries");
    logInfo->setChecked(true);
    if (table->rowCount() == 0) return fail("Qt log level filter did not restore info entries");
    const auto uiLogPath = temp.path() + "/ui-app-log.txt";
    search->setText(QStringLiteral("1"));
    QStringList expectedExportMessages;
    for (int index = 0; index < table->rowCount(); ++index)
        expectedExportMessages.push_back(table->item(index, 4)->text().replace('\r', ' ').replace('\n', ' ').replace('|', '/'));
    if (expectedExportMessages.isEmpty()) return fail("Qt numeric log search unexpectedly returned no visible rows");
    QTimer::singleShot(0, [uiLogPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(uiLogPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    exportLogs->click();
    QFile uiLogs(uiLogPath);
    if (!uiLogs.open(QIODevice::ReadOnly)) return fail("Qt application log export did not create a file");
    const auto uiLogBytes = uiLogs.readAll();
    const auto exportedLines = QString::fromUtf8(uiLogBytes.mid(3)).trimmed().split('\n');
    bool exportMatchesTable = uiLogBytes.startsWith("\xEF\xBB\xBF") && exportedLines.size() == expectedExportMessages.size();
    for (int index = 0; exportMatchesTable && index < expectedExportMessages.size(); ++index)
        exportMatchesTable = exportedLines[index].endsWith(" | " + expectedExportMessages[index]);
    if (!exportMatchesTable) {
        return fail("Qt application log export did not match visible search results or encoding");
    }
    search->clear();
    clearLogs->click();
    if (!logSummary || table->rowCount() != 1 || !logSummary->text().contains(QString::fromUtf8("1 из 1")) ||
        !table->item(0, 4)->text().contains(QString::fromUtf8("очищен"), Qt::CaseInsensitive))
        return fail("Qt application log clear failed");
    QFile persistedLog(temp.path() + "/meta/qt-application-log.json");
    if (!persistedLog.open(QIODevice::ReadOnly)) return fail("Qt application log persistence file could not be opened");
    const auto persistedLogBytes = persistedLog.readAll();
    persistedLog.close();
    if (!persistedLogBytes.contains("Журнал Qt-сессии очищен"))
        return fail("Qt application log was not persisted locally");
    QtWindow viewerWindow(workspace);
    viewerWindow.show();
    QApplication::processEvents();
    auto* viewerNavigation = viewerWindow.findChild<QListWidget*>("navigation");
    auto* viewerTable = viewerWindow.findChild<QTableWidget*>("records");
    auto* viewerAuditExport = viewerWindow.findChild<QPushButton*>("exportAudit");
    auto* viewerAuditSource = viewerWindow.findChild<QComboBox*>("auditSourceFilter");
    if (!viewerNavigation || !viewerTable || !viewerAuditExport || !viewerAuditSource || viewerNavigation->item(7)->isHidden())
        return fail("Task audit page was not exposed to a non-administrator");
    viewerNavigation->setCurrentRow(7);
    if (viewerTable->rowCount() == 0 || viewerAuditSource->isVisible() || !viewerAuditExport->isVisible())
        return fail("Non-administrator task audit controls or rows are incorrect");
    for (int index = 0; index < viewerTable->rowCount(); ++index)
        if (viewerTable->item(index, 0)->text() != QString::fromUtf8("Задача"))
            return fail("Non-administrator task audit leaked profile activity");
    const auto viewerAuditPath = temp.path() + "/viewer-task-audit.csv";
    QTimer::singleShot(0, [viewerAuditPath] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(viewerAuditPath);
            static_cast<QDialog*>(dialog)->accept();
        }
    });
    viewerAuditExport->click();
    QFile viewerAuditFile(viewerAuditPath);
    if (!viewerAuditFile.open(QIODevice::ReadOnly)) return fail("Non-administrator task audit export did not create a file");
    const auto viewerAuditBytes = viewerAuditFile.readAll();
    if (!viewerAuditBytes.startsWith("\xEF\xBB\xBF") ||
        !viewerAuditBytes.contains(QString::fromUtf8("Задача").toUtf8()) ||
        viewerAuditBytes.contains(QString::fromUtf8("Профиль").toUtf8()))
        return fail("Non-administrator task audit export contained wrong sources or encoding");
    QtWindow restartedWindow(workspace);
    restartedWindow.show();
    QApplication::processEvents();
    auto* restartedNavigation = restartedWindow.findChild<QListWidget*>("navigation");
    auto* restartedTable = restartedWindow.findChild<QTableWidget*>("records");
    if (!restartedNavigation || !restartedTable) return fail("Qt application log restart window failed");
    restartedNavigation->setCurrentRow(16);
    if (restartedTable->rowCount() < 2) return fail("Qt application log history did not survive a window restart");
    auto* restartedAutoScroll = restartedWindow.findChild<QCheckBox*>("logAutoScroll");
    auto* restartedCompactView = restartedWindow.findChild<QCheckBox*>("logCompactView");
    auto* restartedLogInfo = restartedWindow.findChild<QCheckBox*>("logInfo");
    auto* restartedLogWarnings = restartedWindow.findChild<QCheckBox*>("logWarnings");
    auto* restartedLogErrors = restartedWindow.findChild<QCheckBox*>("logErrors");
    if (!restartedAutoScroll || !restartedCompactView || restartedAutoScroll->isChecked() ||
        !restartedCompactView->isChecked() || !restartedLogInfo || !restartedLogInfo->isChecked() ||
        !restartedLogWarnings || restartedLogWarnings->isChecked() || !restartedLogErrors || !restartedLogErrors->isChecked() ||
        !restartedTable->isColumnHidden(1) || !restartedTable->isColumnHidden(3)) {
        std::cerr << "Log restart state: autoscr=" << (restartedAutoScroll ? restartedAutoScroll->isChecked() : -1)
                  << " compact=" << (restartedCompactView ? restartedCompactView->isChecked() : -1)
                  << " info=" << (restartedLogInfo ? restartedLogInfo->isChecked() : -1)
                  << " warnings=" << (restartedLogWarnings ? restartedLogWarnings->isChecked() : -1)
                  << " errors=" << (restartedLogErrors ? restartedLogErrors->isChecked() : -1)
                  << " columns=" << restartedTable->isColumnHidden(1) << ',' << restartedTable->isColumnHidden(3) << '\n';
        return fail("Qt log display preferences did not persist across a window restart");
    }
    bool restoredClearEntry = false;
    for (int index = 0; index < restartedTable->rowCount(); ++index)
        restoredClearEntry |= restartedTable->item(index, 4)->text().contains(QString::fromUtf8("очищен"), Qt::CaseInsensitive);
    if (!restoredClearEntry) return fail("Qt application log did not restore the previous session entry");
    const auto auditArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!auditArtifacts.isEmpty()) window.grab().save(auditArtifacts + "/profile-audit.png");
    nav->setCurrentRow(5);
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        dialog->findChild<QLineEdit*>("professionName")->setText("Qt profession");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    primary->click();
    if (LoadProfessionsData(workspace.directory).size() != 1) return fail("Profession entry point failed");
    table->selectRow(0);
    auto* deleteProfession = window.findChild<QPushButton*>("deleteEntry");
    if (!deleteProfession || !deleteProfession->isVisible() || !deleteProfession->isEnabled())
        return fail("Profession delete control unavailable");
    const auto professionArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!professionArtifacts.isEmpty()) window.grab().save(professionArtifacts + "/profession-delete.png");
    QTimer::singleShot(0, [] {
        auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (confirm) confirm->button(QMessageBox::Yes)->click();
    });
    deleteProfession->click();
    if (!LoadProfessionsData(workspace.directory).empty() ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("Profession delete entry point failed");
    if (!workspace.catalog.add_skill("Qt disposable", 1.0, "Delete entry point"))
        return fail("Skill delete UI fixture failed");
    const auto disposableSkill = workspace.catalog.id_for_name("Qt disposable");
    if (!disposableSkill) return fail("Skill delete UI fixture ID failed");
    nav->setCurrentRow(3);
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == QString::fromStdString(*disposableSkill)) table->selectRow(i);
    auto* deleteSkill = window.findChild<QPushButton*>("deleteEntry");
    if (!deleteSkill || !deleteSkill->isVisible() || !deleteSkill->isEnabled())
        return fail("Skill delete control unavailable");
    const auto skillDeleteArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!skillDeleteArtifacts.isEmpty()) window.grab().save(skillDeleteArtifacts + "/skill-delete.png");
    QTimer::singleShot(0, [] {
        auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (confirm) confirm->button(QMessageBox::Yes)->click();
    });
    deleteSkill->click();
    if (workspace.catalog.contains_id(*disposableSkill) ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("Skill delete entry point failed");
    nav->setCurrentRow(4);
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == QString::fromStdString(stage.id)) table->selectRow(i);
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        dialog->findChild<QLineEdit*>("stageTitle")->setText("Updated stage");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    window.findChild<QPushButton*>("editEntry")->click();
    nav->setCurrentRow(1);
    if (table->item(0, 7)->text() != "Updated stage" || LoadTasksData(workspace.directory).front().pipelineStepId != stage.id)
        return fail("Pipeline rename broke linked task display");
    table->selectRow(0);
    bool terminalChecked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        terminalChecked = dialog && dialog->findChild<QComboBox*>("nextStage")->count() == 0 &&
            !dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->isEnabled();
        if (dialog) dialog->reject();
    });
    window.findChild<QPushButton*>("advanceStage")->click();
    if (!terminalChecked) return fail("Terminal stage transition should be unavailable");
    nav->setCurrentRow(4);
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == QString::fromStdString(unusedStage.id)) table->selectRow(i);
    auto* moveUp = window.findChild<QPushButton*>("movePipelineUp");
    auto* moveDown = window.findChild<QPushButton*>("movePipelineDown");
    const auto unusedBefore = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& item) { return item.id == unusedStage.id; });
    const int unusedBeforeIndex = int(std::distance(workspace.data.pipelineSteps.begin(), unusedBefore));
    if (!moveUp || !moveDown || !moveUp->isVisible() || !moveUp->isEnabled() || table->isSortingEnabled() || unusedBefore == workspace.data.pipelineSteps.end())
        return fail("Pipeline reorder controls unavailable");
    moveUp->click();
    const auto unusedAfterUp = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& item) { return item.id == unusedStage.id; });
    if (unusedAfterUp == workspace.data.pipelineSteps.end() || int(std::distance(workspace.data.pipelineSteps.begin(), unusedAfterUp)) != unusedBeforeIndex - 1 || !moveDown->isEnabled())
        return fail("Pipeline move up failed");
    const auto reorderArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!reorderArtifacts.isEmpty()) window.grab().save(reorderArtifacts + "/pipeline-reorder.png");
    moveDown->click();
    const auto unusedAfterDown = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& item) { return item.id == unusedStage.id; });
    if (unusedAfterDown == workspace.data.pipelineSteps.end() || int(std::distance(workspace.data.pipelineSteps.begin(), unusedAfterDown)) != unusedBeforeIndex)
        return fail("Pipeline move down failed");
    auto* deletePipeline = window.findChild<QPushButton*>("deleteEntry");
    if (!deletePipeline || !deletePipeline->isVisible() || !deletePipeline->isEnabled()) return fail("Pipeline delete action unavailable");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) confirm->button(QMessageBox::No)->click();
    });
    deletePipeline->click();
    auto pipelineDisk = LoadPipelineData(workspace.directory);
    if (std::none_of(pipelineDisk.begin(), pipelineDisk.end(), [&](const auto& item) { return item.id == unusedStage.id; }))
        return fail("Pipeline delete cancellation failed");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) confirm->grab().save(artifacts + "/pipeline-delete.png");
            confirm->button(QMessageBox::Yes)->click();
        }
    });
    deletePipeline->click();
    const auto remainingStages = LoadPipelineData(workspace.directory);
    if (std::any_of(remainingStages.begin(), remainingStages.end(), [&](const auto& item) { return item.id == unusedStage.id; }) ||
        std::none_of(remainingStages.begin(), remainingStages.end(), [&](const auto& item) { return item.id == stage.id; }) ||
        LoadTasksData(workspace.directory).front().pipelineStepId != stage.id)
        return fail("Pipeline delete persistence failed");
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == QString::fromStdString(stage.id)) table->selectRow(i);
    bool linkedStageBlocked = false;
    QTimer::singleShot(0, [&] {
        if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            linkedStageBlocked = warning->text().contains(QString::fromUtf8("используется задачами"));
            warning->accept();
        }
    });
    deletePipeline->click();
    pipelineDisk = LoadPipelineData(workspace.directory);
    if (!linkedStageBlocked || std::none_of(pipelineDisk.begin(), pipelineDisk.end(), [&](const auto& item) { return item.id == stage.id; }))
        return fail("Linked pipeline stage was not blocked");
    auto saveForm = [](const QString& title) {
        QTimer::singleShot(0, [title] {
            auto* dialog = QApplication::activeModalWidget();
            if (!dialog) return;
            auto* name = dialog->findChild<QLineEdit*>("entryTitle");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (name && buttons) { name->setText(title); buttons->button(QDialogButtonBox::Save)->click(); }
        });
    };
    nav->setCurrentRow(2);
    saveForm(QString::fromUtf8("Проект Qt"));
    primary->click();
    if (LoadProjectsData(workspace.directory).size() != 1) return fail("Project form did not persist");
    const auto originalProject = LoadProjectsData(workspace.directory).front();
    if (!AppUpdateTaskProject(workspace.directory, workspace.data.tasks, task.id,
        originalProject.id, originalProject.name, "test").ok) return fail("Project link fixture failed");
    nav->setCurrentRow(1);
    nav->setCurrentRow(2);
    auto* focusProject = window.findChild<QPushButton*>("focusProjectTasks");
    table->setCurrentCell(0, 0);
    table->selectRow(0);
    if (!focusProject || !focusProject->isVisible() || !focusProject->isEnabled()) return fail("Project task focus action unavailable");
    focusProject->click();
    if (nav->currentRow() != 1 || window.findChild<QComboBox*>("taskProjectFilter")->currentData().toString() != QString::fromStdString(originalProject.id) ||
        table->rowCount() != 1 || table->item(0, 2)->text() != QString::fromUtf8("Проект Qt"))
        return fail("Project task focus did not open the filtered task list");
    nav->setCurrentRow(2);
    table->setCurrentCell(0, 0);
    table->selectRow(0);
    saveForm(QString::fromUtf8("Проект после правки"));
    window.findChild<QPushButton*>("editEntry")->click();
    const auto editedProjects = LoadProjectsData(workspace.directory);
    if (editedProjects.size() != 1 || editedProjects.front().id != originalProject.id ||
        editedProjects.front().createdAt != originalProject.createdAt ||
        editedProjects.front().name != u8"Проект после правки") return fail("Project editing lost identity");
    nav->setCurrentRow(1);
    if (table->rowCount() != 1 || table->item(0, 2)->text() != QString::fromUtf8("Проект после правки"))
        return fail("Task retained stale project name after rename");
    nav->setCurrentRow(2);
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == QString::fromStdString(originalProject.id)) table->selectRow(i);
    auto* deleteEntry = window.findChild<QPushButton*>("deleteEntry");
    if (!deleteEntry || !deleteEntry->isVisible() || !deleteEntry->isEnabled()) return fail("Project delete action unavailable");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) confirm->button(QMessageBox::No)->click();
    });
    deleteEntry->click();
    if (LoadProjectsData(workspace.directory).size() != 1) return fail("Project delete cancellation failed");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) confirm->grab().save(artifacts + "/project-delete.png");
            confirm->button(QMessageBox::Yes)->click();
        }
    });
    deleteEntry->click();
    if (!LoadProjectsData(workspace.directory).empty()) return fail("Project delete did not persist");
    const auto detachedTask = LoadTasksData(workspace.directory).front();
    if (!detachedTask.projectId.empty() || !detachedTask.project.empty()) return fail("Project delete left task reference");
    if (workspace.data.taskAudit.empty() || workspace.data.taskAudit.back().field != "project") return fail("Project delete audit missing");
    nav->setCurrentRow(1);
    if (table->rowCount() != 1 || !table->item(0, 2)->text().isEmpty()) return fail("Task retained deleted project");
    saveForm(QString::fromUtf8("Создано через Qt"));
    primary->click();
    if (LoadTasksData(workspace.directory).size() != 2) return fail("Task form did not persist");
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == "qt-smoke-task") table->selectRow(i);
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        dialog->findChild<QLineEdit*>("entryTitle")->setText(QString::fromUtf8("Отредактировано через Qt"));
        dialog->findChild<QPlainTextEdit*>("entryDescription")->setPlainText(QString::fromUtf8("Новое описание"));
        dialog->findChild<QComboBox*>("taskPriority")->setCurrentIndex(2);
        dialog->findChild<QCheckBox*>("taskHasDeadline")->setChecked(true);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/task-editor.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    window.findChild<QPushButton*>("editEntry")->click();
    auto taskEdits = LoadTasksData(workspace.directory);
    auto editedTask = std::find_if(taskEdits.begin(), taskEdits.end(), [&](const auto& t) { return t.id == task.id; });
    if (editedTask == taskEdits.end() || editedTask->title != u8"Отредактировано через Qt" ||
        editedTask->priority != 2 || editedTask->deadlineAt == 0 || editedTask->createdAt != task.createdAt ||
        editedTask->assignees != task.assignees) return fail("Task editor did not preserve metadata");
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == "qt-smoke-task") table->selectRow(i);
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/task-status.png");
            dialog->accept();
        }
    });
    window.findChild<QPushButton*>("changeStatus")->click();
    const auto persisted = LoadTasksData(workspace.directory);
    auto changed = std::find_if(persisted.begin(), persisted.end(), [](const auto& t) { return t.id == "qt-smoke-task"; });
    if (changed == persisted.end() || changed->status != 1) return fail("Status form did not persist");
    QFile taskTelemetry(QString::fromUtf8((workspace.directory / "meta/qt-application-log.json").u8string()));
    if (!taskTelemetry.open(QIODevice::ReadOnly)) return fail("Task mutation telemetry file unavailable");
    const auto taskEvents = QJsonDocument::fromJson(taskTelemetry.readAll()).array();
    bool taskCreateLogged = false, taskEditLogged = false, taskStatusLogged = false;
    for (const auto& value : taskEvents) {
        const auto entry = value.toObject();
        if (entry.value("source").toString() != QStringLiteral("CoreTaskMutation")) continue;
        const auto text = entry.value("message").toString();
        taskCreateLogged |= text == QStringLiteral("Task creation committed");
        taskEditLogged |= text == QStringLiteral("Task edit committed");
        taskStatusLogged |= text == QStringLiteral("Task status update committed");
        if (text.contains(QStringLiteral("qt-smoke-task")) ||
            text.contains(QString::fromUtf8("Отредактировано через Qt")) ||
            text.contains(QString::fromStdString(createdProfile->id)))
            return fail("Core task telemetry leaked task or profile data");
    }
    if (!taskCreateLogged || !taskEditLogged || !taskStatusLogged) {
        std::cerr << "Qt task telemetry presence (create/edit/status): " << taskCreateLogged << '/'
                  << taskEditLogged << '/' << taskStatusLogged << '\n';
        return fail("Qt task create, edit, or status core telemetry is missing from retained log");
    }
    QString deletableTaskId;
    for (int i = 0; i < table->rowCount(); ++i) {
        const auto candidate = table->item(i, 0)->data(Qt::UserRole).toString();
        if (candidate != "qt-smoke-task") { deletableTaskId = candidate; table->selectRow(i); break; }
    }
    auto* deleteTask = window.findChild<QPushButton*>("deleteEntry");
    if (deletableTaskId.isEmpty() || !deleteTask || !deleteTask->isVisible() || !deleteTask->isEnabled())
        return fail("Task delete action unavailable");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) confirm->button(QMessageBox::No)->click();
    });
    deleteTask->click();
    if (LoadTasksData(workspace.directory).size() != 2) return fail("Task delete cancellation failed");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) confirm->grab().save(artifacts + "/task-delete.png");
            confirm->button(QMessageBox::Yes)->click();
        }
    });
    deleteTask->click();
    if (LoadTasksData(workspace.directory).size() != 1 ||
        std::any_of(workspace.data.tasks.begin(), workspace.data.tasks.end(), [&](const auto& item) { return QString::fromStdString(item.id) == deletableTaskId; }))
        return fail("Task delete persistence failed");
    login->trigger();
    if (primary->isVisible() || !nav->item(2)->isHidden()) return fail("Admin logout did not revoke controls");
    window.activateWindow();
    window.setFocus();
    QTest::qWait(50);
    QTest::keyClick(&window, Qt::Key_F2);
    QApplication::processEvents();
    if (nav->currentRow() != 3) return fail("F2 navigation failed");
    QTest::keyClick(&window, Qt::Key_F1);
    if (nav->currentRow() != 0) return fail("F1 navigation failed");
    if (!workspace.storage->set_active_profile(createdProfile->id)) return fail("Profile disappeared");
    auto loaded = workspace.storage->load_profile();
    if (!loaded || loaded->name() != profile.name()) return fail("Profile round trip failed");
    if (!profileFile.open(QIODevice::ReadOnly) || profileFile.readAll() != profileBytes)
        return fail("Viewing a profile modified its stored bytes");
    profileFile.close();
    // Cancellation and invalid inputs must never persist a partial task completion.
    QStringList missingTaskCompletionAccessibleNames;
    QTimer::singleShot(0, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            missingTaskCompletionAccessibleNames.append(MissingAccessibleNames(dialog));
            dialog->reject();
        }
    });
    if (ShowTaskCompletionDialog(&window, workspace, "qt-smoke-task", QString::fromStdString(createdProfile->id)))
        return fail("Cancelled completion succeeded");
    QTimer::singleShot(0, [] { SubmitAdminLoginForTest("qt-test-password"); });
    login->trigger();
    nav->setCurrentRow(1);
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == "qt-smoke-task") table->selectRow(i);
    bool dialogChecks = false;
    QTimer::singleShot(0, [&] {
        auto* select = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (!select) return;
        select->setTextValue(QString::fromUtf8("Выполнена — начислить XP"));
        select->accept();
        QTimer::singleShot(0, [&] {
            auto* dialog = QApplication::activeModalWidget();
            if (!dialog) return;
            missingTaskCompletionAccessibleNames.append(MissingAccessibleNames(dialog));
            auto* save = dialog->findChild<QPushButton*>("completeXp");
            auto* participants = dialog->findChild<QTableWidget*>("xpParticipants");
            if (!save || !participants) { std::cerr << "Unexpected XP dialog\n"; qobject_cast<QDialog*>(dialog)->reject(); return; }
            auto* share = qobject_cast<QSpinBox*>(participants->cellWidget(0, 1));
            if (!share || !save->isEnabled()) {
                std::cerr << "XP preview: " << dialog->findChild<QLabel*>("xpSummary")->text().toUtf8().constData() << '\n';
                qobject_cast<QDialog*>(dialog)->reject();
                return;
            }
            share->setValue(99);
            const bool rejectsBadTotal = !save->isEnabled();
            share->setValue(100);
            dialogChecks = rejectsBadTotal && save->isEnabled();
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                dialog->grab().save(artifacts + "/xp-dialog.png");
                dialog->resize(640, 480);
                QApplication::processEvents();
                dialog->grab().save(artifacts + "/xp-dialog-small.png");
            }
            save->click();
        });
    });
    window.findChild<QPushButton*>("changeStatus")->click();
    if (!dialogChecks) return fail("XP form validation failed");
    const auto finishedTasks = LoadTasksData(workspace.directory);
    missingTaskCompletionAccessibleNames.removeDuplicates();
    if (!missingTaskCompletionAccessibleNames.isEmpty()) {
        std::cerr << "Visible task completion controls without accessible names:\n";
        for (const auto& name : missingTaskCompletionAccessibleNames)
            std::cerr << "  " << name.toUtf8().constData() << '\n';
        return fail("Task completion dialog accessibility audit failed");
    }
    auto finished = std::find_if(finishedTasks.begin(), finishedTasks.end(), [](const auto& t) { return t.id == "qt-smoke-task"; });
    if (finished == finishedTasks.end() || finished->status != 2 || finished->participants.size() != 1)
        return fail("XP form did not complete task");
    nav->setCurrentRow(7);
    auditSourceFilter->setCurrentIndex(5);
    QApplication::processEvents();
    bool taskCompletionCoreEvent = false, taskMutationCoreEvent = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->text() != QString::fromUtf8("Core-событие")) continue;
        const auto event = table->item(row, 6)->text();
        const auto category = table->item(row, 3)->text();
        taskCompletionCoreEvent |= category == QString::fromUtf8("Завершение XP") &&
            event == QString::fromUtf8("Task XP transaction committed");
        taskMutationCoreEvent |= category == QString::fromUtf8("Изменение задач") &&
            event == QString::fromUtf8("Task status update committed");
    }
    if (!taskCompletionCoreEvent || !taskMutationCoreEvent)
        return fail("Core task mutation or XP transaction outcome missing from admin audit source");
    auditSourceFilter->setCurrentIndex(1);
    QApplication::processEvents();
    if (table->rowCount() == 0 || table->item(0, 0)->text() != QString::fromUtf8("Задача"))
        return fail("Core task XP audit source leaked into task-only audit filter");
    auditSourceFilter->setCurrentIndex(0);
    nav->setCurrentRow(1);
    workspace.storage->set_active_profile(createdProfile->id);
    const auto earned = workspace.storage->load_profile();
    if (!earned || earned->total_xp() != finished->participants[0].globalXp || earned->tasks_completed() != 1)
        return fail("XP form did not persist profile");
    if (!ProfileMatchesTaskRollbackPostcondition(finished->participants[0].rollbackSnapshot, *earned))
        return fail("XP form rollback postcondition mismatch immediately after completion");
    for (int index = 0; index < table->rowCount(); ++index)
        if (table->item(index, 0)->data(Qt::UserRole).toString() == QString::fromStdString(task.id)) table->selectRow(index);
    bool lockedXpFields = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        lockedXpFields = dialog && !dialog->findChild<QComboBox*>("taskCategory")->isEnabled() &&
            !dialog->findChild<QSpinBox*>("taskPenalty")->isEnabled() &&
            !dialog->findChild<QListWidget*>("taskAssignees")->isEnabled() &&
            !dialog->findChild<QListWidget*>("taskSkills")->isEnabled();
        if (dialog) {
            dialog->findChild<QLineEdit*>("entryTitle")->setText("Cancelled correction");
            dialog->reject();
        }
    });
    window.findChild<QPushButton*>("editEntry")->click();
    if (!lockedXpFields) return fail("Awarded task editor did not lock XP fields");
    const auto afterCancel = LoadTasksData(workspace.directory);
    const auto cancelledTask = std::find_if(afterCancel.begin(), afterCancel.end(), [&](const auto& t) { return t.id == task.id; });
    if (cancelledTask == afterCancel.end() || cancelledTask->title == "Cancelled correction")
        return fail("Cancelled editor persisted changes");
    for (int index = 0; index < table->rowCount(); ++index)
        if (table->item(index, 0)->data(Qt::UserRole).toString() == QString::fromStdString(task.id)) table->selectRow(index);
    workspace.storage->set_active_profile(createdProfile->id);
    const auto beforeUiDelete = workspace.storage->load_profile();
    if (!beforeUiDelete || !ProfileMatchesTaskRollbackPostcondition(cancelledTask->participants[0].rollbackSnapshot, *beforeUiDelete))
        return fail("Profile changed before awarded delete UI");
    QTimer::singleShot(0, [] {
        if (auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
            if (!artifacts.isEmpty()) confirm->grab().save(artifacts + "/task-delete-xp.png");
            confirm->button(QMessageBox::Yes)->click();
            QTimer::singleShot(0, [] {
                if (auto* error = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    std::cerr << "Awarded delete UI error: " << error->text().toUtf8().constData() << '\n';
                    error->accept();
                }
            });
        }
    });
    window.findChild<QPushButton*>("deleteEntry")->click();
    if (!LoadTasksData(workspace.directory).empty()) return fail("Awarded task deletion did not persist");
    workspace.storage->set_active_profile(createdProfile->id);
    const auto rolledBackProfile = workspace.storage->load_profile();
    if (!rolledBackProfile || rolledBackProfile->total_xp() != 0 || rolledBackProfile->tasks_completed() != 0)
        return fail("Awarded task profile rollback failed");
    nav->setCurrentRow(0);
    bool openedManager = false;
    QTimer::singleShot(0, [&] {
        auto* manager = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        openedManager = manager && manager->objectName() == "profileManager";
        if (manager) manager->reject();
    });
    primary->click();
    if (!openedManager) return fail("Admin profile manager navigation failed");
    const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        window.resize(800, 520);
        QApplication::processEvents();
        window.grab().save(artifacts + "/profile-small.png");
    }
    QApplication::processEvents();
    if (!g_dialogAccessibilityFailures.isEmpty()) {
        std::cerr << "Dialog accessible-name audit failed:\n";
        for (const auto& failure : g_dialogAccessibilityFailures)
            std::cerr << "  " << failure.toUtf8().constData() << '\n';
        return 1;
    }
    std::cout << "smoke_qt: OK; accessible dialogs audited: " << g_dialogAccessibilityAudits << '\n';
    return 0;
}
