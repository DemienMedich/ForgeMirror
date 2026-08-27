#include "QtWindow.h"
#include "AppTaskProjectService.h"
#include <QtWidgets>
#include <QtTest/QTest>
#include <iostream>
#include <algorithm>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    qunsetenv("FORGEMIRROR_ADMIN_PASSWORD");
    qunsetenv("FORGEMIRROR_DISABLE_MODULES");
    QTemporaryDir temp;
    auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
    if (!temp.isValid()) return fail("Temporary directory unavailable");
    QtWorkspace workspace(std::filesystem::u8path(temp.path().toUtf8().constData()));
    Profile profile(u8"Тестовый профиль");
    profile.add_skill(u8"Моделирование");
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
    nav->setCurrentRow(1);
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
    std::cout << "smoke_qt: OK\n";
    return 0;
}
