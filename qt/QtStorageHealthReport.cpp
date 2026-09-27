#include "QtStorageHealthReport.h"

#include "AppWorkspaceDataService.h"
#include "AppUtils.h"
#include "CloudSync.h"
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QSaveFile>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <sstream>

namespace {
QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), int(value.size()));
}

QString pathText(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return QString::fromUtf8(bytes.data(), int(bytes.size()));
}

bool isQtOwnedStrayPath(const std::string& path) {
    static const std::unordered_set<std::string> qtOwned = {
        "meta/qt-application-log.json", "meta/qt-reminder-state.json",
        "meta/qt-rules-history.json", "meta/qt-rules-presets.json",
        "meta/qt-deadline-agent-state.json"
    };
    return qtOwned.find(path) != qtOwned.end();
}

bool safeRelative(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) return false;
    for (const auto& part : relative) if (part == ".." || part == ".") return false;
    return relative.lexically_normal() == relative;
}

bool reparsePath(const std::filesystem::path& path, bool* isDirectory = nullptr) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    if (isDirectory) *isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec) return false;
    if (isDirectory) *isDirectory = std::filesystem::is_directory(status);
    return std::filesystem::is_symlink(status);
#endif
}

bool describeEntry(const std::filesystem::path& root, const std::filesystem::path& absolute,
    QtStorageStrayEntry* result, QString* error) {
    std::error_code ec;
    const auto relative = absolute.lexically_relative(root);
    if (!safeRelative(relative)) {
        if (error) *error = QString::fromUtf8("В инвентаре найден небезопасный относительный путь.");
        return false;
    }
    const auto status = std::filesystem::symlink_status(absolute, ec);
    if (ec || status.type() == std::filesystem::file_type::not_found) {
        if (error) *error = QString::fromUtf8("Хранилище изменилось во время сканирования.");
        return false;
    }
    QtStorageStrayEntry item;
    const auto rel8 = relative.generic_u8string();
    item.relativePath.assign(rel8.begin(), rel8.end());
    bool reparseDirectory = false;
    item.reparsePoint = reparsePath(absolute, &reparseDirectory);
    item.directory = std::filesystem::is_directory(status) || (item.reparsePoint && reparseDirectory);
    if (std::filesystem::is_regular_file(status)) {
        item.size = std::filesystem::file_size(absolute, ec);
        if (ec) {
            if (error) *error = QString::fromUtf8("Не удалось прочитать размер элемента хранилища.");
            return false;
        }
        QFile file(pathText(absolute));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QString::fromUtf8("Не удалось прочитать элемент для проверки контрольной суммы.");
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
                if (error) *error = QString::fromUtf8("Ошибка чтения элемента хранилища.");
                return false;
            }
            hash.addData(chunk);
        }
        item.sha256 = hash.result().toHex().toStdString();
    } else if (item.reparsePoint) {
        const auto target = std::filesystem::read_symlink(absolute, ec).generic_u8string();
        if (ec) {
            if (error) *error = QString::fromUtf8("Не удалось проверить цель ссылки хранилища.");
            return false;
        }
        item.linkTarget.assign(target.begin(), target.end());
    }
    ec.clear();
    const auto modified = std::filesystem::last_write_time(absolute, ec);
    if (!ec) item.modifiedTicks = modified.time_since_epoch().count();
    *result = std::move(item);
    return true;
}

