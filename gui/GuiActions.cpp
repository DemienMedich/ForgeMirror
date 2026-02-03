#include "GuiActions.h"

#include "AppUtils.h"
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
    std::string trimmed = TrimCopy(name);
    if (trimmed.empty()) {
        result.userError = true;
        result.message = u8"Имя не может быть пустым.";
        return result;
    }
    Profile profile(trimmed);
    SyncProfileWithCatalog(profile, catalog);
    if (auto info = storage.create_profile(profile)) {
        const std::string login = "user_" + info->id;
        const std::string password = GenerateRandomPassword();
        profile.set_login(login);
        profile.set_password_encoded(EncodePassword(password));
        storage.save_profile(profile);
        result.ok = true;
        result.id = info->id;
        result.login = login;
        result.password = password;
        result.message = u8"Профиль создан.";
    } else {
        result.message = u8"Не удалось создать профиль.";
    }
    return result;
}

ActionResult SetProfileArchivedAction(IJobStorage& storage, const std::string& id, bool archived) {
    ActionResult result;
    result.ok = storage.set_archived(id, archived);
    result.message = result.ok ? u8"Операция выполнена." : u8"Не удалось выполнить операцию.";
    return result;
}

ActionResult DeleteProfileAction(IJobStorage& storage, const std::string& id) {
    ActionResult result;
    result.ok = storage.delete_profile(id);
    result.message = result.ok ? u8"Операция выполнена." : u8"Не удалось выполнить операцию.";
    return result;
}

ActionResult SetProfileBlockedAction(IJobStorage& storage, const std::string& id, bool blocked) {
    ActionResult result;
    if (id.empty()) {
        result.ok = false;
        result.userError = true;
        result.message = u8"Профиль не выбран.";
        return result;
    }
    if (!storage.set_active_profile(id)) {
        result.ok = false;
        result.message = u8"Не удалось активировать профиль.";
        return result;
    }
    if (auto profile = storage.load_profile()) {
        profile->set_blocked(blocked);
        if (storage.save_profile(*profile)) {
            result.ok = true;
            result.changed = true;
            result.message = blocked ? u8"Профиль заблокирован." : u8"Профиль разблокирован.";
            return result;
        }
    }
    result.ok = false;
    result.message = u8"Не удалось сохранить профиль.";
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

ActionResult GrantAchievementAction(Profile& profile, IJobStorage& storage, const std::string& title,
                                    const std::string& skillId, double bonusPercent, const std::string& icon,
                                    std::int64_t nowSec, int durationDays) {
    ActionResult result;
    Achievement a;
    a.title = title;
    a.skill = skillId;
    a.bonusPercent = bonusPercent;
    a.icon = icon;
    a.awardedAt = nowSec;
    if (durationDays > 0) {
        a.expiresAt = nowSec + static_cast<std::int64_t>(durationDays) * 24 * 3600;
    } else {
        a.expiresAt = 0;
    }
    profile.add_achievement(a);
    storage.save_profile(profile);
    result.ok = true;
    result.message = u8"Ачивка выдана.";
    return result;
}

ActionResult UpdateAchievementAction(Profile& profile, IJobStorage& storage, int index,
                                     const std::string& title, double bonusPercent, const std::string& icon,
                                     std::int64_t nowSec, int durationDays) {
    ActionResult result;
    auto achList = profile.achievements();
    if (index < 0 || index >= static_cast<int>(achList.size())) {
        result.userError = true;
        result.message = u8"Сначала выберите ачивку.";
        return result;
    }
    Achievement& a = achList[static_cast<size_t>(index)];
    a.title = title;
    a.icon = icon;
    a.bonusPercent = bonusPercent;
    if (durationDays > 0) {
        if (a.awardedAt == 0) a.awardedAt = nowSec;
        a.expiresAt = a.awardedAt + static_cast<std::int64_t>(durationDays) * 24 * 3600;
    } else {
        a.expiresAt = 0;
    }
    profile.set_achievements(achList);
    storage.save_profile(profile);
    result.ok = true;
    result.message = u8"Ачивка обновлена.";
    return result;
}

ActionResult DeleteAchievementAction(Profile& profile, IJobStorage& storage, int index) {
    ActionResult result;
    auto achList = profile.achievements();
    if (index < 0 || index >= static_cast<int>(achList.size())) {
        result.userError = true;
        result.message = u8"Сначала выберите ачивку.";
        return result;
    }
    achList.erase(achList.begin() + index);
    profile.set_achievements(achList);
    storage.save_profile(profile);
    result.ok = true;
    result.message = u8"Ачивка удалена.";
    return result;
}
