#include "QtCloudPushPreview.h"
#include "QtStorageConflict.h"
#include <QtCore>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
QString q(const fs::path& value) { return QString::fromStdWString(value.wstring()); }
fs::path p(const QString& value) { return fs::u8path(value.toUtf8().toStdString()); }
const char* journalName = "meta/qt-cloud-push.json";
int failureAfterWritesForTests = -1;
bool leaveJournalForTests = false;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void checkPath(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path)) {
        current /= part;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), u8"Предпросмотр: ссылки и junction не поддерживаются.");
#else
        require(!fs::is_symlink(fs::symlink_status(current)), "Preview: symbolic link.");
#endif
    }
}
bool overlaps(const fs::path& first, const fs::path& second) {
    auto a = QDir::fromNativeSeparators(q(fs::weakly_canonical(first)));
    auto b = QDir::fromNativeSeparators(q(fs::weakly_canonical(second)));
#ifdef _WIN32
    a = a.toCaseFolded(); b = b.toCaseFolded();
#endif
    if (!a.endsWith('/')) a += '/';
    if (!b.endsWith('/')) b += '/';
    return a.startsWith(b) || b.startsWith(a);
}
std::map<fs::path, QByteArray> snapshot(const fs::path& root) {
    checkPath(root);
    require(fs::is_directory(root), u8"Папка облака недоступна.");
    std::map<fs::path, QByteArray> result;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        checkPath(entry.path());
        require(entry.is_directory() || entry.is_regular_file(), u8"В облаке найден объект неподдерживаемого типа.");
        if (!entry.is_regular_file()) continue;
        QFile file(q(entry.path()));
        require(file.open(QIODevice::ReadOnly), u8"Не удалось прочитать файл облака.");
        const auto bytes = file.readAll();
        require(file.error() == QFileDevice::NoError, u8"Ошибка чтения файла облака.");
        result.emplace(entry.path().lexically_relative(root), bytes);
    }
    return result;
}
void copyTree(const fs::path& source, const fs::path& target) {
    for (const auto& [relative, bytes] : snapshot(source)) {
        const auto destination = target / relative;
        fs::create_directories(destination.parent_path());
        QFile file(q(destination));
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), u8"Не удалось подготовить временную копию облака.");
    }
}
QByteArray hash(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
bool safeRelative(const QString& name) {
    const auto relative = p(name);
    if (name.isEmpty() || name.contains('\\') || name.contains(':') || relative.is_absolute() ||
        relative.generic_u8string() != relative.lexically_normal().generic_u8string()) return false;
    for (const auto& part : relative) if (part == "." || part == "..") return false;
    return !relative.filename().empty();
}
QByteArray readFile(const fs::path& path) {
    checkPath(path);
    QFile file(q(path)); require(file.open(QIODevice::ReadOnly), u8"Не удалось прочитать файл облачной транзакции.");
    const auto bytes = file.readAll(); require(file.error() == QFileDevice::NoError, u8"Ошибка чтения облачной транзакции.");
    return bytes;
}
void writeFile(const fs::path& path, const QByteArray& bytes) {
    checkPath(path); fs::create_directories(path.parent_path());
    QSaveFile file(q(path)); file.setDirectWriteFallback(false);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            u8"Не удалось атомарно записать файл облачной транзакции.");
}
bool matches(const fs::path& path, bool exists, const QByteArray& expectedHash) {
    checkPath(path);
    if (!fs::exists(path)) return !exists;
    return exists && fs::is_regular_file(path) && hash(readFile(path)) == expectedHash;
}
void writeJournal(const fs::path& path, const QByteArray& bytes) { writeFile(path, bytes); }
}

