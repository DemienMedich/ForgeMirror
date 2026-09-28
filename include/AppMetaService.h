#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "AppDomainTypes.h"
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

using AppMetaEventLogger = std::function<void(AppLogLevel, const std::string&)>;

AppBannerMutationResult AppAddBannerText(const std::filesystem::path& storageDir,
                                         std::vector<std::string>& texts,
                                         const std::string& text,
                                         AppMetaEventLogger eventLogger = {});

AppBannerMutationResult AppUpdateBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index,
                                            const std::string& text,
                                            AppMetaEventLogger eventLogger = {});

AppBannerMutationResult AppDeleteBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index,
                                            AppMetaEventLogger eventLogger = {});

AppVaultMutationResult AppApplyVaultDraft(const std::filesystem::path& storageDir,
                                          StorageVaultData& vault,
                                          const std::string& currencyName,
                                          const std::string& currencyCode,
                                          int logLimit,
                                          int pomodoroStartMinutes,
                                          int pomodoroEndMinutes,
                                          int pomodoroMinMinutes,
                                          int pomodoroCoinsPerCycle,
                                          int pomodoroDaysMask,
                                          AppMetaEventLogger eventLogger = {});
