#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppDomainTypes.h"
#include "IJobStorage.h"

class SkillCatalog;

struct AppSkillDeleteResult {
    bool ok = false;
    bool changed = false;
    int linkedTasks = 0;
    int linkedProfiles = 0;
    int linkedAchievements = 0;
    std::string errorMessage;
};

AppSkillDeleteResult AppDeleteUnusedSkill(const std::filesystem::path& directory,
                                          SkillCatalog& catalog,
                                          IJobStorage& storage,
                                          const std::vector<IJobStorage::ProfileInfo>& profiles,
                                          const std::vector<TaskEntry>& tasks,
                                          const std::string& restoreProfileId,
                                          const std::string& skillId);
