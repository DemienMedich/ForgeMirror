#pragma once

#include <optional>
#include <string>

#include "AppContext.h"
#include "Profile.h"

struct AppProfileMutationResult {
    bool ok = false;
    bool changed = false;
    int affectedProfiles = 0;
    std::optional<Profile> profile;
    std::string errorMessage;
};

AppProfileMutationResult AppSaveProfileSnapshot(IJobStorage& storage,
                                                const std::string& restoreProfileId,
                                                const std::string& profileId,
                                                const Profile& profile);

AppProfileMutationResult AppChangeProfilePassword(IJobStorage& storage,
                                                  const std::string& restoreProfileId,
                                                  const std::string& profileId,
                                                  const std::string& currentPassword,
                                                  const std::string& newPassword,
                                                  bool requireCurrentPassword);

AppProfileMutationResult AppResetProfilePassword(IJobStorage& storage,
                                                 const std::string& restoreProfileId,
                                                 const std::string& profileId,
                                                 const std::string& newPassword);

AppProfileMutationResult AppAssignProfileLevel(IJobStorage& storage,
                                               const std::string& restoreProfileId,
                                               const std::string& profileId,
                                               int newLevel,
                                               int newProgress = -1);

AppProfileMutationResult AppAdjustProfileWallet(IJobStorage& storage,
                                                const std::string& restoreProfileId,
                                                const std::string& profileId,
                                                double delta);

AppProfileMutationResult AppReapplyRulesToProfiles(AppContext& app,
                                                   const std::string& restoreProfileId);