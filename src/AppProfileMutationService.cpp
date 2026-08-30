#include "AppProfileMutationService.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <utility>

#include "AppUtils.h"
#include "IJobStorage.h"
#include "SkillCatalog.h"

namespace {

std::string TrimCopy(const std::string& input) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    std::string out = input;
    out.erase(out.begin(), std::find_if(out.begin(), out.end(),
                                        [&](unsigned char c) { return !is_space(c); }));
    out.erase(std::find_if(out.rbegin(), out.rend(),
                           [&](unsigned char c) { return !is_space(c); }).base(),
              out.end());
    return out;
}

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
        result.userError = true;
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
        result.userError = true;
        result.errorMessage = u8"Профиль не выбран.";
        return result;
    }
    if (!storage.set_active_profile(profileId)) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось активировать профиль.";
        return result;
    }
    auto loaded = storage.load_profile();
    if (!loaded) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось загрузить профиль.";
        return result;
    }
    result.ok = true;
    result.profile = std::move(loaded);
    return result;
}

Achievement BuildAchievement(const std::string& title,
                             const std::string& skillId,
                             double bonusPercent,
                             const std::string& icon,
                             std::int64_t nowSec,
                             int durationDays) {
    Achievement achievement;
    achievement.title = title;
    achievement.skill = skillId;
    achievement.bonusPercent = bonusPercent;
    achievement.icon = icon;
    achievement.awardedAt = nowSec;
    achievement.expiresAt = (durationDays > 0)
        ? nowSec + static_cast<std::int64_t>(durationDays) * 24 * 3600
        : 0;
    return achievement;
}

std::int64_t MutationNowSeconds() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

} // namespace

AppProfileCreateResult AppCreateProfile(IJobStorage& storage,
                                        SkillCatalog& catalog,
                                        const std::string& name) {
    AppProfileCreateResult result;
    const std::string trimmed = TrimCopy(name);
    if (trimmed.empty()) {
        result.userError = true;
        result.errorMessage = u8"Имя не может быть пустым.";
        return result;
    }

    Profile profile(trimmed);
    SyncProfileWithCatalog(profile, catalog);
    auto info = storage.create_profile(profile);
    if (!info) {
        result.errorMessage = u8"Не удалось создать профиль.";
        return result;
    }

    const std::string login = "user_" + info->id;
    const std::string password = GenerateRandomPassword();
    profile.set_login(login);
    profile.set_password_encoded(EncodePassword(password));
    if (!storage.save_profile(profile)) {
        storage.delete_profile(info->id);
        result.errorMessage = u8"Не удалось сохранить профиль.";
        return result;
    }

    result.ok = true;
    result.changed = true;
    result.profileId = info->id;
    result.login = login;
    result.password = password;
    return result;
}

AppProfileActionResult AppSetProfileArchived(IJobStorage& storage,
                                             const std::string& profileId,
                                             bool archived) {
    AppProfileActionResult result;
    if (profileId.empty()) {
        result.userError = true;
        result.errorMessage = u8"Профиль не выбран.";
        return result;
    }
    result.ok = storage.set_archived(profileId, archived);
    result.changed = result.ok;
    if (!result.ok) {
        result.errorMessage = u8"Не удалось выполнить операцию.";
    }
    return result;
}

AppProfileActionResult AppDeleteProfile(IJobStorage& storage,
                                        const std::string& profileId) {
    AppProfileActionResult result;
    if (profileId.empty()) {
        result.userError = true;
        result.errorMessage = u8"Профиль не выбран.";
        return result;
    }
    result.ok = storage.delete_profile(profileId);
    result.changed = result.ok;
    if (!result.ok) {
        result.errorMessage = u8"Не удалось выполнить операцию.";
    }
    return result;
}

