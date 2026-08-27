#include "QtWindow.h"
#include "QtTheme.h"
#include <QtWidgets>
#include <QLockFile>
#include <filesystem>
#include <iostream>

namespace {
std::filesystem::path path(const QString& value) { return std::filesystem::u8path(value.toUtf8().constData()); }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("ForgeMirrorQt");
    QCoreApplication::setOrganizationName("Pharos");
    ApplyQtTheme(app);
    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromUtf8("ForgeMirror Qt — изолированный клиент переноса"));
    parser.addHelpOption();
    parser.addOption({"storage-dir", "Explicit test workspace (never use the production directory).", "path"});
    parser.addOption({"smoke-test", "Open the real window and exit after one second."});
    parser.addOption({"screenshot", "Save the Qt window as PNG before smoke-test exit.", "path"});
    parser.process(app);
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
        std::filesystem::create_directories(directory.parent_path());
        QLockFile lock(QString::fromStdWString(directory.wstring()) + ".qt.lock");
        if (!lock.tryLock(0)) throw std::runtime_error("This Qt workspace is already open in another process.");
        if (!std::filesystem::exists(directory) && !parser.isSet("storage-dir") && std::filesystem::exists(production)) {
            const auto answer = QMessageBox::question(nullptr, QString::fromUtf8("Копия данных для Qt"),
                QString::fromUtf8("Скопировать данные стабильной версии в отдельную папку Qt?\n"
                    "Исходные данные останутся без изменений. Изменения Qt не попадут обратно.\n\n")
                    + QString::fromStdWString(production.wstring()) + "\n → " + QString::fromStdWString(directory.wstring()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) return 0;
            if (answer == QMessageBox::Yes) {
                // Stage the copy so an interrupted import never looks like a complete workspace.
                const auto staging = directory.parent_path() / ("import-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
                std::filesystem::copy(production, staging, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks);
                std::filesystem::rename(staging, directory);
            }
        }
        std::filesystem::create_directories(directory);
        QtWorkspace workspace(directory);
        QtWindow window(workspace);
        window.show();
        if (parser.isSet("smoke-test")) {
            QTimer::singleShot(1000, &app, [&] {
                bool success = window.isVisible();
                if (parser.isSet("screenshot")) success &= window.grab().save(parser.value("screenshot"));
                app.exit(success ? 0 : 2);
            });
        }
        return app.exec();
    } catch (const std::exception& error) {
        if (parser.isSet("smoke-test")) std::cerr << error.what() << '\n';
        else QMessageBox::critical(nullptr, "ForgeMirror Qt", QString::fromUtf8(error.what()));
        return 1;
    }
}
