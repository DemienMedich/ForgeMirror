#include <algorithm>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>

#include "AppProfileMutationService.h"
#include "AppPipelineService.h"
#include "AppRecoveryStorage.h"
#include "AppTaskProjectService.h"
#include "AppTaskWorkflowService.h"
#include "AppUtils.h"
#include "AppWorkspaceDataService.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"
#include "CloudSync.h"

IJobStorage* CreateFileStorage(const std::filesystem::path& dir);

static bool WriteFile(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    return out.good();
}

static bool ReadFile(const std::filesystem::path& path, std::string& outData) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    outData.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

static std::filesystem::path FindRepoRootFromCwd() {
    std::error_code ec;
    std::filesystem::path current = std::filesystem::current_path(ec);
    for (int i = 0; !ec && i < 8 && !current.empty(); ++i) {
        if (std::filesystem::exists(current / "gui" / "GuiTasksPanel.inc", ec)) {
            return current;
        }
        current = current.parent_path();
    }
    return {};
}

static size_t CountSubstring(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return 0;
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

static bool SetFileUnixTimestamp(const std::filesystem::path& path, std::int64_t unixSeconds) {
    using namespace std::chrono;
    std::error_code ec;
    const auto sysTarget = system_clock::time_point(seconds(unixSeconds));
    const auto delta = sysTarget - system_clock::now();
    const auto fsTarget = std::filesystem::file_time_type::clock::now()
        + duration_cast<std::filesystem::file_time_type::duration>(delta);
    std::filesystem::last_write_time(path, fsTarget, ec);
    return !ec;
}

static std::filesystem::path FindBackupByKind(const std::vector<std::filesystem::path>& backups,
                                              const std::string& kind) {
    const std::string token = "." + kind + ".";
    for (const auto& path : backups) {
        const std::string name = path.filename().string();
        if (name.find(token) != std::string::npos) {
            return path;
        }
    }
    return {};
}

static bool TestGameplayConfig(const std::filesystem::path& dir) {
    GameplayConfig cfg;
    cfg.levelBaseXp = 10;
    cfg.levelLinearXp = 2;
    cfg.levelQuadraticXp = 1;
    auto path = dir / "meta" / "gameplay.ini";
    if (!SaveGameplayConfig(cfg, dir)) return false;
    GameplayConfig loaded = LoadGameplayConfig(dir);
    return loaded.levelBaseXp == cfg.levelBaseXp && loaded.levelLinearXp == cfg.levelLinearXp;
}

static bool TestTasksPipelineRoundtrip(const std::filesystem::path& dir) {
    const std::string tasks = R"([{"id":"t1","title":"Task","project":"P"}])";
    const std::string pipeline = R"({"steps":[{"id":"p1","title":"A"}]})";
    const std::string audit = "1700000000|admin|t1|create||Task\n";
    if (!WriteFile(dir / "meta" / "tasks.json", tasks)) return false;
    if (!WriteFile(dir / "meta" / "pipeline.json", pipeline)) return false;
    if (!WriteFile(dir / "meta" / "task-audit.log", audit)) return false;
    ModuleToggles modules;
    WorkspaceSyncHealth health = InspectWorkspaceSyncHealth(dir, modules);
    return health.issueCount == 0 &&
           std::filesystem::exists(dir / "meta" / "tasks.json") &&
           std::filesystem::exists(dir / "meta" / "pipeline.json") &&
           std::filesystem::exists(dir / "meta" / "task-audit.log");
}

static bool TestTaskTextMutation(const std::filesystem::path& dir) {
    std::vector<TaskEntry> tasks;
    TaskEntry task;
    task.id = "task_text";
    task.title = "Old title";
    task.description = "Old description";
    task.createdAt = 1700000000;
    tasks.push_back(task);

    std::vector<TaskAuditEntry> audit;
    AppMutationResult result = AppUpdateTaskText(
        dir, tasks, task.id, "New title", "New description", "admin", &audit);
    if (!result.ok || !result.changed || tasks[0].title != "New title" ||
        tasks[0].description != "New description") {
        return false;
    }

    std::string saved;
    if (!ReadFile(dir / "meta" / "tasks.json", saved)) return false;
    if (saved.find("\"title\":\"New title\"") == std::string::npos ||
        saved.find("\"description\":\"New description\"") == std::string::npos) {
        return false;
    }

    std::string auditLog;
    if (!ReadFile(dir / "meta" / "task-audit.log", auditLog)) return false;
    return auditLog.find("|title|Old title|New title") != std::string::npos &&
           auditLog.find("|description|Old description|New description") != std::string::npos &&
           audit.size() == 2;
}

static bool TestTasksPipelineRecovery(const std::filesystem::path& dir) {
    auto fail = [](const char* reason) {
        std::cerr << "workspaceRecovery: " << reason << "\n";
        return false;
    };

    TaskEntry task;
    task.id = "recovery_task";
    task.title = "Recovery task";
    task.createdAt = 1700000000;
    if (!AppSaveTasks(dir, {task})) return fail("save tasks");

    PipelineStep step;
    step.id = "recovery_step";
    step.title = "Recovery step";
    if (!AppSavePipelineData(dir, {step})) return fail("save pipeline");

    ProjectEntry project;
    project.id = "recovery_project";
    project.name = "Recovery project";
    project.createdAt = 1700000000;
    if (!AppSaveProjects(dir, {project})) return fail("save projects");

    const auto tasksPath = dir / "meta" / "tasks.json";
    const auto pipelinePath = dir / "meta" / "pipeline.json";
    const auto projectsPath = dir / "meta" / "projects.json";
    if (!std::filesystem::exists(AppRecoveryBackupPath(tasksPath))) return fail("tasks backup");
    if (!std::filesystem::exists(AppRecoveryBackupPath(pipelinePath))) return fail("pipeline backup");
    if (!std::filesystem::exists(AppRecoveryBackupPath(projectsPath))) return fail("projects backup");
    if (!WriteFile(tasksPath, "[{\"id\":\"partial\"}")) return fail("break tasks");
    if (!WriteFile(pipelinePath, "{broken")) return fail("break pipeline");
    if (!WriteFile(projectsPath, "[{\"id\":\"broken\"}]")) return fail("break projects");

    ModuleToggles modules;
    const WorkspaceDataSnapshot snapshot = LoadWorkspaceDataSnapshot(dir, modules);
    if (snapshot.tasks.size() != 1 || snapshot.tasks[0].id != task.id) return fail("load tasks backup");
    const auto pipelineMatch = std::find_if(snapshot.pipelineSteps.begin(), snapshot.pipelineSteps.end(), [&](const PipelineStep& item) {
        return item.id == step.id && item.title == step.title;
    });
    if (pipelineMatch == snapshot.pipelineSteps.end()) return fail("load pipeline backup");
    if (snapshot.projects.size() != 1 || snapshot.projects[0].id != project.id) {
        return fail("load projects backup");
    }
    if (snapshot.recoveryWarnings.size() != 3) return fail("recovery warnings");

    std::string restoredTasks;
    std::string restoredPipeline;
    std::string restoredProjects;
    if (!ReadFile(tasksPath, restoredTasks) || restoredTasks.find(task.id) == std::string::npos) {
        return fail("restore tasks file");
    }
    if (!ReadFile(pipelinePath, restoredPipeline) || restoredPipeline.find(step.id) == std::string::npos) {
        return fail("restore pipeline file");
    }
    if (!ReadFile(projectsPath, restoredProjects) || restoredProjects.find(project.id) == std::string::npos) {
        return fail("restore projects file");
    }

    bool tasksDamagedCopy = false;
    bool pipelineDamagedCopy = false;
    bool projectsDamagedCopy = false;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir / "meta" / "updates", ec)) {
        const std::string name = entry.path().filename().string();
        tasksDamagedCopy = tasksDamagedCopy || name.find("tasks.corrupt.") == 0;
        pipelineDamagedCopy = pipelineDamagedCopy || name.find("pipeline.corrupt.") == 0;
        projectsDamagedCopy = projectsDamagedCopy || name.find("projects.corrupt.") == 0;
    }
    if (ec || !tasksDamagedCopy || !pipelineDamagedCopy || !projectsDamagedCopy) {
        return fail("damaged copies");
    }

    const WorkspaceDataSnapshot cleanReload = LoadWorkspaceDataSnapshot(dir, modules);
    if (!cleanReload.recoveryWarnings.empty()) return fail("repeated recovery warning");
    return true;
}

