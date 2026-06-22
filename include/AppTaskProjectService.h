#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "AppDomainTypes.h"

struct AppMutationResult {
    bool ok = false;
    bool changed = false;
    int changedCount = 0;
    int skippedCount = 0;
    std::string errorMessage;
};

struct AppProjectSaveResult {
    bool ok = false;
    bool duplicateName = false;
    int projectIndex = -1;
    std::string errorMessage;
};

struct AppProjectDeleteResult {
    bool ok = false;
    int detachedTasks = 0;
    std::string errorMessage;
};

struct TaskXpFinalizeRequest {
    std::string taskId;
    int category = 0;
    int score = 0;
    int baseXp = 0;
    int basePool = 0;
    std::vector<std::string> assignees;
    std::vector<std::string> skillIds;
    std::vector<TaskParticipant> participants;
    std::string actor;
};

int AppNormalizeTaskStatus(int value);
int AppNormalizeTaskPriority(int value);
bool AppIsTaskStatusTransitionAllowed(int fromStatus, int toStatus);
const char* AppTaskStatusLabel(int status);
const char* AppTaskPriorityLabel(int priority);
std::vector<int> AppDistributeIntegerPool(int totalPool, const std::vector<int>& percents);
int AppApplyPercentPenalty(int value, int penaltyPercent);

bool AppParseTaskDeadlineInput(const std::string& input, std::int64_t& outTs);
std::string AppGenerateTaskId(std::int64_t nowSeconds, size_t index);
std::string AppGenerateProjectId(const std::vector<ProjectEntry>& projects);
std::string AppTaskDisplayTitle(const TaskEntry& task);

bool AppSaveTasks(const std::filesystem::path& storageDir, const std::vector<TaskEntry>& tasks);
bool AppSaveProjects(const std::filesystem::path& storageDir, const std::vector<ProjectEntry>& projects);
bool AppAppendTaskAudit(const std::filesystem::path& storageDir,
                        const std::string& actor,
                        const std::string& taskId,
                        const std::string& field,
                        const std::string& oldValue,
                        const std::string& newValue,
                        std::vector<TaskAuditEntry>* cache = nullptr);

AppMutationResult AppValidateTaskXpFinalize(const std::vector<TaskEntry>& tasks,
                                            const TaskXpFinalizeRequest& request);

AppProjectSaveResult AppSaveProjectEntry(const std::filesystem::path& storageDir,
                                         std::vector<ProjectEntry>& projects,
                                         int editIndex,
                                         const std::string& name,
                                         const std::string& description);

