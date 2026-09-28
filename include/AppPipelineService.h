#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppDomainTypes.h"

struct AppPipelineMutationResult {
    bool ok = false;
    bool changed = false;
    bool staleSnapshot = false;
    int selectedIndex = -1;
    std::string errorMessage;
};

// Low-level serializer for callers that already hold the workspace lock or
// deliberately create fixtures; user-facing edits should use guarded APIs below.
bool AppSavePipelineData(const std::filesystem::path& storageDir,
                         const std::vector<PipelineStep>& steps);

// Saves a full editor candidate only when both the editor's opening snapshot
// and the live in-memory list still match the latest normalized disk state.
AppPipelineMutationResult AppSavePipelineCandidate(const std::filesystem::path& storageDir,
                                                   const std::vector<PipelineStep>& expectedSnapshot,
                                                   std::vector<PipelineStep>& liveSteps,
                                                   const std::vector<PipelineStep>& candidate);

AppPipelineMutationResult AppAddPipelineStep(const std::filesystem::path& storageDir,
                                             std::vector<PipelineStep>& steps,
                                             int insertAfterIndex);

AppPipelineMutationResult AppUpdatePipelineStep(const std::filesystem::path& storageDir,
                                                std::vector<PipelineStep>& steps,
                                                int index,
                                                const PipelineStep& updatedStep);

AppPipelineMutationResult AppDeletePipelineStep(const std::filesystem::path& storageDir,
                                                std::vector<PipelineStep>& steps,
                                                int index);

AppPipelineMutationResult AppMovePipelineStep(const std::filesystem::path& storageDir,
                                              std::vector<PipelineStep>& steps,
                                              int fromIndex,
                                              int toIndex);
