#include "GuiActions.h"

#include "AppUtils.h"
#include "AppProfileMutationService.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

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

int TotalSkillXp(const Skill& skill) {
    int total = skill.xp;
    for (int lvl = 2; lvl <= skill.level; ++lvl) {
        total += Skill::required_xp_for(lvl);
    }
    return total;
}

bool ActivateProfileForEdit(IJobStorage& storage, const IJobStorage::ProfileInfo& info, bool& wasArchived) {
    wasArchived = info.archived;
    if (info.archived) {
        if (!storage.set_archived(info.id, false)) return false;
    }
    if (!storage.set_active_profile(info.id)) {
        if (info.archived) {
            storage.set_archived(info.id, true);
        }
        return false;
    }
    return true;
}

void RestoreArchiveState(IJobStorage& storage, const std::string& id, bool wasArchived) {
    if (wasArchived) {
        storage.set_archived(id, true);
    }
}

bool RemoveSkillFromProfiles(IJobStorage& storage, SkillCatalog& catalog, const std::string& skillName,
                             const std::string& restoreId) {
    std::string target = skillName;
    if (!catalog.contains_id(target)) {
        if (auto id = catalog.id_for_name(skillName)) {
            target = *id;
        }
    }
    bool removedAny = false;
    auto list = storage.list_profiles();
    for (const auto& info : list) {
        bool wasArchived = false;
        if (!ActivateProfileForEdit(storage, info, wasArchived)) continue;
        if (auto profile = storage.load_profile()) {
            auto skills = profile->list_skills();
            auto before = skills.size();
            skills.erase(std::remove_if(skills.begin(), skills.end(), [&](const Skill& s) {
                return s.name == target;
            }), skills.end());
            if (skills.size() != before) {
                profile->set_skills(skills);
                storage.save_profile(*profile);
                removedAny = true;
            }
        }
        RestoreArchiveState(storage, info.id, wasArchived);
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    return removedAny;
}

bool MergeSkillInProfiles(IJobStorage& storage, SkillCatalog& catalog, const std::string& fromId,
                          const std::string& toId, const std::string& restoreId) {
    if (fromId == toId) return false;
    bool changedAny = false;
    auto list = storage.list_profiles();
    for (const auto& info : list) {
        bool wasArchived = false;
        if (!ActivateProfileForEdit(storage, info, wasArchived)) continue;
        if (auto profile = storage.load_profile()) {
            bool changed = false;
            auto skills = profile->list_skills();
            int fromIndex = -1;
            int toIndex = -1;
            for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                if (skills[i].name == fromId) fromIndex = i;
                if (skills[i].name == toId) toIndex = i;
            }
            if (fromIndex >= 0) {
                if (toIndex >= 0 && toIndex != fromIndex) {
                    int totalFrom = TotalSkillXp(skills[fromIndex]);
                    skills[toIndex].add_xp(totalFrom);
                    if (fromIndex > toIndex) {
                        skills.erase(skills.begin() + fromIndex);
                    } else {
                        skills.erase(skills.begin() + fromIndex);
                        toIndex -= 1;
                    }
                } else {
                    skills[fromIndex].name = toId;
                }
                profile->set_skills(skills);
                changed = true;
            }

            auto ach = profile->achievements();
            bool achChanged = false;
            for (auto& a : ach) {
                if (a.skill == fromId) {
                    a.skill = toId;
                    achChanged = true;
                }
            }
            if (achChanged) {
                profile->set_achievements(ach);
            }

            if (changed || achChanged) {
                SyncProfileWithCatalog(*profile, catalog);
                storage.save_profile(*profile);
                changedAny = true;
            }
        }
        RestoreArchiveState(storage, info.id, wasArchived);
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    return changedAny;
}

} // namespace

CreateProfileResult CreateProfileAction(IJobStorage& storage, SkillCatalog& catalog, const std::string& name) {
    CreateProfileResult result;
    const AppProfileCreateResult serviceResult = AppCreateProfile(storage, catalog, name);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.id = serviceResult.profileId;
    result.login = serviceResult.login;
    result.password = serviceResult.password;
    result.message = serviceResult.ok ? std::string(u8"Профиль создан.") : serviceResult.errorMessage;
    return result;
}

ActionResult SetProfileArchivedAction(IJobStorage& storage, const std::string& id, bool archived) {
    ActionResult result;
    const AppProfileActionResult serviceResult = AppSetProfileArchived(storage, id, archived);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.message = serviceResult.ok ? std::string(u8"Операция выполнена.") : serviceResult.errorMessage;
    return result;
}

