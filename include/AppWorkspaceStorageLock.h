#pragma once

#include <filesystem>
#include <string>

// Coordinates file mutations across cooperating clients. Nested acquisitions on
// the same thread are reentrant so a journaled multi-file transaction can keep
// the lock while its ordinary save helpers run.
class AppWorkspaceStorageWriteLock {
public:
    explicit AppWorkspaceStorageWriteLock(const std::filesystem::path& baseDir);
    ~AppWorkspaceStorageWriteLock();

    AppWorkspaceStorageWriteLock(const AppWorkspaceStorageWriteLock&) = delete;
    AppWorkspaceStorageWriteLock& operator=(const AppWorkspaceStorageWriteLock&) = delete;

    bool acquired() const;

private:
    std::string key_;
    bool acquired_ = false;
};
