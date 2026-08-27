#include "QtWorkspace.h"
#include "AppProfileService.h"

IJobStorage* CreateFileStorage(const std::filesystem::path& dir);

QtWorkspace::QtWorkspace(std::filesystem::path path)
    : directory(std::move(path)), storage(CreateFileStorage(directory)),
      catalog(directory), modules(LoadModuleToggles()) {
    reload();
}

void QtWorkspace::reload() {
    catalog.reload();
    data = LoadWorkspaceDataSnapshot(directory, modules);
    SetGameplayConfig(data.rulesConfig);
    profiles = LoadSortedProfiles(*storage);
}
