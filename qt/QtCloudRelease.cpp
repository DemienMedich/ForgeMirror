#include "QtCloudRelease.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
QString q(const fs::path& path) { return QString::fromUtf8(path.u8string()); }

bool safeReleaseName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find_first_of("/\\:<>|\"*?") != std::string::npos) return false;
    if (std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32 || ch == 127; })) return false;
    const auto extension = QFileInfo(QString::fromUtf8(name.data(), int(name.size()))).suffix().toLower();
    if (name.back() == ' ' || name.back() == '.') return false;
    const auto base = QFileInfo(QString::fromUtf8(name.data(), int(name.size()))).completeBaseName().toUpper();
    const auto deviceStem = base.section('.', 0, 0).trimmed();
    if (deviceStem == "CON" || deviceStem == "PRN" || deviceStem == "AUX" || deviceStem == "NUL" ||
        QRegularExpression("^(COM|LPT)[1-9]$").match(deviceStem).hasMatch()) return false;
    return extension == QStringLiteral("exe") || extension == QStringLiteral("msi");
}

void checkNoReparseOrSymlink(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path)) {
        current /= part;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Путь обновления содержит ссылку или junction.");
#else
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(current, ec))) throw std::runtime_error("Путь обновления содержит symbolic link.");
#endif
    }
}

bool overlaps(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    const auto left = fs::weakly_canonical(a, ec); if (ec) return true;
    const auto right = fs::weakly_canonical(b, ec); if (ec) return true;
    auto x = QString::fromUtf8(left.u8string()).replace('\\', '/');
    auto y = QString::fromUtf8(right.u8string()).replace('\\', '/');
#ifdef _WIN32
    x = x.toCaseFolded(); y = y.toCaseFolded();
#endif
    if (!x.endsWith('/')) x += '/';
    if (!y.endsWith('/')) y += '/';
    return x.startsWith(y) || y.startsWith(x);
}

QByteArray readBounded(const fs::path& path) {
    QFile file(q(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 256LL * 1024 * 1024)
        throw std::runtime_error("Файл установщика пуст, недоступен или больше 256 МиБ.");
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || bytes.size() != file.size())
        throw std::runtime_error("Не удалось полностью прочитать установщик.");
    return bytes;
}
}

std::optional<std::filesystem::path> QtCloudReleaseTargetPath(
    const std::filesystem::path& workspace, const CloudManifest& manifest) {
    if (!safeReleaseName(manifest.releaseFile)) return {};
    return workspace / "meta/updates" / fs::u8path(manifest.releaseFile);
}

QtCloudReleaseResult DownloadQtCloudRelease(const CloudSyncConfig& config,
    const std::filesystem::path& workspace, const CloudManifest& manifest) {
    QtCloudReleaseResult result;
    try {
        if (!config.enabled) throw std::runtime_error("Облако отключено.");
        const auto target = QtCloudReleaseTargetPath(workspace, manifest);
        if (!target) throw std::runtime_error("Manifest должен указывать имя EXE или MSI без каталогов.");
        const auto cloud = ResolveCloudRootPath(config, workspace);
        if (overlaps(cloud, workspace)) throw std::runtime_error("Путь облака пересекается с рабочей папкой.");
        auto releases = config.releasesDir.empty() ? fs::path("releases") : config.releasesDir;
        if (!releases.is_absolute()) releases = cloud / releases;
        checkNoReparseOrSymlink(cloud);
        checkNoReparseOrSymlink(releases);
        const auto source = releases / fs::u8path(manifest.releaseFile);
        checkNoReparseOrSymlink(source);
        if (!fs::is_regular_file(source)) throw std::runtime_error("Файл обновления не найден в облаке.");
        const auto beforeSize = fs::file_size(source);
        const auto beforeTime = fs::last_write_time(source);
        const auto bytes = readBounded(source);
        if (fs::file_size(source) != beforeSize || fs::last_write_time(source) != beforeTime)
            throw std::runtime_error("Файл обновления изменился во время чтения.");

        const auto destinationDirectory = workspace / "meta/updates";
        checkNoReparseOrSymlink(workspace);
        checkNoReparseOrSymlink(destinationDirectory);
        fs::create_directories(destinationDirectory);
        checkNoReparseOrSymlink(destinationDirectory);
        QSaveFile output(q(*target));
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
            throw std::runtime_error("Не удалось атомарно сохранить установщик.");
        if (readBounded(*target) != bytes) throw std::runtime_error("Проверка сохранённого установщика не прошла.");
        result.ok = result.changed = true;
        result.path = *target;
        result.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
        result.message = u8"Установщик загружен; SHA-256 локальной копии: " + result.sha256;
    } catch (const std::exception& error) {
        result.message = error.what();
    }
    return result;
}

