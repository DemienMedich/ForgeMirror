#include "AppTaskWorkflowService.h"

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
    return AppValidateTaskXpFinalize(tasks_, request);
}

AppMutationResult AppTaskWorkflowService::FinalizeXp(const TaskXpFinalizeRequest& request) {
    return AppFinalizeTaskXp(storageDir_, tasks_, request, auditCache_);
}

bool AppTaskWorkflowService::IsStatusTransitionAllowed(int fromStatus, int toStatus) {
    return AppIsTaskStatusTransitionAllowed(fromStatus, toStatus);
}

std::vector<int> AppTaskWorkflowService::DistributeIntegerPool(
    int totalPool,
    const std::vector<int>& percents) {
    return AppDistributeIntegerPool(totalPool, percents);
}

int AppTaskWorkflowService::ApplyPercentPenalty(int value, int penaltyPercent) {
    return AppApplyPercentPenalty(value, penaltyPercent);
}
