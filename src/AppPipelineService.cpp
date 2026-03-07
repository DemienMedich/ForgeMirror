#include "AppPipelineService.h"

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

bool WriteAllUtf8BomAtomic(const std::filesystem::path& path, const std::string& payloadWithoutBom) {
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        static constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        out.write(payloadWithoutBom.data(), static_cast<std::streamsize>(payloadWithoutBom.size()));
        if (!out.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
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

} // namespace

bool AppSavePipelineData(const std::filesystem::path& storageDir,
                         const std::vector<PipelineStep>& steps) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        out << "  {\"title\":\"" << EscapeJson(step.title)
            << "\",\"description\":\"" << EscapeJson(step.description) << "\"}";
        if (i + 1 < steps.size()) out << ",";
        out << "\n";
    }
    out << "]";
    return WriteAllUtf8BomAtomic(PipelineStoragePath(storageDir), out.str());
}

AppPipelineMutationResult AppAddPipelineStep(const std::filesystem::path& storageDir,
                                             std::vector<PipelineStep>& steps,
                                             int insertAfterIndex) {
    AppPipelineMutationResult result;
    std::vector<PipelineStep> backup = steps;
    PipelineStep step;
    step.title = u8"Новый этап";
    step.description = u8"Описание этапа.";
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
                                                const std::string& title,
                                                const std::string& description) {
    AppPipelineMutationResult result;
    if (title.empty()) {
        result.errorMessage = u8"Название этапа не может быть пустым.";
        return result;
    }
    if (!IsValidIndex(steps, index)) {
        result.errorMessage = u8"Этап не найден.";
        return result;
    }
    std::vector<PipelineStep> backup = steps;
    steps[index].title = title;
    steps[index].description = description;
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