AppProfileMutationResult AppSetProfileBlocked(IJobStorage& storage,
                                              const std::string& restoreProfileId,
                                              const std::string& profileId,
                                              bool blocked) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }

    Profile profile = *loaded.profile;
    if (profile.is_blocked() == blocked) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = true;
        loaded.changed = false;
        loaded.affectedProfiles = 0;
        loaded.profile = profile;
        loaded.errorMessage.clear();
        return loaded;
    }

    profile.set_blocked(blocked);
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppSaveProfileSnapshot(IJobStorage& storage,
                                                const std::string& restoreProfileId,
                                                const std::string& profileId,
                                                const Profile& profile) {
    const std::string trimmed = TrimCopy(profile.name());
    if (trimmed.empty() || std::any_of(trimmed.begin(), trimmed.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
        AppProfileMutationResult result;
        result.userError = true;
        result.errorMessage = u8"Введите непустое имя без управляющих символов.";
        return result;
    }
    Profile normalized = profile;
    normalized.set_name(trimmed);
    return PersistProfile(storage, restoreProfileId, profileId, normalized);
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
        loaded.userError = true;
        loaded.errorMessage = u8"Новый пароль не может быть пустым.";
        loaded.profile.reset();
        return loaded;
    }

    Profile profile = *loaded.profile;
    if (requireCurrentPassword && !profile.password_encoded().empty() &&
        !VerifyEncodedPassword(profile, currentPassword)) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.userError = true;
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

AppProfileMutationResult AppRemoveEvilSpiritForCoins(IJobStorage& storage,
                                                     const std::string& restoreProfileId,
                                                     const std::string& profileId,
                                                     const std::filesystem::path& storageDir,
                                                     StorageVaultData& vault,
                                                     double cost) {
    if (profileId.empty() || profileId != restoreProfileId) {
        AppProfileMutationResult result;
        result.userError = true;
        result.errorMessage = u8"Снять Злого духа можно только в своём профиле.";
        return result;
    }

    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }
    if (cost <= 0.0) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.userError = true;
        loaded.profile.reset();
        loaded.errorMessage = u8"Некорректная стоимость снятия духа.";
        return loaded;
    }

    Profile originalProfile = *loaded.profile;
    if (originalProfile.spirit() != ProfileSpirit::Evil) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.userError = true;
        loaded.profile.reset();
        loaded.errorMessage = u8"На профиле нет Злого духа.";
        return loaded;
    }
    if (originalProfile.wallet_balance() + 0.000001 < cost) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.userError = true;
        loaded.profile.reset();
        loaded.errorMessage = u8"Недостаточно Кукоинов для снятия Злого духа.";
        return loaded;
    }

    Profile updatedProfile = originalProfile;
    updatedProfile.set_spirit(ProfileSpirit::None);
    updatedProfile.set_wallet_balance(std::max(0.0, originalProfile.wallet_balance() - cost));

    AppProfileMutationResult savedProfile =
        PersistProfile(storage, restoreProfileId, profileId, updatedProfile);
    if (!savedProfile.ok || !savedProfile.profile) {
        return savedProfile;
    }

    StorageVaultData updatedVault = vault;
    updatedVault.balance += cost;
    StorageLogEntry logEntry;
    logEntry.timestamp = MutationNowSeconds();
    logEntry.amount = cost;
    logEntry.action = "spirit_cleanup";
    logEntry.note = profileId + " " + originalProfile.name() + " removed evil spirit";
    updatedVault.log.push_back(std::move(logEntry));

    if (!SaveStorageVault(storageDir, updatedVault)) {
        AppProfileMutationResult rollback =
            PersistProfile(storage, restoreProfileId, profileId, originalProfile);
        AppProfileMutationResult result;
        result.userError = false;
        result.errorMessage = rollback.ok
            ? u8"Не удалось пополнить хранилище. Изменения профиля отменены."
            : u8"Не удалось пополнить хранилище; откат профиля тоже не выполнен.";
        return result;
    }

    vault = LoadStorageVault(storageDir);
    savedProfile.changed = true;
    savedProfile.affectedProfiles = 1;
    savedProfile.profile = updatedProfile;
    return savedProfile;
}

