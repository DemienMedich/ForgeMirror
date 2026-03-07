#include "AppProfileMutationService.h"

#include <algorithm>
#include <cmath>

#include "AppUtils.h"
#include "IJobStorage.h"
#include "SkillCatalog.h"

namespace {

void RestoreActiveProfile(IJobStorage& storage, const std::string& restoreProfileId) {
    if (!restoreProfileId.empty()) {
        storage.set_active_profile(restoreProfileId);
    }
}

bool VerifyEncodedPassword(const Profile& profile, const std::string& input) {
    if (profile.password_encoded().empty()) return false;
    const std::string decoded = DecodePassword(profile.password_encoded());
    return !decoded.empty() && decoded == input;
}

AppProfileMutationResult PersistProfile(IJobStorage& storage,
                                        const std::string& restoreProfileId,
                                        const std::string& profileId,
                                        const Profile& profile) {
    AppProfileMutationResult result;
    if (profileId.empty()) {
        result.errorMessage = u8"Профиль не выбран.";
        return result;
    }
    if (!storage.set_active_profile(profileId)) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось активировать профиль.";
        return result;
    }
    if (!storage.save_profile(profile)) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось сохранить профиль.";
        return result;
    }
    RestoreActiveProfile(storage, restoreProfileId);
    result.ok = true;
    result.changed = true;
    result.affectedProfiles = 1;
    result.profile = profile;
    return result;
}

AppProfileMutationResult LoadProfileForMutation(IJobStorage& storage,
                                                const std::string& restoreProfileId,
                                                const std::string& profileId) {
    AppProfileMutationResult result;
    if (profileId.empty()) {
        result.errorMessage = u8"Профиль не выбран.";
        return result;
    }
    if (!storage.set_active_profile(profileId)) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось активировать профиль.";
        return result;
    }
    auto profile = storage.load_profile();
    if (!profile) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось загрузить профиль.";
        return result;
    }
    result.ok = true;
    result.profile = std::move(profile);
    return result;
}

} // namespace

AppProfileMutationResult AppSaveProfileSnapshot(IJobStorage& storage,
                                                const std::string& restoreProfileId,
                                                const std::string& profileId,
                                                const Profile& profile) {
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppChangeProfilePassword(IJobStorage& storage,
                                                  const std::string& restoreProfileId,
                                                  const std::string& profileId,
                                                  const std::string& currentPassword,
                                                  const std::string& newPassword,
                                                  bool requireCurrentPassword) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }

    if (newPassword.empty()) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.errorMessage = u8"Новый пароль не может быть пустым.";
        loaded.profile.reset();
        return loaded;
    }

    Profile profile = *loaded.profile;
    if (requireCurrentPassword && !profile.password_encoded().empty() &&
        !VerifyEncodedPassword(profile, currentPassword)) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.errorMessage = u8"Неверный текущий пароль.";
        loaded.profile.reset();
        return loaded;
    }

    const std::string encoded = EncodePassword(newPassword);
    if (profile.password_encoded() == encoded) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = true;
        loaded.changed = false;
        loaded.affectedProfiles = 0;
        loaded.profile = profile;
        loaded.errorMessage.clear();
        return loaded;
    }

    profile.set_password_encoded(encoded);
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppResetProfilePassword(IJobStorage& storage,
                                                 const std::string& restoreProfileId,
                                                 const std::string& profileId,
                                                 const std::string& newPassword) {
    return AppChangeProfilePassword(storage, restoreProfileId, profileId, {}, newPassword, false);
}

AppProfileMutationResult AppAssignProfileLevel(IJobStorage& storage,
                                               const std::string& restoreProfileId,
                                               const std::string& profileId,
                                               int newLevel,
                                               int newProgress) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }

    Profile profile = *loaded.profile;
    const int level = std::max(1, newLevel);
    const int progress = (newProgress >= 0) ? newProgress : level;
    if (profile.overall_level() == level && profile.level_progress() == progress) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = true;
        loaded.changed = false;
        loaded.affectedProfiles = 0;
        loaded.profile = profile;
        loaded.errorMessage.clear();
        return loaded;
    }

    profile.set_level_and_progress(level, progress);
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppAdjustProfileWallet(IJobStorage& storage,
                                                const std::string& restoreProfileId,
                                                const std::string& profileId,
                                                double delta) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }

    Profile profile = *loaded.profile;
    const double nextBalance = std::max(0.0, profile.wallet_balance() + delta);
    if (std::abs(profile.wallet_balance() - nextBalance) < 0.000001) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = true;
        loaded.changed = false;
        loaded.affectedProfiles = 0;
        loaded.profile = profile;
        loaded.errorMessage.clear();
        return loaded;
    }

    profile.set_wallet_balance(nextBalance);
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppReapplyRulesToProfiles(AppContext& app,
                                                   const std::string& restoreProfileId) {
    AppProfileMutationResult result;
    auto list = app.storage.list_profiles();
    for (const auto& info : list) {
        if (!app.storage.set_active_profile(info.id)) {
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось активировать профиль [") + info.id + u8"].";
            return result;
        }
        auto profile = app.storage.load_profile();
        if (!profile) {
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось загрузить профиль [") + info.id + u8"].";
            return result;
        }
        SyncProfileWithCatalog(*profile, app.catalog);
        const int level = profile->overall_level();
        const int progress = profile->level_progress();
        profile->set_level_and_progress(level, progress);
        if (!app.storage.save_profile(*profile)) {
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось сохранить профиль [") + info.id + u8"].";
            return result;
        }
        result.changed = true;
        result.affectedProfiles += 1;
    }
    RestoreActiveProfile(app.storage, restoreProfileId);
    result.ok = true;
    return result;
}