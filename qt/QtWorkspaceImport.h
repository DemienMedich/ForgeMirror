#pragma once

#include <QString>
#include <filesystem>

bool ImportQtWorkspaceSnapshot(const std::filesystem::path& source,
                               const std::filesystem::path& destination,
                               QString* error = nullptr);