AppProfileMutationResult AppGrantAchievement(IJobStorage& storage,
                                             const std::string& restoreProfileId,
                                             const std::string& profileId,
                                             const std::string& title,
                                             const std::string& skillId,
                                             double bonusPercent,
                                             const std::string& icon,
                                             std::int64_t nowSec,
                                             int durationDays) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }
    Profile profile = *loaded.profile;
    profile.add_achievement(BuildAchievement(title, skillId, bonusPercent, icon, nowSec, durationDays));
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppUpdateAchievement(IJobStorage& storage,
                                              const std::string& restoreProfileId,
                                              const std::string& profileId,
                                              int index,
                                              const std::string& title,
                                              double bonusPercent,
                                              const std::string& icon,
                                              std::int64_t nowSec,
                                              int durationDays) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }
    Profile profile = *loaded.profile;
    auto achievements = profile.achievements();
    if (index < 0 || index >= static_cast<int>(achievements.size())) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.userError = true;
        loaded.errorMessage = u8"Сначала выберите ачивку.";
        loaded.profile.reset();
        return loaded;
    }
    Achievement& achievement = achievements[static_cast<size_t>(index)];
    achievement.title = title;
    achievement.icon = icon;
    achievement.bonusPercent = bonusPercent;
    if (durationDays > 0) {
        if (achievement.awardedAt == 0) achievement.awardedAt = nowSec;
        achievement.expiresAt = achievement.awardedAt + static_cast<std::int64_t>(durationDays) * 24 * 3600;
    } else {
        achievement.expiresAt = 0;
    }
    profile.set_achievements(achievements);
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppDeleteAchievement(IJobStorage& storage,
                                              const std::string& restoreProfileId,
                                              const std::string& profileId,
                                              int index) {
    AppProfileMutationResult loaded = LoadProfileForMutation(storage, restoreProfileId, profileId);
    if (!loaded.ok || !loaded.profile) {
        return loaded;
    }
    Profile profile = *loaded.profile;
    auto achievements = profile.achievements();
    if (index < 0 || index >= static_cast<int>(achievements.size())) {
        RestoreActiveProfile(storage, restoreProfileId);
        loaded.ok = false;
        loaded.userError = true;
        loaded.errorMessage = u8"Сначала выберите ачивку.";
        loaded.profile.reset();
        return loaded;
    }
    achievements.erase(achievements.begin() + index);
    profile.set_achievements(achievements);
    return PersistProfile(storage, restoreProfileId, profileId, profile);
}

AppProfileMutationResult AppReapplyRulesToProfiles(AppContext& app,
                                                   const std::string& restoreProfileId) {
    AppProfileMutationResult result;
    const auto list = app.storage.list_profiles();
    if (list.empty()) {
        result.ok = true;
        return result;
    }
    std::vector<Profile> profiles;
    profiles.reserve(list.size());
    for (const auto& info : list) {
        if (info.archived && !app.storage.set_archived(info.id, false)) {
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось временно восстановить архивный профиль [") + info.id + u8"].";
            return result;
        }
        if (!app.storage.set_active_profile(info.id)) {
            if (info.archived) app.storage.set_archived(info.id, true);
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось активировать профиль [") + info.id + u8"].";
            return result;
        }
        auto profile = app.storage.load_profile();
        if (!profile) {
            if (info.archived) app.storage.set_archived(info.id, true);
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось загрузить профиль [") + info.id + u8"].";
            return result;
        }
        profiles.push_back(*profile);
        if (info.archived && !app.storage.set_archived(info.id, true)) {
            RestoreActiveProfile(app.storage, restoreProfileId);
            result.errorMessage = std::string(u8"Не удалось вернуть профиль в архив [") + info.id + u8"].";
            return result;
        }
    }
    RestoreActiveProfile(app.storage, restoreProfileId);
    for (size_t index = 0; index < list.size(); ++index) {
        const auto& info = list[index];
        if (info.archived && !app.storage.set_archived(info.id, false)) {
            result.errorMessage = std::string(u8"Не удалось временно восстановить архивный профиль [") + info.id + u8"].";
            break;
        }
        if (!app.storage.set_active_profile(info.id)) {
            if (info.archived) app.storage.set_archived(info.id, true);
            result.errorMessage = std::string(u8"Не удалось активировать профиль [") + info.id + u8"].";
            break;
        }
        auto profile = profiles[index];
        SyncProfileWithCatalog(profile, app.catalog);
        const int totalXp = profile.total_xp();
        profile.set_total_xp(totalXp);
        if (!app.storage.save_profile(profile)) {
            if (info.archived) app.storage.set_archived(info.id, true);
            result.errorMessage = std::string(u8"Не удалось сохранить профиль [") + info.id + u8"].";
            break;
        }
        if (info.archived && !app.storage.set_archived(info.id, true)) {
            result.errorMessage = std::string(u8"Не удалось вернуть профиль в архив [") + info.id + u8"].";
            break;
        }
        ++result.affectedProfiles;
    }
    result.ok = result.affectedProfiles == int(list.size());
    result.changed = result.affectedProfiles > 0;
    RestoreActiveProfile(app.storage, restoreProfileId);
    return result;
}
