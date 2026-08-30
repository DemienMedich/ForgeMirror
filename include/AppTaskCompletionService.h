#pragma once
#include "AppContext.h"
#include "AppTaskProjectService.h"
#include "AppProfileMutationService.h"
#include "Profile.h"

struct TaskXpShare { std::string profileId; int percent = 0; };
struct TaskSkillRating { std::string skillId; int rating = 0; };
struct TaskCompletionInput {
    std::string taskId;
    int category = 0;
    int score = 10;
    std::vector<TaskXpShare> shares;
    std::vector<TaskSkillRating> skills;
    std::int64_t now = 0;
    std::string restoreProfileId;
    std::string actor;
};
struct TaskCompletionPreview {
    bool ok = false;
    std::string errorMessage;
    TaskXpFinalizeRequest finalize;
    std::vector<int> skillPercents;
    std::vector<Profile> updatedProfiles;
    std::vector<std::string> participantNames;
    std::vector<std::string> modifiers;
    int rawPool = 0;
    int penaltyPercent = 0;
};

// Preview never saves profiles. All validation is repeated immediately before commit.
TaskCompletionPreview PreviewTaskCompletion(AppContext& app, const std::vector<TaskEntry>& tasks,
                                            const TaskCompletionInput& input);
AppMutationResult CompleteTaskWithXp(AppContext& app, std::vector<TaskEntry>& tasks,
                                     std::vector<TaskAuditEntry>& audit, const TaskCompletionInput& input);
// Recover a pending Qt XP or metadata transaction before loading workspace data. Throws on failure.
bool RecoverTaskCompletion(const std::filesystem::path& directory);

// Project deletion spans projects, tasks and audit. The Qt caller brackets the existing
// mutation with this journal so a process interruption restores one coherent snapshot.
void PrepareProjectDeletionRecovery(const std::filesystem::path& directory);
void PrepareProfessionDeletionRecovery(const std::filesystem::path& directory,
                                       const std::vector<std::string>& profileIds);
void PrepareSkillDeletionRecovery(const std::filesystem::path& directory);
void PrepareProfileDeletionRecovery(const std::filesystem::path& directory,
                                    const std::string& profileId);
void PrepareRulesReapplyRecovery(const std::filesystem::path& directory,
                                 const std::vector<std::pair<std::string, bool>>& profiles);
void PrepareDirectXpRecovery(const std::filesystem::path& directory,
                             const std::string& profileId);
void CommitQtRecoveryTransaction(const std::filesystem::path& directory);
AppProfileMutationResult ReapplyRulesWithRecovery(AppContext& app,
                                                  const std::string& restoreProfileId);
AppProfileMutationResult GrantDirectSkillXpWithRecovery(AppContext& app,
    const std::string& restoreProfileId, const std::string& profileId,
    const std::string& skillId, int amount, std::int64_t nowSec);

// Qt metadata edits share the task/audit recovery journal; XP fields are never assigned.
AppMutationResult EditTaskDetails(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const TaskEntry& draft, const std::string& actor);

AppMutationResult CreateTaskWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const TaskEntry& task, const std::string& actor);

AppMutationResult UpdateTaskStatusWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, int newStatus, const std::string& actor);

AppMutationResult DeleteTaskWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, const std::string& actor);

AppMutationResult DeleteAwardedTaskWithRecovery(AppContext& app,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, const std::string& restoreProfileId, const std::string& actor);

AppMutationResult AdvanceTaskPipeline(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::vector<PipelineStep>& steps, const std::string& taskId,
    const std::string& expectedStageId, const std::string& targetId, const std::string& actor);