QtCloudPushPreviewResult PreviewQtCloudWorkspacePush(const CloudSyncConfig& config,
    const fs::path& workspaceDirectory, CloudRole role) {
    QtCloudPushPreviewResult result;
    try {
        require(config.enabled, u8"Облако отключено.");
        require(role == CloudRole::Admin, u8"Предпросмотр выгрузки доступен только администратору.");
        const auto cloudRoot = ResolveCloudRootPath(config, workspaceDirectory);
        result.cloudRoot = fs::absolute(cloudRoot).lexically_normal();
        checkPath(workspaceDirectory); checkPath(cloudRoot);
        require(!overlaps(cloudRoot, workspaceDirectory), u8"Папка облака пересекается с рабочим пространством.");
        const auto before = snapshot(cloudRoot);
        const auto originalManifest = LoadCloudManifest(config, workspaceDirectory);
        QTemporaryDir temp(q(workspaceDirectory.parent_path() / "qt-cloud-preview-XXXXXX"));
        require(temp.isValid(), u8"Не удалось создать временную папку предпросмотра.");
        const auto stagedRoot = p(temp.path()) / "cloud";
        require(fs::create_directory(stagedRoot), u8"Не удалось создать временную папку облака.");
        copyTree(cloudRoot, stagedRoot);
        auto stagedConfig = config;
        stagedConfig.root = fs::absolute(stagedRoot);
        if (config.updateManifestOnPush && !config.manifest.empty() && config.manifest.is_absolute()) {
            const auto manifest = fs::weakly_canonical(config.manifest);
            const auto root = fs::weakly_canonical(cloudRoot);
            require(!overlaps(manifest, workspaceDirectory) && overlaps(manifest, root) && manifest != root,
                    u8"Абсолютный manifest должен находиться внутри папки облака для изолированного предпросмотра.");
            auto relative = manifest.lexically_relative(root);
            require(!relative.empty() && *relative.begin() != "..", u8"Путь manifest выходит за пределы папки облака.");
            stagedConfig.manifest = fs::absolute(stagedRoot / relative);
        }
        result.sync = PushCloudSnapshot(stagedConfig, workspaceDirectory, role);
        require(result.sync.ok, result.sync.message.c_str());
        if (config.updateManifestOnPush) {
            auto stagedManifest = LoadCloudManifest(stagedConfig, workspaceDirectory);
            if (stagedManifest.releaseFile.empty()) stagedManifest.releaseFile = originalManifest.releaseFile;
            if (stagedManifest.notes.empty()) stagedManifest.notes = originalManifest.notes;
            require(SaveCloudManifest(stagedConfig, workspaceDirectory, stagedManifest),
                    u8"Не удалось сохранить manifest во временном cloud-preview.");
        }
        const auto after = snapshot(stagedRoot);
        for (const auto& [relative, bytes] : after) {
            const auto found = before.find(relative);
            if (found == before.end()) {
                ++result.filesAdded;
                result.changes.push_back({relative.generic_u8string(), false, true, {}, bytes});
            } else if (found->second != bytes) {
                ++result.filesReplaced;
                result.changes.push_back({relative.generic_u8string(), true, true, found->second, bytes});
            }
        }
        for (const auto& [relative, bytes] : before) {
            if (!after.count(relative)) {
                ++result.filesRemoved;
                result.changes.push_back({relative.generic_u8string(), true, false, bytes, {}});
            }
        }
        result.sync.changed = result.filesAdded || result.filesReplaced || result.filesRemoved;
        result.message = u8"Предпросмотр завершён. Будут добавлены: " + std::to_string(result.filesAdded) +
            u8", заменены: " + std::to_string(result.filesReplaced) + u8", удалены: " + std::to_string(result.filesRemoved) +
            u8". Файлы облака не изменялись.";
    } catch (const std::exception& error) {
        result.sync.ok = false;
        result.sync.changed = false;
        result.message = error.what();
    }
    return result;
}

