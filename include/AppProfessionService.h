#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppDomainTypes.h"
#include "IJobStorage.h"

class SkillCatalog;

struct AppProfessionMutationResult {
    bool ok = false;
    bool changed = false;
    int professionIndex = -1;
    int affectedProfiles = 0;
    int affectedSkills = 0;
    std::string errorMessage;
};

bool AppSaveProfessionsData(const std::filesystem::path& storageDir,
                            const std::vector<ProfessionEntry>& professions);
std::string AppGenerateProfessionId(const std::vector<ProfessionEntry>& professions);

AppProfessionMutationResult AppSaveProfessionEntry(const std::filesystem::path& storageDir,
                                                   std::vector<ProfessionEntry>& professions,
                                                   int editIndex,
                                                   const std::string& name,
                                                   const std::string& description);

AppProfessionMutationResult AppAssignProfessionToProfile(IJobStorage& storage,
                                                         const std::string& restoreProfileId,
                                                         const std::string& profileId,
                                                         const std::string& professionId);

AppProfessionMutationResult AppDeleteProfessionEntry(const std::filesystem::path& storageDir,
                                                     std::vector<ProfessionEntry>& professions,
                                                     IJobStorage& storage,
                                                     const std::vector<IJobStorage::ProfileInfo>& profiles,
                                                     SkillCatalog& catalog,
                                                     const std::string& restoreProfileId,
                                                     const std::string& professionId);
