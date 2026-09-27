#include "QtWorkspace.h"
#include "QtCloudPull.h"
#include "QtCloudPushPreview.h"
#include "AppProfileService.h"
#include "AppTaskCompletionService.h"

namespace {
std::filesystem::path prepare(std::filesystem::path directory, bool& taskRecoveryNotice) {
    RecoverQtCloudPull(directory);
    RecoverQtCloudPush(directory);
    taskRecoveryNotice |= RecoverTaskCompletion(directory);
    return directory;
}
}

IJobStorage* CreateFileStorage(const std::filesystem::path& dir);

QtWorkspace::QtWorkspace(std::filesystem::path path)
    : directory(prepare(std::move(path), taskRecoveryNotice)), storage(CreateFileStorage(directory)),
      catalog(directory), modules(LoadModuleToggles()) {
    reload();
}

void QtWorkspace::reload() {
    RecoverQtCloudPull(directory);
    RecoverQtCloudPush(directory);
    taskRecoveryNotice |= RecoverTaskCompletion(directory);
    catalog.reload();
    data = LoadWorkspaceDataSnapshot(directory, modules);
    SetGameplayConfig(data.rulesConfig);
    profiles = LoadSortedProfiles(*storage);
}
