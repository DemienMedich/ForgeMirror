#include "QtWindow.h"
#include "AppTaskProjectService.h"
#include "AppTaskCompletionService.h"
#include "AppRecoveryStorage.h"
#include "QtTaskCompletionDialog.h"
#include "QtTheme.h"
#include "QtProfileDialogs.h"
#include "QtSkillEditor.h"
#include <QtWidgets>
#include <QtTest/QTest>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <iomanip>

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
    if (CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input).ok) return fail("duplicate XP accepted");
    Profile restored = *after;
    if (!ApplyProfileTaskRollbackSnapshot(workspace.data.tasks[0].participants[0].rollbackSnapshot, restored) || restored.total_xp() != 0 || restored.tasks_completed() != 0)
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
        auto* credentials = manager->findChild<QLineEdit*>("createdProfileCredentials");
        checks &= !credentials->text().isEmpty() && credentials->echoMode() == QLineEdit::Password;
        auto select = [&] {
            for (int r = 0; r < table->rowCount(); ++r) if (table->item(r, 0)->data(Qt::UserRole).toString() == id) table->selectRow(r);
        };
        select();
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!editor) { checks = false; return; }
            editor->findChild<QComboBox*>("profileProfession")->setCurrentIndex(1);
            editor->findChild<QComboBox*>("profileSpirit")->setCurrentIndex(1);
            editor->findChild<QCheckBox*>("profileBlocked")->setChecked(true);
            auto* save = editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
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
        checks &= edited && edited->profession_id() == "artist" && edited->spirit() == ProfileSpirit::Good && edited->is_blocked();
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
        const auto artifacts = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            manager->grab().save(artifacts + "/profile-manager.png");
            manager->resize(640, 440);
            QApplication::processEvents();
            manager->grab().save(artifacts + "/profile-manager-small.png");
        }
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
    // Legacy parsing cannot round-trip category + profession together: fail closed.
    const auto linkedBytes = read();
    if (SaveQtSkill(workspace, *linked, "Linked renamed", 1.25, "Changed description", "New category").isEmpty() ||
        read() != linkedBytes) return false;
    return true;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ApplyQtTheme(app);
    qunsetenv("FORGEMIRROR_ADMIN_PASSWORD");
    qunsetenv("FORGEMIRROR_DISABLE_MODULES");
    if (!TestTaskCompletion()) return 1;
    if (!TestProfileDialogs()) return 1;
    if (!TestSkillEditor()) { std::cerr << "Skill editor failed\n"; return 1; }
    QTemporaryDir temp;
    auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
    if (!temp.isValid()) return fail("Temporary directory unavailable");
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    Profile profile(u8"Тестовый профиль");
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
    nav->setCurrentRow(1);
    if (table->rowCount() != 1 || !table->item(0, 0)->text().contains(QString::fromUtf8("Проверка Qt"))) return fail("Task loading failed");
    if (primary->isVisible()) return fail("Unauthenticated user can mutate data");
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
    saveForm(QString::fromUtf8("Создано через Qt"));
    primary->click();
    if (LoadTasksData(workspace.directory).size() != 2) return fail("Task form did not persist");
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->data(Qt::UserRole).toString() == "qt-smoke-task") table->selectRow(i);
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) dialog->accept();
    });
    window.findChild<QPushButton*>("changeStatus")->click();
    const auto persisted = LoadTasksData(workspace.directory);
    auto changed = std::find_if(persisted.begin(), persisted.end(), [](const auto& t) { return t.id == "qt-smoke-task"; });
    if (changed == persisted.end() || changed->status != 1) return fail("Status form did not persist");
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
