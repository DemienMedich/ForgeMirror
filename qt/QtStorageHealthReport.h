#pragma once

#include <QString>
#include <cstdint>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ModuleToggles;

struct QtStorageStrayEntry {
    std::string relativePath;
    bool directory = false;
    bool reparsePoint = false;
    std::uint64_t size = 0;
    std::int64_t modifiedTicks = 0;
    std::string sha256;
    std::string linkTarget;
};

bool BuildQtStorageHealthReport(const std::filesystem::path& storageDir, const ModuleToggles& modules,
    std::int64_t now, QString* report, QString* error = nullptr);
bool ExportQtStorageHealthReport(const QString& path, const QString& report, QString* error = nullptr);
bool BuildQtStorageStrayInventory(const std::filesystem::path& storageDir,
    std::vector<QtStorageStrayEntry>* entries, QString* error = nullptr);
bool RemoveQtStorageStrayEntries(const std::filesystem::path& storageDir,
    const std::vector<QtStorageStrayEntry>& expectedInventory,
    const std::vector<std::string>& approvedPaths, int* removedCount, QString* error = nullptr);
