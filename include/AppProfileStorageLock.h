#pragma once

#include <filesystem>

// Coordinates profile and achievement sidecar writes across cooperating clients.
class AppProfileStorageWriteLock {
public:
    explicit AppProfileStorageWriteLock(const std::filesystem::path& baseDir);
    ~AppProfileStorageWriteLock();

    AppProfileStorageWriteLock(const AppProfileStorageWriteLock&) = delete;
    AppProfileStorageWriteLock& operator=(const AppProfileStorageWriteLock&) = delete;

    bool acquired() const;

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#elif defined(__unix__) || defined(__APPLE__)
    int descriptor_ = -1;
#endif
};