bool RecoverQtCloudPush(const fs::path& workspaceDirectory) {
    const auto journal = workspaceDirectory / journalName;
    checkPath(journal);
    if (!fs::exists(journal)) return false;
    const auto document = QJsonDocument::fromJson(readFile(journal));
    require(document.isObject(), u8"Повреждён журнал cloud push; рабочее пространство заблокировано.");
    const auto object = document.object();
    require(object["version"].toInt() == 1 && object["files"].isArray() && object["createdDirectories"].isArray() &&
            object["cloudRoot"].isString(),
            u8"Неподдерживаемый журнал cloud push; требуется ручная проверка.");
    const auto config = LoadCloudSyncConfig(workspaceDirectory);
    const auto configuredRoot = fs::weakly_canonical(ResolveCloudRootPath(config, workspaceDirectory));
    const auto journalRoot = fs::weakly_canonical(p(object["cloudRoot"].toString()));
    auto configuredText = q(configuredRoot), journalText = q(journalRoot);
#ifdef _WIN32
    configuredText = configuredText.toCaseFolded(); journalText = journalText.toCaseFolded();
#endif
    require(configuredText == journalText && !overlaps(journalRoot, workspaceDirectory),
            u8"Папка облака изменилась после прерывания; восстановление остановлено.");
    const auto backupName = object["backup"].toString();
    require(QRegularExpression("^qt-cloud-push-backup-[0-9a-f-]{36}$").match(backupName).hasMatch(),
            u8"Некорректный путь снимка cloud push.");
    const auto backup = workspaceDirectory.parent_path() / p(backupName);
    checkPath(backup);
    require(fs::is_directory(backup) && !overlaps(backup, journalRoot) && !overlaps(backup, workspaceDirectory),
            u8"Снимок cloud push недоступен или находится в опасном расположении.");
    const auto entries = object["files"].toArray();
    require(!entries.isEmpty(), u8"Пустой журнал cloud push.");
    struct RecoveryEntry { fs::path relative; bool beforeExists; QByteArray beforeHash; bool afterExists; QByteArray afterHash; };
    std::vector<RecoveryEntry> validated;
    std::set<QString> seen;
    std::set<QString> seenDirectories;
    std::vector<fs::path> createdDirectories;
    for (const auto& value : object["createdDirectories"].toArray()) {
        const auto name = value.toString();
        require(value.isString() && safeRelative(name) && seenDirectories.insert(name.toCaseFolded()).second,
                u8"Некорректный созданный каталог в журнале cloud push.");
        const auto relative = p(name);
        const auto directory = journalRoot / relative;
        checkPath(directory);
        require(!fs::exists(directory) || fs::is_directory(directory),
                u8"На месте созданного cloud-каталога обнаружен файл.");
        createdDirectories.push_back(relative);
    }
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        const auto name = entry["path"].toString();
        require(entry["path"].isString() && safeRelative(name) && seen.insert(name.toCaseFolded()).second &&
                entry["beforeExists"].isBool() && entry["afterExists"].isBool(),
                u8"Некорректная запись журнала cloud push.");
        const bool beforeExists = entry["beforeExists"].toBool(), afterExists = entry["afterExists"].toBool();
        const auto beforeHash = entry["beforeHash"].toString().toLatin1();
        const auto afterHash = entry["afterHash"].toString().toLatin1();
        const auto hashValid = [](const QByteArray& value) { return QRegularExpression("^[0-9a-f]{64}$").match(QString::fromLatin1(value)).hasMatch(); };
        require((!beforeExists || hashValid(beforeHash)) && (!afterExists || hashValid(afterHash)) &&
                (beforeExists || afterExists), u8"Некорректная контрольная сумма журнала cloud push.");
        const auto relative = p(name);
        const auto target = journalRoot / relative;
        const auto saved = backup / relative;
        checkPath(target);
        if (beforeExists) {
            checkPath(saved);
            require(fs::is_regular_file(saved) && hash(readFile(saved)) == beforeHash,
                    u8"Резервная копия cloud push повреждена.");
        }
        require(matches(target, beforeExists, beforeHash) || matches(target, afterExists, afterHash),
                u8"Облачный файл изменён посторонним процессом; откат остановлен во избежание потери данных.");
        validated.push_back({relative, beforeExists, beforeHash, afterExists, afterHash});
    }
    for (const auto& entry : validated) {
        const auto target = journalRoot / entry.relative;
        if (matches(target, entry.beforeExists, entry.beforeHash)) continue;
        if (entry.beforeExists) writeFile(target, readFile(backup / entry.relative));
        else require(fs::remove(target), u8"Не удалось удалить добавленный cloud-файл при откате.");
    }
    std::sort(createdDirectories.begin(), createdDirectories.end(), [](const auto& a, const auto& b) {
        return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
    });
    for (const auto& relative : createdDirectories) {
        const auto directory = journalRoot / relative;
        checkPath(directory);
        if (fs::is_directory(directory) && fs::is_empty(directory)) {
            std::error_code ignored;
            fs::remove(directory, ignored);
        }
    }
    require(fs::remove(journal), u8"Не удалось закрыть журнал отката cloud push.");
    return true;
}

