#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppUtils.h"

struct AppBannerMutationResult {
    bool ok = false;
    bool changed = false;
    int itemIndex = -1;
    std::string errorMessage;
};

struct AppVaultMutationResult {
    bool ok = false;
    bool changed = false;
    std::string errorMessage;
};

AppBannerMutationResult AppAddBannerText(const std::filesystem::path& storageDir,
                                         std::vector<std::string>& texts,
                                         const std::string& text);

AppBannerMutationResult AppUpdateBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index,
                                            const std::string& text);

AppBannerMutationResult AppDeleteBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index);

AppVaultMutationResult AppApplyVaultDraft(const std::filesystem::path& storageDir,
                                          StorageVaultData& vault,
                                          const std::string& currencyName,
                                          const std::string& currencyCode,
                                          int logLimit,
                                          int pomodoroStartMinutes,
                                          int pomodoroEndMinutes,
                                          int pomodoroMinMinutes,
                                          int pomodoroCoinsPerCycle,
                                          int pomodoroDaysMask);
