#include "QtCloudPushPreview.h"
#include <QtCore>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
QString q(const fs::path& value) { return QString::fromStdWString(value.wstring()); }
fs::path p(const QString& value) { return fs::u8path(value.toUtf8().toStdString()); }
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
}

QtCloudPushPreviewResult PreviewQtCloudWorkspacePush(const CloudSyncConfig& config,
    const fs::path& workspaceDirectory, CloudRole role) {
    QtCloudPushPreviewResult result;
    try {
        require(config.enabled, u8"Облако отключено.");
        require(role == CloudRole::Admin, u8"Предпросмотр выгрузки доступен только администратору.");
        const auto cloudRoot = ResolveCloudRootPath(config, workspaceDirectory);
        checkPath(workspaceDirectory); checkPath(cloudRoot);
        require(!overlaps(cloudRoot, workspaceDirectory), u8"Папка облака пересекается с рабочим пространством.");
        const auto before = snapshot(cloudRoot);
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
        const auto after = snapshot(stagedRoot);
        for (const auto& [relative, bytes] : after) {
            const auto found = before.find(relative);
            if (found == before.end()) ++result.filesAdded;
            else if (found->second != bytes) ++result.filesReplaced;
        }
        for (const auto& [relative, bytes] : before) {
            Q_UNUSED(bytes);
            if (!after.count(relative)) ++result.filesRemoved;
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
