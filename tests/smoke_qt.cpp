#include "QtWindow.h"
#include "QtReportChart.h"
#include "QtLogActivityChart.h"
#include "AppTaskProjectService.h"
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
#include "QtCloudConflict.h"
#include "QtStorageConflict.h"
#include "QtDisplaySettings.h"
#include "QtReportExport.h"
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
#include <QtTest/QTest>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// File-backed failure injection keeps the test on the real persistence path.
class FailingProfileStorage : public IJobStorage {
public:
    explicit FailingProfileStorage(IJobStorage& delegate) : delegate(delegate) {}
    IJobStorage& delegate;
    int failAt = 0;
    int writes = 0;
    bool set_active_profile(const std::string& id) override { return delegate.set_active_profile(id); }
    std::vector<ProfileInfo> list_profiles() override { return delegate.list_profiles(); }
    std::optional<Profile> load_profile() override { return delegate.load_profile(); }
    bool save_profile(const Profile& profile) override { return ++writes != failAt && delegate.save_profile(profile); }
    std::optional<ProfileInfo> create_profile(const Profile& p) override { return delegate.create_profile(p); }
    bool set_archived(const std::string& id, bool value) override { return delegate.set_archived(id, value); }
    bool delete_profile(const std::string& id) override { return delegate.delete_profile(id); }
    std::optional<std::string> load_token() override { return delegate.load_token(); }
    bool save_token(const std::string& token) override { return delegate.save_token(token); }
    std::vector<XpEvent> load_queue() override { return delegate.load_queue(); }
    bool save_queue(const std::vector<XpEvent>& events) override { return delegate.save_queue(events); }
};