static bool TestWorkspaceMutationsRollBackWhenSaveFails(const std::filesystem::path& dir) {
    auto fail = [](const char* reason) {
        AppSetRecoveryPrimaryWriteFailureForTests(false);
        std::cerr << "workspaceSaveRollback: " << reason << "\n";
        return false;
    };

    TaskEntry task;
    task.id = "rollback_task";
    task.title = "Old task";
    task.description = "Old description";
    std::vector<TaskEntry> tasks = {task};
    if (!AppSaveTasks(dir, tasks)) return fail("initial tasks save");

    ProjectEntry project;
    project.id = "rollback_project";
    project.name = "Old project";
    std::vector<ProjectEntry> projects = {project};
    if (!AppSaveProjects(dir, projects)) return fail("initial projects save");

    PipelineStep step;
    step.id = "rollback_step";
    step.title = "Old pipeline";
    std::vector<PipelineStep> pipeline = {step};
    if (!AppSavePipelineData(dir, pipeline)) return fail("initial pipeline save");

    AppSetRecoveryPrimaryWriteFailureForTests(true);
    const AppMutationResult taskResult = AppUpdateTaskText(
        dir, tasks, task.id, "New task", "New description", "admin");
    const AppProjectSaveResult projectResult = AppSaveProjectEntry(
        dir, projects, 0, "New project", "New description");
    PipelineStep updatedStep = step;
    updatedStep.title = "New pipeline";
    const AppPipelineMutationResult pipelineResult = AppUpdatePipelineStep(dir, pipeline, 0, updatedStep);
    AppSetRecoveryPrimaryWriteFailureForTests(false);

    if (taskResult.ok || tasks[0].title != task.title || tasks[0].description != task.description) {
        return fail("task memory rollback");
    }
    if (projectResult.ok || projects[0].name != project.name) return fail("project memory rollback");
    if (pipelineResult.ok || pipeline[0].title != step.title) return fail("pipeline memory rollback");

    const auto loadedTasks = LoadTasksData(dir);
    const auto loadedProjects = LoadProjectsData(dir);
    const auto loadedPipeline = LoadPipelineData(dir);
    if (loadedTasks.size() != 1 || loadedTasks[0].title != task.title) return fail("task disk rollback");
    if (loadedProjects.size() != 1 || loadedProjects[0].name != project.name) return fail("project disk rollback");
    const auto pipelineMatch = std::find_if(loadedPipeline.begin(), loadedPipeline.end(), [&](const PipelineStep& item) {
        return item.id == step.id && item.title == step.title;
    });
    if (pipelineMatch == loadedPipeline.end()) return fail("pipeline disk rollback");

    std::string tasksBackup;
    if (!ReadFile(AppRecoveryBackupPath(dir / "meta" / "tasks.json"), tasksBackup) ||
        tasksBackup.find("New task") != std::string::npos) {
        return fail("uncommitted backup");
    }
    return true;
}

static bool TestTaskFinalizeXpRollsBackWhenAuditFails(const std::filesystem::path& dir) {
    auto fail = [](const char* reason) {
        std::cerr << "taskFinalizeRollback: " << reason << "\n";
        return false;
    };
    std::vector<TaskEntry> tasks;
    TaskEntry task;
    task.id = "task_finalize";
    task.title = "Finalize XP";
    task.status = 1;
    task.category = 0;
    task.score = 0;
    task.baseXp = 0;
    task.basePool = 0;
    task.assignees = {"p_old"};
    task.skillIds = {"skill_old"};
    task.createdAt = 1700000000;
    tasks.push_back(task);
    if (!AppSaveTasks(dir, tasks)) return fail("initial save");

    TaskParticipant participant;
    participant.profileId = "p_new";
    participant.percent = 100;
    participant.globalXp = 20;
    participant.skillXp = 10;
    std::vector<TaskAuditEntry> audit;
    TaskXpFinalizeRequest request;
    request.taskId = task.id;
    request.category = 2;
    request.score = 4;
    request.baseXp = 20;
    request.basePool = 20;
    request.assignees = {"p_new"};
    request.skillIds = {"skill_new"};
    request.participants = {participant};
    request.actor = "admin";
    AppSetTaskAuditFailureHookForTests(true);
    AppTaskWorkflowService workflow(dir, tasks, &audit);
    AppMutationResult result = workflow.FinalizeXp(request);
    AppSetTaskAuditFailureHookForTests(false);

    if (result.ok) return fail("result ok");
    if (result.errorMessage.find("task-audit.log") == std::string::npos) return fail("wrong error");
    if (tasks[0].status != 1) return fail("status not restored");
    if (tasks[0].category != 0) return fail("category not restored");
    if (tasks[0].score != 0) return fail("score not restored");
    if (tasks[0].baseXp != 0) return fail("baseXp not restored");
    if (tasks[0].basePool != 0) return fail("basePool not restored");
    if (tasks[0].assignees != std::vector<std::string>{"p_old"}) return fail("assignees not restored");
    if (tasks[0].skillIds != std::vector<std::string>{"skill_old"}) return fail("skills not restored");
    if (!tasks[0].participants.empty()) return fail("participants not restored");
    if (!audit.empty()) return fail("audit cache changed");

    std::string saved;
    if (!ReadFile(dir / "meta" / "tasks.json", saved)) return fail("read saved");
    if (saved.find("\"status\":1") == std::string::npos) return fail("saved status");
    if (saved.find("\"category\":0") == std::string::npos) return fail("saved category");
    if (saved.find("\"assignees\":\"p_old\"") == std::string::npos) return fail("saved assignees");
    if (saved.find("\"skillIds\":\"skill_old\"") == std::string::npos) return fail("saved skills");
    if (saved.find("p_new") != std::string::npos) return fail("saved new assignee");
    if (saved.find("skill_new") != std::string::npos) return fail("saved new skill");
    return true;
}

static bool TestTaskFinalizeXpValidatesWorkflowContract(const std::filesystem::path& dir) {
    std::vector<TaskEntry> tasks;
    TaskEntry task;
    task.id = "task_contract";
    task.title = "Contract XP";
    task.status = 2;
    task.category = 0;
    task.createdAt = 1700000000;
    tasks.push_back(task);

    TaskParticipant participant;
    participant.profileId = "p1";
    participant.percent = 90;
    participant.globalXp = 10;
    participant.skillXp = 5;

    TaskXpFinalizeRequest request;
    request.taskId = task.id;
    request.category = 1;
    request.score = 5;
    request.baseXp = 10;
    request.basePool = 10;
    request.assignees = {"p1"};
    request.skillIds = {"skill"};
    request.participants = {participant};
    request.actor = "admin";

    AppTaskWorkflowService workflow(dir, tasks);
    AppMutationResult invalidPercent = workflow.ValidateFinalizeXp(request);
    if (invalidPercent.ok) return false;

    request.participants[0].percent = 100;
    AppMutationResult valid = workflow.ValidateFinalizeXp(request);
    if (!valid.ok) return false;

    tasks[0].participants = request.participants;
    AppMutationResult duplicate = workflow.ValidateFinalizeXp(request);
    return !duplicate.ok;
}

static bool TestTaskXpDistributionHelpers() {
    const std::vector<int> splitA = AppTaskWorkflowService::DistributeIntegerPool(101, {33, 33, 34});
    if (splitA != std::vector<int>({33, 33, 35})) return false;

    const std::vector<int> splitB = AppTaskWorkflowService::DistributeIntegerPool(99, {50, 50});
    if (splitB != std::vector<int>({50, 49})) return false;

    const std::vector<int> splitC = AppTaskWorkflowService::DistributeIntegerPool(20, {0, 100, -5});
    if (splitC != std::vector<int>({0, 20, 0})) return false;

    if (AppTaskWorkflowService::ApplyPercentPenalty(101, 25) != 76) return false;
    if (AppTaskWorkflowService::ApplyPercentPenalty(100, 150) != 0) return false;
    if (AppTaskWorkflowService::ApplyPercentPenalty(-100, 10) != 0) return false;
    return true;
}

static bool TestGuiTasksImGuiStackPatterns() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string source;
    if (!ReadFile(root / "gui" / "GuiTasksPanel.inc", source)) return false;

    if (source.find("&& BeginCard(") != std::string::npos) {
        return false;
    }

    const size_t beginCards = CountSubstring(source, "BeginCard(");
    const size_t endCards = CountSubstring(source, "EndCard();");
    return beginCards == endCards;
}

static bool TestTaskWorkflowServiceBoundary() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string tasksPanel;
    std::string xpModal;
    std::string workflowHeader;
    std::string workflowSource;
    if (!ReadFile(root / "gui" / "GuiTasksPanel.inc", tasksPanel)) return false;
    if (!ReadFile(root / "gui" / "GuiXpModal.inc", xpModal)) return false;
    if (!ReadFile(root / "include" / "AppTaskWorkflowService.h", workflowHeader)) return false;
    if (!ReadFile(root / "src" / "AppTaskWorkflowService.cpp", workflowSource)) return false;

    const std::string guiSource = tasksPanel + xpModal;
    return workflowHeader.find("class AppTaskWorkflowService") != std::string::npos &&
           workflowSource.find("AppTaskWorkflowService::FinalizeXp") != std::string::npos &&
           guiSource.find("AppTaskWorkflowService workflow") != std::string::npos &&
           guiSource.find("AppUpdateTaskStatus(") == std::string::npos &&
           guiSource.find("AppBulkUpdateTaskStatus(") == std::string::npos &&
           guiSource.find("AppFinalizeTaskXp(") == std::string::npos &&
           guiSource.find("AppApplyPercentPenalty(") == std::string::npos &&
           guiSource.find("AppDistributeIntegerPool(") == std::string::npos;
}

static int CountImGuiPopCalls(const std::string& source, const char* call) {
    const std::regex pattern(std::string("ImGui::") + call + R"(\s*\(\s*([0-9]*)\s*\))");
    int count = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), pattern), end; it != end; ++it) {
        const std::string amount = (*it)[1].str();
        count += amount.empty() ? 1 : std::stoi(amount);
    }
    return count;
}

