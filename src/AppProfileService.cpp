#include "AppProfileService.h"

#include "AppUtils.h"
#include "SkillCatalog.h"

#include <algorithm>

std::vector<IJobStorage::ProfileInfo> LoadSortedProfiles(IJobStorage& storage) {
    auto list = storage.list_profiles();
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return list;
}

ActiveProfileLoadResult LoadActiveProfile(AppContext& app,
                                          const std::vector<IJobStorage::ProfileInfo>& profiles,
                                          int selectedIndex) {
    ActiveProfileLoadResult result;
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(profiles.size())) {
        result.ok = true;
        result.hasSelection = false;
        return result;
    }

    const auto& info = profiles[static_cast<size_t>(selectedIndex)];
    result.hasSelection = true;
    result.profileId = info.id;
    if (!app.storage.set_active_profile(info.id)) {
        result.errorMessage = u8"Не удалось активировать профиль.";
        return result;
    }

    auto loaded = app.storage.load_profile();
    if (!loaded) {
        result.errorMessage = std::string(u8"Не удалось загрузить профиль [") + info.id + u8"].";
        return result;
    }

    SyncProfileWithCatalog(*loaded, app.catalog);
    app.storage.save_profile(*loaded);
    result.profile = std::move(loaded);
    result.ok = true;
    return result;
}

ProfilesReloadResult ReloadProfiles(AppContext& app,
                                    const std::vector<IJobStorage::ProfileInfo>& currentProfiles,
                                    int currentSelectedIndex,
                                    const std::string& preferredId) {
    ProfilesReloadResult result;
    std::string currentId;
    if (currentSelectedIndex >= 0 && currentSelectedIndex < static_cast<int>(currentProfiles.size())) {
        currentId = currentProfiles[static_cast<size_t>(currentSelectedIndex)].id;
    }

    result.profiles = LoadSortedProfiles(app.storage);
    if (result.profiles.empty()) {
        result.errorMessage = u8"Профили не найдены.";
        return result;
    }

    const std::string targetId = preferredId.empty() ? currentId : preferredId;
    if (!targetId.empty()) {
        for (int i = 0; i < static_cast<int>(result.profiles.size()); ++i) {
            if (result.profiles[static_cast<size_t>(i)].id == targetId) {
                result.selectedIndex = i;
                result.active = LoadActiveProfile(app, result.profiles, i);
                return result;
            }
        }
    }

    result.selectedIndex = 0;
    result.active = LoadActiveProfile(app, result.profiles, 0);
    return result;
}