QtCloudPushPreviewResult RunQtCloudWorkspacePush(const CloudSyncConfig& config,
    const fs::path& workspaceDirectory, CloudRole role, const QtCloudPushPreviewResult* approvedPreview) {
    QtCloudPushPreviewResult result;
    fs::path backup;
    bool journalCreated = false;
    try {
        require(role == CloudRole::Admin, u8"Выгрузка в облако доступна только администратору.");
        require(!HasQtStorageConflict(workspaceDirectory), u8"Конфликт storage.json нужно разрешить до полной выгрузки.");
        checkPath(workspaceDirectory / journalName);
        require(!fs::exists(workspaceDirectory / journalName) &&
                !fs::exists(workspaceDirectory / "meta/qt-cloud-pull.json") &&
                !fs::exists(workspaceDirectory / "meta/qt-xp-transaction"),
                u8"Сначала восстановите незавершённую операцию и повторите выгрузку.");
        result = PreviewQtCloudWorkspacePush(config, workspaceDirectory, role);
        require(result.sync.ok, result.message.c_str());
        if (approvedPreview) {
            bool same = approvedPreview->sync.ok && approvedPreview->cloudRoot == result.cloudRoot &&
                approvedPreview->changes.size() == result.changes.size();
            for (size_t index = 0; same && index < result.changes.size(); ++index) {
                const auto& approved = approvedPreview->changes[index];
                const auto& current = result.changes[index];
                same = approved.relativePath == current.relativePath &&
                    approved.existedBefore == current.existedBefore && approved.existsAfter == current.existsAfter &&
                    hash(approved.beforeBytes) == hash(current.beforeBytes) && hash(approved.afterBytes) == hash(current.afterBytes);
            }
            require(same, u8"Облако или локальная версия изменились после предпросмотра; повторите проверку.");
        }
        if (result.changes.empty()) return result;
        const auto parent = workspaceDirectory.parent_path();
        backup = parent / fs::u8path("qt-cloud-push-backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        require(fs::create_directory(backup), u8"Не удалось создать резервную копию перед cloud push.");
        QJsonArray files;
        std::set<fs::path> missingDirectories;
        for (const auto& change : result.changes) {
            const auto relative = fs::u8path(change.relativePath);
            const auto target = result.cloudRoot / relative;
            require(safeRelative(QString::fromUtf8(change.relativePath)) &&
                    matches(target, change.existedBefore, change.existedBefore ? hash(change.beforeBytes) : QByteArray()),
                    u8"Облачная папка изменилась после предпросмотра; повторите операцию.");
            if (change.existsAfter) {
                fs::path directory;
                for (const auto& part : relative.parent_path()) {
                    directory /= part;
                    const auto full = result.cloudRoot / directory;
                    checkPath(full);
                    if (fs::exists(full)) {
                        require(fs::is_directory(full), u8"Файл блокирует создание папки для cloud push.");
                        break;
                    }
                    missingDirectories.insert(directory);
                }
            }
            if (change.existedBefore) writeFile(backup / relative, change.beforeBytes);
            files.append(QJsonObject{{"path", QString::fromUtf8(change.relativePath)},
                {"beforeExists", change.existedBefore}, {"beforeHash", QString::fromLatin1(hash(change.beforeBytes))},
                {"afterExists", change.existsAfter},
                {"afterHash", QString::fromLatin1(hash(change.afterBytes))}});
        }
        QJsonArray directories;
        for (const auto& directory : missingDirectories)
            directories.append(QString::fromUtf8(directory.generic_u8string()));
        QJsonObject journal{{"version", 1}, {"cloudRoot", q(result.cloudRoot)},
            {"backup", q(backup.filename())}, {"files", files}, {"createdDirectories", directories}};
        writeJournal(workspaceDirectory / journalName, QJsonDocument(journal).toJson(QJsonDocument::Compact));
        journalCreated = true;
        int written = 0;
        for (const auto& change : result.changes) {
            const auto target = result.cloudRoot / fs::u8path(change.relativePath);
            require(matches(target, change.existedBefore, change.existedBefore ? hash(change.beforeBytes) : QByteArray()),
                    u8"Облачная папка изменилась во время выгрузки; операция отменена.");
            if (change.existsAfter) writeFile(target, change.afterBytes);
            else require(fs::remove(target), u8"Не удалось удалить лишний cloud-файл.");
            ++written;
            if (failureAfterWritesForTests > 0 && written >= failureAfterWritesForTests)
                throw std::runtime_error("test: injected cloud push interruption");
        }
        require(fs::remove(workspaceDirectory / journalName), u8"Не удалось завершить журнал cloud push.");
        journalCreated = false;
        result.backupPath = backup;
        result.message = u8"Выгрузка выполнена. Создан снимок старой cloud-версии: " + backup.u8string();
    } catch (const std::exception& error) {
        result.sync.ok = false; result.sync.changed = false; result.message = error.what();
        if (journalCreated) {
            if (!leaveJournalForTests) {
                try { result.sync.changed = false; RecoverQtCloudPush(workspaceDirectory); result.message += u8" Облачные файлы восстановлены из снимка."; }
                catch (const std::exception& recovery) { result.message += std::string(u8" Требуется восстановление при запуске: ") + recovery.what(); }
            } else {
                result.message += u8" Тест оставил журнал для проверки восстановления при запуске.";
            }
        } else if (!backup.empty()) {
            std::error_code ignored; fs::remove_all(backup, ignored);
        }
    }
    return result;
}

void QtSetCloudPushFailureAfterFileWritesForTests(int count) { failureAfterWritesForTests = count; }
void QtSetCloudPushLeaveJournalForTests(bool enabled) { leaveJournalForTests = enabled; }
