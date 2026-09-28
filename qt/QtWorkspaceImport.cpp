#include "QtWorkspaceImport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <map>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;

bool isReparseOrSymlink(const fs::path& path, std::error_code& ec) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    const auto status = fs::symlink_status(path, ec);
    return !ec && fs::is_symlink(status);
}

struct SnapshotEntry {
    fs::path relativePath;
    bool directory = false;
    QByteArray digest;
    std::uintmax_t size = 0;
};
using Snapshot = std::map<std::string, SnapshotEntry>;

bool fingerprintFile(const fs::path& path, QByteArray& digest, std::uintmax_t& size, QString& error) {
    std::error_code ec;
    if (isReparseOrSymlink(path, ec) || ec) {
        error = QString::fromUtf8("Источник импорта содержит изменившийся или недоступный путь.");
        return false;
    }
    const auto before = fs::file_size(path, ec);
    if (ec) {
        error = QString::fromUtf8("Не удалось проверить файл источника импорта.");
        return false;
    }
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString::fromUtf8("Не удалось прочитать файл источника импорта.");
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError) {
        error = QString::fromUtf8("Не удалось проверить содержимое файла источника.");
        return false;
    }
    const auto after = fs::file_size(path, ec);
    if (ec || before != after || static_cast<std::uintmax_t>(file.size()) != before) {
        error = QString::fromUtf8("Файл источника менялся во время проверки импорта.");
        return false;
    }
    digest = hash.result().toHex();
    size = before;
    return true;
}

bool collectSnapshot(const fs::path& root, Snapshot& snapshot, QString& error) {
    std::error_code ec;
    const auto rootStatus = fs::symlink_status(root, ec);
    if (ec || fs::is_symlink(rootStatus) || !fs::is_directory(rootStatus) || isReparseOrSymlink(root, ec) || ec) {
        error = QString::fromUtf8("Источник импорта отсутствует, недоступен или является ссылкой.");
        return false;
    }
    fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
    if (ec) {
        error = QString::fromUtf8("Не удалось перечислить источник импорта.");
        return false;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            error = QString::fromUtf8("Источник импорта изменился или недоступен при перечислении.");
            return false;
        }
        const auto path = it->path();
        const bool linked = isReparseOrSymlink(path, ec);
        if (ec) {
            error = QString::fromUtf8("Не удалось проверить ссылку в источнике импорта.");
            return false;
        }
        if (linked) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            ec.clear();
            continue;
        }
        const auto status = it->status(ec);
        if (ec) {
            error = QString::fromUtf8("Не удалось проверить элемент источника импорта.");
            return false;
        }
        const auto relative = path.lexically_relative(root);
        const auto key = relative.generic_u8string();
        SnapshotEntry entry;
        entry.relativePath = relative;
        if (fs::is_directory(status)) {
            entry.directory = true;
        } else if (fs::is_regular_file(status)) {
            if (!fingerprintFile(path, entry.digest, entry.size, error)) return false;
        } else {
            continue;
        }
        snapshot.emplace(key, std::move(entry));
    }
    if (ec) {
        error = QString::fromUtf8("Источник импорта изменился или недоступен при перечислении.");
        return false;
    }
    return true;
}

bool sameSnapshot(const Snapshot& left, const Snapshot& right) {
    if (left.size() != right.size()) return false;
    auto a = left.begin();
    auto b = right.begin();
    for (; a != left.end(); ++a, ++b) {
        if (a->first != b->first || a->second.directory != b->second.directory ||
            a->second.digest != b->second.digest || a->second.size != b->second.size) return false;
    }
    return true;
}

bool rootsOverlap(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    const auto src = fs::weakly_canonical(source, ec);
    if (ec) return true;
    const auto dst = fs::weakly_canonical(destination, ec);
    if (ec) return true;
    auto s = QDir::fromNativeSeparators(QString::fromStdWString(src.wstring())).toCaseFolded();
    auto d = QDir::fromNativeSeparators(QString::fromStdWString(dst.wstring())).toCaseFolded();
    auto isWithin = [](const QString& child, const QString& parent) {
        return child == parent || (child.size() > parent.size() && child.startsWith(parent) &&
            (parent.endsWith('/') || child.at(parent.size()) == '/'));
    };
    return isWithin(s, d) || isWithin(d, s);
}
}

bool ImportQtWorkspaceSnapshot(const std::filesystem::path& source,
                               const std::filesystem::path& destination,
                               QString* error) {
    QString failure;
    auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    try {
        std::error_code ec;
        if (fs::exists(destination, ec) || ec || rootsOverlap(source, destination))
            return fail(QString::fromUtf8("Импорт отменён: папки источника и Qt-хранилища должны быть раздельными, а назначение — отсутствовать."));
        Snapshot before;
        if (!collectSnapshot(source, before, failure)) return fail(failure);
        const auto staging = destination.parent_path() / ("import-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        bool stagingCreated = false;
        auto cleanup = [&] {
            if (stagingCreated) {
                std::error_code cleanupError;
                fs::remove_all(staging, cleanupError);
            }
        };
        try {
            if (!fs::create_directory(staging, ec) || ec)
                return fail(QString::fromUtf8("Не удалось создать временную папку импорта."));
            stagingCreated = true;
            for (const auto& [key, entry] : before) {
                (void)key;
                const auto sourcePath = source / entry.relativePath;
                const auto targetPath = staging / entry.relativePath;
                if (entry.directory) {
                    fs::create_directories(targetPath, ec);
                    if (ec) throw std::runtime_error("create directory failed");
                } else {
                    fs::create_directories(targetPath.parent_path(), ec);
                    if (ec) throw std::runtime_error("create parent failed");
                    if (isReparseOrSymlink(sourcePath, ec) || ec) throw std::runtime_error("source path changed");
                    fs::copy_file(sourcePath, targetPath, fs::copy_options::none, ec);
                    if (ec) throw std::runtime_error("copy file failed");
                }
            }
            Snapshot afterSource, staged;
            if (!collectSnapshot(source, afterSource, failure) || !collectSnapshot(staging, staged, failure))
                throw std::runtime_error("snapshot check failed");
            if (!sameSnapshot(before, afterSource) || !sameSnapshot(before, staged)) {
                failure = QString::fromUtf8("Источник изменился во время копирования. Импорт не применён; повторите его после закрытия стабильной версии.");
                throw std::runtime_error("snapshot mismatch");
            }
            fs::rename(staging, destination, ec);
            if (ec) throw std::runtime_error("publish failed");
            stagingCreated = false;
            if (error) error->clear();
            return true;
        } catch (...) {
            cleanup();
            if (failure.isEmpty()) failure = QString::fromUtf8("Копирование не завершено. Qt-хранилище не создано.");
            return fail(failure);
        }
    } catch (...) {
        return fail(QString::fromUtf8("Импорт не выполнен. Исходные данные не изменены."));
    }
}
