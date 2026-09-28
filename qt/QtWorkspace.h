#pragma once
#include "AppWorkspaceDataService.h"
#include "IJobStorage.h"
#include "SkillCatalog.h"
#include <functional>
#include <memory>

// Migration workspaces are isolated from the production client's cloud folder.
class QtWorkspace {
public:
    explicit QtWorkspace(std::filesystem::path directory);
    void reload();
    bool transactionRecoveryNotice = false;
    std::filesystem::path transactionRecoveryPreservedFiles;
    bool cloudPullRecoveryNotice = false;
    bool cloudPushRecoveryNotice = false;
    std::filesystem::path directory;
    std::unique_ptr<IJobStorage> storage;
    SkillCatalog catalog;
    ModuleToggles modules;
    WorkspaceDataSnapshot data;
    std::vector<IJobStorage::ProfileInfo> profiles;
    // Optional, exception-isolated outcome observers installed by the owning UI.
    std::function<void(AppLogLevel, const std::string&)> profileEventLogger;
    std::function<void(AppLogLevel, const std::string&)> taskEventLogger;
    std::function<void(AppLogLevel, const std::string&)> metaEventLogger;
};
