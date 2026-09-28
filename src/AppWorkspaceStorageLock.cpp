#include "AppWorkspaceStorageLock.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <unordered_map>

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

namespace {
struct NativeLock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#elif defined(__unix__) || defined(__APPLE__)
    int descriptor = -1;
#endif
    unsigned depth = 0;
};

thread_local std::unordered_map<std::string, NativeLock> heldLocks;

std::string lockKey(const std::filesystem::path& baseDir) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(baseDir, ec);
    if (ec) absolute = baseDir;
    auto key = absolute.lexically_normal().u8string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return key;
}

void release(const std::string& key) {
    const auto found = heldLocks.find(key);
    if (found == heldLocks.end()) return;
    auto& lock = found->second;
    if (lock.depth > 1) {
        --lock.depth;
        return;
    }
#ifdef _WIN32
    if (lock.handle != INVALID_HANDLE_VALUE) CloseHandle(lock.handle);
#elif defined(__unix__) || defined(__APPLE__)
    if (lock.descriptor >= 0) {
        ::flock(lock.descriptor, LOCK_UN);
        ::close(lock.descriptor);
    }
#endif
    heldLocks.erase(found);
}
} // namespace

AppWorkspaceStorageWriteLock::AppWorkspaceStorageWriteLock(const std::filesystem::path& baseDir) {
    key_ = lockKey(baseDir);
    const auto held = heldLocks.find(key_);
    if (held != heldLocks.end()) {
        ++held->second.depth;
        acquired_ = true;
        return;
    }
    const auto metaDir = baseDir / "meta";
    const auto lockPath = metaDir / "workspace-write.lock";
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
    heldLocks.emplace(key_, NativeLock{handle, 1});
    acquired_ = true;
#elif defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (descriptor < 0) return;
    struct stat info{};
    if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        ::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        ::close(descriptor);
        return;
    }
    heldLocks.emplace(key_, NativeLock{descriptor, 1});
    acquired_ = true;
#endif
}

AppWorkspaceStorageWriteLock::~AppWorkspaceStorageWriteLock() {
    if (acquired_) release(key_);
}

bool AppWorkspaceStorageWriteLock::acquired() const {
    return acquired_;
}
