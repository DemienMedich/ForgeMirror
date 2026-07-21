#include "AppTaskWorkflowService.h"
#include "Profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

constexpr int kTaskStatusNew = 0;
constexpr int kTaskStatusDone = 2;

std::string TrimCopy(const std::string& input) {
    size_t first = 0;
    while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
    size_t last = input.size();
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
    return input.substr(first, last - first);
}

int NormalizeTaskStatus(int value) {
    return std::clamp(value, kTaskStatusNew, kTaskStatusDone);
}

} // namespace

AppTaskWorkflowService::AppTaskWorkflowService(
    const std::filesystem::path& storageDir,
    std::vector<TaskEntry>& tasks,
    std::vector<TaskAuditEntry>* auditCache)
    : storageDir_(storageDir), tasks_(tasks), auditCache_(auditCache) {}

AppMutationResult AppTaskWorkflowService::UpdateStatus(const std::string& taskId,
                                                       int newStatus,
                                                       const std::string& actor) {
    return AppUpdateTaskStatus(storageDir_, tasks_, taskId, newStatus, actor, auditCache_);
}

AppMutationResult AppTaskWorkflowService::BulkUpdateStatus(
    const std::unordered_set<std::string>& taskIds,
    int targetStatus,
    const std::string& actor) {
    return AppBulkUpdateTaskStatus(storageDir_, tasks_, taskIds, targetStatus, actor, auditCache_);
}

AppMutationResult AppTaskWorkflowService::ValidateFinalizeXp(
    const TaskXpFinalizeRequest& request) const {
    return ValidateFinalizeXp(tasks_, request);
}

AppMutationResult AppTaskWorkflowService::FinalizeXp(const TaskXpFinalizeRequest& request) {
    return AppFinalizeTaskXp(storageDir_, tasks_, request, auditCache_);
}

bool AppTaskWorkflowService::IsStatusTransitionAllowed(int fromStatus, int toStatus) {
    const int from = NormalizeTaskStatus(fromStatus);
    const int to = NormalizeTaskStatus(toStatus);
    return from == to || !(from == kTaskStatusDone && to == kTaskStatusNew);
}

std::vector<int> AppTaskWorkflowService::DistributeIntegerPool(
    int totalPool,
    const std::vector<int>& percents) {
    std::vector<int> distribution(percents.size(), 0);
    if (totalPool <= 0 || percents.empty()) return distribution;

    int remainder = totalPool;
    int fallbackIndex = -1;
    for (int i = 0; i < static_cast<int>(percents.size()); ++i) {
        const int percent = std::clamp(percents[static_cast<size_t>(i)], 0, 100);
        if (percent <= 0) continue;
        const int share = (totalPool * percent) / 100;
        distribution[static_cast<size_t>(i)] = share;
        remainder -= share;
        if (fallbackIndex == -1 ||
            percent > std::clamp(percents[static_cast<size_t>(fallbackIndex)], 0, 100)) {
            fallbackIndex = i;
        }
    }
    if (remainder > 0 && fallbackIndex >= 0) {
        distribution[static_cast<size_t>(fallbackIndex)] += remainder;
    }
    return distribution;
}

int AppTaskWorkflowService::ApplyPercentPenalty(int value, int penaltyPercent) {
    if (value <= 0) return 0;
    const int penalty = std::clamp(penaltyPercent, 0, 100);
    const int result = static_cast<int>(std::round(
        static_cast<double>(value) * static_cast<double>(100 - penalty) / 100.0));
    return std::max(0, result);
}

AppMutationResult AppTaskWorkflowService::ValidateFinalizeXp(
    const std::vector<TaskEntry>& tasks,
    const TaskXpFinalizeRequest& request) {
    AppMutationResult result;
    if (TrimCopy(request.taskId).empty()) {
        result.errorMessage = u8"Не выбрана задача для записи XP.";
        return result;
    }
    const auto taskIt = std::find_if(tasks.begin(), tasks.end(), [&](const TaskEntry& item) {
        return item.id == request.taskId;
    });
    if (taskIt == tasks.end()) {
        result.errorMessage = u8"Задача не найдена.";
        return result;
    }
    if (!taskIt->participants.empty()) {
        result.errorMessage = u8"XP по этой задаче уже записан.";
        return result;
    }
    if (request.participants.empty()) {
        result.errorMessage = u8"Нет участников для записи XP.";
        return result;
    }
    if (request.assignees.empty()) {
        result.errorMessage = u8"Нет исполнителей для записи XP.";
        return result;
    }
    if (request.skillIds.empty()) {
        result.errorMessage = u8"Нет навыков для записи XP.";
        return result;
    }
    int participantPercent = 0;
    for (const auto& participant : request.participants) {
        if (TrimCopy(participant.profileId).empty()) {
            result.errorMessage = u8"У участника XP не указан профиль.";
            return result;
        }
        if (participant.percent <= 0) {
            result.errorMessage = u8"Участник XP должен иметь положительный вклад.";
            return result;
        }
        if (participant.globalXp < 0 || participant.skillXp < 0) {
            result.errorMessage = u8"XP участника не может быть отрицательным.";
            return result;
        }
        participantPercent += participant.percent;
    }
    if (participantPercent != 100) {
        result.errorMessage = u8"Сумма вкладов участников должна быть 100%.";
        return result;
    }
    if (request.baseXp < 0 || request.basePool < 0) {
        result.errorMessage = u8"Базовый XP задачи не может быть отрицательным.";
        return result;
    }
    if (request.score < 0 || request.score > Profile::kMaxCategoryScore) {
        result.errorMessage = u8"Оценка задачи должна быть в диапазоне 0-10.";
        return result;
    }
    result.ok = true;
    return result;
}
