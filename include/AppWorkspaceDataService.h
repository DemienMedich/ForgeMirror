#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppDomainTypes.h"
#include "AppUtils.h"
#include "GameplayConfig.h"

struct WorkspaceDataSnapshot {
    std::vector<TaskEntry> tasks;
    std::vector<TaskAuditEntry> taskAudit;
    std::vector<ProjectEntry> projects;
    std::vector<ShortcutEntry> shortcuts;
    std::vector<PipelineStep> pipelineSteps;
    std::vector<ProfessionEntry> professions;
    std::vector<std::string> bannerTexts;
    GameplayConfig rulesConfig;
    StorageVaultData vault;
};

std::vector<TaskEntry> LoadTasksData(const std::filesystem::path& storageDir);
std::vector<TaskAuditEntry> LoadTaskAuditData(const std::filesystem::path& storageDir, size_t maxEntries = 200);
std::vector<ProjectEntry> LoadProjectsData(const std::filesystem::path& storageDir);
std::vector<ShortcutEntry> LoadShortcutsData(const std::filesystem::path& storageDir);
std::vector<PipelineStep> LoadPipelineData(const std::filesystem::path& storageDir);
std::vector<ProfessionEntry> LoadProfessionsData(const std::filesystem::path& storageDir);
WorkspaceDataSnapshot LoadWorkspaceDataSnapshot(const std::filesystem::path& storageDir,
                                                const ModuleToggles& modules);
