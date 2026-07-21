#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "AppTaskProjectService.h"

class AppTaskWorkflowService {
public:
    AppTaskWorkflowService(const std::filesystem::path& storageDir,
                           std::vector<TaskEntry>& tasks,
                           std::vector<TaskAuditEntry>* auditCache = nullptr);

    AppMutationResult UpdateStatus(const std::string& taskId,
                                   int newStatus,
                                   const std::string& actor);
    AppMutationResult BulkUpdateStatus(const std::unordered_set<std::string>& taskIds,
                                       int targetStatus,
                                       const std::string& actor);
    AppMutationResult ValidateFinalizeXp(const TaskXpFinalizeRequest& request) const;
    AppMutationResult FinalizeXp(const TaskXpFinalizeRequest& request);

    static AppMutationResult ValidateFinalizeXp(const std::vector<TaskEntry>& tasks,
                                                const TaskXpFinalizeRequest& request);
    static bool IsStatusTransitionAllowed(int fromStatus, int toStatus);
    static std::vector<int> DistributeIntegerPool(int totalPool, const std::vector<int>& percents);
    static int ApplyPercentPenalty(int value, int penaltyPercent);

private:
    const std::filesystem::path& storageDir_;
    std::vector<TaskEntry>& tasks_;
    std::vector<TaskAuditEntry>* auditCache_ = nullptr;
};
