#include "QtWindow.h"
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
#include "QtDisplaySettings.h"
#include "QtReportExport.h"
#include "QtPipelineEditor.h"
#include "QtPipelineTransition.h"
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
    workspace.data.vault = LoadStorageVault(workspace.directory);
    QtWindow window(workspace); window.show(); QApplication::processEvents();
    auto* remove = window.findChild<QPushButton*>("removeEvilSpirit");
    auto* access = window.findChild<QAction*>("profileAccess");
    if (!remove || !access || remove->isVisible()) return false;
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
    if (!remove->isVisible() || !remove->isEnabled()) return false;
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
    return changed && changed->spirit() == ProfileSpirit::None && changed->wallet_balance() == 51.0 &&
        vault.balance == 200.0 && !vault.log.empty() && vault.log.back().action == "spirit_cleanup" && !remove->isEnabled();
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
    auto settings = LoadQtDisplaySettings(directory); bool saved = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget(); auto* scale = dialog->findChild<QComboBox*>("qtScale");
        scale->setCurrentIndex(scale->findData(125)); dialog->findChild<QCheckBox*>("qtCompactRows")->setChecked(true);
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS"); if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/display-settings.png");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); saved = true;
    });
    if (!ShowQtDisplaySettings(nullptr, directory, settings) || !saved || settings.scalePercent != 125 || !settings.compactRows) return false;
    QFile file(temp.path() + "/meta/ui.ini"); if (!file.open(QIODevice::ReadOnly)) return false; const auto before = file.readAll(); file.close();
    if (!before.contains("unknown=kept") || !before.contains("[qt]") || !before.contains("scalePercent=125") || !before.startsWith("\xEF\xBB\xBF")) return false;
    const auto loaded = LoadQtDisplaySettings(directory); if (loaded.scalePercent != 125 || !loaded.compactRows) return false;
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
    if (!TestProfileDeletionRecovery()) { std::cerr << "Profile deletion recovery failed\n"; return 1; }
    if (!TestPersonalWallet()) { std::cerr << "Personal wallet failed\n"; return 1; }
    if (!TestPomodoro()) { std::cerr << "Pomodoro failed\n"; return 1; }
    if (!TestRulesEditor()) { std::cerr << "Rules editor failed\n"; return 1; }
    if (!TestDisplaySettings(app)) { std::cerr << "Display settings failed\n"; return 1; }
    if (!TestShortcutPersistence()) { std::cerr << "Shortcut persistence failed\n"; return 1; }
    if (!TestReportExport()) { std::cerr << "Report export failed\n"; return 1; }
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
        shortcutHelpChecked = dialog && dialog->objectName() == "shortcutHelp" && helpTable && helpTable->rowCount() == 12 &&
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
    if (nav->item(13)->isHidden() || !primary->isVisible() || primary->text() != QString::fromUtf8("Настроить облако") ||
        !window.findChild<QLabel*>("summary")->text().contains(QString::fromUtf8("без передачи данных"))) return fail("Cloud readiness page unavailable");
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
    for (int i = 0; i < table->rowCount(); ++i) profileAuditVisible |= table->item(i, 0)->text() == QString::fromUtf8("Профиль");
    if (!profileAuditVisible) return fail("Profile audit is not visible");
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
