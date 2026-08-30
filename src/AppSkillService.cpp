#include "AppSkillService.h"

#include "Profile.h"
#include "SkillCatalog.h"

#include <algorithm>
#include <cmath>

namespace {
bool sameCatalog(const SkillCatalog& expected, const SkillCatalog& actual, std::string* mismatch) {
    if (expected.skills() != actual.skills()) { if (mismatch) *mismatch = "order"; return false; }
    for (const auto& id : expected.skills()) {
        if (expected.display_name(id) != actual.display_name(id)) { if (mismatch) *mismatch = id + ":name"; return false; }
        if (expected.description(id) != actual.description(id)) { if (mismatch) *mismatch = id + ":description"; return false; }
        if (expected.category(id) != actual.category(id)) { if (mismatch) *mismatch = id + ":category"; return false; }
        if (std::abs(expected.weight(id) - actual.weight(id)) > 0.000001) { if (mismatch) *mismatch = id + ":weight"; return false; }
        if (expected.professions(id) != actual.professions(id)) { if (mismatch) *mismatch = id + ":professions"; return false; }
    }
    return true;
}
}

AppSkillDeleteResult AppDeleteUnusedSkill(const std::filesystem::path& directory,
                                          SkillCatalog& catalog,
                                          IJobStorage& storage,
                                          const std::vector<IJobStorage::ProfileInfo>& profiles,
                                          const std::vector<TaskEntry>& tasks,
                                          const std::string& restoreProfileId,
                                          const std::string& skillId) {
    AppSkillDeleteResult result;
    if (skillId.empty() || std::count(catalog.skills().begin(), catalog.skills().end(), skillId) != 1) {
        result.errorMessage = u8"Навык не найден или его ID неоднозначен.";
        return result;
    }
    for (const auto& task : tasks)
        if (std::find(task.skillIds.begin(), task.skillIds.end(), skillId) != task.skillIds.end()) ++result.linkedTasks;
    for (const auto& info : profiles) {
        if (info.archived) continue;
        if (!storage.set_active_profile(info.id)) {
            if (!restoreProfileId.empty()) storage.set_active_profile(restoreProfileId);
            result.errorMessage = u8"Не удалось проверить профиль перед удалением навыка.";
            return result;
        }
        const auto profile = storage.load_profile();
        if (!profile) {
            if (!restoreProfileId.empty()) storage.set_active_profile(restoreProfileId);
            result.errorMessage = u8"Не удалось загрузить профиль перед удалением навыка.";
            return result;
        }
        for (const auto& skill : profile->list_skills()) if (skill.name == skillId) ++result.linkedProfiles;
        for (const auto& achievement : profile->achievements()) if (achievement.skill == skillId) ++result.linkedAchievements;
    }
    if (!restoreProfileId.empty()) storage.set_active_profile(restoreProfileId);
    if (result.linkedTasks || result.linkedProfiles || result.linkedAchievements) {
        result.errorMessage = u8"Навык используется. Сначала удалите или перенесите все связи.";
        return result;
    }
    const auto expectedIds = catalog.skills();
    if (!catalog.remove_skill(skillId)) { result.errorMessage = u8"Не удалось удалить навык."; return result; }
    SkillCatalog checked(directory);
    std::vector<std::string> remaining = expectedIds;
    remaining.erase(std::remove(remaining.begin(), remaining.end(), skillId), remaining.end());
    std::string mismatch;
    if (checked.skills() != remaining) {
        mismatch = "order " + std::to_string(remaining.size()) + "/" + std::to_string(checked.skills().size()) + " expected=";
        for (const auto& id : remaining) mismatch += id + "(" + catalog.display_name(id) + "),";
        mismatch += " actual=";
        for (const auto& id : checked.skills()) mismatch += id + "(" + checked.display_name(id) + "),";
    }
    if (!mismatch.empty() || !sameCatalog(catalog, checked, &mismatch)) {
        result.errorMessage = u8"Запись skills.txt не прошла проверку" + (mismatch.empty() ? std::string() : ": " + mismatch) + ".";
        return result;
    }
    result.ok = true;
    result.changed = true;
    return result;
}
