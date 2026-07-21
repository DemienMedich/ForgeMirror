#include "AppTeamValueReportService.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <unordered_map>

namespace {

constexpr int kStatusNew = 0;
constexpr int kStatusInProgress = 1;
constexpr int kStatusDone = 2;

int NormalizeStatus(int value) {
    return std::clamp(value, kStatusNew, kStatusDone);
}

bool HasRecordedXp(const TaskEntry& task) {
    for (const auto& participant : task.participants) {
        if (participant.globalXp > 0 || participant.skillXp > 0) {
            return true;
        }
    }
    return false;
}

bool NeedsXpAward(const TaskEntry& task) {
    return NormalizeStatus(task.status) == kStatusDone && !HasRecordedXp(task);
}

bool IsOverdue(const TaskEntry& task, std::int64_t nowSeconds) {
    return task.deadlineAt > 0 && task.deadlineAt < nowSeconds && NormalizeStatus(task.status) != kStatusDone;
}

bool IsDeadlineNext24h(const TaskEntry& task, std::int64_t nowSeconds) {
    constexpr std::int64_t kDaySeconds = 24 * 60 * 60;
    return task.deadlineAt >= nowSeconds && task.deadlineAt <= nowSeconds + kDaySeconds &&
           NormalizeStatus(task.status) != kStatusDone;
}

bool HasProject(const TaskEntry& task) {
    return !task.projectId.empty() || !task.project.empty();
}

bool HasPipeline(const TaskEntry& task) {
    return !task.pipelineStepId.empty() || !task.pipelineStep.empty();
}

std::string ProjectKey(const TaskEntry& task) {
    if (!task.projectId.empty()) return task.projectId;
    if (!task.project.empty()) return std::string("name:") + task.project;
    return "__no_project";
}

std::string ProjectName(const TaskEntry& task) {
    if (!task.project.empty()) return task.project;
    if (!task.projectId.empty()) return task.projectId;
    return u8"Без проекта";
}

std::vector<std::string> TaskProfileIds(const TaskEntry& task) {
    std::vector<std::string> ids;
    ids.reserve(task.assignees.size() + task.participants.size());
    for (const auto& id : task.assignees) {
        if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    for (const auto& participant : task.participants) {
        if (!participant.profileId.empty() && std::find(ids.begin(), ids.end(), participant.profileId) == ids.end()) {
            ids.push_back(participant.profileId);
        }
    }
    return ids;
}

std::string CsvEscapeTeamValue(const std::string& text) {
    bool needsQuotes = false;
    for (char ch : text) {
        if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return text;
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char ch : text) {
        if (ch == '"') out.push_back('"');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

void WriteMetric(std::ostream& out, const char* key, int value) {
    out << "Summary," << key << "," << value << "\n";
}

void WriteMetric(std::ostream& out, const char* key, double value) {
    out << "Summary," << key << "," << std::fixed << std::setprecision(1) << value << "\n";
    out << std::defaultfloat;
}

} // namespace

TeamValueReport BuildTeamValueReport(const std::vector<TaskEntry>& tasks,
                                     const std::vector<ProjectEntry>& projects,
                                     std::int64_t nowSeconds) {
    TeamValueReport report;
    report.generatedAt = nowSeconds;
    report.totalTasks = static_cast<int>(tasks.size());
    report.totalProjects = static_cast<int>(projects.size());

    std::unordered_map<std::string, TeamValueProjectMetric> projectMetrics;
    projectMetrics.reserve(projects.size() + tasks.size());
    for (const auto& project : projects) {
        TeamValueProjectMetric metric;
        metric.id = project.id;
        metric.name = project.name.empty() ? project.id : project.name;
        projectMetrics.emplace(project.id, std::move(metric));
    }

    std::unordered_map<std::string, TeamValueAssigneeMetric> assigneeMetrics;
    assigneeMetrics.reserve(tasks.size());

    for (const TaskEntry& task : tasks) {
        const int status = NormalizeStatus(task.status);
        const bool done = status == kStatusDone;
        const bool active = !done;
        const bool overdue = IsOverdue(task, nowSeconds);
        const bool deadlineNext24h = IsDeadlineNext24h(task, nowSeconds);
        const bool xpPending = NeedsXpAward(task);
        const bool recordedXp = HasRecordedXp(task);
        const bool hasProject = HasProject(task);
        const bool hasPipeline = HasPipeline(task);

        if (status == kStatusNew) ++report.newTasks;
        if (status == kStatusInProgress) ++report.inProgressTasks;
        if (done) ++report.doneTasks;
        if (active) ++report.activeTasks;
        if (overdue) ++report.overdueTasks;
        if (deadlineNext24h) ++report.deadlineNext24hTasks;
        if (xpPending) ++report.xpPendingTasks;
        if (recordedXp) ++report.tasksWithXp;
        if (TaskProfileIds(task).empty()) ++report.unassignedTasks;
        if (!hasProject) ++report.tasksWithoutProject;
        if (!hasPipeline) ++report.tasksWithoutPipeline;

        int taskGlobalXp = 0;
        int taskSkillXp = 0;
        for (const auto& participant : task.participants) {
            taskGlobalXp += std::max(0, participant.globalXp);
            taskSkillXp += std::max(0, participant.skillXp);
        }
        report.totalGlobalXp += taskGlobalXp;
        report.totalSkillXp += taskSkillXp;

        const std::string projectKey = ProjectKey(task);
        auto& projectMetric = projectMetrics[projectKey];
        if (projectMetric.id.empty()) {
            projectMetric.id = projectKey == "__no_project" ? std::string{} : projectKey;
            projectMetric.name = ProjectName(task);
        }
        ++projectMetric.totalTasks;
        if (active) ++projectMetric.activeTasks;
        if (done) ++projectMetric.doneTasks;
        if (overdue) ++projectMetric.overdueTasks;
        if (xpPending) ++projectMetric.xpPendingTasks;
        if (!hasPipeline) ++projectMetric.withoutPipelineTasks;
        projectMetric.totalGlobalXp += taskGlobalXp;
        projectMetric.totalSkillXp += taskSkillXp;

        const auto profileIds = TaskProfileIds(task);
        for (const auto& profileId : profileIds) {
            auto& assignee = assigneeMetrics[profileId];
            if (assignee.profileId.empty()) assignee.profileId = profileId;
            ++assignee.totalTasks;
            if (active) ++assignee.activeTasks;
            if (done) ++assignee.doneTasks;
            if (overdue) ++assignee.overdueTasks;
            if (xpPending) ++assignee.xpPendingTasks;
        }
        for (const auto& participant : task.participants) {
            if (participant.profileId.empty()) continue;
            auto& assignee = assigneeMetrics[participant.profileId];
            if (assignee.profileId.empty()) assignee.profileId = participant.profileId;
            assignee.totalGlobalXp += std::max(0, participant.globalXp);
            assignee.totalSkillXp += std::max(0, participant.skillXp);
        }
    }

    for (auto& item : projectMetrics) {
        if (item.second.totalTasks <= 0) continue;
        if (item.second.activeTasks > 0) ++report.activeProjects;
        if (item.second.overdueTasks > 0) ++report.projectsWithOverdue;
        if (item.second.xpPendingTasks > 0) ++report.projectsWithXpPending;
        report.projects.push_back(std::move(item.second));
    }
    std::sort(report.projects.begin(), report.projects.end(), [](const auto& a, const auto& b) {
        if (a.overdueTasks != b.overdueTasks) return a.overdueTasks > b.overdueTasks;
        if (a.xpPendingTasks != b.xpPendingTasks) return a.xpPendingTasks > b.xpPendingTasks;
        if (a.activeTasks != b.activeTasks) return a.activeTasks > b.activeTasks;
        return a.name < b.name;
    });

    for (auto& item : assigneeMetrics) {
        report.assignees.push_back(std::move(item.second));
    }
    std::sort(report.assignees.begin(), report.assignees.end(), [](const auto& a, const auto& b) {
        if (a.activeTasks != b.activeTasks) return a.activeTasks > b.activeTasks;
        if (a.overdueTasks != b.overdueTasks) return a.overdueTasks > b.overdueTasks;
        return a.profileId < b.profileId;
    });

    if (report.totalTasks > 0) {
        const double overdueRate = static_cast<double>(report.overdueTasks) / static_cast<double>(report.totalTasks);
        const double xpPendingRate = static_cast<double>(report.xpPendingTasks) / static_cast<double>(report.totalTasks);
        const double noProjectRate = static_cast<double>(report.tasksWithoutProject) / static_cast<double>(report.totalTasks);
        const double noPipelineRate = static_cast<double>(report.tasksWithoutPipeline) / static_cast<double>(report.totalTasks);
        report.deliveryScore = std::clamp(100.0 - overdueRate * 60.0 - xpPendingRate * 25.0 -
                                          noProjectRate * 10.0 - noPipelineRate * 10.0,
                                          0.0, 100.0);
    }
    return report;
}

bool WriteTeamValueReportCsv(std::ostream& out, const TeamValueReport& report) {
    if (!out) return false;

    out << "\nTeamValueReport\n";
    out << "Section,Key,Value\n";
    WriteMetric(out, "TotalTasks", report.totalTasks);
    WriteMetric(out, "NewTasks", report.newTasks);
    WriteMetric(out, "InProgressTasks", report.inProgressTasks);
    WriteMetric(out, "DoneTasks", report.doneTasks);
    WriteMetric(out, "ActiveTasks", report.activeTasks);
    WriteMetric(out, "OverdueTasks", report.overdueTasks);
    WriteMetric(out, "DeadlineNext24hTasks", report.deadlineNext24hTasks);
    WriteMetric(out, "XpPendingTasks", report.xpPendingTasks);
    WriteMetric(out, "TasksWithXp", report.tasksWithXp);
    WriteMetric(out, "UnassignedTasks", report.unassignedTasks);
    WriteMetric(out, "TasksWithoutProject", report.tasksWithoutProject);
    WriteMetric(out, "TasksWithoutPipeline", report.tasksWithoutPipeline);
    WriteMetric(out, "TotalProjects", report.totalProjects);
    WriteMetric(out, "ActiveProjects", report.activeProjects);
    WriteMetric(out, "ProjectsWithOverdue", report.projectsWithOverdue);
    WriteMetric(out, "ProjectsWithXpPending", report.projectsWithXpPending);
    WriteMetric(out, "TotalGlobalXp", report.totalGlobalXp);
    WriteMetric(out, "TotalSkillXp", report.totalSkillXp);
    WriteMetric(out, "DeliveryScore", report.deliveryScore);

    out << "\nProjects\n";
    out << "ID,Name,TotalTasks,ActiveTasks,DoneTasks,OverdueTasks,XpPendingTasks,WithoutPipelineTasks,GlobalXP,SkillXP\n";
    for (const auto& project : report.projects) {
        out << CsvEscapeTeamValue(project.id) << ","
            << CsvEscapeTeamValue(project.name) << ","
            << project.totalTasks << ","
            << project.activeTasks << ","
            << project.doneTasks << ","
            << project.overdueTasks << ","
            << project.xpPendingTasks << ","
            << project.withoutPipelineTasks << ","
            << project.totalGlobalXp << ","
            << project.totalSkillXp << "\n";
    }

    out << "\nAssignees\n";
    out << "ProfileId,TotalTasks,ActiveTasks,DoneTasks,OverdueTasks,XpPendingTasks,GlobalXP,SkillXP\n";
    for (const auto& assignee : report.assignees) {
        out << CsvEscapeTeamValue(assignee.profileId) << ","
            << assignee.totalTasks << ","
            << assignee.activeTasks << ","
            << assignee.doneTasks << ","
            << assignee.overdueTasks << ","
            << assignee.xpPendingTasks << ","
            << assignee.totalGlobalXp << ","
            << assignee.totalSkillXp << "\n";
    }
    return out.good();
}
