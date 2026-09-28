#include "AppProfileStorageLock.h"

#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

AppProfileStorageWriteLock::AppProfileStorageWriteLock(const std::filesystem::path& baseDir) {
    const auto metaDir = baseDir / "meta";
    const auto lockPath = metaDir / "profile-write.lock";
    std::error_code ec;
    std::filesystem::create_directories(metaDir, ec);
    if (ec) return;
    const auto metaStatus = std::filesystem::symlink_status(metaDir, ec);
    if (ec || std::filesystem::is_symlink(metaStatus) || !std::filesystem::is_directory(metaStatus)) return;
#ifdef _WIN32
    HANDLE handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(handle);
        return;
    }
    handle_ = handle;
#elif defined(__unix__) || defined(__APPLE__)
    descriptor_ = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (descriptor_ < 0) return;
    struct stat info{};
    if (::fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode) ||
        ::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
#endif
}

AppProfileStorageWriteLock::~AppProfileStorageWriteLock() {
#ifdef _WIN32
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#elif defined(__unix__) || defined(__APPLE__)
    if (descriptor_ >= 0) {
        ::flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
#endif
}

bool AppProfileStorageWriteLock::acquired() const {
#ifdef _WIN32
    return handle_ != nullptr;
#elif defined(__unix__) || defined(__APPLE__)
    return descriptor_ >= 0;
#else
    return false;
#endif
}
