#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppDomainTypes.h"

struct AppShortcutMutationResult {
    bool ok = false;
    bool changed = false;
    int itemIndex = -1;
    std::string errorMessage;
};

bool AppSaveShortcutsData(const std::filesystem::path& storageDir,
                          const std::vector<ShortcutEntry>& shortcuts);

AppShortcutMutationResult AppAddShortcut(const std::filesystem::path& storageDir,
                                         std::vector<ShortcutEntry>& shortcuts,
                                         const std::string& label,
                                         const std::string& path);

AppShortcutMutationResult AppDeleteShortcut(const std::filesystem::path& storageDir,
                                            std::vector<ShortcutEntry>& shortcuts,
                                            int index);

AppShortcutMutationResult AppMoveShortcut(const std::filesystem::path& storageDir,
                                          std::vector<ShortcutEntry>& shortcuts,
                                          int fromIndex,
                                          int toIndex);