static bool TestGuiImGuiScopeTotals() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;

    struct ScopePair {
        const char* begin;
        const char* end;
        bool countedEnd;
    };
    const ScopePair pairs[] = {
        {"ImGui::PushStyleColor(", "PopStyleColor", true},
        {"ImGui::PushStyleVar(", "PopStyleVar", true},
        {"ImGui::BeginDisabled(", "ImGui::EndDisabled(", false},
        {"ImGui::BeginTable(", "ImGui::EndTable(", false},
        {"ImGui::BeginCombo(", "ImGui::EndCombo(", false},
        {"ImGui::BeginTabBar(", "ImGui::EndTabBar(", false},
        {"ImGui::BeginTabItem(", "ImGui::EndTabItem(", false},
        {"ImGui::BeginTooltip(", "ImGui::EndTooltip(", false},
    };

    std::string combinedSource;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "gui", ec)) {
        if (ec) return false;
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension().string();
        if (extension != ".inc" && extension != ".cpp") continue;

        std::string source;
        if (!ReadFile(entry.path(), source)) return false;
        combinedSource += source;
        combinedSource.push_back('\n');
    }
    for (const ScopePair& pair : pairs) {
        const int beginCount = static_cast<int>(CountSubstring(combinedSource, pair.begin));
        const int endCount = pair.countedEnd
            ? CountImGuiPopCalls(combinedSource, pair.end)
            : static_cast<int>(CountSubstring(combinedSource, pair.end));
        if (beginCount != endCount) {
            std::cerr << "guiScopeTotals: " << pair.begin << "=" << beginCount
                      << " end=" << endCount << "\n";
            return false;
        }
    }
    return !ec;
}

static bool TestGuiPipelineImGuiStackPatterns() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string source;
    if (!ReadFile(root / "gui" / "GuiPipelinePanel.inc", source)) return false;

    const size_t actions = source.find("if (ImGui::BeginTable(\"pipeline_edit_actions\"");
    const size_t actionsEnd = source.find("                    ImGui::EndTable();\n                }", actions);
    return actions != std::string::npos && actionsEnd != std::string::npos && actionsEnd > actions &&
           source.find("CompactTableScopeGui splitCompactTable") != std::string::npos &&
           source.find("BeginTable(\"pipeline_split\", 2, ControlTableFlagsGui()") != std::string::npos &&
           source.find("BeginTable(\"pipeline_filter_row\", 3, ControlTableFlagsGui()") != std::string::npos &&
           source.find("BeginTable(\"pipeline_editor_grid\", 2, ControlTableFlagsGui()") != std::string::npos &&
           source.find("BeginTable(\"pipeline_meta_table\", 2, ControlTableFlagsGui()") != std::string::npos &&
           source.find("pipeline_select_hint") != std::string::npos;
}

static bool TestGuiEmptyStateRegistersLayoutSize() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string source;
    if (!ReadFile(root / "gui" / "GuiIcons.inc", source)) return false;

    const size_t fn = source.find("static bool DrawEmptyStateGui(");
    if (fn == std::string::npos) return false;
    const size_t dummy = source.find("ImGui::Dummy(ImVec2(width, height + style.ItemSpacing.y));", fn);
    const size_t pop = source.find("ImGui::PopID();", fn);
    return dummy != std::string::npos && pop != std::string::npos && dummy < pop;
}

static bool TestGuiRowStateHelpersUsedAcrossModules() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string icons;
    std::string tasks;
    std::string projects;
    std::string profilePanel;
    std::string profileSections;
    if (!ReadFile(root / "gui" / "GuiIcons.inc", icons)) return false;
    if (!ReadFile(root / "gui" / "GuiTasksPanel.inc", tasks)) return false;
    if (!ReadFile(root / "gui" / "GuiProjects.inc", projects)) return false;
    if (!ReadFile(root / "gui" / "GuiProfilePanel.inc", profilePanel)) return false;
    if (!ReadFile(root / "gui" / "GuiProfileSections.inc", profileSections)) return false;

    return icons.find("static void ApplyTableRowStateTintGui(") != std::string::npos &&
           CountSubstring(tasks, "ApplyTableRowStateTintGui(") >= 2 &&
           projects.find("ApplyTableRowStateTintGui(") != std::string::npos &&
           profilePanel.find("ApplyTableRowStateTintGui(") != std::string::npos &&
           CountSubstring(profileSections, "ApplyTableRowStateTintGui(") >= 2;
}