static bool TestTaskCompletion() {
    auto fail = [](const char* text) { std::cerr << "taskCompletion: " << text << '\n'; return false; };
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
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
    const auto completion = CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input);
    if (!completion.ok) { std::cerr << completion.errorMessage << '\n'; return fail("completion failed"); }
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
    if (staleDelete.ok || workspace.data.tasks.empty() || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("later progress did not block awarded delete");
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
    if (!CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input).ok || workspace.data.tasks.back().participants[0].globalXp != 0)
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
    return true;
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
    GameplayConfig changed = GetGameplayConfig();
    changed.levelBaseXp = 300; changed.levelLinearXp = 20; changed.levelQuadraticXp = 2;
    SetGameplayConfig(changed);
    failing.failAt = 2;
    const auto rejected = ReapplyRulesWithRecovery(context, active->id);
    if (rejected.ok || read(activePath) != activeBefore || read(archivedPath) != archivedBefore ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("partial-write rollback");

    PrepareRulesReapplyRecovery(workspace.directory, {{active->id, false}, {archived->id, true}});
    { std::ofstream corrupt(workspace.directory / activePath, std::ios::binary | std::ios::trunc); corrupt << "interrupted"; }
    workspace.reload();
    SetGameplayConfig(changed);
    if (read(activePath) != activeBefore || read(archivedPath) != archivedBefore) return fail("restart recovery");

    failing.writes = 0; failing.failAt = 0;
    const auto applied = ReapplyRulesWithRecovery(context, active->id);
    if (!applied.ok || applied.affectedProfiles != 2 || std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) {
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
    const auto beforeProfile = read(profilePath), beforeAchievements = read(achievementPath);
    FailingProfileStorage failing(*workspace.storage);
    AppContext context{workspace.directory, failing, workspace.catalog};
    failing.failAt = 1;
    const auto rejected = GrantDirectSkillXpWithRecovery(context, created->id, created->id, skillId, 100, 1000);
    if (rejected.ok || read(profilePath) != beforeProfile || read(achievementPath) != beforeAchievements ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("write rollback");

    PrepareDirectXpRecovery(workspace.directory, created->id);
    { std::ofstream changed(workspace.directory / profilePath, std::ios::binary | std::ios::trunc); changed << "interrupted"; }
    { std::ofstream changed(workspace.directory / achievementPath, std::ios::binary | std::ios::trunc); changed << "[]"; }
    workspace.reload();
    if (read(profilePath) != beforeProfile || read(achievementPath) != beforeAchievements) return fail("restart recovery");

    failing.writes = 0; failing.failAt = 0;
    const auto applied = GrantDirectSkillXpWithRecovery(context, created->id, created->id, skillId, 100, 1000);
    if (!applied.ok || applied.awardedGlobalXp != 100 || applied.awardedSkillXp != 150 ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("commit");
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
    QJsonArray entries{QJsonObject{{"path", "meta/tasks.json"}, {"existed", true},
        {"hash", QString::fromLatin1(QCryptographicHash::hash(before, QCryptographicHash::Sha256).toHex())}},
        QJsonObject{{"path", "meta/banner.json"}, {"existed", false}}};
    const auto journal = workspace / "meta/qt-cloud-pull.json";
    auto journalBytes = QJsonDocument(QJsonObject{{"version", 1},
        {"backup", QString::fromStdWString(result.backupPath.filename().wstring())}, {"files", entries}}).toJson();
    if (!write(journal, journalBytes)) return false;
    { QtWorkspace recovered(workspace); }
    if (bytes(workspace / "meta/tasks.json") != before || std::filesystem::exists(journal) ||
        std::filesystem::exists(workspace / "meta/banner.json")) return false;
    entries.append(QJsonObject{{"path", "../outside.ini"}, {"existed", false}});
    journalBytes = QJsonDocument(QJsonObject{{"version", 1},
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
    window.close();
    CloudSyncConfig disabled = config; disabled.enabled = false;
    return !RunQtCloudPullTransaction(disabled, workspace, CloudRole::Viewer).sync.ok;
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
    Profile original("Original");
    original.set_total_xp(777);
    original.set_wallet_balance(42);
    original.set_login("original-login");
    original.set_password_encoded(EncodePassword("original-password"));
    original.add_skill("test-skill");
    auto created = workspace.storage->create_profile(original);
    if (!created) return false;
    const QString id = QString::fromStdString(created->id);
    workspace.data.professions.push_back({"artist", "Artist", "3D"});
    auto delegate = std::move(workspace.storage);
    auto wrapper = std::make_unique<FailingProfileStorage>(*delegate);
    auto* failures = wrapper.get();
    workspace.storage = std::move(wrapper);
    bool checks = true;
    QString disposableId;
    QTimer::singleShot(0, [&] {
        auto* manager = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!manager) { checks = false; return; }
        auto* table = manager->findChild<QTableWidget*>("profileRecords");
        QTimer::singleShot(0, [] {
            if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                input->setTextValue(QString::fromUtf8("Новый профиль")); input->accept();
            }
        });
        manager->findChild<QPushButton*>("createProfile")->click();
        checks &= delegate->list_profiles().size() == 2;
        for (const auto& p : delegate->list_profiles()) if (p.id != created->id) disposableId = QString::fromStdString(p.id);
        auto* credentials = manager->findChild<QLineEdit*>("createdProfileCredentials");
        checks &= !credentials->text().isEmpty() && credentials->echoMode() == QLineEdit::Password;
        auto select = [&] {
            for (int r = 0; r < table->rowCount(); ++r) if (table->item(r, 0)->data(Qt::UserRole).toString() == id) table->selectRow(r);
        };
        select();
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!editor) { checks = false; return; }
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
        auto* archive = manager->findChild<QPushButton*>("archiveProfile");
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
            password->findChild<QLineEdit*>("newPassword")->setText("reset-password");
            password->findChild<QLineEdit*>("confirmPassword")->setText("different");
            auto* save = password->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            save->click();
            checks &= !password->findChild<QLabel*>("profileNotice")->text().isEmpty();
            password->findChild<QLineEdit*>("confirmPassword")->setText("reset-password");
            save->click();
        });
        manager->findChild<QPushButton*>("resetProfilePassword")->click();
        delegate->set_active_profile(created->id);
        checks &= DecodePassword(delegate->load_profile()->password_encoded()) == "reset-password";
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
        QTimer::singleShot(0, [] { if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        permanentDelete->click();
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
        dialog->findChild<QLineEdit*>("currentPassword")->setText("wrong-password");
        dialog->findChild<QLineEdit*>("newPassword")->setText("my-password");
        dialog->findChild<QLineEdit*>("confirmPassword")->setText("my-password");
        auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        save->click();
        checks &= DecodePassword(delegate->load_profile()->password_encoded()) == "reset-password";
        dialog->findChild<QLineEdit*>("currentPassword")->setText("reset-password");
        save->click();
    });
    checks &= ShowProfilePasswordDialog(nullptr, workspace, id, id, false);
    delegate->set_active_profile(created->id);
    checks &= DecodePassword(delegate->load_profile()->password_encoded()) == "my-password";
    if (!checks) std::cerr << "profile dialog lifecycle failed\n";
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

static bool TestProfileSession() {
    QTemporaryDir temp;
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toStdString()));
    Profile profile("Session profile");
    profile.set_password_encoded(EncodePassword("secret"));
    const auto created = workspace.storage->create_profile(profile);
    if (!created) return false;
    QtProfileSession session(workspace.directory);
    if (session.unlock(*workspace.storage, created->id, "wrong") ||
        !session.unlock(*workspace.storage, created->id, "secret") ||
        !session.isUnlocked(*workspace.storage, created->id)) return false;
    QtProfileSession fresh(workspace.directory);
    if (fresh.isUnlocked(*workspace.storage, created->id)) return false;
    if (!session.unlock(*workspace.storage, created->id, "secret", 30) || !session.isTrusted() || session.trustedUntil() <= QDateTime::currentSecsSinceEpoch()) return false;
    QFile ui(temp.path() + "/meta/ui.ini"); if (!ui.open(QIODevice::ReadOnly)) return false;
    const auto trustBytes = ui.readAll(); ui.close();
    if (!trustBytes.contains("trusted=" + QByteArray::fromStdString(created->id) + ":")) return false;
    QtProfileSession restored(workspace.directory);
    if (!restored.isUnlocked(*workspace.storage, created->id) || !restored.isTrusted() || !restored.lock(true)) return false;
    QtProfileSession forgotten(workspace.directory);
    if (forgotten.isUnlocked(*workspace.storage, created->id)) return false;
    if (!ui.open(QIODevice::ReadWrite)) return false;
    auto expiredBytes = ui.readAll();
    expiredBytes.replace("trusted=", "trusted=" + QByteArray::fromStdString(created->id) + ":1");
    if (!ui.resize(0) || !ui.seek(0) || ui.write(expiredBytes) != expiredBytes.size()) return false;
    ui.close();
    QtProfileSession expired(workspace.directory);
    if (expired.isUnlocked(*workspace.storage, created->id)) return false;
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
    QFile audit(temp.path() + "/meta/profile-audit.log"); if (!audit.open(QIODevice::ReadOnly)) return false;
    const auto auditBytes = audit.readAll();
    if (!auditBytes.contains("|unlock|trust_days=30") || !auditBytes.contains("|trusted_unlock") || !auditBytes.contains("|lock")) return false;
    if (session.isUnlocked(*workspace.storage, "other") || session.isUnlocked(*workspace.storage, created->id)) return false;
    if (!session.unlock(*workspace.storage, created->id, "secret")) return false;
    profile.set_password_encoded(EncodePassword("changed"));
    if (!workspace.storage->save_profile(profile) || session.isUnlocked(*workspace.storage, created->id)) return false;
    if (!session.unlock(*workspace.storage, created->id, "changed")) return false;
    profile.set_blocked(true);
    if (!workspace.storage->save_profile(profile) || session.isUnlocked(*workspace.storage, created->id) ||
        session.unlock(*workspace.storage, created->id, "changed")) return false;
    profile.set_blocked(false);
    if (!workspace.storage->save_profile(profile) || !session.unlock(*workspace.storage, created->id, "changed")) return false;
    if (!workspace.storage->set_archived(created->id, true) || session.isUnlocked(*workspace.storage, created->id) ||
        session.unlock(*workspace.storage, created->id, "changed")) return false;
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
        workspace.data.taskAudit, createCandidate, "test");
    AppSetTaskAuditFailureHookForTests(false);
    if (failedCreate.ok || workspace.data.tasks.size() != 1 ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) return false;
    AppSetTaskAuditFailureHookForTests(true);
    const auto failedStatus = UpdateTaskStatusWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, original.id, 1, "test");
    AppSetTaskAuditFailureHookForTests(false);
    if (failedStatus.ok || workspace.data.tasks.front().status != 0 ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return false;
    for (size_t i = 0; i < files.size(); ++i) if (read(files[i]) != before[i]) return false;
    AppSetTaskAuditFailureHookForTests(true);
    const auto failedDelete = DeleteTaskWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, original.id, "test");
    AppSetTaskAuditFailureHookForTests(false);
    if (failedDelete.ok || workspace.data.tasks.size() != 1 || workspace.data.tasks.front().id != original.id ||
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
    auto edit = [&] { return EditTaskDetails(workspace.directory, workspace.data.tasks, workspace.data.taskAudit, draft, "test"); };
    AppSetRecoveryPrimaryWriteFailureForTests(true);
    const auto failedWrite = edit();
    AppSetRecoveryPrimaryWriteFailureForTests(false);
    if (failedWrite.ok) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
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
    // Simulate interrupted metadata editing and exercise the startup recovery format.
    const auto journal = workspace.directory / "meta/qt-xp-transaction";
    std::filesystem::create_directories(journal);
    {
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
    auto& awarded = workspace.data.tasks.front();
    awarded.participants.push_back({"legacy-profile", 100, 77, 22, "snapshot"});
    awarded.status = 2;
    awarded.score = 9;
    if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) { std::cerr << "Task edit failure at " << __LINE__ << "\n"; return false; }
    const auto blockedDelete = DeleteTaskWithRecovery(workspace.directory, workspace.data.tasks,
        workspace.data.taskAudit, awarded.id, "test");
    if (blockedDelete.ok || workspace.data.tasks.size() != 1 ||
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
    return disk.status == 2 && disk.score == 9 && disk.participants.size() == 1 &&
        disk.participants.front().rollbackSnapshot == "snapshot" && disk.participants.front().globalXp == 77;
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
    workspace.reload();

    auto bytes = [&](const QString& relative) {
        QFile file(temp.path() + "/" + relative);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };
    const auto professionBytes = bytes("meta/professions.txt");
    const auto skillBytes = bytes("skills.txt");
    const auto profileBytes = bytes(QString::fromStdString(created->id) + ".ini");
    const std::vector<std::string> profileIds = {created->id};

    PrepareProfessionDeletionRecovery(workspace.directory, profileIds);
    const auto interrupted = AppDeleteProfessionEntry(workspace.directory, workspace.data.professions, *workspace.storage,
        workspace.profiles, workspace.catalog, created->id, "artist");
    if (!interrupted.ok || !std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return fail("interrupted transaction fixture");
    workspace.reload();
    if (bytes("meta/professions.txt") != professionBytes || bytes("skills.txt") != skillBytes ||
        bytes(QString::fromStdString(created->id) + ".ini") != profileBytes ||
        workspace.data.professions.size() != 1 || !workspace.catalog.has_profession(*skillId, "artist"))
        return fail("restart recovery");

    PrepareProfessionDeletionRecovery(workspace.directory, profileIds);
    const auto removed = AppDeleteProfessionEntry(workspace.directory, workspace.data.professions, *workspace.storage,
        workspace.profiles, workspace.catalog, created->id, "artist");
    if (!removed.ok || removed.affectedProfiles != 1 || removed.affectedSkills != 1) return fail("delete result");
    CommitQtRecoveryTransaction(workspace.directory);
    workspace.reload();
    if (!workspace.data.professions.empty() || workspace.catalog.has_profession(*skillId, "artist") ||
        std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return fail("commit state");
    if (!workspace.storage->set_active_profile(created->id)) return fail("profile reload");
    const auto cleared = workspace.storage->load_profile();
    return cleared && cleared->profession_id().empty();
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
        !AppendProfileAudit(workspace.directory, created->id, "test_profile_event", "profile history marker")) return false;
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* table = dialog ? dialog->findChild<QTableWidget*>("profileActivityHistoryTable") : nullptr;
        if (!table || table->rowCount() < 6 || table->columnCount() != 3) return;
        bool foundWallet = false, foundPassword = false, foundUnknown = false, foundSpirit = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            foundWallet |= table->item(row, 1)->text().contains(QString::fromUtf8("Изменение кошелька")) &&
                table->item(row, 2)->text().contains(QString::fromUtf8("проверка истории"));
            foundPassword |= table->item(row, 1)->text() == QString::fromUtf8("Сброс пароля");
            foundUnknown |= table->item(row, 1)->text() == "test_profile_event" &&
                table->item(row, 2)->text() == "profile history marker";
            foundSpirit |= table->item(row, 1)->text().contains(QString::fromUtf8("Злого духа"));
        }
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/profile-activity-history.png"); }
        auto* tabs = dialog ? dialog->findChild<QTabWidget*>("profileHistoryTabs") : nullptr;
        auto* taskTable = dialog ? dialog->findChild<QTableWidget*>("profileTaskXpHistoryTable") : nullptr;
        auto* taskFilter = dialog ? dialog->findChild<QLineEdit*>("profileTaskXpHistoryFilter") : nullptr;
        auto* taskSummary = dialog ? dialog->findChild<QLabel*>("profileTaskXpHistorySummary") : nullptr;
        bool foundAwarded = false, foundPending = false;
        if (tabs && taskTable && taskFilter && taskSummary) {
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
            taskFilter->clear();
            foundAwarded = foundAwarded && filters && taskTable->rowCount() == 2;
            foundAwarded = foundAwarded && taskSummary->text().contains("17") && taskSummary->text().contains("8");
        }
        if (foundWallet && foundPassword && foundUnknown && foundSpirit && foundAwarded && foundPending) dialog->accept();
    });
    profileHistory->click();
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
    TaskEntry completed;
    completed.id = "deadline-completed";
    completed.title = "Already done";
    completed.deadlineAt = now + 60;
    completed.status = 2;
    workspace.data.tasks = {upcoming, overdue, completed};
    if (!AppSaveTasks(workspace.directory, workspace.data.tasks)) return false;
    QtWindow window(workspace);
    window.show();
    QApplication::processEvents();
    auto* timer = window.findChild<QTimer*>("deadlineReminderTimer");
    if (!timer || !QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) return false;
    const auto first = window.statusBar()->currentMessage();
    if (!first.contains(QString::fromUtf8("Проверить сборку")) || !first.contains(QString::fromUtf8("Срок задачи"))) return false;
    window.statusBar()->clearMessage();
    if (!QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) return false;
    if (!window.statusBar()->currentMessage().isEmpty()) return false;
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
    return true;
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
            if (action->text() == QString::fromUtf8("Вход / выход администратора")) adminAction = action;
    if (!adminAction) return false;
    QTimer::singleShot(0, [] {
        auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (input) { input->setTextValue(QString::fromUtf8("admin123")); input->accept(); }
    });
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
    return completedStatusUnavailable;
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
    return phase->text() == QString::fromUtf8("Фокус") && time->text() == "30:00" && !pause->isVisible();
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
        if (!base || !repeat || !buttons) { if (auto* modal = qobject_cast<QDialog*>(dialog)) modal->reject(); return; }
        base->setValue(2345); repeat->setValue(0.55);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); dialog->grab().save(artifacts + "/rules-editor.png"); }
        buttons->button(QDialogButtonBox::Save)->click(); saved = true;
    });
    if (!ShowRulesEditor(nullptr, workspace) || !saved) return false;
    const auto loaded = LoadGameplayConfig(workspace.directory);
    if (loaded.levelBaseXp != 2345 || std::abs(loaded.repeatRewardFactor - 0.55f) > 0.0001f || GetGameplayConfig().levelBaseXp != 2345) return false;
    QFile file(temp.path() + "/meta/gameplay.ini"); if (!file.open(QIODevice::ReadOnly)) return false;
    const auto before = file.readAll(); file.close(); if (!before.startsWith("\xEF\xBB\xBF")) return false;
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
    QTemporaryDir temp; if (!temp.isValid()) return false;
    QDir().mkpath(temp.path() + "/meta"); QFile seed(temp.path() + "/meta/ui.ini");
    if (!seed.open(QIODevice::WriteOnly) || seed.write("\xEF\xBB\xBF[other]\nunknown=kept\n") < 0) return false; seed.close();
    const auto directory = std::filesystem::u8path(temp.path().toUtf8().constData());
    auto settings = LoadQtDisplaySettings(directory); settings.auditSourceFilter = 2;
    settings.logAutoScroll = false; settings.logCompactView = true; bool saved = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget(); auto* scale = dialog->findChild<QComboBox*>("qtScale");
        scale->setCurrentIndex(scale->findData(125)); dialog->findChild<QCheckBox*>("qtCompactRows")->setChecked(true);
        auto* tray = dialog->findChild<QCheckBox*>("qtMinimizeToTray");
        const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
        if (!tray || tray->isEnabled() != trayAvailable) { qobject_cast<QDialog*>(dialog)->reject(); return; }
        tray->setChecked(trayAvailable);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS"); if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/display-settings.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); saved = true;
    });
    if (!ShowQtDisplaySettings(nullptr, directory, settings) || !saved || settings.scalePercent != 125 || !settings.compactRows ||
        settings.minimizeToTray != (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages())) return false;
    QFile file(temp.path() + "/meta/ui.ini"); if (!file.open(QIODevice::ReadOnly)) return false; const auto before = file.readAll(); file.close();
    if (!before.contains("unknown=kept") || !before.contains("[qt]") || !before.contains("scalePercent=125") || !before.startsWith("\xEF\xBB\xBF")) return false;
    const auto loaded = LoadQtDisplaySettings(directory); if (loaded.scalePercent != 125 || !loaded.compactRows ||
        loaded.auditSourceFilter != 2 || loaded.logAutoScroll || !loaded.logCompactView || loaded.minimizeToTray !=
            (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages())) return false;
    ApplyQtDisplaySettings(app, loaded); if (app.font().pointSizeF() <= app.property("forgeBasePointSize").toDouble()) return false;
    ApplyQtDisplaySettings(app, QtDisplaySettings{});
