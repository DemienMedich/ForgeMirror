#pragma once
#include <filesystem>
#include <string>

class QWidget;

struct QtStorageConflictResult {
    bool ok = false;
    bool changed = false;
    std::string message;
};

bool HasQtStorageConflict(const std::filesystem::path& workspaceDirectory);
QtStorageConflictResult ResolveQtStorageConflict(const std::filesystem::path& workspaceDirectory,
                                                 bool preferCloud);
bool ShowQtStorageConflictResolver(QWidget* parent, const std::filesystem::path& workspaceDirectory,
                                   bool* localChanged = nullptr);
