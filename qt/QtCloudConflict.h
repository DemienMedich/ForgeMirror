#pragma once
#include <filesystem>
#include <string>
#include <vector>

class QWidget;

struct QtCloudConflictResult {
    bool ok = false;
    bool changed = false;
    std::string message;
    std::filesystem::path backupPath;
    std::vector<std::filesystem::path> backupPaths;
};

QtCloudConflictResult ApplyQtCloudWorkspaceFile(
    const std::filesystem::path& workspaceDirectory,
    const std::filesystem::path& sourcePath,
    const std::string& relativePath,
    const std::string& sourceKind);

QtCloudConflictResult PushQtCloudWorkspaceFile(
    const std::filesystem::path& workspaceDirectory,
    const std::string& relativePath);

QtCloudConflictResult ApplyQtCloudCatalogPair(const std::filesystem::path& workspaceDirectory,
                                              const std::filesystem::path& cloudRoot);
QtCloudConflictResult PushQtCloudCatalogPair(const std::filesystem::path& workspaceDirectory);
QtCloudConflictResult RestoreQtCloudCatalogPair(const std::filesystem::path& workspaceDirectory,
                                                 const std::filesystem::path& skillsBackup,
                                                 const std::filesystem::path& professionsBackup);

bool ShowCloudConflictResolver(QWidget* parent, const std::filesystem::path& workspaceDirectory);