AppProjectDeleteResult AppDeleteProjectAndDetachTasks(const std::filesystem::path& storageDir,
                                                      std::vector<ProjectEntry>& projects,
                                                      std::vector<TaskEntry>& tasks,
                                                      const std::string& projectId,
                                                      const std::string& actor,
                                                      std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppCreateTaskEntry(const std::filesystem::path& storageDir,
                                     std::vector<TaskEntry>& tasks,
                                     const TaskEntry& task,
                                     const std::string& actor,
                                     std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskStatus(const std::filesystem::path& storageDir,
                                      std::vector<TaskEntry>& tasks,
                                      const std::string& taskId,
                                      int newStatus,
                                      const std::string& actor,
                                      std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskPriority(const std::filesystem::path& storageDir,
                                        std::vector<TaskEntry>& tasks,
                                        const std::string& taskId,
                                        int newPriority,
                                        const std::string& actor,
                                        std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskProject(const std::filesystem::path& storageDir,
                                       std::vector<TaskEntry>& tasks,
                                       const std::string& taskId,
                                       const std::string& nextProjectId,
                                       const std::string& nextProjectName,
                                       const std::string& actor,
                                       std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskPipelineStep(const std::filesystem::path& storageDir,
                                            std::vector<TaskEntry>& tasks,
                                            const std::string& taskId,
                                            const std::string& nextPipelineStepId,
                                            const std::string& nextPipelineStepName,
                                            const std::string& actor,
                                            std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskDeadline(const std::filesystem::path& storageDir,
                                        std::vector<TaskEntry>& tasks,
                                        const std::string& taskId,
                                        const std::optional<std::int64_t>& deadlineAt,
                                        const std::string& actor,
                                        std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskText(const std::filesystem::path& storageDir,
                                    std::vector<TaskEntry>& tasks,
                                    const std::string& taskId,
                                    const std::string& title,
                                    const std::string& description,
                                    const std::string& actor,
                                    std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskCategory(const std::filesystem::path& storageDir,
                                        std::vector<TaskEntry>& tasks,
                                        const std::string& taskId,
                                        int newCategory,
                                        const std::string& actor,
                                        std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskPenaltyPercent(const std::filesystem::path& storageDir,
                                              std::vector<TaskEntry>& tasks,
                                              const std::string& taskId,
                                              int newPenaltyPercent,
                                              const std::string& actor,
                                              std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskSkillIds(const std::filesystem::path& storageDir,
                                        std::vector<TaskEntry>& tasks,
                                        const std::string& taskId,
                                        const std::vector<std::string>& skillIds,
                                        const std::string& actor,
                                        std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppUpdateTaskAssignees(const std::filesystem::path& storageDir,
                                         std::vector<TaskEntry>& tasks,
                                         const std::string& taskId,
                                         const std::vector<std::string>& assignees,
                                         const std::string& actor,
                                         std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppFinalizeTaskXp(const std::filesystem::path& storageDir,
                                    std::vector<TaskEntry>& tasks,
                                    const TaskXpFinalizeRequest& request,
                                    std::vector<TaskAuditEntry>* auditCache = nullptr);
void AppSetTaskAuditFailureHookForTests(bool enabled);

AppMutationResult AppFinalizeTaskXp(const std::filesystem::path& storageDir,
                                    std::vector<TaskEntry>& tasks,
                                    const std::string& taskId,
                                    int category,
                                    int score,
                                    int baseXp,
                                    int basePool,
                                    const std::vector<std::string>& assignees,
                                    const std::vector<std::string>& skillIds,
                                    const std::vector<TaskParticipant>& participants,
                                    const std::string& actor,
                                    std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppBulkUpdateTaskStatus(const std::filesystem::path& storageDir,
                                          std::vector<TaskEntry>& tasks,
                                          const std::unordered_set<std::string>& taskIds,
                                          int targetStatus,
                                          const std::string& actor,
                                          std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppBulkUpdateTaskPriority(const std::filesystem::path& storageDir,
                                            std::vector<TaskEntry>& tasks,
                                            const std::unordered_set<std::string>& taskIds,
                                            int targetPriority,
                                            const std::string& actor,
                                            std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppBulkUpdateTaskProject(const std::filesystem::path& storageDir,
                                           std::vector<TaskEntry>& tasks,
                                           const std::unordered_set<std::string>& taskIds,
                                           const std::string& nextProjectId,
                                           const std::string& nextProjectName,
                                           const std::string& actor,
                                           std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppBulkUpdateTaskPipelineStep(const std::filesystem::path& storageDir,
                                                std::vector<TaskEntry>& tasks,
                                                const std::unordered_set<std::string>& taskIds,
                                                const std::string& nextPipelineStepId,
                                                const std::string& nextPipelineStepName,
                                                const std::string& actor,
                                                std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppBulkUpdateTaskDeadline(const std::filesystem::path& storageDir,
                                            std::vector<TaskEntry>& tasks,
                                            const std::unordered_set<std::string>& taskIds,
                                            const std::optional<std::int64_t>& deadlineAt,
                                            const std::string& actor,
                                            std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppBulkUpdateTaskAssignees(const std::filesystem::path& storageDir,
                                             std::vector<TaskEntry>& tasks,
                                             const std::unordered_set<std::string>& taskIds,
                                             const std::vector<std::string>& assignees,
                                             const std::string& actor,
                                             std::vector<TaskAuditEntry>* auditCache = nullptr);

AppMutationResult AppDeleteTasksByIds(const std::filesystem::path& storageDir,
                                      std::vector<TaskEntry>& tasks,
                                      const std::vector<std::string>& taskIds,
                                      const std::string& actor,
                                      std::vector<TaskAuditEntry>* auditCache = nullptr,
                                      std::vector<TaskEntry>* removedTasks = nullptr);
