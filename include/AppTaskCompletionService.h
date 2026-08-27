#pragma once
#include "AppContext.h"
#include "AppTaskProjectService.h"
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
// Recover a pending Qt XP transaction before loading any workspace data. Throws on failure.
bool RecoverTaskCompletion(const std::filesystem::path& directory);
