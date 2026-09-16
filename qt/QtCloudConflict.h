#pragma once
#include <filesystem>
#include <string>

class QWidget;

struct QtCloudConflictResult {
    bool ok = false;
    bool changed = false;
    std::string message;
    std::filesystem::path backupPath;
};

QtCloudConflictResult ApplyQtCloudWorkspaceFile(
    const std::filesystem::path& workspaceDirectory,
    const std::filesystem::path& sourcePath,
    const std::string& relativePath,
    const std::string& sourceKind);

bool ShowCloudConflictResolver(QWidget* parent, const std::filesystem::path& workspaceDirectory);
