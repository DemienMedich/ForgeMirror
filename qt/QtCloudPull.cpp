#include "QtCloudPull.h"
#include <QtCore>
#include <set>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
QString q(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
fs::path p(const QString& s) { return fs::u8path(s.toUtf8().toStdString()); }
const char* journalName = "meta/qt-cloud-pull.json";
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void checkPath(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path)) {
        current /= part;
#ifdef _WIN32
        const auto attrs = GetFileAttributesW(current.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
            require(!(attrs & FILE_ATTRIBUTE_REPARSE_POINT), u8"Pull: ссылки и junction не поддерживаются.");
#else
        require(!fs::is_symlink(fs::symlink_status(current)), "Pull: symbolic link.");
#endif
    }
}
bool overlap(const fs::path& a, const fs::path& b) {
    auto x = QDir::fromNativeSeparators(q(fs::weakly_canonical(a)));
    auto y = QDir::fromNativeSeparators(q(fs::weakly_canonical(b)));
#ifdef _WIN32
    x = x.toCaseFolded(); y = y.toCaseFolded();
#endif
    if (!x.endsWith('/')) x += '/';
    if (!y.endsWith('/')) y += '/';
    return x.startsWith(y) || y.startsWith(x);
}
std::set<fs::path> inventory(const fs::path& root) {
    checkPath(root);
    require(fs::is_directory(root), u8"Папка pull недоступна.");
    std::set<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        checkPath(entry.path());
        require(entry.is_directory() || entry.is_regular_file(), u8"Pull: ожидается обычный файл.");
        if (entry.is_regular_file()) files.insert(entry.path().lexically_relative(root));
    }
    return files;
}
QByteArray read(const fs::path& file) {
    checkPath(file);
    QFile input(q(file));
    require(input.open(QIODevice::ReadOnly), u8"Не удалось прочитать файл pull.");
    auto data = input.readAll();
    require(input.error() == QFileDevice::NoError, u8"Ошибка чтения файла pull.");
    return data;
}
void write(const fs::path& file, const QByteArray& data) {
    checkPath(file);
    fs::create_directories(file.parent_path());
    QSaveFile output(q(file)); output.setDirectWriteFallback(false);
    require(output.open(QIODevice::WriteOnly) && output.write(data) == data.size() && output.commit(),
            u8"Не удалось атомарно сохранить файл pull.");
}
void copyTree(const fs::path& from, const fs::path& to) {
    for (const auto& relative : inventory(from)) {
        fs::create_directories((to / relative).parent_path());
        fs::copy_file(from / relative, to / relative);
        require(read(from / relative) == read(to / relative), u8"Проверка резервной копии не пройдена.");
    }
}
bool safeRelative(const QString& name) {
    const auto relative = p(name);
    if (name.isEmpty() || name.contains('\\') || name.contains(':') || relative.is_absolute() ||
        relative.generic_u8string() != relative.lexically_normal().generic_u8string()) return false;
    for (const auto& part : relative) if (part == "." || part == "..") return false;
    if (relative.filename().empty()) return false;
    const auto parent = relative.parent_path().generic_u8string();
    if (parent.empty()) return relative.extension() == ".ini" || name == "skills.txt";
    if (parent == "archive") return relative.extension() == ".ini";
    if (parent == "achievements") return relative.extension() == ".json";
    if (parent == "achievements/icons" || parent == "spirits" || parent == "meta/patch-notes") return true;
    static const std::set<QString> meta = {"meta/pipeline.json", "meta/tasks.json", "meta/projects.json",
        "meta/gameplay.ini", "meta/professions.txt", "meta/banner.json", "meta/storage.json",
        "meta/profile-audit.log", "meta/task-audit.log"};
    return meta.count(name) != 0;
}
QByteArray hash(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
}

bool RecoverQtCloudPull(const std::filesystem::path& workspace) {
    const auto journal = workspace / journalName;
    checkPath(journal);
    if (!fs::exists(journal)) return false;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(read(journal), &error);
    const auto obj = doc.object();
    require(error.error == QJsonParseError::NoError && obj["version"].toInt() == 1 &&
            obj["files"].isArray(), u8"Повреждён журнал pull. Рабочая папка заблокирована до восстановления.");
    const auto name = obj["backup"].toString();
    require(QRegularExpression("^qt-cloud-backup-[0-9a-f-]{36}$").match(name).hasMatch(),
            u8"Некорректный путь резервной копии pull.");
    const auto backup = workspace.parent_path() / p(name);
    checkPath(backup);
    const auto entries = obj["files"].toArray();
    require(!entries.isEmpty(), u8"Пустой журнал pull.");
    std::set<QString> seen;
    // Validate the complete journal before restoring anything.
    for (const auto& entry : entries) {
        const auto item = entry.toObject(); const auto relative = item["path"].toString();
        require(safeRelative(relative) && seen.insert(relative.toCaseFolded()).second && item["existed"].isBool(),
                u8"Некорректная запись журнала pull.");
        checkPath(workspace / p(relative)); checkPath(backup / p(relative));
        require(!fs::exists(workspace / p(relative)) || fs::is_regular_file(workspace / p(relative)),
                u8"Вместо файла pull найден каталог.");
        if (item["existed"].toBool())
            require(hash(read(backup / p(relative))) == item["hash"].toString().toLatin1(),
                    u8"Резервная копия pull повреждена.");
    }
    for (const auto& entry : entries) {
        const auto item = entry.toObject(); const auto relative = p(item["path"].toString());
        const auto target = workspace / relative;
        if (item["existed"].toBool()) {
            const auto before = read(backup / relative);
            // An unchanged sharing-locked file must not prevent other files' rollback.
            if (!fs::exists(target) || read(target) != before) write(target, before);
        } else if (fs::exists(target)) {
            require(fs::is_regular_file(target) && fs::remove(target), u8"Не удалось отменить новый файл pull.");
        }
    }
    require(fs::remove(journal), u8"Не удалось завершить восстановление pull.");
    return true;
}