static bool TestCompactControlTablesUseSharedScope() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string helpers;
    std::string logs;
    std::string adminStats;
    std::string rules;
    std::string tasks;
    std::string profileSections;
    std::string projects;
    std::string pipeline;
    std::string banner;
    std::string professions;
    std::string vault;
    std::string profilePanel;
    std::string mainMenu;
    std::string profileModals;
    std::string skillModals;
    std::string skillCatalog;
    std::string adminModal;
    std::string shortcuts;
    std::string uiSettings;
    std::string xpModal;
    if (!ReadFile(root / "gui" / "GuiUiHelpers.inc", helpers)) return false;
    if (!ReadFile(root / "gui" / "GuiLogsPanel.inc", logs)) return false;
    if (!ReadFile(root / "gui" / "GuiAdminStatsPanel.inc", adminStats)) return false;
    if (!ReadFile(root / "gui" / "GuiRulesPanel.inc", rules)) return false;
    if (!ReadFile(root / "gui" / "GuiTasksPanel.inc", tasks)) return false;
    if (!ReadFile(root / "gui" / "GuiProfileSections.inc", profileSections)) return false;
    if (!ReadFile(root / "gui" / "GuiProjects.inc", projects)) return false;
    if (!ReadFile(root / "gui" / "GuiPipelinePanel.inc", pipeline)) return false;
    if (!ReadFile(root / "gui" / "GuiBannerPanel.inc", banner)) return false;
    if (!ReadFile(root / "gui" / "GuiProfessions.inc", professions)) return false;
    if (!ReadFile(root / "gui" / "GuiVaultPanel.inc", vault)) return false;
    if (!ReadFile(root / "gui" / "GuiProfilePanel.inc", profilePanel)) return false;
    if (!ReadFile(root / "gui" / "GuiMainMenuPanel.inc", mainMenu)) return false;
    if (!ReadFile(root / "gui" / "GuiProfileModals.inc", profileModals)) return false;
    if (!ReadFile(root / "gui" / "GuiSkillModals.inc", skillModals)) return false;
    if (!ReadFile(root / "gui" / "GuiSkillCatalogPanel.inc", skillCatalog)) return false;
    if (!ReadFile(root / "gui" / "GuiAdminModal.inc", adminModal)) return false;
    if (!ReadFile(root / "gui" / "GuiShortcuts.inc", shortcuts)) return false;
    if (!ReadFile(root / "gui" / "GuiUiSettingsPanel.inc", uiSettings)) return false;
    if (!ReadFile(root / "gui" / "GuiXpModal.inc", xpModal)) return false;

    return helpers.find("struct CompactTableScopeGui") != std::string::npos &&
           helpers.find("ControlTableFlagsGui(") != std::string::npos &&
           logs.find("CompactTableScopeGui compactTable") != std::string::npos &&
           logs.find("BeginTable(\"log_filters\", 6, ControlTableFlagsGui()") != std::string::npos &&
           adminStats.find("BeginTable(\"admin_filters\", 6, ControlTableFlagsGui()") != std::string::npos &&
           adminStats.find("BeginTable(\"admin_refresh\", 5, ControlTableFlagsGui()") != std::string::npos &&
           adminStats.find("BeginTable(\"admin_kpi\", 3, ControlTableFlagsGui()") != std::string::npos &&
           adminStats.find("BeginTable(\"inactive_table\", 6, ControlTableFlagsGui(ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)") != std::string::npos &&
           CountSubstring(adminStats, "CompactTableScopeGui compactTable") >= 7 &&
           rules.find("BeginTable(\"rules_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("tasksCompactCellPadding") == std::string::npos &&
           CountSubstring(tasks, "CompactTableScopeGui") >= 7 &&
           tasks.find("BeginTable(\"tasks_filters\", 6, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_filters_extra\", 4, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_bulk_compact_row1\", 8, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_summary_table\", 5, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_table\", tableColumns, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           tasks.find("BeginTable(\"task_plan_steps\", 4, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_create_form\", 2, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_plan_assignees\", 3, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           tasks.find("BeginTable(\"task_plan_review\", 4, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_plan_navigation\", 3, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_projects_bridge_table\", 4, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_work_queue_metrics\", queueColumns, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_pipeline_risks_table\", riskColumns, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_detail_headline\", 2, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_detail_meta\", 4, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_detail_text_actions\", 2, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_detail_admin_meta\", 2, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"task_detail_skill_actions\", 2, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_active_tasks_table\", 5,") != std::string::npos &&
           profileSections.find("ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_tasks\", 5, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_state_summary\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_balance_table\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_status\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_active_tasks_summary\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"top_skills\", 4, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           profileSections.find("BeginTable(\"ach_filters\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"profile_skill_filters\", 5, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"cat_bars\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"top_skill_bars\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"radar_controls\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileSections.find("BeginTable(\"activity_filters\", 5, ControlTableFlagsGui()") != std::string::npos &&
           CountSubstring(profileSections, "CompactTableScopeGui compactTable") >= 13 &&
           projects.find("BeginTable(\"projects_status_slice\", 4, ControlTableFlagsGui()") != std::string::npos &&
           projects.find("BeginTable(\"projects_table\", 5, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           CountSubstring(projects, "CompactTableScopeGui compactTable") >= 3 &&
           pipeline.find("BeginTable(\"pipeline_filter_row\", 3, ControlTableFlagsGui()") != std::string::npos &&
           pipeline.find("BeginTable(\"pipeline_split\", 2, ControlTableFlagsGui()") != std::string::npos &&
           pipeline.find("BeginTable(\"pipeline_editor_grid\", 2, ControlTableFlagsGui()") != std::string::npos &&
           pipeline.find("BeginTable(\"pipeline_meta_table\", 2, ControlTableFlagsGui()") != std::string::npos &&
           logs.find("BeginTable(\"log_task_audit_filters\", 4, ControlTableFlagsGui()") != std::string::npos &&
           logs.find("BeginTable(\"log_task_audit_table\", 5,") != std::string::npos &&
           logs.find("ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           banner.find("BeginTable(\"banner_rows\", 3, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           professions.find("BeginTable(\"prof_list\", 3, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           professions.find("BeginTable(\"prof_members\", 3, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           vault.find("BeginTable(\"vault_pomodoro_time\", 4, ControlTableFlagsGui()") != std::string::npos &&
           vault.find("BeginTable(\"vault_log_table\", 4, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           mainMenu.find("BeginTable(\"profile_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           mainMenu.find("BeginTable(\"profile_filters\", 5, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_select_actions\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_task_brief_rows\", 4,") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_task_brief_stats\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_header_compact\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_info\", 2, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_signal_cards\", signalColumns, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_rank_actions\", 4, ControlTableFlagsGui()") != std::string::npos &&
           profilePanel.find("BeginTable(\"profile_report_actions\", 5, ControlTableFlagsGui()") != std::string::npos &&
           profileModals.find("BeginTable(\"create_profile_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileModals.find("BeginTable(\"confirm_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           profileModals.find("BeginTable(\"unlock_actions\", 2, ControlTableFlagsGui()") != std::string::npos &&
           profileModals.find("BeginTable(\"change_actions\", 2, ControlTableFlagsGui()") != std::string::npos &&
           profileModals.find("BeginTable(\"reset_actions\", 2, ControlTableFlagsGui()") != std::string::npos &&
           skillModals.find("BeginTable(\"add_skill_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillModals.find("BeginTable(\"delete_skill_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillModals.find("BeginTable(\"merge_skill_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillModals.find("BeginTable(\"clear_skills_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"catalog_split\", 2,") != std::string::npos &&
           skillCatalog.find("ControlTableFlagsGui(ImGuiTableFlags_Resizable)") != std::string::npos &&
           skillCatalog.find("BeginTable(\"catalog_filters\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"catalog_kpi\", columns, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"catalog_weights\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"catalog_actions\", 5, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"catalog_table\", 4,") != std::string::npos &&
           skillCatalog.find("BeginTable(\"edit_skill_professions\", profColumns, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"skill_ach_filters\", 3, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"skill_ach_icon_row\", 2, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("BeginTable(\"skill_ach_meta\", 2, ControlTableFlagsGui()") != std::string::npos &&
           skillCatalog.find("ImGuiTableFlags_SizingStretch") == std::string::npos &&
           CountSubstring(skillCatalog, "CompactTableScopeGui compactTable") >= 9 &&
           adminModal.find("BeginTable(\"admin_login_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           adminModal.find("BeginTable(\"admin_password_actions\", 3, ControlTableFlagsGui()") != std::string::npos &&
           CountSubstring(adminModal, "CompactTableScopeGui compactTable") >= 2 &&
           logs.find("BeginTable(\"log_kpi\", columns, ControlTableFlagsGui()") != std::string::npos &&
           logs.find("BeginTable(\"log_toggles\", 3, ControlTableFlagsGui()") != std::string::npos &&
           rules.find("BeginTable(\"level_rules\", 3, ControlTableFlagsGui()") != std::string::npos &&
           rules.find("BeginTable(\"category_rules\", 3, ControlTableFlagsGui()") != std::string::npos &&
           rules.find("BeginTable(\"bonus_rules\", 3, ControlTableFlagsGui()") != std::string::npos &&
           tasks.find("BeginTable(\"tasks_toolbar_row\", state.isAdmin ? 5 : 3,") != std::string::npos &&
           shortcuts.find("BeginTable(\"shortcuts_list\", 3, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           CountSubstring(uiSettings, "CompactTableScopeGui compactTable") >= 8 &&
           uiSettings.find("ImGuiTableFlags_SizingStretch") == std::string::npos &&
           xpModal.find("BeginTable(\"xp_task_header\", 2, ControlTableFlagsGui()") != std::string::npos &&
           xpModal.find("BeginTable(\"task_member_controls\", 3, ControlTableFlagsGui()") != std::string::npos &&
           xpModal.find("BeginTable(\"task_members\", 3, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos &&
           xpModal.find("BeginTable(\"xp_controls\", 5, ControlTableFlagsGui()") != std::string::npos &&
           xpModal.find("BeginTable(\"xp_sheet\", 6, ControlTableFlagsGui(ImGuiTableFlags_RowBg)") != std::string::npos;
}

static bool TestProfileTaskEmptyStatesUseSharedHelper() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string profilePanel;
    std::string profileSections;
    if (!ReadFile(root / "gui" / "GuiProfilePanel.inc", profilePanel)) return false;
    if (!ReadFile(root / "gui" / "GuiProfileSections.inc", profileSections)) return false;

    return profilePanel.find("profile_task_brief_empty") != std::string::npos &&
           CountSubstring(profilePanel, "DrawEmptyStateGui(") >= 1 &&
           profileSections.find("profile_active_tasks_empty") != std::string::npos &&
           profileSections.find("profile_done_tasks_empty") != std::string::npos &&
           CountSubstring(profileSections, "DrawEmptyStateGui(") >= 2;
}

static bool TestTasksDetailEmptyStatesUseSharedHelper() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string tasks;
    if (!ReadFile(root / "gui" / "GuiTasksPanel.inc", tasks)) return false;

    return tasks.find("tasks_summary_empty_filter") != std::string::npos &&
           tasks.find("tasks_activity_empty_period") != std::string::npos &&
           tasks.find("tasks_detail_assignees_empty") != std::string::npos &&
           tasks.find("tasks_detail_empty_selection") != std::string::npos &&
           CountSubstring(tasks, "DrawEmptyStateGui(") >= 7;
}

static bool TestSharedEmptyStatesUsedInServicePanels() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string logs;
    std::string vault;
    std::string banner;
    std::string adminStats;
    if (!ReadFile(root / "gui" / "GuiLogsPanel.inc", logs)) return false;
    if (!ReadFile(root / "gui" / "GuiVaultPanel.inc", vault)) return false;
    if (!ReadFile(root / "gui" / "GuiBannerPanel.inc", banner)) return false;
    if (!ReadFile(root / "gui" / "GuiAdminStatsPanel.inc", adminStats)) return false;

    return logs.find("logs_empty_all") != std::string::npos &&
           logs.find("task_audit_empty_all") != std::string::npos &&
           CountSubstring(logs, "DrawEmptyStateGui(") >= 4 &&
           vault.find("vault_log_empty") != std::string::npos &&
           banner.find("banner_texts_empty") != std::string::npos &&
           adminStats.find("admin_stats_empty_all") != std::string::npos &&
           adminStats.find("admin_stats_empty_filtered") != std::string::npos;
}

static bool TestSharedEmptyStatesUsedInProfileAdminPanels() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string profilePanel;
    std::string professions;
    std::string adminStats;
    if (!ReadFile(root / "gui" / "GuiProfilePanel.inc", profilePanel)) return false;
    if (!ReadFile(root / "gui" / "GuiProfessions.inc", professions)) return false;
    if (!ReadFile(root / "gui" / "GuiAdminStatsPanel.inc", adminStats)) return false;

    return profilePanel.find("profile_select_empty") != std::string::npos &&
           profilePanel.find("profile_body_select_profile") != std::string::npos &&
           professions.find("profession_members_empty") != std::string::npos &&
           professions.find("profession_skills_empty") != std::string::npos &&
           professions.find("profession_select_empty") != std::string::npos &&
           adminStats.find("admin_stats_inactive_empty") != std::string::npos &&
           adminStats.find("admin_stats_recovery_empty") != std::string::npos;
}

static bool TestProfileModalsMissingProfileUsesSharedEmptyState() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string profileModals;
    if (!ReadFile(root / "gui" / "GuiProfileModals.inc", profileModals)) return false;

    return profileModals.find("profile_confirm_missing_empty") != std::string::npos &&
           profileModals.find("profile_unlock_missing_empty") != std::string::npos &&
           profileModals.find("profile_change_password_missing_empty") != std::string::npos &&
           profileModals.find("profile_reset_password_missing_empty") != std::string::npos &&
           profileModals.find("TextDisabled(u8\"Профиль не выбран.") == std::string::npos &&
           CountSubstring(profileModals, "DrawEmptyStateGui(") >= 4;
}

static bool TestSharedEmptyStatesUsedInProfileSections() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string profileSections;
    if (!ReadFile(root / "gui" / "GuiProfileSections.inc", profileSections)) return false;

    return profileSections.find("profile_achievements_empty") != std::string::npos &&
           profileSections.find("profile_skills_filter_empty") != std::string::npos &&
           profileSections.find("profile_chart_radar_empty") != std::string::npos &&
           profileSections.find("profile_activity_empty") != std::string::npos &&
           CountSubstring(profileSections, "DrawEmptyStateGui(") >= 10;
}

static bool TestSharedEmptyStatesUsedInSkillCatalog() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string catalogPanel;
    if (!ReadFile(root / "gui" / "GuiSkillCatalogPanel.inc", catalogPanel)) return false;

    return catalogPanel.find("catalog_empty_all") != std::string::npos &&
           catalogPanel.find("catalog_summary_empty") != std::string::npos &&
           catalogPanel.find("catalog_skill_list_empty") != std::string::npos &&
           catalogPanel.find("skill_achievements_empty") != std::string::npos &&
           catalogPanel.find("skill_icon_picker_filter_empty") != std::string::npos &&
           catalogPanel.find("catalog_detail_empty") != std::string::npos &&
           CountSubstring(catalogPanel, "DrawEmptyStateGui(") >= 8;
}

static bool TestProfileSkillUtilityEmptyStatesUseSharedHelper() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string mainMenu;
    std::string skillModals;
    if (!ReadFile(root / "gui" / "GuiMainMenuPanel.inc", mainMenu)) return false;
    if (!ReadFile(root / "gui" / "GuiSkillModals.inc", skillModals)) return false;

    return mainMenu.find("profiles_filter_empty") != std::string::npos &&
           skillModals.find("merge_skill_empty") != std::string::npos &&
           mainMenu.find("TextDisabled(u8\"Нет профилей по фильтру.") == std::string::npos &&
           skillModals.find("TextDisabled(u8\"Нет данных для слияния.") == std::string::npos;
}

static bool TestSemanticActionIconsUsedForCriticalActions() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string icons;
    std::string tasks;
    std::string pipeline;
    std::string rules;
    std::string catalog;
    std::string navigation;
    std::string mainMenu;
    std::string projects;
    std::string skillModals;
    std::string profilePanel;
    std::string profileSections;
    std::string logs;
    std::string adminStats;
    std::string uiSettings;
    std::string xpModal;
    if (!ReadFile(root / "gui" / "GuiIcons.inc", icons)) return false;
    if (!ReadFile(root / "gui" / "GuiTasksPanel.inc", tasks)) return false;
    if (!ReadFile(root / "gui" / "GuiPipelinePanel.inc", pipeline)) return false;
    if (!ReadFile(root / "gui" / "GuiRulesPanel.inc", rules)) return false;
    if (!ReadFile(root / "gui" / "GuiSkillCatalogPanel.inc", catalog)) return false;
    if (!ReadFile(root / "gui" / "GuiNavigationPanel.inc", navigation)) return false;
    if (!ReadFile(root / "gui" / "GuiMainMenuPanel.inc", mainMenu)) return false;
    if (!ReadFile(root / "gui" / "GuiProjects.inc", projects)) return false;
    if (!ReadFile(root / "gui" / "GuiSkillModals.inc", skillModals)) return false;
    if (!ReadFile(root / "gui" / "GuiProfilePanel.inc", profilePanel)) return false;
    if (!ReadFile(root / "gui" / "GuiProfileSections.inc", profileSections)) return false;
    if (!ReadFile(root / "gui" / "GuiLogsPanel.inc", logs)) return false;
    if (!ReadFile(root / "gui" / "GuiAdminStatsPanel.inc", adminStats)) return false;
    if (!ReadFile(root / "gui" / "GuiUiSettingsPanel.inc", uiSettings)) return false;
    if (!ReadFile(root / "gui" / "GuiXpModal.inc", xpModal)) return false;

    return icons.find("Save,") != std::string::npos &&
           icons.find("Edit,") != std::string::npos &&
           icons.find("Award,") != std::string::npos &&
           icons.find("Export,") != std::string::npos &&
           icons.find("Task,") != std::string::npos &&
           icons.find("Project,") != std::string::npos &&
           icons.find("Shortcut,") != std::string::npos &&
           icons.find("Sync,") != std::string::npos &&
           icons.find("Apply,") != std::string::npos &&
           icons.find("Calendar,") != std::string::npos &&
           icons.find("Merge,") != std::string::npos &&
           icons.find("Clear,") != std::string::npos &&
           icons.find("Assign,") != std::string::npos &&
           icons.find("FileText,") != std::string::npos &&
           icons.find("Focus,") != std::string::npos &&
           icons.find("SelectAll,") != std::string::npos &&
           icons.find("Warning,") != std::string::npos &&
           icons.find("Timer,") != std::string::npos &&
           icons.find("Filter,") != std::string::npos &&
           icons.find("Image,") != std::string::npos &&
           tasks.find("\"task_detail_text_save\", UiIcon::Save") != std::string::npos &&
           tasks.find("\"task_detail_skill_save\", UiIcon::Save") != std::string::npos &&
           tasks.find("\"task_detail_award_xp\", UiIcon::Award") != std::string::npos &&
           tasks.find("\"tasks_export_csv\", UiIcon::Export") != std::string::npos &&
           tasks.find("\"tasks_export_txt\", UiIcon::FileText") != std::string::npos &&
           tasks.find("\"tasks_empty_all\", UiIcon::Task") != std::string::npos &&
           tasks.find("\"tasks_toolbar_reset\", UiIcon::Clear") != std::string::npos &&
           tasks.find("\"bulk_apply_deadline\", UiIcon::Calendar") != std::string::npos &&
           tasks.find("\"bulk_open_assignees\", UiIcon::Assign") != std::string::npos &&
           tasks.find("\"bridge_focus\", UiIcon::Focus") != std::string::npos &&
           tasks.find("\"queue_open\", UiIcon::Focus") != std::string::npos &&
           tasks.find("\"tasks_focus_open\", UiIcon::Focus") != std::string::npos &&
           tasks.find("\"bulk_select_visible\", UiIcon::SelectAll") != std::string::npos &&
           tasks.find("\"tasks_empty_filtered\", UiIcon::Filter") != std::string::npos &&
           tasks.find("\"tasks_summary_empty_filter\", UiIcon::Filter") != std::string::npos &&
           tasks.find("UiIcon::Clear, u8\"Сбросить фильтры задач\"") != std::string::npos &&
           pipeline.find("\"pipeline_save\", UiIcon::Save") != std::string::npos &&
           pipeline.find("\"pipeline_edit\", UiIcon::Edit") != std::string::npos &&
           pipeline.find("\"pipeline_filter_reset\", UiIcon::Clear") != std::string::npos &&
           rules.find("\"rules_save\", UiIcon::Save") != std::string::npos &&
           catalog.find("\"catalog_save_skill_details\", UiIcon::Save") != std::string::npos &&
           navigation.find("\"nav_tasks\", UiIcon::Task") != std::string::npos &&
           navigation.find("\"nav_projects\", UiIcon::Project") != std::string::npos &&
           navigation.find("\"nav_shortcuts\", UiIcon::Shortcut") != std::string::npos &&
           navigation.find("\"nav_pomodoro\", UiIcon::Timer") != std::string::npos &&
           navigation.find("\"nav_banner\", UiIcon::Bell") != std::string::npos &&
           mainMenu.find("\"top_sync\", UiIcon::Sync") != std::string::npos &&
           mainMenu.find("\"profiles_filter_empty\", UiIcon::Filter") != std::string::npos &&
           projects.find("\"project_editor_save\", UiIcon::Save") != std::string::npos &&
           projects.find("\"projects_open_xp\", UiIcon::Award") != std::string::npos &&
           projects.find("\"projects_empty_filter\", UiIcon::Filter") != std::string::npos &&
           skillModals.find("\"add_skill_save\", UiIcon::Save") != std::string::npos &&
           skillModals.find("\"merge_skill_apply\", UiIcon::Merge") != std::string::npos &&
           skillModals.find("\"delete_skill_cancel\", UiIcon::Close") != std::string::npos &&
           catalog.find("\"catalog_clear_skills\", UiIcon::Clear") != std::string::npos &&
           catalog.find("\"skill_ach_icon_pick\", UiIcon::Image") != std::string::npos &&
           catalog.find("\"skill_achievements_empty\", UiIcon::Award") != std::string::npos &&
           catalog.find("{\"shown\", UiIcon::Filter") != std::string::npos &&
           catalog.find("\"skill_achievements_filter_empty\", UiIcon::Filter") != std::string::npos &&
           tasks.find("\"task_detail_reopen\", UiIcon::Play") != std::string::npos &&
           tasks.find("\"task_detail_handoff_reopen\", UiIcon::Play") != std::string::npos &&
           mainMenu.find("\"top_refresh\", UiIcon::Refresh") != std::string::npos &&
           profilePanel.find("\"profile_reset_password\", UiIcon::LockClosed") != std::string::npos &&
           profilePanel.find("\"profile_change_password\", UiIcon::LockClosed") != std::string::npos &&
           profilePanel.find("\"open_focus\", UiIcon::Focus") != std::string::npos &&
           profilePanel.find("\"profile_body_select_profile\", UiIcon::User") != std::string::npos &&
           profilePanel.find("\"profile_signal_balance\", UiIcon::Warning") != std::string::npos &&
           profileSections.find("\"profile_achievements_empty\", UiIcon::Award") != std::string::npos &&
           profileSections.find("\"profile_skills_filter_empty\", UiIcon::Filter") != std::string::npos &&
           logs.find("\"log_filter_reset\", UiIcon::Clear") != std::string::npos &&
           logs.find("\"logs_empty_filtered\", UiIcon::Filter") != std::string::npos &&
           logs.find("{\"warn\", UiIcon::Warning") != std::string::npos &&
           logs.find("{\"error\", UiIcon::Warning") != std::string::npos &&
           logs.find("\"task_audit_open\", UiIcon::Launch") != std::string::npos &&
           adminStats.find("\"admin_stats_reset\", UiIcon::Clear") != std::string::npos &&
           adminStats.find("\"admin_stats_empty_filtered\", UiIcon::Filter") != std::string::npos &&
           adminStats.find("\"kpi_no_activity\", UiIcon::Calendar") != std::string::npos &&
           adminStats.find("\"kpi_ach_total\", UiIcon::Award") != std::string::npos &&
           adminStats.find("\"kpi_recovery\", UiIcon::Timer") != std::string::npos &&
           uiSettings.find("\"ui_backgrounds_empty\", UiIcon::Image") != std::string::npos &&
           uiSettings.find("\"ui_backgrounds_filter_empty\", UiIcon::Filter") != std::string::npos &&
           uiSettings.find("UiIcon::Clear,") != std::string::npos &&
           xpModal.find("\"xp_modal_skills_filter_empty\", UiIcon::Filter") != std::string::npos &&
           rules.find("\"rules_reset\", UiIcon::Clear") != std::string::npos;
}

static bool TestSharedEmptyStatesUsedInUiSettings() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string uiSettings;
    if (!ReadFile(root / "gui" / "GuiUiSettingsPanel.inc", uiSettings)) return false;

    return uiSettings.find("ui_cloud_picker_empty") != std::string::npos &&
           uiSettings.find("ui_presets_empty") != std::string::npos &&
           uiSettings.find("ui_backgrounds_empty") != std::string::npos &&
           uiSettings.find("ui_background_combo_empty") != std::string::npos &&
           uiSettings.find("ui_backgrounds_filter_empty") != std::string::npos &&
           uiSettings.find("ui_appearance_clean_state") != std::string::npos &&
           CountSubstring(uiSettings, "DrawEmptyStateGui(") >= 7;
}

static bool TestSharedEmptyStatesUsedInUtilityPanels() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string shortcuts;
    std::string rules;
    std::string xpModal;
    if (!ReadFile(root / "gui" / "GuiShortcuts.inc", shortcuts)) return false;
    if (!ReadFile(root / "gui" / "GuiRulesPanel.inc", rules)) return false;
    if (!ReadFile(root / "gui" / "GuiXpModal.inc", xpModal)) return false;

    return shortcuts.find("shortcuts_sidebar_empty") != std::string::npos &&
           rules.find("rules_clean_state") != std::string::npos &&
           xpModal.find("xp_modal_skills_empty") != std::string::npos &&
           xpModal.find("xp_modal_profile_empty") != std::string::npos &&
           xpModal.find("xp_modal_skills_filter_empty") != std::string::npos &&
           CountSubstring(xpModal, "DrawEmptyStateGui(") >= 3;
}

static bool TestProfileTaskBriefStatsHaveUniqueIds() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string profilePanel;
    if (!ReadFile(root / "gui" / "GuiProfilePanel.inc", profilePanel)) return false;

    return profilePanel.find("profile_task_brief_stat_created") != std::string::npos &&
           profilePanel.find("profile_task_brief_stat_progress") != std::string::npos &&
           profilePanel.find("profile_task_brief_stat_overdue") != std::string::npos &&
           profilePanel.find("profile_task_brief_stat_xp") != std::string::npos &&
           profilePanel.find("##profile_task_brief_stat") == std::string::npos;
}

static bool TestPasswordModalsSubmitOnEnterAndAdminCanStayLoggedIn() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string adminModal;
    std::string profileModals;
    std::string mainMenu;
    std::string guiState;
    std::string stateInit;
    std::string appUtils;
    if (!ReadFile(root / "gui" / "GuiAdminModal.inc", adminModal)) return false;
    if (!ReadFile(root / "gui" / "GuiProfileModals.inc", profileModals)) return false;
    if (!ReadFile(root / "gui" / "GuiMainMenuPanel.inc", mainMenu)) return false;
    if (!ReadFile(root / "gui" / "GuiState.inc", guiState)) return false;
    if (!ReadFile(root / "gui" / "GuiStateInit.inc", stateInit)) return false;
    if (!ReadFile(root / "src" / "AppUtils.cpp", appUtils)) return false;

    return CountSubstring(adminModal, "ImGuiInputTextFlags_EnterReturnsTrue") >= 4 &&
           CountSubstring(profileModals, "ImGuiInputTextFlags_EnterReturnsTrue") >= 4 &&
           adminModal.find(u8"Не выходить") != std::string::npos &&
           adminModal.find("SetAdminStayLoggedIn(state.storageDir, state.adminStayLoggedIn)") != std::string::npos &&
           mainMenu.find("SetAdminStayLoggedIn(state.storageDir, false)") != std::string::npos &&
           guiState.find("bool adminStayLoggedIn") != std::string::npos &&
           stateInit.find("state.isAdmin = state.adminStayLoggedIn") != std::string::npos &&
           appUtils.find("stayLoggedIn=") != std::string::npos;
}

static bool TestGuiXpModalAllowsProjectlessSourceTask() {
    const std::filesystem::path root = FindRepoRootFromCwd();
    if (root.empty()) return false;
    std::string source;
    if (!ReadFile(root / "gui" / "GuiXpModal.inc", source)) return false;
    return source.find("(!fromTask && project.empty())") != std::string::npos &&
           source.find("fromTask ? u8\"В задаче должны быть название и описание.\"") != std::string::npos &&
           source.find("static bool RollbackXpParticipants(") != std::string::npos &&
           source.find("participantsForRollback = participants") != std::string::npos &&
           source.find("RollbackXpParticipants(state, storage, catalog, participantsForRollback, activeId)") != std::string::npos &&
           source.find("начисление XP откатано") != std::string::npos;
}

static bool TestSyncHealthDetectsBrokenFiles(const std::filesystem::path& dir) {
    if (!WriteFile(dir / "meta" / "tasks.json", "{broken")) return false;
    if (!WriteFile(dir / "meta" / "pipeline.json", "{\"steps\":[{\"id\":\"p1\"}]}")) return false;
    if (!WriteFile(dir / "meta" / "task-audit.log", "bad-line-without-separators")) return false;
    ModuleToggles modules;
    WorkspaceSyncHealth health = InspectWorkspaceSyncHealth(dir, modules);
    bool tasksIssue = false;
    bool pipelineIssue = false;
    bool auditIssue = false;
    for (const auto& issue : health.issues) {
        tasksIssue = tasksIssue || issue.find("meta/tasks.json") != std::string::npos;
        pipelineIssue = pipelineIssue || issue.find("meta/pipeline.json") != std::string::npos;
        auditIssue = auditIssue || issue.find("meta/task-audit.log") != std::string::npos;
    }
    return health.issueCount >= 3 && tasksIssue && pipelineIssue && auditIssue;
}

static bool TestProfileRank() {
    Profile p("Test");
    p.set_overall_level(40);
    const std::string rank = DescribeOverallRank(p);
    return rank.find("Джуниор") != std::string::npos;
}

static bool TestProfileSpirit(const std::filesystem::path& dir) {
    Profile profile("Spirit");
    if (profile.spirit() != ProfileSpirit::None) return false;
    if (ProfileSpiritFromString("good") != ProfileSpirit::Good) return false;
    if (ProfileSpiritFromString("evil") != ProfileSpirit::Evil) return false;
    if (ApplyProfileSpiritXpModifier(ProfileSpirit::Good, 1000) != 1010) return false;
    if (ApplyProfileSpiritXpModifier(ProfileSpirit::Evil, 1000) != 990) return false;
    if (ApplyProfileSpiritXpModifier(ProfileSpirit::None, 1000) != 1000) return false;

    profile.set_spirit(ProfileSpirit::Good);
    std::unique_ptr<IJobStorage> storage(CreateFileStorage(dir));
    auto info = storage->create_profile(profile);
    if (!info) return false;
    if (!storage->set_active_profile(info->id)) return false;
    auto loaded = storage->load_profile();
    if (!loaded || loaded->spirit() != ProfileSpirit::Good) return false;

    loaded->set_spirit(ProfileSpirit::Evil);
    if (!storage->save_profile(*loaded)) return false;
    auto reloaded = storage->load_profile();
    if (!reloaded || reloaded->spirit() != ProfileSpirit::Evil) return false;

    Profile legacy("Legacy");
    auto legacyInfo = storage->create_profile(legacy);
    if (!legacyInfo) return false;
    if (!storage->set_active_profile(legacyInfo->id)) return false;
    auto legacyLoaded = storage->load_profile();
    return legacyLoaded && legacyLoaded->spirit() == ProfileSpirit::None;
}

static bool TestEvilSpiritRemovalForCoins(const std::filesystem::path& dir) {
    std::unique_ptr<IJobStorage> storage(CreateFileStorage(dir));

    Profile buyer("Buyer");
    buyer.set_wallet_balance(250.0);
    buyer.set_spirit(ProfileSpirit::Evil);
    auto buyerInfo = storage->create_profile(buyer);
    if (!buyerInfo) return false;
    if (!storage->set_active_profile(buyerInfo->id)) return false;
    if (!storage->save_profile(buyer)) return false;

    StorageVaultData vault = LoadStorageVault(dir);
    vault.balance = 10.0;
    if (!SaveStorageVault(dir, vault)) return false;
    vault = LoadStorageVault(dir);

    AppProfileMutationResult result = AppRemoveEvilSpiritForCoins(
        *storage, buyerInfo->id, buyerInfo->id, dir, vault, 200.0);
    if (!result.ok || !result.changed || !result.profile) return false;
    if (result.profile->spirit() != ProfileSpirit::None) return false;
    if (std::abs(result.profile->wallet_balance() - 50.0) > 0.000001) return false;
    if (std::abs(vault.balance - 210.0) > 0.000001) return false;
    if (vault.log.empty() || vault.log.back().action != "spirit_cleanup") return false;

    if (!storage->set_active_profile(buyerInfo->id)) return false;
    auto savedBuyer = storage->load_profile();
    if (!savedBuyer || savedBuyer->spirit() != ProfileSpirit::None ||
        std::abs(savedBuyer->wallet_balance() - 50.0) > 0.000001) {
        return false;
    }
    const StorageVaultData savedVault = LoadStorageVault(dir);
    if (std::abs(savedVault.balance - 210.0) > 0.000001) return false;

    Profile poor("Poor");
    poor.set_wallet_balance(100.0);
    poor.set_spirit(ProfileSpirit::Evil);
    auto poorInfo = storage->create_profile(poor);
    if (!poorInfo) return false;
    if (!storage->set_active_profile(poorInfo->id)) return false;
    if (!storage->save_profile(poor)) return false;

    AppProfileMutationResult foreignResult = AppRemoveEvilSpiritForCoins(
        *storage, buyerInfo->id, poorInfo->id, dir, vault, 200.0);
    if (foreignResult.ok || !foreignResult.userError) return false;

    AppProfileMutationResult poorResult = AppRemoveEvilSpiritForCoins(
        *storage, poorInfo->id, poorInfo->id, dir, vault, 200.0);
    if (poorResult.ok || !poorResult.userError) return false;
    if (!storage->set_active_profile(poorInfo->id)) return false;
    auto savedPoor = storage->load_profile();
    return savedPoor && savedPoor->spirit() == ProfileSpirit::Evil &&
           std::abs(savedPoor->wallet_balance() - 100.0) <= 0.000001;
}

static bool TestWhitelist(const std::filesystem::path& dir) {
    WriteFile(dir / "bad.txt", "x");
    WriteFile(dir / "meta" / "bad.json", "{}");
    WriteFile(dir / "spirits" / "good.png", "x");
    WriteFile(dir / "spirits" / "bad.txt", "x");
    WriteFile(dir / "meta" / "task-audit.log", "1700000000|admin|t1|create||Task\n");
    int removed = 0;
    RemoveStrayFiles(dir, removed);
    return removed >= 2 &&
           !std::filesystem::exists(dir / "bad.txt") &&
           std::filesystem::exists(dir / "spirits" / "good.png") &&
           !std::filesystem::exists(dir / "spirits" / "bad.txt") &&
           std::filesystem::exists(dir / "meta" / "task-audit.log");
}

static bool TestStorageVaultRobustParsing(const std::filesystem::path& dir) {
    const std::string nbsp = u8" ";
    std::string storageJson;
    storageJson += "{\n";
    storageJson += "  \"currency_name\": \"Кукоин\",\n";
    storageJson += "  \"currency_code\": \"KUK\",\n";
    storageJson += "  \"balance_enc\": \"xor:76414257\",\n";
    storageJson += "  \"log_limit\": 10,\n";
    storageJson += "  \"rev\": 15,\n";
    storageJson += "  \"updated_at\": 1" + nbsp + "770" + nbsp + "312" + nbsp + "982,\n";
    storageJson += "  \"content_hash\": \"b07b8cc957f0aeb5\",\n";
    storageJson += "  \"pomodoro_start\": 540,\n";
    storageJson += "  \"pomodoro_end\": 1" + nbsp + "200,\n";
    storageJson += "  \"pomodoro_min\": 20,\n";
    storageJson += "  \"pomodoro_coin\": 1,\n";
    storageJson += "  \"pomodoro_days\": 62,\n";
    storageJson += "  \"log\": []\n";
    storageJson += "}\n";
    if (!WriteFile(dir / "meta" / "storage.json", storageJson)) return false;

    StorageVaultData loaded = LoadStorageVault(dir);
    if (loaded.updatedAt != 1770312982LL) return false;
    if (loaded.pomodoroEndMinutes != 1200) return false;
    if (loaded.pomodoroMinMinutes != 20) return false;

    if (!SaveStorageVault(dir, loaded)) return false;
    std::string saved;
    if (!ReadFile(dir / "meta" / "storage.json", saved)) return false;
    if (saved.find(nbsp) != std::string::npos) return false;
    if (saved.find("\"pomodoro_end\": 1200") == std::string::npos) return false;
    return true;
}

static bool TestCloudAtomicOverwrite(const std::filesystem::path& dir) {
    CloudSyncConfig config;
    config.enabled = true;
    config.root = "cloud";
    config.autoPull = true;
    config.autoPush = false;
    config.autoSyncEnabled = true;
    config.autoSyncMinutes = 15;

    if (!SaveCloudSyncConfig(dir, config)) return false;
    config.autoPull = false;
    config.autoPush = true;
    config.autoSyncMinutes = 45;
    if (!SaveCloudSyncConfig(dir, config)) return false;

    const CloudSyncConfig loadedConfig = LoadCloudSyncConfig(dir);
    if (loadedConfig.autoPull != false) return false;
    if (loadedConfig.autoPush != true) return false;
    if (loadedConfig.autoSyncMinutes != 45) return false;

    CloudManifest manifest;
    manifest.appVersion = "0.4.32";
    manifest.dataUpdatedAt = 100;
    manifest.releaseFile = "ForgeMirrorSetup_0.4.32.exe";
    manifest.notes = "First";
    if (!SaveCloudManifest(config, dir, manifest)) return false;

    manifest.appVersion = "0.4.33";
    manifest.dataUpdatedAt = 200;
    manifest.releaseFile.clear();
    manifest.notes.clear();
    if (!SaveCloudManifest(config, dir, manifest)) return false;

    const CloudManifest loadedManifest = LoadCloudManifest(config, dir);
    if (loadedManifest.appVersion != "0.4.33") return false;
    if (loadedManifest.dataUpdatedAt != 200) return false;
    if (loadedManifest.releaseFile != "ForgeMirrorSetup_0.4.32.exe") return false;
    if (loadedManifest.notes != "First") return false;
    return true;
}

static bool TestCloudSpiritIcons(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::path cloudRoot = dir;
    cloudRoot += "_spirit_cloud";
    std::filesystem::remove_all(cloudRoot, ec);

    CloudSyncConfig config;
    config.enabled = true;
    config.root = cloudRoot;

    if (!WriteFile(dir / "spirits" / "good.png", "local-good")) return false;
    CloudSyncResult push = PushCloudSnapshot(config, dir, CloudRole::Admin);
    if (!push.ok) return false;
    if (!std::filesystem::exists(cloudRoot / "spirits" / "good.png", ec)) return false;

    std::filesystem::remove(dir / "spirits" / "good.png", ec);
    if (!WriteFile(cloudRoot / "spirits" / "evil.png", "cloud-evil")) return false;
    CloudSyncResult pull = PullCloudSnapshot(config, dir, CloudRole::Admin);
    if (!pull.ok) return false;
    const bool ok = std::filesystem::exists(dir / "spirits" / "evil.png", ec);
    std::filesystem::remove_all(cloudRoot, ec);
    return ok;
}

static bool TestCloudDriftResolveRestore(const std::filesystem::path& dir) {
    auto fail = [](const char* message) {
        std::cerr << "cloudWorkspace: " << message << "\n";
        return false;
    };
    std::error_code ec;
    std::filesystem::path cloudRoot = dir;
    cloudRoot += "_cloud";
    std::filesystem::remove_all(cloudRoot, ec);

    CloudSyncConfig config;
    config.enabled = true;
    config.root = cloudRoot;

    const std::string localTasksOriginal = R"([{"id":"t1","title":"Local task"}])";
    const std::string cloudTasksVersion = R"([{"id":"t1","title":"Cloud task"}])";
    const std::string localPipelineOriginal = R"({"steps":[{"id":"p1","title":"Local old"}]})";
    const std::string cloudPipelineOriginal = R"({"steps":[{"id":"p1","title":"Cloud old"}]})";
    const std::string localPipelinePushed = R"({"steps":[{"id":"p1","title":"Local pushed"}]})";

    const auto localTasksPath = dir / "meta" / "tasks.json";
    const auto localPipelinePath = dir / "meta" / "pipeline.json";
    const auto cloudTasksPath = cloudRoot / "meta" / "tasks.json";
    const auto cloudPipelinePath = cloudRoot / "meta" / "pipeline.json";

    if (!WriteFile(localTasksPath, localTasksOriginal)) return fail("write local tasks");
    if (!WriteFile(cloudTasksPath, cloudTasksVersion)) return fail("write cloud tasks");
    if (!WriteFile(localPipelinePath, localPipelineOriginal)) return fail("write local pipeline");
    if (!WriteFile(cloudPipelinePath, cloudPipelineOriginal)) return fail("write cloud pipeline");

    const std::int64_t lastSyncAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 300;
    if (!SetFileUnixTimestamp(localTasksPath, lastSyncAt + 60)) return fail("timestamp local tasks");
    if (!SetFileUnixTimestamp(cloudTasksPath, lastSyncAt + 120)) return fail("timestamp cloud tasks");
    if (!SetFileUnixTimestamp(localPipelinePath, lastSyncAt - 120)) return fail("timestamp local pipeline");
    if (!SetFileUnixTimestamp(cloudPipelinePath, lastSyncAt + 120)) return fail("timestamp cloud pipeline");

    const CloudWorkspaceDriftSummary drift = InspectCloudWorkspaceDrift(config, dir, lastSyncAt);
    bool tasksConflict = false;
    bool pipelineCloudNewer = false;
    for (const auto& file : drift.files) {
        if (file.relativePath == "meta/tasks.json") {
            tasksConflict = file.hasIssue && file.conflict && file.localChanged && file.cloudChanged;
        } else if (file.relativePath == "meta/pipeline.json") {
            pipelineCloudNewer = file.hasIssue && !file.conflict && !file.localChanged && file.cloudChanged;
        }
    }
    if (drift.issueCount < 2 || drift.conflictCount < 1 || !tasksConflict || !pipelineCloudNewer) {
        return fail("unexpected drift summary");
    }

    const CloudWorkspaceResolveResult pullTasks =
        ResolveCloudWorkspaceFileVersion(config, dir, "meta/tasks.json", true);
    if (!pullTasks.ok || !pullTasks.changed) return fail("pull tasks resolve");
    std::string tasksAfterPull;
    if (!ReadFile(localTasksPath, tasksAfterPull) || tasksAfterPull != cloudTasksVersion) return fail("verify pulled tasks");
    if (ListCloudWorkspaceBackups(dir, "meta/tasks.json").size() < 2) return fail("tasks backups");

    const std::filesystem::path localTasksBackup = FindBackupByKind(pullTasks.backupPaths, "local");
    if (localTasksBackup.empty()) return fail("find local tasks backup");
    const CloudWorkspaceResolveResult restoreTasks =
        RestoreCloudWorkspaceBackup(dir, "meta/tasks.json", localTasksBackup);
    if (!restoreTasks.ok || !restoreTasks.changed) return fail("restore tasks backup");
    std::string tasksAfterRestore;
    if (!ReadFile(localTasksPath, tasksAfterRestore) || tasksAfterRestore != localTasksOriginal) return fail("verify restored tasks");

    if (!WriteFile(localPipelinePath, localPipelinePushed)) return fail("write local pipeline pushed");
    if (!SetFileUnixTimestamp(localPipelinePath, lastSyncAt + 180)) return fail("timestamp local pipeline pushed");
    const CloudWorkspaceResolveResult pushPipeline =
        ResolveCloudWorkspaceFileVersion(config, dir, "meta/pipeline.json", false);
    if (!pushPipeline.ok || !pushPipeline.changed) return fail("push pipeline resolve");
    std::string pipelineAfterPush;
    if (!ReadFile(cloudPipelinePath, pipelineAfterPush) || pipelineAfterPush != localPipelinePushed) return fail("verify pushed pipeline");
    if (ListCloudWorkspaceBackups(dir, "meta/pipeline.json").size() < 2) return fail("pipeline backups");

    const std::filesystem::path cloudPipelineBackup = FindBackupByKind(pushPipeline.backupPaths, "cloud");
    if (cloudPipelineBackup.empty()) return fail("find cloud pipeline backup");
    const CloudWorkspaceResolveResult restorePipeline =
        RestoreCloudWorkspaceBackup(dir, "meta/pipeline.json", cloudPipelineBackup);
    if (!restorePipeline.ok || !restorePipeline.changed) return fail("restore pipeline backup");
    std::string pipelineAfterRestore;
    if (!ReadFile(localPipelinePath, pipelineAfterRestore) || pipelineAfterRestore != cloudPipelineOriginal) {
        return fail("verify restored pipeline");
    }

    std::filesystem::remove_all(cloudRoot, ec);
    return true;
}

int main() {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "forgemirror_smoke";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);

    const bool okProfile = TestProfileRank();
    const bool okSpirit = TestProfileSpirit(tmp / "spirit");
    const bool okSpiritRemoval = TestEvilSpiritRemovalForCoins(tmp / "spirit_removal");
    const bool okRules = TestGameplayConfig(tmp);
    const bool okTasks = TestTasksPipelineRoundtrip(tmp);
    const bool okWorkspaceRecovery = TestTasksPipelineRecovery(tmp / "workspace_recovery");
    const bool okWorkspaceSaveRollback = TestWorkspaceMutationsRollBackWhenSaveFails(tmp / "workspace_save_rollback");
    const bool okTaskText = TestTaskTextMutation(tmp / "task_text");
    const bool okTaskFinalizeRollback = TestTaskFinalizeXpRollsBackWhenAuditFails(tmp / "task_finalize_rollback");
    const bool okTaskFinalizeContract = TestTaskFinalizeXpValidatesWorkflowContract(tmp / "task_finalize_contract");
    const bool okTaskXpDistribution = TestTaskXpDistributionHelpers();
    const bool okGuiStack = TestGuiTasksImGuiStackPatterns();
    const bool okTaskWorkflowBoundary = TestTaskWorkflowServiceBoundary();
    const bool okGuiScopeTotals = TestGuiImGuiScopeTotals();
    const bool okPipelineGuiStack = TestGuiPipelineImGuiStackPatterns();
    const bool okGuiRowStates = TestGuiRowStateHelpersUsedAcrossModules();
    const bool okCompactControlTables = TestCompactControlTablesUseSharedScope();
    const bool okProfileTaskEmptyStates = TestProfileTaskEmptyStatesUseSharedHelper();
    const bool okTasksDetailEmptyStates = TestTasksDetailEmptyStatesUseSharedHelper();
    const bool okServiceEmptyStates = TestSharedEmptyStatesUsedInServicePanels();
    const bool okProfileAdminEmptyStates = TestSharedEmptyStatesUsedInProfileAdminPanels();
    const bool okProfileModalsEmptyStates = TestProfileModalsMissingProfileUsesSharedEmptyState();
    const bool okProfileSectionEmptyStates = TestSharedEmptyStatesUsedInProfileSections();
    const bool okSkillCatalogEmptyStates = TestSharedEmptyStatesUsedInSkillCatalog();
    const bool okProfileSkillUtilityEmptyStates = TestProfileSkillUtilityEmptyStatesUseSharedHelper();
    const bool okSemanticActionIcons = TestSemanticActionIconsUsedForCriticalActions();
    const bool okUiSettingsEmptyStates = TestSharedEmptyStatesUsedInUiSettings();
    const bool okUtilityEmptyStates = TestSharedEmptyStatesUsedInUtilityPanels();
    const bool okProfileTaskBriefIds = TestProfileTaskBriefStatsHaveUniqueIds();
    const bool okPasswordEnter = TestPasswordModalsSubmitOnEnterAndAdminCanStayLoggedIn();
    const bool okXpProjectless = TestGuiXpModalAllowsProjectlessSourceTask();
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okSyncHealth = TestSyncHealthDetectsBrokenFiles(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okWhitelist = TestWhitelist(tmp);
    const bool okVault = TestStorageVaultRobustParsing(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okCloudOverwrite = TestCloudAtomicOverwrite(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okCloudSpirits = TestCloudSpiritIcons(tmp);
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    const bool okCloudWorkspace = TestCloudDriftResolveRestore(tmp);

    const bool okEmptyStateLayout = TestGuiEmptyStateRegistersLayoutSize();

    if (okProfile && okSpirit && okSpiritRemoval && okRules && okTasks && okWorkspaceRecovery && okWorkspaceSaveRollback && okTaskText && okTaskFinalizeRollback && okTaskFinalizeContract && okTaskXpDistribution && okGuiStack && okTaskWorkflowBoundary && okGuiScopeTotals && okPipelineGuiStack && okGuiRowStates && okCompactControlTables && okProfileTaskEmptyStates && okTasksDetailEmptyStates && okServiceEmptyStates && okProfileAdminEmptyStates && okProfileModalsEmptyStates && okProfileSectionEmptyStates && okSkillCatalogEmptyStates && okProfileSkillUtilityEmptyStates && okSemanticActionIcons && okUiSettingsEmptyStates && okUtilityEmptyStates && okProfileTaskBriefIds && okPasswordEnter && okEmptyStateLayout && okXpProjectless && okSyncHealth && okWhitelist && okVault &&
        okCloudOverwrite && okCloudSpirits && okCloudWorkspace) {
        std::cout << "smoke_core: OK\n";
        return 0;
    }
    std::cerr << "smoke_core failed: "
              << "profile=" << okProfile
              << " spirit=" << okSpirit
              << " spiritRemoval=" << okSpiritRemoval
              << " rules=" << okRules
              << " tasks=" << okTasks
              << " workspaceRecovery=" << okWorkspaceRecovery
              << " workspaceSaveRollback=" << okWorkspaceSaveRollback
              << " taskText=" << okTaskText
              << " taskFinalizeRollback=" << okTaskFinalizeRollback
              << " taskFinalizeContract=" << okTaskFinalizeContract
              << " taskXpDistribution=" << okTaskXpDistribution
              << " guiStack=" << okGuiStack
              << " taskWorkflowBoundary=" << okTaskWorkflowBoundary
              << " guiScopeTotals=" << okGuiScopeTotals
              << " pipelineGuiStack=" << okPipelineGuiStack
              << " guiRowStates=" << okGuiRowStates
              << " compactControlTables=" << okCompactControlTables
              << " profileTaskEmptyStates=" << okProfileTaskEmptyStates
              << " tasksDetailEmptyStates=" << okTasksDetailEmptyStates
              << " serviceEmptyStates=" << okServiceEmptyStates
              << " profileAdminEmptyStates=" << okProfileAdminEmptyStates
              << " profileModalsEmptyStates=" << okProfileModalsEmptyStates
              << " profileSectionEmptyStates=" << okProfileSectionEmptyStates
              << " skillCatalogEmptyStates=" << okSkillCatalogEmptyStates
              << " profileSkillUtilityEmptyStates=" << okProfileSkillUtilityEmptyStates
              << " semanticActionIcons=" << okSemanticActionIcons
              << " uiSettingsEmptyStates=" << okUiSettingsEmptyStates
              << " utilityEmptyStates=" << okUtilityEmptyStates
              << " profileTaskBriefIds=" << okProfileTaskBriefIds
              << " passwordEnter=" << okPasswordEnter
              << " emptyStateLayout=" << okEmptyStateLayout
              << " xpProjectless=" << okXpProjectless
              << " syncHealth=" << okSyncHealth
              << " whitelist=" << okWhitelist
              << " vault=" << okVault
              << " cloudOverwrite=" << okCloudOverwrite
              << " cloudSpirits=" << okCloudSpirits
              << " cloudWorkspace=" << okCloudWorkspace << "\n";
    return 1;
}
