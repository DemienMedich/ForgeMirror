#pragma once

#include <string>
#include <vector>

#include "AppDomainTypes.h"
#include "IJobStorage.h"

struct AppProfileDeleteResult {
    bool ok = false;
    bool changed = false;
    int linkedTasks = 0;
    bool hasProgress = false;
    std::string errorMessage;
};

AppProfileDeleteResult AppDeleteEmptyArchivedProfile(IJobStorage& storage,
                                                     const std::vector<IJobStorage::ProfileInfo>& profiles,
                                                     const std::vector<TaskEntry>& tasks,
                                                     const std::string& profileId);
