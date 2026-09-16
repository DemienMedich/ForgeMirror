#include "QtWorkspace.h"
#include "QtCloudPull.h"
#include "AppProfileService.h"
#include "AppTaskCompletionService.h"

namespace {
std::filesystem::path prepare(std::filesystem::path directory) {
    RecoverQtCloudPull(directory);
    RecoverTaskCompletion(directory);
    return directory;
}
}

IJobStorage* CreateFileStorage(const std::filesystem::path& dir);

QtWorkspace::QtWorkspace(std::filesystem::path path)
    : directory(prepare(std::move(path))), storage(CreateFileStorage(directory)),
      catalog(directory), modules(LoadModuleToggles()) {
    reload();
}

void QtWorkspace::reload() {
    RecoverQtCloudPull(directory);
    RecoverTaskCompletion(directory);
    catalog.reload();
    data = LoadWorkspaceDataSnapshot(directory, modules);
    SetGameplayConfig(data.rulesConfig);
    profiles = LoadSortedProfiles(*storage);
}
