#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "AppContext.h"
#include "Profile.h"

struct StorageVaultData;

struct AppProfileActionResult {
    bool ok = false;
    bool userError = false;
    bool changed = false;
    std::string errorMessage;
};

struct AppProfileCreateResult : AppProfileActionResult {
    std::string profileId;
    std::string login;
    std::string password;
};

struct AppProfileMutationResult {
    bool ok = false;
    bool userError = false;
    bool changed = false;
    int affectedProfiles = 0;
    int awardedGlobalXp = 0;
    int awardedSkillXp = 0;
    std::optional<Profile> profile;
    std::string errorMessage;
};

AppProfileCreateResult AppCreateProfile(IJobStorage& storage,
                                        SkillCatalog& catalog,
                                        const std::string& name);

AppProfileActionResult AppSetProfileArchived(IJobStorage& storage,
                                             const std::string& profileId,
                                             bool archived);

AppProfileActionResult AppDeleteProfile(IJobStorage& storage,
                                        const std::string& profileId);

AppProfileMutationResult AppSetProfileBlocked(IJobStorage& storage,
                                              const std::string& restoreProfileId,
                                              const std::string& profileId,
                                              bool blocked);

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

AppProfileMutationResult AppGrantDirectSkillXp(IJobStorage& storage,
                                               SkillCatalog& catalog,
                                               const std::string& restoreProfileId,
                                               const std::string& profileId,
                                               const std::string& skillId,
                                               int amount,
                                               std::int64_t nowSec);

AppProfileMutationResult AppRemoveEvilSpiritForCoins(IJobStorage& storage,
                                                     const std::string& restoreProfileId,
                                                     const std::string& profileId,
                                                     const std::filesystem::path& storageDir,
                                                     StorageVaultData& vault,
                                                     double cost);

AppProfileMutationResult AppGrantAchievement(IJobStorage& storage,
                                             const std::string& restoreProfileId,
                                             const std::string& profileId,
                                             const std::string& title,
                                             const std::string& skillId,
                                             double bonusPercent,
                                             const std::string& icon,
                                             std::int64_t nowSec,
                                             int durationDays);

AppProfileMutationResult AppUpdateAchievement(IJobStorage& storage,
                                              const std::string& restoreProfileId,
                                              const std::string& profileId,
                                              int index,
                                              const std::string& title,
                                              double bonusPercent,
                                              const std::string& icon,
                                              std::int64_t nowSec,
                                              int durationDays);

AppProfileMutationResult AppDeleteAchievement(IJobStorage& storage,
                                              const std::string& restoreProfileId,
                                              const std::string& profileId,
                                              int index);

AppProfileMutationResult AppReapplyRulesToProfiles(AppContext& app,
                                                   const std::string& restoreProfileId);
