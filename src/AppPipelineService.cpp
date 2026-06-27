#include "AppPipelineService.h"
#include "AppRecoveryStorage.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path PipelineStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "pipeline.json";
}

std::string EscapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

bool IsValidIndex(const std::vector<PipelineStep>& steps, int index) {
    return index >= 0 && index < static_cast<int>(steps.size());
}

std::string JoinStringList(const std::vector<std::string>& values, const char* delimiter) {
    std::ostringstream out;
    bool first = true;
    for (const auto& value : values) {
        if (value.empty()) continue;
        if (!first) out << delimiter;
        first = false;
        out << value;
    }
    return out.str();
}

std::string MakeUniqueStepId(const std::vector<PipelineStep>& steps) {
    int index = static_cast<int>(steps.size()) + 1;
    while (true) {
        std::string candidate = "custom-step-" + std::to_string(index);
        bool exists = std::any_of(steps.begin(), steps.end(), [&](const PipelineStep& step) {
            return step.id == candidate;
        });
        if (!exists) return candidate;
        ++index;
    }
}

} // namespace

bool AppSavePipelineData(const std::filesystem::path& storageDir,
                         const std::vector<PipelineStep>& steps) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        out << "  {"
            << "\"id\":\"" << EscapeJson(step.id)
            << "\",\"stage_code\":\"" << EscapeJson(step.stageCode)
            << "\",\"branch\":\"" << EscapeJson(step.branch)
            << "\",\"title\":\"" << EscapeJson(step.title)
            << "\",\"description\":\"" << EscapeJson(step.description)
            << "\",\"input\":\"" << EscapeJson(step.input)
            << "\",\"output\":\"" << EscapeJson(step.output)
            << "\",\"owner\":\"" << EscapeJson(step.owner)
            << "\",\"done_criteria\":\"" << EscapeJson(step.doneCriteria)
            << "\",\"engine_check\":\"" << EscapeJson(step.engineCheck)
            << "\",\"risk\":\"" << EscapeJson(step.risk)
            << "\",\"next_stage_label\":\"" << EscapeJson(step.nextStageLabel)
            << "\",\"legacy_notes\":\"" << EscapeJson(step.legacyNotes)
            << "\",\"next_ids\":\"" << EscapeJson(JoinStringList(step.nextIds, ";"))
            << "\",\"hints\":\"" << EscapeJson(JoinStringList(step.hints, "\n"))
            << "\"}";
        if (i + 1 < steps.size()) out << ",";
        out << "\n";
    }
    out << "]";
    return AppWriteUtf8BomWithRecovery(PipelineStoragePath(storageDir), out.str());
}

AppPipelineMutationResult AppAddPipelineStep(const std::filesystem::path& storageDir,
                                             std::vector<PipelineStep>& steps,
                                             int insertAfterIndex) {
    AppPipelineMutationResult result;
    std::vector<PipelineStep> backup = steps;
    PipelineStep step;
    step.id = MakeUniqueStepId(steps);
    step.branch = u8"Пользовательский блок";
    step.title = u8"Новый блок";
    step.description = u8"Краткое описание блока.";
    const int insertPos = std::clamp(insertAfterIndex + 1, 0, static_cast<int>(steps.size()));
    steps.insert(steps.begin() + insertPos, std::move(step));
    if (!AppSavePipelineData(storageDir, steps)) {
        steps = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить пайплайн.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.selectedIndex = insertPos;
    return result;
}

AppPipelineMutationResult AppUpdatePipelineStep(const std::filesystem::path& storageDir,
                                                std::vector<PipelineStep>& steps,
                                                int index,
                                                const PipelineStep& updatedStep) {
    AppPipelineMutationResult result;
    if (!IsValidIndex(steps, index)) {
        result.errorMessage = u8"Этап не найден.";
        return result;
    }
    const std::string title = updatedStep.title;
    if (title.empty()) {
        result.errorMessage = u8"Название этапа не может быть пустым.";
        return result;
    }
    std::vector<PipelineStep> backup = steps;
    PipelineStep next = updatedStep;
    next.id = backup[index].id;
    steps[index] = std::move(next);
    if (!AppSavePipelineData(storageDir, steps)) {
        steps = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить пайплайн.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.selectedIndex = index;
    return result;
}

AppPipelineMutationResult AppDeletePipelineStep(const std::filesystem::path& storageDir,
                                                std::vector<PipelineStep>& steps,
                                                int index) {
    AppPipelineMutationResult result;
    if (!IsValidIndex(steps, index)) {
        result.errorMessage = u8"Этап не найден.";
        return result;
    }
    std::vector<PipelineStep> backup = steps;
    steps.erase(steps.begin() + index);
    if (!AppSavePipelineData(storageDir, steps)) {
        steps = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить пайплайн.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.selectedIndex = steps.empty() ? -1 : std::min(index, static_cast<int>(steps.size()) - 1);
    return result;
}

AppPipelineMutationResult AppMovePipelineStep(const std::filesystem::path& storageDir,
                                              std::vector<PipelineStep>& steps,
                                              int fromIndex,
                                              int toIndex) {
    AppPipelineMutationResult result;
    if (!IsValidIndex(steps, fromIndex) || !IsValidIndex(steps, toIndex)) {
        result.errorMessage = u8"Этап не найден.";
        return result;
    }
    if (fromIndex == toIndex) {
        result.ok = true;
        result.selectedIndex = toIndex;
        return result;
    }
    std::vector<PipelineStep> backup = steps;
    std::swap(steps[fromIndex], steps[toIndex]);
    if (!AppSavePipelineData(storageDir, steps)) {
        steps = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить пайплайн.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.selectedIndex = toIndex;
    return result;
}
