#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

#include "GameplayConfig.h"

class IJobStorage;
class SkillCatalog;
class Profile;

struct ActionResult {
    bool ok = false;
    bool userError = false;
    bool changed = false;
    std::string message;
};

struct CreateProfileResult : ActionResult {
    std::string id;
    std::string login;
    std::string password;
};

struct SaveRulesResult : ActionResult {
    GameplayConfig config;
};

CreateProfileResult CreateProfileAction(IJobStorage& storage, SkillCatalog& catalog, const std::string& name);
ActionResult SetProfileArchivedAction(IJobStorage& storage, const std::string& id, bool archived);
ActionResult DeleteProfileAction(IJobStorage& storage, const std::string& id);
ActionResult SetProfileBlockedAction(IJobStorage& storage, const std::string& id, bool blocked);
SaveRulesResult SaveGameplayRulesAction(const GameplayConfig& draft, const std::filesystem::path& storageDir);

struct AddSkillResult : ActionResult {
    std::string id;
};

AddSkillResult AddSkillAction(SkillCatalog& catalog, const std::string& name, double weight,
                              const std::string& desc, const std::string& category, const std::vector<std::string>& professions);
ActionResult UpdateSkillWeightAction(SkillCatalog& catalog, const std::string& skillId, double weight,
                                    const std::string& displayName, const std::string& desc, const std::string& category,
                                    const std::vector<std::string>& professions);
ActionResult UpdateSkillDetailsAction(SkillCatalog& catalog, const std::string& skillId, const std::string& name,
                                      double weight, const std::string& desc, const std::string& category,
                                      const std::vector<std::string>& professions);
ActionResult RemoveSkillAction(IJobStorage& storage, SkillCatalog& catalog, const std::string& skillId,
                               const std::string& restoreId, bool& removedFromProfiles);
ActionResult MergeSkillAction(IJobStorage& storage, SkillCatalog& catalog, const std::string& fromId,
                              const std::string& toId, const std::string& newName, double newWeight,
                              const std::string& newDesc, const std::string& newCategory,
                              const std::vector<std::string>& newProfessions,
                              const std::string& restoreId);
ActionResult ClearAllSkillsAction(IJobStorage& storage, SkillCatalog& catalog,
                                  const std::filesystem::path& storageDir,
                                  const std::string& restoreId);

ActionResult GrantAchievementAction(const std::string& profileId, Profile& profile, IJobStorage& storage,
                                    const std::string& title, const std::string& skillId, double bonusPercent,
                                    const std::string& icon, std::int64_t nowSec, int durationDays);
ActionResult UpdateAchievementAction(const std::string& profileId, Profile& profile, IJobStorage& storage, int index,
                                     const std::string& title, double bonusPercent, const std::string& icon,
                                     std::int64_t nowSec, int durationDays);
ActionResult DeleteAchievementAction(const std::string& profileId, Profile& profile, IJobStorage& storage, int index);