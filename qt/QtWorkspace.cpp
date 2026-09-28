#include "QtWorkspace.h"
#include "QtCloudPull.h"
#include "QtCloudPushPreview.h"
#include "AppProfileService.h"
#include "AppTaskCompletionService.h"

namespace {
void recover(std::filesystem::path const& directory, bool& transactionRecoveryNotice,
             std::filesystem::path& transactionRecoveryPreservedFiles,
             bool& cloudPullRecoveryNotice, bool& cloudPushRecoveryNotice) {
    cloudPullRecoveryNotice |= RecoverQtCloudPull(directory);
    cloudPushRecoveryNotice |= RecoverQtCloudPush(directory);
    transactionRecoveryNotice |= RecoverTaskCompletion(directory, &transactionRecoveryPreservedFiles);
}
std::filesystem::path prepare(std::filesystem::path directory, bool& transactionRecoveryNotice,
                              std::filesystem::path& transactionRecoveryPreservedFiles,
                              bool& cloudPullRecoveryNotice, bool& cloudPushRecoveryNotice) {
    recover(directory, transactionRecoveryNotice, transactionRecoveryPreservedFiles,
            cloudPullRecoveryNotice, cloudPushRecoveryNotice);
    return directory;
}
}

IJobStorage* CreateFileStorage(const std::filesystem::path& dir);

QtWorkspace::QtWorkspace(std::filesystem::path path)
    : directory(prepare(std::move(path), transactionRecoveryNotice, transactionRecoveryPreservedFiles,
                        cloudPullRecoveryNotice, cloudPushRecoveryNotice)), storage(CreateFileStorage(directory)),
      catalog(directory), modules(LoadModuleToggles()) {
    reload();
}

void QtWorkspace::reload() {
    recover(directory, transactionRecoveryNotice, transactionRecoveryPreservedFiles,
            cloudPullRecoveryNotice, cloudPushRecoveryNotice);
    catalog.reload();
    data = LoadWorkspaceDataSnapshot(directory, modules);
    SetGameplayConfig(data.rulesConfig);
    profiles = LoadSortedProfiles(*storage);
}