QtCloudPullResult RunQtCloudPullTransaction(const CloudSyncConfig& config,
                                           const std::filesystem::path& workspace,
                                           CloudRole role) {
    QtCloudPullResult result;
    bool journalCreated = false;
    try {
        require(config.enabled, u8"Облако отключено.");
        const auto cloud = ResolveCloudRootPath(config, workspace);
        require(!overlap(cloud, workspace), u8"Путь облака пересекается с рабочей папкой.");
        checkPath(workspace / journalName);
        require(!fs::exists(workspace / journalName) && !fs::exists(workspace / "meta/qt-xp-transaction"),
                u8"Сначала перезапустите Qt для восстановления незавершённой операции.");
        inventory(cloud); const auto original = inventory(workspace);
        QTemporaryDir staged(q(workspace.parent_path() / "qt-cloud-stage-XXXXXX"));
        require(staged.isValid(), u8"Не удалось создать временную папку pull.");
        const auto stage = p(staged.path());
        copyTree(workspace, stage);
        std::map<fs::path, QByteArray> originals;
        for (const auto& relative : original) originals.emplace(relative, read(stage / relative));
        auto resolved = config; resolved.root = fs::absolute(cloud);
        result.sync = PullCloudSnapshot(resolved, stage, role);
        require(result.sync.ok, result.sync.message.c_str());
        require(!result.sync.storageConflict, u8"Конфликт storage.json: pull отменён до изменения локальных данных.");
        QJsonArray entries;
        for (const auto& relative : inventory(stage)) {
            const auto data = read(stage / relative);
            const bool existed = original.count(relative) != 0;
            const auto before = existed ? originals.at(relative) : QByteArray();
            if (existed && before == data) continue;
            const auto name = QString::fromUtf8(relative.generic_u8string());
            require(safeRelative(name), u8"Pull попытался изменить неподдерживаемый файл.");
            if (relative.extension() == ".json") {
                QJsonParseError parse;
                const auto parsed = QJsonDocument::fromJson(data, &parse);
                require(parse.error == QJsonParseError::NoError && (parsed.isObject() || parsed.isArray()),
                        u8"Облачный JSON повреждён: pull отменён.");
            }
            entries.append(QJsonObject{{"path", name}, {"existed", existed}, {"hash", QString::fromLatin1(hash(before))}});
        }
        if (entries.isEmpty()) { result.sync.changed = false; result.message = u8"Локальная и облачная копии совпадают."; return result; }
        const auto name = "qt-cloud-backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        result.backupPath = workspace.parent_path() / p(name);
        require(fs::create_directory(result.backupPath), u8"Не удалось создать резервную копию pull.");
        copyTree(workspace, result.backupPath);
        for (const auto& entry : entries) {
            const auto item = entry.toObject(); const auto target = workspace / p(item["path"].toString());
            require(fs::exists(target) == item["existed"].toBool() &&
                (!item["existed"].toBool() || hash(read(target)) == item["hash"].toString().toLatin1()),
                u8"Локальные данные изменились во время подготовки pull. Повторите операцию.");
        }
        write(workspace / journalName, QJsonDocument(QJsonObject{{"version", 1}, {"backup", name}, {"files", entries}}).toJson());
        journalCreated = true;
        for (const auto& entry : entries) {
            const auto relative = p(entry.toObject()["path"].toString());
            write(workspace / relative, read(stage / relative));
        }
        require(fs::remove(workspace / journalName), u8"Не удалось завершить журнал pull.");
        journalCreated = false;
        result.sync.changed = true;
        result.message = u8"Данные получены. Резервная копия: " + result.backupPath.u8string();
    } catch (const std::exception& error) {
        result.sync.ok = false; result.sync.changed = false;
        result.message = error.what();
        if (journalCreated) {
            try { result.rolledBack = RecoverQtCloudPull(workspace); }
            catch (const std::exception& recovery) { result.message += std::string(u8" Требуется восстановление при запуске: ") + recovery.what(); }
        }
        if (result.rolledBack) result.message += u8" Локальные изменения отменены.";
    }
    return result;
}