bool sameEntry(const QtStorageStrayEntry& left, const QtStorageStrayEntry& right) {
    return left.relativePath == right.relativePath && left.directory == right.directory &&
        left.reparsePoint == right.reparsePoint && left.size == right.size &&
        left.modifiedTicks == right.modifiedTicks && left.sha256 == right.sha256 && left.linkTarget == right.linkTarget;
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

    std::vector<QtStorageStrayEntry> strays;
    if (!BuildQtStorageStrayInventory(storageDir, &strays, error)) return false;
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
    else for (const auto& entry : strays) lines << QStringLiteral("  • ") + fromUtf8(entry.relativePath);
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

bool BuildQtStorageStrayInventory(const std::filesystem::path& storageDir,
    std::vector<QtStorageStrayEntry>* entries, QString* error) {
    if (entries) entries->clear();
    if (error) error->clear();
    if (!entries || storageDir.empty()) {
        if (error) *error = QString::fromUtf8("Некорректная папка локального хранилища.");
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(storageDir, ec) || ec) {
        if (error) *error = QString::fromUtf8("Папка локального хранилища недоступна.");
        return false;
    }
    std::vector<std::string> roots;
    if (!CollectStrayStorageFiles(storageDir, roots)) {
        if (error) *error = QString::fromUtf8("Не удалось просканировать локальное хранилище.");
        return false;
    }
    std::vector<std::filesystem::path> allPaths;
    for (const auto& rootUtf8 : roots) {
        if (isQtOwnedStrayPath(rootUtf8)) continue;
        const auto relative = std::filesystem::u8path(rootUtf8).lexically_normal();
        if (!safeRelative(relative)) {
            if (error) *error = QString::fromUtf8("Сканер вернул небезопасный путь.");
            return false;
        }
        const auto rootPath = (storageDir / relative).lexically_normal();
        allPaths.push_back(rootPath);
        const auto status = std::filesystem::symlink_status(rootPath, ec);
        if (ec) {
            if (error) *error = QString::fromUtf8("Хранилище изменилось во время сканирования.");
            return false;
        }
        if (!std::filesystem::is_directory(status) || reparsePath(rootPath)) continue;
        std::filesystem::recursive_directory_iterator it(rootPath, std::filesystem::directory_options::none, ec), end;
        if (ec) {
            if (error) *error = QString::fromUtf8("Нет доступа к содержимому лишней папки.");
            return false;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) {
                if (error) *error = QString::fromUtf8("Сканирование лишней папки прервано.");
                return false;
            }
            allPaths.push_back(it->path());
            const auto childStatus = it->symlink_status(ec);
            if (ec) {
                if (error) *error = QString::fromUtf8("Не удалось проверить вложенный элемент.");
                return false;
            }
            if (reparsePath(it->path())) it.disable_recursion_pending();
        }
        if (ec) {
            if (error) *error = QString::fromUtf8("Сканирование лишней папки прервано.");
            return false;
        }
    }
    std::sort(allPaths.begin(), allPaths.end(), [&](const auto& left, const auto& right) {
        const auto l = left.lexically_relative(storageDir).generic_u8string();
        const auto r = right.lexically_relative(storageDir).generic_u8string();
        return l < r;
    });
    allPaths.erase(std::unique(allPaths.begin(), allPaths.end()), allPaths.end());
    entries->reserve(allPaths.size());
    for (const auto& path : allPaths) {
        QtStorageStrayEntry entry;
        if (!describeEntry(storageDir, path, &entry, error)) {
            entries->clear();
            return false;
        }
        entries->push_back(std::move(entry));
    }
    return true;
}