#ifdef _WIN32
    const auto path = (directory / "meta/ui.ini").wstring(); const auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false; settings.scalePercent = 90; const bool blocked = SaveQtDisplaySettings(directory, settings); CloseHandle(lock);
    if (blocked || !file.open(QIODevice::ReadOnly)) return false; const auto after = file.readAll(); file.close(); if (after != before) return false;
#endif
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
    const std::vector<AppLogEntry> identicalTimes{
        {500, AppLogLevel::Info, "Qt", "one"},
        {500, AppLogLevel::Info, "Qt", "two"},
        {500, AppLogLevel::Info, "Qt", "three"}
    };
    const auto fallback = QtLogActivityChart::BuildHistogram(identicalTimes);
    if (fallback[0] != 1 || fallback[5] != 1 || fallback[10] != 1 ||
        std::accumulate(fallback.begin(), fallback.end(), 0) != int(identicalTimes.size())) return false;
    const std::vector<AppLogEntry> missingTimes{
        {100, AppLogLevel::Info, "Qt", "first"},
        {0, AppLogLevel::Warning, "Qt", "unknown time"},
        {200, AppLogLevel::Error, "Qt", "last"}
    };
    const auto withMissingTime = QtLogActivityChart::BuildHistogram(missingTimes);
    return std::accumulate(withMissingTime.begin(), withMissingTime.end(), 0) == int(missingTimes.size());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ApplyQtTheme(app);
    qunsetenv("FORGEMIRROR_ADMIN_PASSWORD");
    qunsetenv("FORGEMIRROR_DISABLE_MODULES");
    if (!TestTaskCompletion()) return 1;
    if (!TestRulesReapplyRecovery()) return 1;
    if (!TestDirectXpRecovery()) return 1;
    if (!TestVaultEditor()) { std::cerr << "Vault editor failed\n"; return 1; }
    if (!TestBannerEditor()) { std::cerr << "Banner editor failed\n"; return 1; }
    if (!TestCloudSettings()) { std::cerr << "Cloud settings failed\n"; return 1; }
    if (!TestCloudPullTransaction()) { std::cerr << "Cloud pull transaction failed\n"; return 1; }
    if (!TestCloudConflictResolver()) { std::cerr << "Cloud conflict resolver failed\n"; return 1; }
    if (!TestStorageConflictResolver()) { std::cerr << "Storage conflict resolver failed\n"; return 1; }
    if (!TestAchievements()) { std::cerr << "Achievements failed\n"; return 1; }
    if (!TestProfileSession()) { std::cerr << "Profile session failed\n"; return 1; }
    if (!TestPipelineTransition()) { std::cerr << "Pipeline transition failed\n"; return 1; }
    if (!TestPipelineEditor()) { std::cerr << "Pipeline editor failed\n"; return 1; }
    if (!TestTaskEditorTransaction()) { std::cerr << "Task editor transaction failed\n"; return 1; }
    if (!TestProjectDeletionRecovery()) { std::cerr << "Project deletion recovery failed\n"; return 1; }
    if (!TestProfileDialogs()) return 1;
    if (!TestSkillEditor()) { std::cerr << "Skill editor failed\n"; return 1; }
    if (!TestProfessionEditor()) { std::cerr << "Profession editor failed\n"; return 1; }
    if (!TestProfessionDeletionRecovery()) { std::cerr << "Profession deletion recovery failed\n"; return 1; }
    if (!TestSkillDeletionRecovery()) { std::cerr << "Skill deletion recovery failed\n"; return 1; }
    if (!TestSkillMergeRecovery()) { std::cerr << "Skill merge recovery failed\n"; return 1; }
    if (!TestProfileDeletionRecovery()) { std::cerr << "Profile deletion recovery failed\n"; return 1; }
    if (!TestPersonalWallet()) { std::cerr << "Personal wallet failed\n"; return 1; }
    if (!TestPomodoro()) { std::cerr << "Pomodoro failed\n"; return 1; }
    if (!TestRulesEditor()) { std::cerr << "Rules editor failed\n"; return 1; }
    if (!TestDisplaySettings(app)) { std::cerr << "Display settings failed\n"; return 1; }
    if (!TestShortcutPersistence()) { std::cerr << "Shortcut persistence failed\n"; return 1; }
    if (!TestReportExport()) { std::cerr << "Report export failed\n"; return 1; }
    if (!TestMonthlyCompletionTrend()) { std::cerr << "Monthly completion trend failed\n"; return 1; }
    if (!TestPipelineMap()) { std::cerr << "Pipeline map failed\n"; return 1; }
    if (!TestDeadlineReminders()) { std::cerr << "Deadline reminders failed\n"; return 1; }
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
    workspace.catalog.add_skill(u8"Моделирование", 1.0, u8"Создание геометрии");
    workspace.catalog.add_skill(u8"Текстурирование", 1.0, u8"Подготовка материалов");
    workspace.catalog.add_skill(u8"Анимация", 1.0, u8"Движение персонажа");
    profile.add_skill(*workspace.catalog.id_for_name(u8"Моделирование"));
    auto createdProfile = workspace.storage->create_profile(profile);
    if (!createdProfile) return fail("Profile creation failed");
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
    auto* displayAction = window.findChild<QAction*>("qtDisplaySettingsAction");
    if (!displayAction) return fail("Display settings action missing");
    auto* shortcutHelpAction = window.findChild<QAction*>("shortcutHelpAction");
    const std::vector<std::pair<const char*, QKeySequence>> shortcuts = {
        {"shortcutCreate", QKeySequence::New}, {"shortcutEdit", QKeySequence("Ctrl+E")},
        {"shortcutDelete", QKeySequence::Delete}, {"shortcutRefresh", QKeySequence::Refresh},
        {"shortcutDetails", QKeySequence("Ctrl+I")}, {"shortcutHelp", QKeySequence("Ctrl+/")}
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
        shortcutHelpChecked = dialog && dialog->objectName() == "shortcutHelp" && helpTable && helpTable->rowCount() == 13 &&
            helpTable->item(6, 0)->text() == "Ctrl+N" && helpTable->item(11, 0)->text() == "Ctrl+/";
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
    nav->setCurrentRow(13);
    auto* cloudPull = window.findChild<QPushButton*>("cloudPull");
    auto* storageResolve = window.findChild<QPushButton*>("storageResolve");
    if (nav->item(13)->isHidden() || !primary->isVisible() || primary->text() != QString::fromUtf8("Настроить облако") ||
        !cloudPull || !cloudPull->isVisible() || cloudPull->isEnabled() || !storageResolve || !storageResolve->isVisible() || storageResolve->isEnabled() ||
        !window.findChild<QLabel*>("summary")->text().contains(QString::fromUtf8("Ручные pull"))) return fail("Cloud guarded pull page unavailable");
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
        if (action->text().contains(QString::fromUtf8("Вход / выход"))) login = action;
    if (!login) return fail("Admin action missing");
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            dialog->setTextValue("qt-test-password");
            dialog->accept();
        }
    });
    login->trigger();
    if (!primary->isVisible()) return fail("Admin login failed");
    nav->setCurrentRow(0);
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
    if (!reportView || reportView->count() != 2) return fail("Report view selector unavailable");
    reportView->setCurrentIndex(1);
    bool assigneeVisible = table->columnCount() == 8;
    for (int i = 0; i < table->rowCount(); ++i)
        assigneeVisible = assigneeVisible && table->item(i, 0)->text() == QString::fromUtf8("Тестовый профиль") &&
            table->item(i, 1)->text() == QString::fromStdString(createdProfile->id);
    if (!assigneeVisible || table->rowCount() != 1) return fail("Assignee report view failed");
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
    const auto rulesArtifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (!rulesArtifacts.isEmpty()) window.grab().save(rulesArtifacts + "/rules-page.png");
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
    if (!auditSourceFilter || !auditSourceFilter->isVisible() || auditSourceFilter->count() != 3 || auditSourceFilter->currentIndex() != 0)
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
    auto* logSummary = window.findChild<QLabel*>("summary");
    QFile appLogFile(temp.path() + "/meta/qt-application-log.json");
    if (!logSummary || !appLogFile.open(QIODevice::ReadOnly)) return fail("Qt application log summary unavailable");
    auto* logActivityChartWidget = window.findChild<QWidget*>("logActivityChart");
    auto* logActivityChart = static_cast<QtLogActivityChart*>(logActivityChartWidget);
    if (!logActivityChart || !logActivityChart->isVisible() ||
        !logActivityChart->accessibleDescription().contains(QString::fromUtf8("по 16 временным интервалам")))
        return fail("Qt application log activity chart unavailable");
    const auto appLogBytes = appLogFile.readAll();
    appLogFile.close();
    const auto appLogEntries = QJsonDocument::fromJson(appLogBytes).array();
    if (std::accumulate(logActivityChart->values().begin(), logActivityChart->values().end(), 0) != appLogEntries.size())
        return fail("Qt application log activity chart omitted entries or followed active filters");
    std::array<int, 3> appLogCounts{};
    for (const auto& value : appLogEntries) {
        const int level = value.toObject().value("level").toInt(-1);
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
    logSourceFilter->setCurrentIndex(0);
    auto* logAutoScroll = window.findChild<QCheckBox*>("logAutoScroll");
    auto* logCompactView = window.findChild<QCheckBox*>("logCompactView");
    if (!logAutoScroll || !logCompactView || !logAutoScroll->isVisible() || !logCompactView->isVisible())
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
    if (!exportMatchesTable)
        return fail("Qt application log export did not match visible search results or encoding");
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
    if (!restartedAutoScroll || !restartedCompactView || restartedAutoScroll->isChecked() ||
        !restartedCompactView->isChecked() || !restartedTable->isColumnHidden(1) || !restartedTable->isColumnHidden(3))
        return fail("Qt log display preferences did not persist across a window restart");
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
    if (table->item(0, 5)->text() != "Updated stage" || LoadTasksData(workspace.directory).front().pipelineStepId != stage.id)
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
        table->rowCount() != 1 || table->item(0, 1)->text() != QString::fromUtf8("Проект Qt"))
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
    if (table->rowCount() != 1 || table->item(0, 1)->text() != QString::fromUtf8("Проект после правки"))
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
    if (table->rowCount() != 1 || !table->item(0, 1)->text().isEmpty()) return fail("Task retained deleted project");
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
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    if (ShowTaskCompletionDialog(&window, workspace, "qt-smoke-task", QString::fromStdString(createdProfile->id)))
        return fail("Cancelled completion succeeded");
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            dialog->setTextValue("qt-test-password");
            dialog->accept();
        }
    });
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
    auto finished = std::find_if(finishedTasks.begin(), finishedTasks.end(), [](const auto& t) { return t.id == "qt-smoke-task"; });
    if (finished == finishedTasks.end() || finished->status != 2 || finished->participants.size() != 1)
        return fail("XP form did not complete task");
    workspace.storage->set_active_profile(createdProfile->id);
    const auto earned = workspace.storage->load_profile();
    if (!earned || earned->total_xp() != finished->participants[0].globalXp || earned->tasks_completed() != 1)
        return fail("XP form did not persist profile");
    if (!ProfileMatchesTaskRollbackPostcondition(finished->participants[0].rollbackSnapshot, *earned))
        return fail("XP form rollback postcondition mismatch immediately after completion");
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
    std::cout << "smoke_qt: OK\n";
    return 0;
}
