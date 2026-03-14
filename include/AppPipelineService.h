#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "AppDomainTypes.h"

struct AppPipelineMutationResult {
    bool ok = false;
    bool changed = false;
    int selectedIndex = -1;
    std::string errorMessage;
};

bool AppSavePipelineData(const std::filesystem::path& storageDir,
                         const std::vector<PipelineStep>& steps);

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
