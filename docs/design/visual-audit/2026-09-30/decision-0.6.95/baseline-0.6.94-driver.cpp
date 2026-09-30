#include "QtWindow.h"
#include "QtTheme.h"
#include "QtCloudConflict.h"
#include "QtStorageConflict.h"
#include "CloudSync.h"
#include "AppUtils.h"
#include <QtWidgets>
#include <QtTest/QTest>
#include <iostream>

static std::filesystem::path path(const QString& value) { return std::filesystem::u8path(value.toUtf8().toStdString()); }
static bool write(const std::filesystem::path& value, const QByteArray& bytes) {
    QFile file(QString::fromUtf8(value.u8string()));
    QDir().mkpath(QFileInfo(file).absolutePath());
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static void drive(QApplication& app, const QString& expected, std::function<void(QDialog*)> body) {
    const int loop = QThread::currentThread()->loopLevel() + 1;
    QTimer::singleShot(0, &app, [&app, expected, body, loop] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != expected) { std::cerr << "Missing modal " << expected.toStdString() << '\n'; app.exit(2); return; }
        auto* ready = new QTimer(dialog); ready->setInterval(1);
        QObject::connect(ready, &QTimer::timeout, dialog, [ready, dialog, body, loop] {
            if (QThread::currentThread()->loopLevel() < loop) return;
            ready->stop(); dialog->showNormal(); dialog->raise(); dialog->activateWindow();
            if (!QTest::qWaitForWindowExposed(dialog, 2000) || !QTest::qWaitForWindowActive(dialog, 2000))
                std::cerr << "Unexposed modal " << dialog->objectName().toStdString() << '\n';
            body(dialog); ready->deleteLater();
        }); ready->start();
    });
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); ApplyQtTheme(app);
    const QString output = qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (output.isEmpty() || !QDir().mkpath(output)) return 3;
    QTemporaryDir temporary; if (!temporary.isValid()) return 3;
    const auto local = path(temporary.path() + QString::fromUtf8("/Рабочая папка с длинным русским названием для сравнения версий"));
    const auto remote = path(temporary.path() + QString::fromUtf8("/Облачная папка с длинным русским названием для сравнения версий"));
    std::filesystem::create_directories(local); std::filesystem::create_directories(remote);
    CloudSyncConfig config; config.enabled = true; config.root = remote;
    if (!SaveCloudSyncConfig(local, config)) return 3;
    for (const auto& root : {local, remote}) {
        const bool cloud = root == remote;
        const QString title = cloud ? QString::fromUtf8("Очень длинное облачное название задачи для проверки сравнений и полного контекста")
            : QString::fromUtf8("Очень длинное локальное название задачи для проверки сравнений и полного контекста");
        const auto tasks = QJsonDocument(QJsonArray{QJsonObject{{"id", cloud ? "remote" : "local"}, {"title", title}}}).toJson();
        if (!write(root / "meta/tasks.json", tasks) || !write(root / "meta/pipeline.json", "{\"steps\":[]}") ||
            !write(root / "meta/projects.json", "[]") || !write(root / "meta/banner.json", "{\"items\":[\"Long phrase\"]}") ||
            !write(root / "meta/gameplay.ini", "[leveling]\nbase=1500\n") ||
            !write(root / "meta/professions.txt", "pr_fixture|Fixture profession|Description\n") ||
            !write(root / "skills.txt", "sk_fixture|Fixture skill|1|prof=pr_fixture|Description\n")) return 3;
        StorageVaultData vault; vault.balance = cloud ? 250 : 50;
        vault.currencyName = u8"Очень длинное название валюты тестового кошелька"; vault.currencyCode = cloud ? "CLD" : "LOC";
        vault.log.push_back({1700000000, vault.balance, "fixture", u8"Длинное основание записи для сравнения баланса и полного аудита"});
        if (!SaveStorageVault(root, vault)) return 3;
    }
    // Produce actual reversible cloud snapshots in disposable data only.
    if (!ApplyQtCloudWorkspaceFile(local, remote / "meta/tasks.json", "meta/tasks.json", "cloud").ok) return 3;
    if (!SetAdminPassword(local, "baseline-only-admin") || !SetAdminStayLoggedIn(local, true)) return 3;
    QtWorkspace workspace(local);
    Profile profile(u8"Очень длинное русское имя профиля для безопасной проверки кошелька"); profile.set_wallet_balance(250);
    const auto created = workspace.storage->create_profile(profile); if (!created) return 3;
    const auto target = local / "safe shortcut folder"; std::filesystem::create_directories(target);
    int captures = 0;
    auto capture = [&](QDialog* dialog, const QString& stem) {
        QApplication::processEvents(); QApplication::processEvents();
        if (!dialog->grab().save(output + '/' + stem + ".png")) { std::cerr << "Capture failed " << stem.toStdString() << '\n'; return; }
        ++captures;
        std::cout << stem.toStdString() << " actual=" << dialog->width() << 'x' << dialog->height()
            << " minimum=" << dialog->minimumWidth() << 'x' << dialog->minimumHeight() << '\n';
        for (auto* table : dialog->findChildren<QTableWidget*>()) {
            if (!table->isVisible()) continue;
            std::cout << "  table " << table->objectName().toStdString() << " size=" << table->width() << 'x' << table->height()
                << " rows=" << table->rowCount() << " row0=" << (table->rowCount() ? table->rowHeight(0) : 0) << '\n';
            for (int row=0; row<table->rowCount(); ++row) for(int column=0; column<table->columnCount(); ++column)
                if (auto* button = qobject_cast<QPushButton*>(table->cellWidget(row, column)))
                    std::cout << "  cell command actual=" << button->width() << 'x' << button->height()
                        << " hint=" << button->sizeHint().width() << 'x' << button->sizeHint().height() << '\n';
        }
        for (auto* button : dialog->findChildren<QPushButton*>()) if (button->isVisible())
            std::cout << "  command " << button->objectName().toStdString() << " actual=" << button->width() << 'x' << button->height()
                << " hint=" << button->sizeHint().width() << 'x' << button->sizeHint().height() << '\n';
    };
    for (const int scale : {100, 200}) {
        QtDisplaySettings settings; settings.scalePercent = scale; settings.motionEnabled = false;
        SaveQtDisplaySettings(local, settings); ApplyQtDisplaySettings(app, settings);
        const QString suffix = QString::number(scale);
        drive(app, "cloudConflictResolver", [&](QDialog* dialog) {
            dialog->resize(640,520); auto* tabs=dialog->findChild<QTabWidget*>("cloudConflictTabs");
            for(int index=0; tabs && index<tabs->count(); ++index) { tabs->setCurrentIndex(index); capture(dialog, "cloud-tab-" + QString::number(index) + '-' + suffix); }
            dialog->reject();
        }); ShowCloudConflictResolver(nullptr, local, {}, true, true);
        drive(app, "storageConflictResolver", [&](QDialog* dialog) { dialog->resize(640,520); capture(dialog, "storage-" + suffix); dialog->reject(); });
        bool localChanged = false; ShowQtStorageConflictResolver(nullptr, local, &localChanged);
        QtWindow window(workspace); window.showNormal(); QApplication::processEvents();
        auto* navigation = window.findChild<QListWidget*>("navigation");
        auto* adjust=window.findChild<QPushButton*>("adjustProfileWallet");
        auto* create=window.findChild<QPushButton*>("primary");
        auto* help=window.findChild<QAction*>("shortcutHelpAction");
        if (!navigation || !adjust || !create || !help) return 4;
        navigation->setCurrentRow(0); QApplication::processEvents();
        for(int operation=0; operation<2; ++operation) {
            drive(app, "walletAdjustmentDialog", [&](QDialog* dialog) {
                dialog->resize(640,520); dialog->findChild<QComboBox*>("walletOperation")->setCurrentIndex(operation);
                dialog->findChild<QLineEdit*>("walletReason")->setText(QString::fromUtf8("Основание безопасной отменяемой операции с длинным русским текстом"));
                capture(dialog, QString("wallet-%1-%2").arg(operation).arg(scale)); dialog->reject();
            }); adjust->click();
        }
        navigation->setCurrentRow(11); QApplication::processEvents();
        drive(app, "shortcutEditor", [&](QDialog* dialog) {
            dialog->resize(640,520); dialog->findChild<QLineEdit*>("shortcutLabel")->setText(QString::fromUtf8("Длинное русское название ярлыка тестовой папки"));
            dialog->findChild<QLineEdit*>("shortcutPath")->setText(QString::fromUtf8(target.u8string()));
            capture(dialog, "shortcut-create-" + suffix); dialog->reject();
        }); create->click();
        drive(app, "shortcutHelp", [&](QDialog* dialog) { dialog->resize(640,520); capture(dialog, "shortcut-help-" + suffix); dialog->reject(); });
        help->trigger(); window.close(); QApplication::processEvents();
    }
    std::cout << "BEFORE captures=" << captures << " isolated fixture; no accepted mutations\n";
    return captures == 24 ? 0 : 5;
}
