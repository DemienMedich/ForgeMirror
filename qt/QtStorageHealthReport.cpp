#include "QtStorageHealthReport.h"

#include "AppWorkspaceDataService.h"
#include "AppUtils.h"
#include "CloudSync.h"
#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#include <unordered_set>
#include <sstream>

namespace {
QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), int(value.size()));
}

QString pathText(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return QString::fromUtf8(bytes.data(), int(bytes.size()));
}
}

bool BuildQtStorageHealthReport(const std::filesystem::path& storageDir, const ModuleToggles& modules,
    std::int64_t now, QString* report, QString* error) {
    if (report) report->clear();
    if (error) error->clear();
    if (!report || storageDir.empty() || !std::filesystem::is_directory(storageDir)) {
        if (error) *error = QString::fromUtf8("Папка локального хранилища недоступна.");
        return false;
    }

    std::vector<std::string> strays;
    if (!CollectStrayStorageFiles(storageDir, strays)) {
        if (error) *error = QString::fromUtf8("Не удалось просканировать локальное хранилище.");
        return false;
    }
    // The shared ImGui-era scanner does not know about legitimate Qt-only
    // workspace metadata. Leave unknown qt-* files visible for investigation.
    static const std::unordered_set<std::string> qtOwned = {
        "meta/qt-application-log.json", "meta/qt-reminder-state.json",
        "meta/qt-rules-history.json", "meta/qt-rules-presets.json",
        "meta/qt-deadline-agent-state.json"
    };
    strays.erase(std::remove_if(strays.begin(), strays.end(), [](const std::string& path) {
        return qtOwned.find(path) != qtOwned.end();
    }), strays.end());
    const auto sync = InspectWorkspaceSyncHealth(storageDir, modules);
    const auto cloudConfig = LoadCloudSyncConfig(storageDir);
    CloudWorkspaceDriftSummary drift;
    QString cloudStatus;
    if (!cloudConfig.enabled) {
        cloudStatus = QString::fromUtf8("Облачное сравнение отключено.");
    } else {
        const auto cloudRoot = ResolveCloudRootPath(cloudConfig, storageDir);
        std::error_code ec;
        if (!std::filesystem::is_directory(cloudRoot, ec) || ec) {
            cloudStatus = QString::fromUtf8("Настроенная папка облака недоступна.");
        } else {
            drift = InspectCloudWorkspaceDrift(cloudConfig, storageDir, 0);
            cloudStatus = QString::fromUtf8("Сопоставление по содержимому, без предположения о том, какая сторона новее.");
        }
    }

    QStringList lines;
    lines << QString::fromUtf8("Отчёт здоровья хранилища ForgeMirrorQt")
          << QString::fromUtf8("Папка: %1").arg(pathText(storageDir))
          << QString::fromUtf8("Дата: %1").arg(QDateTime::fromSecsSinceEpoch(now).toString("yyyy-MM-dd HH:mm:ss"))
          << QString()
          << QString::fromUtf8("РАСХОЖДЕНИЯ ЛОКАЛЬНОЙ И ОБЛАЧНОЙ КОПИИ")
          << QString::fromUtf8("Проблем: %1. %2").arg(drift.issueCount).arg(cloudStatus);
    if (drift.issues.empty()) lines << (cloudConfig.enabled && cloudStatus.startsWith(QString::fromUtf8("Сопоставление"))
        ? QString::fromUtf8("Локальная и облачная версии совпадают по отслеживаемым файлам.")
        : QString::fromUtf8("Нет доступных для сравнения облачных расхождений."));
    else for (const auto& issue : drift.issues) lines << QStringLiteral("  • ") + fromUtf8(issue);

    lines << QString() << QString::fromUtf8("ПРОВЕРКА SYNC-ФАЙЛОВ")
          << QString::fromUtf8("Проблем: %1").arg(sync.issueCount);
    if (sync.files.empty()) lines << QString::fromUtf8("Для включённых модулей sync-файлы не настроены.");
    for (const auto& file : sync.files) {
        const QString state = !file.exists ? QString::fromUtf8("нет")
            : file.valid ? QString::fromUtf8("исправен") : QString::fromUtf8("проблема");
        lines << QString::fromUtf8("  %1 · %2 · записей: %3/%4 · %5")
            .arg(fromUtf8(file.relativePath), state)
            .arg(qulonglong(file.loadedEntries)).arg(qulonglong(file.rawEntries))
            .arg(fromUtf8(file.message));
    }
    if (!sync.issues.empty()) {
        lines << QString::fromUtf8("Подробности проблем:");
        for (const auto& issue : sync.issues) lines << QStringLiteral("  • ") + fromUtf8(issue);
    }

    lines << QString() << QString::fromUtf8("ЭЛЕМЕНТЫ ВНЕ СПИСКА ХРАНИЛИЩА")
          << QString::fromUtf8("Найдено: %1").arg(qulonglong(strays.size()));
    if (strays.empty()) lines << QString::fromUtf8("Лишние элементы не найдены.");
    else for (const auto& entry : strays) lines << QStringLiteral("  • ") + fromUtf8(entry);
    lines << QString() << QString::fromUtf8("Отчёт только читает локальные данные; файлы не изменялись и не удалялись.");
    *report = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return true;
}

bool ExportQtStorageHealthReport(const QString& path, const QString& report, QString* error) {
    if (error) error->clear();
    if (path.trimmed().isEmpty() || QFileInfo(path).isDir()) {
        if (error) *error = QString::fromUtf8("Выберите имя TXT-файла отчёта.");
        return false;
    }
    const QByteArray bytes = report.toUtf8();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF", 3) != 3 ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить отчёт хранилища.");
        return false;
    }
    return true;
}
