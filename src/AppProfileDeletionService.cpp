#include "AppProfileDeletionService.h"

#include "Profile.h"

#include <algorithm>
#include <cmath>

AppProfileDeleteResult AppDeleteEmptyArchivedProfile(IJobStorage& storage,
                                                     const std::vector<IJobStorage::ProfileInfo>& profiles,
                                                     const std::vector<TaskEntry>& tasks,
                                                     const std::string& profileId) {
    AppProfileDeleteResult result;
    const auto matches = std::count_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId; });
    const auto info = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId; });
    if (profileId.empty() || matches != 1 || info == profiles.end() || !info->archived) {
        result.errorMessage = u8"Для окончательного удаления выберите один архивный профиль.";
        return result;
    }
    for (const auto& task : tasks) {
        const bool assignee = std::find(task.assignees.begin(), task.assignees.end(), profileId) != task.assignees.end();
        const bool participant = std::any_of(task.participants.begin(), task.participants.end(),
            [&](const auto& p) { return p.profileId == profileId; });
        if (assignee || participant) ++result.linkedTasks;
    }
    if (result.linkedTasks) {
        result.errorMessage = u8"Профиль упоминается в задачах. Историю удалять нельзя.";
        return result;
    }
    if (!storage.set_archived(profileId, false) || !storage.set_active_profile(profileId)) {
        result.errorMessage = u8"Не удалось открыть архивный профиль для проверки.";
        return result;
    }
    const auto profile = storage.load_profile();
    const auto queue = storage.load_queue();
    if (!profile) {
        storage.set_archived(profileId, true);
        result.errorMessage = u8"Не удалось прочитать архивный профиль.";
        return result;
    }
    bool skillProgress = false;
    for (const auto& skill : profile->list_skills()) skillProgress |= skill.level > 1 || skill.xp > 0;
    bool categoryProgress = false;
    for (int value : profile->category_best_scores()) categoryProgress |= value != 0;
    for (int value : profile->category_cooldowns()) categoryProgress |= value != 10;
    result.hasProgress = profile->is_admin() || profile->total_xp() != 0 || profile->tasks_completed() != 0 ||
        profile->last_task_timestamp() != 0 || profile->inactivity_tasks() != 0 || profile->recovery_tasks_remaining() != 0 ||
        std::abs(profile->wallet_balance()) > 0.000001 || !profile->achievements().empty() || !queue.empty() ||
        skillProgress || categoryProgress;
    if (!storage.set_archived(profileId, true)) {
        result.errorMessage = u8"Не удалось вернуть профиль в архив после проверки.";
        return result;
    }
    if (result.hasProgress) {
        result.errorMessage = u8"В профиле есть прогресс, баланс, очередь или достижения. Сохраните его в архиве.";
        return result;
    }
    if (!storage.delete_profile(profileId)) {
        result.errorMessage = u8"Не удалось удалить файлы профиля.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    return result;
}