bool RemoveQtStorageStrayEntries(const std::filesystem::path& storageDir,
    const std::vector<QtStorageStrayEntry>& expectedInventory,
    const std::vector<std::string>& approvedPaths, int* removedCount, QString* error) {
    if (removedCount) *removedCount = 0;
    if (error) error->clear();
    if (!removedCount || expectedInventory.empty() || approvedPaths.empty()) {
        if (error) *error = QString::fromUtf8("Не выбраны элементы для очистки.");
        return false;
    }
    std::vector<QtStorageStrayEntry> current;
    if (!BuildQtStorageStrayInventory(storageDir, &current, error)) return false;
    if (current.size() != expectedInventory.size() || !std::equal(current.begin(), current.end(), expectedInventory.begin(), sameEntry)) {
        if (error) *error = QString::fromUtf8("Хранилище изменилось после просмотра списка. Повторите сканирование; ничего не удалено.");
        return false;
    }
    std::unordered_map<std::string, const QtStorageStrayEntry*> byPath;
    for (const auto& entry : expectedInventory) byPath.emplace(entry.relativePath, &entry);
    std::unordered_set<std::string> selected;
    for (const auto& path : approvedPaths) {
        if (!byPath.count(path) || !selected.insert(path).second) {
            if (error) *error = QString::fromUtf8("Подтверждённый список содержит неизвестный или повторный путь.");
            return false;
        }
    }
    for (const auto& path : selected) {
        const auto* entry = byPath.at(path);
        if (!entry->directory || entry->reparsePoint) continue;
        const std::string prefix = path + "/";
        for (const auto& child : expectedInventory) {
            if (child.relativePath.compare(0, prefix.size(), prefix) == 0 && !selected.count(child.relativePath)) {
                if (error) *error = QString::fromUtf8("Для удаления папки отметьте также все элементы внутри неё.");
                return false;
            }
        }
    }

    std::vector<const QtStorageStrayEntry*> ordered;
    for (const auto& path : selected) ordered.push_back(byPath.at(path));
    auto depth = [](const std::string& path) { return std::count(path.begin(), path.end(), '/'); };
    std::sort(ordered.begin(), ordered.end(), [&](const auto* left, const auto* right) {
        const int leftDepth = depth(left->relativePath), rightDepth = depth(right->relativePath);
        return leftDepth == rightDepth ? left->relativePath > right->relativePath : leftDepth > rightDepth;
    });
    std::error_code ec;
    const auto rootCanonical = std::filesystem::canonical(storageDir, ec);
    if (ec) {
        if (error) *error = QString::fromUtf8("Папка локального хранилища недоступна.");
        return false;
    }
    for (const auto* approved : ordered) {
        const auto relative = std::filesystem::u8path(approved->relativePath);
        if (!safeRelative(relative)) {
            if (error) *error = QString::fromUtf8("Путь очистки оказался вне хранилища.");
            return false;
        }
        const auto target = (storageDir / relative).lexically_normal();
        ec.clear();
        const auto parentCanonical = std::filesystem::canonical(target.parent_path(), ec);
        const auto parentRelative = parentCanonical.lexically_relative(rootCanonical);
        const bool parentInsideRoot = !parentRelative.empty() && !parentRelative.is_absolute() &&
            !parentRelative.has_root_name() && !parentRelative.has_root_directory() &&
            std::none_of(parentRelative.begin(), parentRelative.end(), [](const auto& part) { return part == ".."; });
        if (ec || !parentInsideRoot) {
            if (error) *error = QString::fromUtf8("Путь очистки покинул корень локального хранилища.");
            return false;
        }
        auto checkStatus = std::filesystem::symlink_status(target, ec);
        if (ec) {
            if (error) *error = QString::fromUtf8("Элемент изменился перед удалением; дальнейшая очистка остановлена.");
            return false;
        }
        if (!approved->directory || approved->reparsePoint) {
            QtStorageStrayEntry actual;
            if (!describeEntry(storageDir, target, &actual, error) || !sameEntry(actual, *approved)) {
                if (error && error->isEmpty()) *error = QString::fromUtf8("Файл изменился после подтверждения; дальнейшая очистка остановлена.");
                return false;
            }
        } else if (!std::filesystem::is_directory(checkStatus) || reparsePath(target)) {
            if (error) *error = QString::fromUtf8("Папка изменилась после подтверждения; дальнейшая очистка остановлена.");
            return false;
        }
        ec.clear();
        if (!std::filesystem::remove(target, ec) || ec) {
            if (error) *error = QString::fromUtf8("Очистка остановлена после удаления %1 элементов; остальные сохранены.").arg(*removedCount);
            return false;
        }
        ++*removedCount;
    }
    return true;
}