ActionResult DeleteProfileAction(IJobStorage& storage, const std::string& id) {
    ActionResult result;
    const AppProfileActionResult serviceResult = AppDeleteProfile(storage, id);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.message = serviceResult.ok ? std::string(u8"Операция выполнена.") : serviceResult.errorMessage;
    return result;
}

ActionResult SetProfileBlockedAction(IJobStorage& storage, const std::string& id, bool blocked) {
    ActionResult result;
    AppProfileMutationResult serviceResult = AppSetProfileBlocked(storage, id, id, blocked);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.message = serviceResult.ok
        ? (blocked ? std::string(u8"Профиль заблокирован.") : std::string(u8"Профиль разблокирован."))
        : serviceResult.errorMessage;
    return result;
}


SaveRulesResult SaveGameplayRulesAction(const GameplayConfig& draft, const std::filesystem::path& storageDir) {
    SaveRulesResult result;
    result.config = SanitizeGameplayConfig(draft);
    if (SaveGameplayConfig(result.config, storageDir)) {
        result.ok = true;
        result.message = u8"Правила сохранены.";
    } else {
        result.message = u8"Не удалось сохранить правила.";
    }
    return result;
}

AddSkillResult AddSkillAction(SkillCatalog& catalog, const std::string& name, double weight,
                              const std::string& desc, const std::string& category, const std::vector<std::string>& professions) {
    AddSkillResult result;
    std::string trimmedName = TrimCopy(name);
    std::string trimmedDesc = TrimCopy(desc);
    std::string trimmedCategory = TrimCopy(category);
    std::vector<std::string> trimmedProfessions;
    trimmedProfessions.reserve(professions.size());
    for (const auto& prof : professions) {
        std::string trimmed = TrimCopy(prof);
        if (!trimmed.empty()) trimmedProfessions.push_back(trimmed);
    }
    if (trimmedName.empty()) {
        result.userError = true;
        result.message = u8"Название навыка не может быть пустым.";
        return result;
    }
    if (trimmedDesc.empty()) {
        result.userError = true;
        result.message = u8"Описание навыка не может быть пустым.";
        return result;
    }
    result.ok = catalog.add_skill(trimmedName, weight, trimmedDesc, trimmedCategory, trimmedProfessions);
    if (result.ok) {
        if (auto id = catalog.id_for_name(trimmedName)) {
            result.id = *id;
        }
        result.message = u8"Навык добавлен.";
    } else {
        result.message = u8"Не удалось добавить навык.";
    }
    return result;
}

ActionResult UpdateSkillWeightAction(SkillCatalog& catalog, const std::string& skillId, double weight,
                                     const std::string& displayName, const std::string& desc, const std::string& category,
                                     const std::vector<std::string>& professions) {
    ActionResult result;
    result.changed = catalog.update_skill(skillId, displayName, weight, desc, category, professions);
    result.ok = result.changed;
    if (result.changed) {
        result.message = u8"Вес навыка обновлён.";
    } else {
        result.userError = true;
        result.message = u8"Изменений нет.";
    }
    return result;
}

ActionResult UpdateSkillDetailsAction(SkillCatalog& catalog, const std::string& skillId, const std::string& name,
                                      double weight, const std::string& desc, const std::string& category,
                                      const std::vector<std::string>& professions) {
    ActionResult result;
    std::string trimmedName = TrimCopy(name);
    std::string trimmedDesc = TrimCopy(desc);
    std::string trimmedCategory = TrimCopy(category);
    std::vector<std::string> trimmedProfessions;
    trimmedProfessions.reserve(professions.size());
    for (const auto& prof : professions) {
        std::string trimmed = TrimCopy(prof);
        if (!trimmed.empty()) trimmedProfessions.push_back(trimmed);
    }
    if (trimmedName.empty()) {
        result.userError = true;
        result.message = u8"Название не может быть пустым.";
        return result;
    }
    if (trimmedDesc.empty()) {
        result.userError = true;
        result.message = u8"Описание не может быть пустым.";
        return result;
    }
    result.changed = catalog.update_skill(skillId, trimmedName, weight, trimmedDesc, trimmedCategory, trimmedProfessions);
    result.ok = result.changed;
    if (result.changed) {
        result.message = u8"Навык обновлён.";
    } else {
        result.userError = true;
        result.message = u8"Изменений нет.";
    }
    return result;
}

