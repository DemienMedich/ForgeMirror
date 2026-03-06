#pragma once

#include <optional>
#include <string>
#include <vector>

#include "AppContext.h"
#include "IJobStorage.h"
#include "Profile.h"

struct ActiveProfileLoadResult {
    bool ok = false;
    bool hasSelection = false;
    std::string profileId;
    std::optional<Profile> profile;
    std::string errorMessage;
};

struct ProfilesReloadResult {
    std::vector<IJobStorage::ProfileInfo> profiles;
    int selectedIndex = -1;
    ActiveProfileLoadResult active;
    std::string errorMessage;
};

std::vector<IJobStorage::ProfileInfo> LoadSortedProfiles(IJobStorage& storage);
ActiveProfileLoadResult LoadActiveProfile(AppContext& app,
                                          const std::vector<IJobStorage::ProfileInfo>& profiles,
                                          int selectedIndex);
ProfilesReloadResult ReloadProfiles(AppContext& app,
                                    const std::vector<IJobStorage::ProfileInfo>& currentProfiles,
                                    int currentSelectedIndex,
                                    const std::string& preferredId = {});
