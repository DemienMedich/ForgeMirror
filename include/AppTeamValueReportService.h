#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "AppDomainTypes.h"

struct TeamValueProjectMetric {
    std::string id;
    std::string name;
    int totalTasks = 0;
    int activeTasks = 0;
    int doneTasks = 0;
    int overdueTasks = 0;
    int xpPendingTasks = 0;
    int withoutPipelineTasks = 0;
    int totalGlobalXp = 0;
    int totalSkillXp = 0;
};

struct TeamValueAssigneeMetric {
    std::string profileId;
    int totalTasks = 0;
    int activeTasks = 0;
    int doneTasks = 0;
    int overdueTasks = 0;
    int xpPendingTasks = 0;
    int totalGlobalXp = 0;
    int totalSkillXp = 0;
};

struct TeamValueReport {
    std::int64_t generatedAt = 0;
    int totalTasks = 0;
    int newTasks = 0;
    int inProgressTasks = 0;
    int doneTasks = 0;
    int activeTasks = 0;
    int overdueTasks = 0;
    int deadlineNext24hTasks = 0;
    int xpPendingTasks = 0;
    int tasksWithXp = 0;
    int unassignedTasks = 0;
    int tasksWithoutProject = 0;
    int tasksWithoutPipeline = 0;
    int totalProjects = 0;
    int activeProjects = 0;
    int projectsWithOverdue = 0;
    int projectsWithXpPending = 0;
    int totalGlobalXp = 0;
    int totalSkillXp = 0;
    double deliveryScore = 0.0;
    std::vector<TeamValueProjectMetric> projects;
    std::vector<TeamValueAssigneeMetric> assignees;
};

TeamValueReport BuildTeamValueReport(const std::vector<TaskEntry>& tasks,
                                     const std::vector<ProjectEntry>& projects,
                                     std::int64_t nowSeconds);

bool WriteTeamValueReportCsv(std::ostream& out, const TeamValueReport& report);