ActionResult RemoveSkillAction(IJobStorage& storage, SkillCatalog& catalog, const std::string& skillId,
                               const std::string& restoreId, bool& removedFromProfiles) {
    ActionResult result;
    removedFromProfiles = false;
    const bool removed = catalog.remove_skill(skillId);
    if (removed) {
        removedFromProfiles = RemoveSkillFromProfiles(storage, catalog, skillId, restoreId);
        result.ok = true;
        result.message = u8"Навык удалён.";
        if (removedFromProfiles) {
            result.message += u8" Удалён из профилей.";
        }
    } else {
        result.message = u8"Навык не найден.";
    }
    return result;
}

ActionResult MergeSkillAction(IJobStorage& storage, SkillCatalog& catalog, const std::string& fromId,
                              const std::string& toId, const std::string& newName, double newWeight,
                              const std::string& newDesc, const std::string& newCategory,
                              const std::vector<std::string>& newProfessions,
                              const std::string& restoreId) {
    ActionResult result;
    if (fromId.empty() || toId.empty()) {
        result.userError = true;
        result.message = u8"Навык не выбран.";
        return result;
    }
    MergeSkillInProfiles(storage, catalog, fromId, toId, restoreId);
    catalog.remove_skill(fromId);
    catalog.update_skill(toId, newName, newWeight, newDesc, newCategory, newProfessions);
    result.ok = true;
    result.message = u8"Навыки объединены.";
    return result;
}

ActionResult ClearAllSkillsAction(IJobStorage& storage, SkillCatalog& catalog,
                                  const std::filesystem::path& storageDir,
                                  const std::string& restoreId) {
    ActionResult result;
    bool changedAny = false;
    auto list = storage.list_profiles();
    for (const auto& info : list) {
        bool wasArchived = false;
        if (!ActivateProfileForEdit(storage, info, wasArchived)) continue;
        if (auto profile = storage.load_profile()) {
            if (!profile->list_skills().empty()) {
                profile->set_skills({});
                storage.save_profile(*profile);
                changedAny = true;
            }
        }
        RestoreArchiveState(storage, info.id, wasArchived);
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    std::error_code ec;
    const auto skillsPath = storageDir / "skills.txt";
    const bool removedFile = std::filesystem::remove(skillsPath, ec);
    catalog.reload();
    result.ok = true;
    result.changed = changedAny || removedFile;
    result.message = result.changed ? u8"Навыки очищены." : u8"Каталог навыков уже пустой.";
    return result;
}

ActionResult GrantAchievementAction(const std::string& profileId, Profile& profile, IJobStorage& storage,
                                    const std::string& title, const std::string& skillId, double bonusPercent,
                                    const std::string& icon, std::int64_t nowSec, int durationDays) {
    ActionResult result;
    AppProfileMutationResult serviceResult = AppGrantAchievement(storage, profileId, profileId, title, skillId,
                                                                 bonusPercent, icon, nowSec, durationDays);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.message = serviceResult.ok ? std::string(u8"Ачивка выдана.") : serviceResult.errorMessage;
    if (serviceResult.ok && serviceResult.profile) {
        profile = *serviceResult.profile;
    }
    return result;
}

ActionResult UpdateAchievementAction(const std::string& profileId, Profile& profile, IJobStorage& storage, int index,
                                     const std::string& title, double bonusPercent, const std::string& icon,
                                     std::int64_t nowSec, int durationDays) {
    ActionResult result;
    AppProfileMutationResult serviceResult = AppUpdateAchievement(storage, profileId, profileId, index, title,
                                                                  bonusPercent, icon, nowSec, durationDays);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.message = serviceResult.ok ? std::string(u8"Ачивка обновлена.") : serviceResult.errorMessage;
    if (serviceResult.ok && serviceResult.profile) {
        profile = *serviceResult.profile;
    }
    return result;
}

ActionResult DeleteAchievementAction(const std::string& profileId, Profile& profile, IJobStorage& storage, int index) {
    ActionResult result;
    AppProfileMutationResult serviceResult = AppDeleteAchievement(storage, profileId, profileId, index);
    result.ok = serviceResult.ok;
    result.userError = serviceResult.userError;
    result.changed = serviceResult.changed;
    result.message = serviceResult.ok ? std::string(u8"Ачивка удалена.") : serviceResult.errorMessage;
    if (serviceResult.ok && serviceResult.profile) {
        profile = *serviceResult.profile;
    }
    return result;
}
