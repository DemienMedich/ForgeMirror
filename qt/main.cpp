#include "QtWindow.h"
#include "QtTheme.h"
#include "QtLogSanitization.h"
#include "QtDeadlineAgent.h"
#include "QtCommandHelpDialog.h"
#include "QtWorkspaceImport.h"
#include <QtWidgets>
#include <QLockFile>
#include <filesystem>
#include <iostream>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
std::filesystem::path path(const QString& value) { return std::filesystem::u8path(value.toUtf8().constData()); }
QPointer<QtWindow> runtimeLogWindow;
QtMessageHandler previousQtMessageHandler = nullptr;
thread_local bool forwardingQtMessage = false;
bool hasCommandOutputTarget() {
#ifdef _WIN32
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    return output && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN;
#else
    return true;
#endif
}

void qtRuntimeMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& text) {
    if (previousQtMessageHandler) previousQtMessageHandler(type, context, text);
    if (type < QtWarningMsg || type == QtFatalMsg || forwardingQtMessage || !qApp) return;
    forwardingQtMessage = true;
    const auto level = type >= QtCriticalMsg ? AppLogLevel::Error : AppLogLevel::Warning;
    const auto safeText = SanitizeQtLogMessage(text);
    QMetaObject::invokeMethod(qApp, [level, safeText] {
        if (runtimeLogWindow) runtimeLogWindow->recordRuntimeMessage(level, safeText);
    }, Qt::QueuedConnection);
    forwardingQtMessage = false;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("ForgeMirrorQt");
    QCoreApplication::setOrganizationName("Pharos");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    ApplyQtTheme(app);
    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromUtf8("ForgeMirror Qt — изолированный клиент переноса"));
    const auto helpOption = parser.addHelpOption();
    const auto versionOption = parser.addVersionOption();
    parser.addOption({"storage-dir", "Explicit test workspace (never use the production directory).", "path"});
    parser.addOption({"smoke-test", "Open the real window and exit after one second."});
    parser.addOption({"screenshot", "Save the Qt window as PNG before smoke-test exit.", "path"});
    parser.addOption({"deadline-agent", "One-shot deadline notifier used by the opt-in Windows schedule."});
    parser.addOption({"remove-deadline-schedule", "Remove this installation's opt-in Windows deadline schedule."});
    // Parse without process() so command-line help and version never fall
    // through into normal workspace startup in this GUI-subsystem executable.
    if (!parser.parse(app.arguments())) {
        std::cerr << parser.errorText().toUtf8().constData() << '\n';
        return 2;
    }
    if (parser.isSet(helpOption)) {
        if (!parser.isSet("smoke-test") && hasCommandOutputTarget()) {
            parser.showHelp(0);
            return 0;
        }
        std::unique_ptr<QDialog> dialog(CreateQtCommandHelpDialog(
            BuildQtGuiCommandHelpText(parser.helpText(), QCoreApplication::applicationName())));
        bool success = true;
        if (parser.isSet("smoke-test")) {
            QTimer::singleShot(1000, dialog.get(), [&] {
                if (parser.isSet("screenshot"))
                    success = dialog->grab().save(parser.value("screenshot"));
                dialog->accept();
            });
        }
        dialog->exec();
        return success ? 0 : 2;
    }
    if (parser.isSet(versionOption)) {
        if (hasCommandOutputTarget()) {
            parser.showVersion();
            return 0;
        }
        QMessageBox::information(nullptr, QString::fromUtf8("ForgeMirror Qt"),
            QString::fromUtf8("ForgeMirror Qt %1").arg(QCoreApplication::applicationVersion()));
        return 0;
    }
    if (parser.isSet("remove-deadline-schedule"))
        return ConfigureQtDeadlineSchedule(false, nullptr) ? 0 : 1;
    try {
        const auto defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/workspace";
        const auto directory = path(parser.isSet("storage-dir") ? parser.value("storage-dir") : defaultPath);
        if (parser.isSet("smoke-test") && !parser.isSet("storage-dir"))
            throw std::runtime_error("Smoke testing requires an explicit disposable --storage-dir.");
#ifdef _WIN32
        const auto productionDefault = qEnvironmentVariable("APPDATA") + "/ForgeMirror";
#elif defined(__APPLE__)
        const auto productionDefault = QDir::homePath() + "/Library/Application Support/ForgeMirror";
#else
        const auto productionDefault = QDir::homePath() + "/.forgemirror";
#endif
        const auto production = path(qEnvironmentVariable("FORGEMIRROR_STORAGE_DIR", productionDefault));
        // Prevent an accidental --storage-dir pointing at the production workspace or its parent.
        auto canonical = [](const auto& input) {
            auto result = QDir::fromNativeSeparators(QString::fromStdWString(std::filesystem::weakly_canonical(input).wstring()));
#ifdef _WIN32
            result = result.toCaseFolded();
#endif
            return result;
        };
        const auto normalized = canonical(directory);
        const auto original = canonical(production);
        if (normalized == original || normalized.startsWith(original + '/') || original.startsWith(normalized + '/'))
            throw std::runtime_error("Qt migration must use a separate workspace outside the production directory.");
        if (parser.isSet("deadline-agent")) return RunQtDeadlineAgent(directory);
        const auto workspaceParent = directory.parent_path();
        if (!workspaceParent.empty()) std::filesystem::create_directories(workspaceParent);
        QLockFile lock(QString::fromStdWString(directory.wstring()) + ".qt.lock");
        if (!lock.tryLock(0)) throw std::runtime_error("This Qt workspace is already open in another process.");
        int exitCode = 0;
        bool restartRequested = false;
        {
            if (!std::filesystem::exists(directory) && !parser.isSet("storage-dir") && std::filesystem::exists(production)) {
                const auto answer = QMessageBox::question(nullptr, QString::fromUtf8("Копия данных для Qt"),
                    QString::fromUtf8("Скопировать данные стабильной версии в отдельную папку Qt?\n"
                        "Исходные данные останутся без изменений. Изменения Qt не попадут обратно.\n\n")
                        + QString::fromStdWString(production.wstring()) + "\n → " + QString::fromStdWString(directory.wstring()),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer == QMessageBox::Cancel) return 0;
                if (answer == QMessageBox::Yes) {
                    QString importError;
                    if (!ImportQtWorkspaceSnapshot(production, directory, &importError))
                        throw std::runtime_error(importError.toUtf8().toStdString());
                }
            }
            std::filesystem::create_directories(directory);
            QtWorkspace workspace(directory);
            QtWindow window(workspace);
            runtimeLogWindow = &window;
            previousQtMessageHandler = qInstallMessageHandler(qtRuntimeMessageHandler);
            window.show();
            if (parser.isSet("smoke-test")) {
                qWarning("ForgeMirror Qt runtime warning smoke probe: token=SMOKE_TOKEN password=SMOKE_PASSWORD https://smoke-user:smoke-pass@example.com");
                qCritical("ForgeMirror Qt runtime critical smoke probe");
                QTimer::singleShot(1000, &app, [&] {
                    bool success = window.isVisible();
                    if (parser.isSet("screenshot")) success &= window.grab().save(parser.value("screenshot"));
                    app.exit(success ? 0 : 2);
                });
            }
            exitCode = app.exec();
            qInstallMessageHandler(previousQtMessageHandler);
            previousQtMessageHandler = nullptr;
            runtimeLogWindow.clear();
            restartRequested = app.property("forgeRestartRequested").toBool();
            if (restartRequested) window.hide();
        }
        if (restartRequested) {
            lock.unlock();
            QStringList restartArguments;
            restartArguments << QStringLiteral("--storage-dir") << QString::fromStdWString(directory.wstring());
            if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), restartArguments)) {
                QMessageBox::critical(nullptr, QString::fromUtf8("ForgeMirror Qt"),
                    QString::fromUtf8("Настройки сохранены, но автоматически перезапустить программу не удалось. Запустите её снова вручную."));
                return 1;
            }
        }
        return exitCode;
    } catch (const std::exception& error) {
        if (parser.isSet("smoke-test")) std::cerr << error.what() << '\n';
        else QMessageBox::critical(nullptr, "ForgeMirror Qt", QString::fromUtf8(error.what()));
        return 1;
    }
}
