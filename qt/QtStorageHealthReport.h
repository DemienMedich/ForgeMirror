#pragma once

#include <QString>
#include <cstdint>
#include <filesystem>

struct ModuleToggles;

bool BuildQtStorageHealthReport(const std::filesystem::path& storageDir, const ModuleToggles& modules,
    std::int64_t now, QString* report, QString* error = nullptr);
bool ExportQtStorageHealthReport(const QString& path, const QString& report, QString* error = nullptr);
