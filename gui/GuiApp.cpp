#include "AppUtils.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <GL/gl.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <clocale>
#include <locale>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <deque>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <sstream>
#include <unordered_map>
#include <numeric>
#include <vector>
#include <chrono>

namespace {

// View-model representing a loaded profile + storage id for the GUI.
struct GuiProfile {
    Profile profile;
    std::string id;
};

// User-entered share for a skill in the Add Experience sheet.
struct XpEntry {
    int percent = 0;
};

// Simple description struct for pipeline hints shown in the UI sidebar.
struct PipelineStep {
    const char* title;
    const char* description;
};

// Actions supported by the confirmation modal.
enum class ConfirmAction {
    None,
    Archive,
    Restore,
    Delete
};

// Aggregates every bit of GUI state (selected profile, popups, sheet values, etc.).
struct GuiState {
    std::vector<IJobStorage::ProfileInfo> profiles;
    int selectedIndex = -1;
    std::optional<GuiProfile> active;
    std::string statusMessage;
    float statusColor[4] = {0.6f, 0.7f, 1.0f, 1.0f};
    std::filesystem::path storageDir;
    GameplayConfig rulesConfig;
    GameplayConfig rulesDraft;

    bool createPopupRequest = false;
    bool confirmPopupRequest = false;
    bool xpPopupRequest = false;
    ConfirmAction confirmAction = ConfirmAction::None;
    std::array<char, 128> modalBuffer{};
    std::vector<XpEntry> xpEntries;
    std::unordered_map<std::string, std::deque<std::string>> activityLogs;
    int taskScore = 10;
    int taskCategoryIndex = 0;
    int selectedCatalogIndex = -1;
    int selectedPipelineIndex = 0;
};

// Walk parent directories trying to locate an asset (fonts, ini, etc.).
std::optional<std::filesystem::path> FindAssetUpwards(const std::filesystem::path& relative) {
    std::error_code ec;
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        auto candidate = current / relative;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        auto parent = current.parent_path();
        if (parent == current) break;
        current = std::move(parent);
    }
    return std::nullopt;
}

// Pull profile list from storage and sort it for deterministic GUI display.
std::vector<IJobStorage::ProfileInfo> LoadProfiles(IJobStorage& storage) {
    auto list = storage.list_profiles();
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return list;
}

// Helper to show feedback banner with color-coded message.
void SetStatus(GuiState& state, const std::string& msg, float r, float g, float b, float a = 1.0f) {
    state.statusMessage = msg;
    state.statusColor[0] = r;
    state.statusColor[1] = g;
    state.statusColor[2] = b;
    state.statusColor[3] = a;
}

// Reset Add Experience sheet: one row per catalog skill, even split of 100%.
void PrepareXpEntries(GuiState& state, const SkillCatalog& catalog) {
    const auto& skills = catalog.skills();
    state.xpEntries.assign(skills.size(), {});
    state.taskScore = 10;
    state.taskCategoryIndex = 0;
    const int count = static_cast<int>(skills.size());
    if (count <= 0) return;
    const int base = 100 / count;
    int remainder = 100 - base * count;
    for (int i = 0; i < count; ++i) {
        state.xpEntries[i].percent = base;
        if (remainder > 0) {
            state.xpEntries[i].percent += 1;
            --remainder;
        }
    }
}

// Maintain a small rolling activity feed per profile.
void AppendLog(GuiState& state, const std::string& profileId, const std::string& message) {
    auto& log = state.activityLogs[profileId];
    if (log.size() == 3) {
        log.pop_front();
    }
    log.push_back(message);
}

// Generic clamp used all over the GUI logic.
int ClampToRange(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int NormalizeCategoryIndex(int index) {
    if (index < 0) return 0;
    if (index >= Profile::kCategoryCount) return Profile::kCategoryCount - 1;
    return index;
}

const char* ClassificationLabel(int categoryIndex) {
    return Profile::kCategoryLabels[NormalizeCategoryIndex(categoryIndex)];
}

// Utility functions below keep the Add Experience sliders in sync.
int SumPercentages(const std::vector<XpEntry>& entries) {
    int total = 0;
    for (const auto& entry : entries) total += entry.percent;
    return total;
}

// Reduce percentages on non-selected rows until the sum drops back to 100%.
void ReduceOthers(std::vector<XpEntry>& entries, int fixedIndex, int overflow) {
    if (overflow <= 0) return;
    const int size = static_cast<int>(entries.size());
    if (size == 0) return;
    std::vector<int> indices;
    indices.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (i == fixedIndex) continue;
        if (entries[i].percent > 0) indices.push_back(i);
    }
    if (indices.empty()) {
        entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
        return;
    }
    while (overflow > 0) {
        bool changed = false;
        int positiveCount = 0;
        for (int idx : indices) {
            if (entries[idx].percent > 0) ++positiveCount;
        }
        if (positiveCount == 0) {
            entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
            break;
        }
        int share = std::max(1, overflow / positiveCount);
        for (int idx : indices) {
            if (overflow <= 0) break;
            if (entries[idx].percent <= 0) continue;
            int delta = std::min(entries[idx].percent, share);
            if (delta <= 0) continue;
            entries[idx].percent -= delta;
            overflow -= delta;
            changed = true;
        }
        if (!changed) {
            for (int idx : indices) {
                if (overflow <= 0) break;
                if (entries[idx].percent <= 0) continue;
                entries[idx].percent -= 1;
                --overflow;
                changed = true;
            }
        }
        if (!changed) {
            entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
            break;
        }
    }
}

// Distribute remaining percent points across other rows to reach 100%.
void IncreaseOthers(std::vector<XpEntry>& entries, int fixedIndex, int deficit) {
    if (deficit <= 0) return;
    const int size = static_cast<int>(entries.size());
    if (size == 0) return;
    std::vector<int> indices;
    indices.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (i == fixedIndex) continue;
        indices.push_back(i);
    }
    if (indices.empty()) {
        entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent + deficit, 0, 100);
        return;
    }
    std::size_t cursor = 0;
    int safety = deficit * static_cast<int>(indices.size() + 1) + 100;
    auto allFull = [&]() {
        for (int idx : indices) {
            if (entries[idx].percent < 100) return false;
        }
        return true;
    };
    while (deficit > 0 && safety-- > 0) {
        if (allFull()) {
            entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent + deficit, 0, 100);
            break;
        }
        int idx = indices[cursor % indices.size()];
        cursor++;
        if (entries[idx].percent < 100) {
            entries[idx].percent += 1;
            --deficit;
        }
    }
}

// High-level balancer that ensures the sheet always totals 100%.
void BalancePercentages(std::vector<XpEntry>& entries, int fixedIndex) {
    if (entries.empty()) return;
    if (fixedIndex < 0 || fixedIndex >= static_cast<int>(entries.size())) {
        fixedIndex = 0;
    }
    int total = SumPercentages(entries);
    if (total > 100) {
        ReduceOthers(entries, fixedIndex, total - 100);
    } else if (total < 100) {
        IncreaseOthers(entries, fixedIndex, 100 - total);
    }
    int adjustedTotal = SumPercentages(entries);
    if (adjustedTotal != 100) {
        int diff = adjustedTotal - 100;
        entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent - diff, 0, 100);
        adjustedTotal = SumPercentages(entries);
        if (adjustedTotal != 100 && !entries.empty()) {
            int correction = 100 - adjustedTotal;
            entries[0].percent = ClampToRange(entries[0].percent + correction, 0, 100);
        }
    }
}

// Entry point invoked when the user drags a slider.
void AdjustSkillShare(GuiState& state, int index, int desiredPercent) {
    if (index < 0 || index >= static_cast<int>(state.xpEntries.size())) return;
    desiredPercent = ClampToRange(desiredPercent, 0, 100);
    state.xpEntries[index].percent = desiredPercent;
    BalancePercentages(state.xpEntries, index);
}

// Render a polar chart of skill levels inside the profile preview.
void DrawSkillRadarChart(const std::vector<Skill>& skills, const ImVec2& canvasPos, const ImVec2& canvasSize, ImDrawList* drawList) {
    if (skills.size() < 3) {
        drawList->AddText(canvasPos, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.5f, 1.0f)),
                          "Недостаточно данных для диаграммы (минимум 3 навыка).");
        return;
    }

    const float radius = 0.4f * std::min(canvasSize.x, canvasSize.y);
    if (radius <= 1.0f) {
        return;
    }

    const ImVec2 center = ImVec2(canvasPos.x + canvasSize.x * 0.5f,
                                 canvasPos.y + canvasSize.y * 0.5f);

    std::vector<float> values;
    values.reserve(skills.size());
    float maxValue = 0.0f;
    for (const auto& skill : skills) {
        float fractionalLevel = static_cast<float>(skill.level);
        if (skill.xpToNext > 0) {
            fractionalLevel += static_cast<float>(skill.xp) / static_cast<float>(skill.xpToNext);
        }
        values.push_back(fractionalLevel);
        if (fractionalLevel > maxValue) maxValue = fractionalLevel;
    }
    if (maxValue <= 0.0f) {
        maxValue = 1.0f;
    }

    std::vector<ImVec2> polygon(values.size());
    std::vector<ImVec2> temp(values.size());

    constexpr float kPi = 3.14159265358979323846f;
    const float angleStep = 2.0f * kPi / static_cast<float>(values.size());
    const float startAngle = -kPi * 0.5f;

    const ImU32 gridColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
    const ImU32 axisColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
    const ImU32 fillColor = ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 0.9f, 0.25f));
    const ImU32 outlineColor = ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 0.9f, 0.85f));
    const ImU32 pointColor = ImGui::GetColorU32(ImVec4(0.95f, 0.98f, 1.0f, 1.0f));
    const ImU32 labelColor = ImGui::GetColorU32(ImVec4(0.8f, 0.9f, 1.0f, 1.0f));

    const int ringCount = 4;
    for (int ring = 1; ring <= ringCount; ++ring) {
        float ringRatio = static_cast<float>(ring) / ringCount;
        float ringRadius = radius * ringRatio;
        for (size_t i = 0; i < values.size(); ++i) {
            float angle = startAngle + angleStep * static_cast<float>(i);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            temp[i] = ImVec2(center.x + cosA * ringRadius,
                             center.y + sinA * ringRadius);
        }
        drawList->AddPolyline(temp.data(), static_cast<int>(temp.size()), gridColor, true, 1.0f);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        float angle = startAngle + angleStep * static_cast<float>(i);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        ImVec2 axisEnd(center.x + cosA * radius, center.y + sinA * radius);
        drawList->AddLine(center, axisEnd, axisColor, 1.0f);

        ImVec2 labelPos(center.x + cosA * (radius + 14.0f),
                        center.y + sinA * (radius + 14.0f));
        const std::string& name = skills[i].name;
        ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
        drawList->AddText(ImVec2(labelPos.x - textSize.x * 0.5f,
                                 labelPos.y - textSize.y * 0.5f),
                          labelColor, name.c_str());
    }

    for (size_t i = 0; i < values.size(); ++i) {
        float angle = startAngle + angleStep * static_cast<float>(i);
        float ratio = values[i] / maxValue;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        polygon[i] = ImVec2(center.x + cosA * radius * ratio,
                            center.y + sinA * radius * ratio);
    }

    drawList->AddConvexPolyFilled(polygon.data(), static_cast<int>(polygon.size()), fillColor);
    drawList->AddPolyline(polygon.data(), static_cast<int>(polygon.size()), outlineColor, true, 2.0f);
    for (const ImVec2& p : polygon) {
        drawList->AddCircleFilled(p, 3.0f, pointColor);
    }

    std::ostringstream legend;
    legend << "Макс. уровень: " << std::fixed << std::setprecision(1) << maxValue;
    drawList->AddText(ImVec2(canvasPos.x + 5.0f, canvasPos.y + 5.0f),
                      ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.7f, 1.0f)),
                      legend.str().c_str());
}
const std::vector<PipelineStep> kPipelineSteps = {
    {"0. Реф-борд",
R"(Сборка реф-листа, наряду с блокингом - важнейшие этапы!
При надобности, создается отдельный! муд-борд!)"},
    {"1. Блокинг",
R"(Блокинг — это процесс создания базовых пропорций и соотношений между моделями и сценами.

На этом этапе особенно важно правильно отобразить пропорции отдельных деталей и модели в целом. В отличие от других этапов, здесь не так важно уделять внимание сетке, как точности и скорости.

Блокинг позволяет увидеть общую форму и силуэт будущей модели, что дает возможность внести необходимые коррективы, если это необходимо.

!!!Важно не вдаваться в детали на данном этапе!!!)"},
    {"2. Создание Low-Poly",
R"(На этом этапе каждый элемент прорабатывается отдельно. Формы уточняются, а топология модели приводится в порядок.

Всё, что требует симметрии, выполняется с помощью модификатора Mirror и включённого параметра Clipping. Для удобства можно добавить объект-пустышку в нулевой координате, чтобы настроить зеркальное отражение на него.

ДЕЛАЕМ CNTRL+A -> SCALE)"},
    {"3. Шейдинг",
R"(На этом этапе мы расставляем шарпэджи, проверяем шейдинг модели и устанавливаем параметры сглаживания.)"},
    {"4. High-poly",
R"(В нашей сцене мы присваиваем всем объектам суффикс "_low" и создаем их дубликат. В копиях меняем суффикс на "_high", используя встроенный инструмент Blender – Batch Rename.

На одном из объектов мы настраиваем модификаторы Bevel и Subdivision Surface:

* Bevel: – Amount: 0.001 – Segments: 2 – Limit Method: Weight – Profile->Shape: 1
* Subdivision Surface: – Levels: 3

Затем, с помощью аддона Copy Attribute (который можно установить через Preferences), мы копируем эти модификаторы на все объекты High-poly.

После этого мы проходим по каждому объекту и устанавливаем значение Bevel Weight (вкладка Item, меню "N") на 1, применяя этот параметр ко всем шарпэджам.

Если требуется, мы модифицируем сетку, чтобы модель соответствовала Low-poly версии.)"},
    {"5. UV-развертка",
R"(Производится только на Low-poly.

Проходим по всем объектам, выравнивая швы на шарпэдах. При необходимости добавляем дополнительные швы.

Затем разворачиваем объект и проверяем цвет UV-Stretch. Он должен быть максимально холодным.

Проверяем, нет ли искажений или деформаций на островках. При необходимости выравниваем их и, если нужно, поворачиваем в нужные нам координаты.

Когда развертка нас устраивает, применяем Mirror, создавая Overlaps.

После завершения работы со всеми объектами, мы приступаем к общей упаковке:

1. Нажимаем CNTRL+A и выбираем SCALE.
2. Выделяем все объекты, переключаемся в Edit Mode и в меню UV выбираем Average Islands Scale.
3. Упаковываем, отключив поворот островков.

margin – 0.004

Texel Density (TD) – 2px/cm при разрешении 4k (средний параметр, для каждого проекта рассчитывается отдельно)

Если необходимо достичь заданной плотности Texels, делим UV на несколько частей.)"},
    {"6. Запекание и текстурирование",
R"(Более подробно я расскажу об этом позже, а сейчас поделюсь основными шагами:

1. Проводим запекание по всем объектам, используя минимальную длину луча. Проверяем, чтобы области High и Low не пересекались.
2. Выполняем пробные запекания, анализируем результаты и завершаем финальное запекание на уровне 8k.
3. Приступаем к текстурированию. В конце добавляем постобработку в виде Ambient Occlusion и, при необходимости, дополнительные затенения к текстурам.)"},
    {"7. Экспорт в движок",
R"(❗️❗️При экспорте из Blender, в настройках эспорта во вкладке Geometry, Smoothing меняем на Face.

Экспорт модели и текстур в движок: Модель и текстуры распределяются по соответствующим папкам (Mesh, Material, Texture). Я отдельно опубликую информацию о том, как именовать текстуры и меш.

Настройка шейдеров (материалов) и текстур, анимаций: На этом этапе мы настраиваем шейдеры и текстуры, а затем проверяем результат.

❗️❗️Соблюдаем нейминг папок, файлов, иерархию.

Упаковка, создание PreFab и отправка разработчикам: все необходимые файлы упаковываются и отправляются разработчикам для дальнейшей работы.

✅Каждый этап отправляется Арт-лиду на одобрение!)"}
};

// Load the currently selected profile from storage and synchronise its skills.
void RefreshActiveProfile(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    state.active.reset();
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.profiles.size())) return;
    const auto& info = state.profiles[state.selectedIndex];
    if (!storage.set_active_profile(info.id)) return;
    if (auto loaded = storage.load_profile()) {
        SyncProfileWithCatalog(*loaded, catalog);
        storage.save_profile(*loaded);
        state.active = GuiProfile{*loaded, info.id};
        state.activityLogs[state.active->id];
        PrepareXpEntries(state, catalog);
    }
}

void ReapplyRulesToProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    auto list = storage.list_profiles();
    std::string focusId = state.active ? state.active->id : std::string{};
    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            // Preserve current level/progress but re-evaluate XP with the new rules.
            int level = profile->overall_level();
            int progress = profile->level_progress();
            profile->set_level_and_progress(level, progress);
            storage.save_profile(*profile);
        }
    }
    if (!focusId.empty()) {
        storage.set_active_profile(focusId);
    }
}

// Reload profile list and try to keep the previously selected/active entry.
void RefreshProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& preferredId = {}) {
    std::string currentId;
    if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
        currentId = state.profiles[state.selectedIndex].id;
    }

    state.profiles = LoadProfiles(storage);
    if (state.profiles.empty()) {
        state.selectedIndex = -1;
        state.active.reset();
        return;
    }

    std::string targetId = preferredId.empty() ? currentId : preferredId;
    if (!targetId.empty()) {
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            if (state.profiles[i].id == targetId) {
                state.selectedIndex = i;
                RefreshActiveProfile(state, storage, catalog);
                return;
            }
        }
    }

    state.selectedIndex = 0;
    RefreshActiveProfile(state, storage, catalog);
}

void ShowStatus(const GuiState& state) {
    if (state.statusMessage.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(state.statusColor[0], state.statusColor[1], state.statusColor[2], state.statusColor[3]));
    ImGui::TextWrapped("%s", state.statusMessage.c_str());
    ImGui::PopStyleColor();
}

void ConfigureLocale() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "");
    try {
        std::locale sys("");
        std::locale::global(sys);
        std::cout.imbue(sys);
        std::cerr.imbue(sys);
        std::clog.imbue(sys);
        std::wcout.imbue(sys);
        std::wcerr.imbue(sys);
    } catch (...) {
#if defined(_WIN32)
        try {
            std::locale utf8(".UTF-8");
            std::locale::global(utf8);
            std::cout.imbue(utf8);
            std::cerr.imbue(utf8);
            std::clog.imbue(utf8);
            std::wcout.imbue(utf8);
            std::wcerr.imbue(utf8);
        } catch (...) {
            // ignore
        }
#endif
    }
}

} // namespace

// GUI entry point: configure locale, init GLFW/ImGui, then run main loop.
int main() {
    ConfigureLocale();
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "JobSkill GUI", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    auto storageDir = ResolveStorageDirectory();
    auto metaDir = storageDir / "meta";
    std::error_code iniEc;
    std::filesystem::create_directories(metaDir, iniEc);
    static std::string layoutPath;
    layoutPath = (metaDir / "gui-layout.ini").string();
    if (!layoutPath.empty()) {
        io.IniFilename = layoutPath.c_str();
    }

    const std::filesystem::path fontCandidates[] = {
        "gui/fonts/Roboto-Medium.ttf",
        "gui/fonts/Roboto-Regular.ttf",
        "fonts/Roboto-Medium.ttf",
        "fonts/Roboto-Regular.ttf",
        "Roboto-Medium.ttf",
        "Roboto-Regular.ttf",
        "libs/imgui/misc/fonts/Roboto-Medium.ttf",
        "libs/imgui/misc/fonts/DroidSans.ttf",
        "libs/imgui/misc/fonts/Karla-Regular.ttf",
        "libs/imgui/misc/fonts/Cousine-Regular.ttf"
    };
    ImFont* primaryFont = nullptr;
    for (const auto& candidate : fontCandidates) {
        if (auto resolved = FindAssetUpwards(candidate)) {
            primaryFont = io.Fonts->AddFontFromFileTTF(resolved->string().c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
            if (primaryFont) break;
        }
    }
    if (!primaryFont) {
        ImFont* fallback = io.Fonts->AddFontDefault();
        (void)fallback;
        if (auto droid = FindAssetUpwards("libs/imgui/misc/fonts/DroidSans.ttf")) {
            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(droid->string().c_str(), 18.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    extern IJobStorage* CreateFileStorage(const std::filesystem::path& dir);
    SkillCatalog catalog(storageDir);
    auto gameplayConfig = LoadGameplayConfig(storageDir);
    SetGameplayConfig(gameplayConfig);
    std::unique_ptr<IJobStorage> storage(CreateFileStorage(storageDir));
    EnsureAdminProfile(*storage, catalog);

    GuiState state;
    state.storageDir = storageDir;
    state.rulesConfig = gameplayConfig;
    state.rulesDraft = gameplayConfig;
    RefreshProfiles(state, *storage, catalog);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Main menu window
        ImGui::Begin(u8"Главное меню");
        if (ImGui::Button(u8"Обновить")) {
            RefreshProfiles(state, *storage, catalog);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"Создать")) {
            state.modalBuffer.fill('\0');
            state.createPopupRequest = true;
        }
        ImGui::SameLine();
        bool canArchive = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size()) && !state.profiles[state.selectedIndex].archived;
        if (!canArchive) ImGui::BeginDisabled();
        if (ImGui::Button(u8"В архив")) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Archive;
        }
        if (!canArchive) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canRestore = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size()) && state.profiles[state.selectedIndex].archived;
        if (!canRestore) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Вернуть")) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Restore;
        }
        if (!canRestore) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canDelete = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size());
        if (!canDelete) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Удалить")) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Delete;
        }
        if (!canDelete) ImGui::EndDisabled();
        ImGui::SameLine();
        bool hasActive = state.active.has_value();
        if (!hasActive) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Добавить опыт")) {
            PrepareXpEntries(state, catalog);
            state.xpPopupRequest = true;
        }
        if (!hasActive) ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::BeginChild("profiles", ImVec2(0, 0), false)) {
            for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                const auto& info = state.profiles[i];
                std::string label = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
                if (ImGui::Selectable(label.c_str(), state.selectedIndex == i)) {
                    state.selectedIndex = i;
                    RefreshActiveProfile(state, *storage, catalog);
                    SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();

        // Profile details window
        ImGui::Begin(u8"Профиль");
        if (!state.active) {
            ImGui::TextUnformatted(u8"Выберите профиль, чтобы увидеть детали.");
        } else {
            Profile& profile = state.active->profile;
            const int overallLevel = profile.overall_level();
            const int totalXp = profile.total_xp();
            const int progressXp = profile.level_progress();
            const int xpToNext = profile.xp_to_next_level();
            const int xpNeededThisLevel = progressXp + xpToNext;
            const float progressRatio = xpNeededThisLevel > 0 ? static_cast<float>(progressXp) / static_cast<float>(xpNeededThisLevel) : 0.0f;
            const std::string levelText = "Level " + std::to_string(overallLevel);
            const std::string progressLabel = std::to_string(progressXp) + " / " + std::to_string(xpNeededThisLevel);

            const float levelScale = 1.8f;
            ImVec2 headerStartScreen = ImGui::GetCursorScreenPos();
            ImVec2 levelSize = ImGui::CalcTextSize(levelText.c_str());
            float scaledWidth = levelSize.x * levelScale;
            ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
            ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
            ImVec2 windowPos = ImGui::GetWindowPos();
            float rightEdge = windowPos.x + contentMax.x;
            float leftEdge = windowPos.x + contentMin.x;
            float levelX = rightEdge - scaledWidth - ImGui::GetStyle().ItemInnerSpacing.x;
            if (levelX < leftEdge) levelX = leftEdge;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * levelScale,
                              ImVec2(levelX, headerStartScreen.y),
                              ImGui::GetColorU32(ImGuiCol_Text), levelText.c_str());

            ImGui::Text(u8"Имя: %s", profile.name().c_str());
            ImGui::Text("ID: %s", state.active->id.c_str());
            ImGui::Text(u8"Ранг: %s", DescribeOverallRank(state.active->profile).c_str());
            {
                const auto& catScores = state.active->profile.category_best_scores();
                std::ostringstream catStream;
                for (size_t i = 0; i < catScores.size(); ++i) {
                    if (i) catStream << ", ";
                    catStream << Profile::kCategoryLabels[i] << "=" << catScores[i] << "/10";
                }
                ImGui::Text(u8"Категории: %s", catStream.str().c_str());
            }
            ImGui::Text(u8"Всего XP: %d", totalXp);
            ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f), progressLabel.c_str());
            ImGui::Separator();

            ImGui::TextUnformatted(u8"Навыки");
            ImGui::Columns(4, "skill_table");
            ImGui::TextUnformatted(u8"Навык");
            ImGui::NextColumn();
            ImGui::TextUnformatted(u8"Уровень");
            ImGui::NextColumn();
            ImGui::TextUnformatted("XP");
            ImGui::NextColumn();
            ImGui::TextUnformatted(u8"Вес");
            ImGui::NextColumn();
            ImGui::Separator();

            auto skills = state.active->profile.list_skills();
            std::sort(skills.begin(), skills.end(), [](const Skill& a, const Skill& b) {
                return a.name < b.name;
            });
            for (const auto& skill : skills) {
                ImGui::TextUnformatted(skill.name.c_str());
                ImGui::NextColumn();
                ImGui::Text("%d", skill.level);
                ImGui::NextColumn();
                ImGui::Text("%d / %d", skill.xp, skill.xpToNext);
                ImGui::NextColumn();
                ImGui::Text("%.2f", skill.weight);
                ImGui::NextColumn();
            }
            ImGui::Columns(1);

            ImGui::Separator();
            ImGui::TextUnformatted("Диаграмма навыков");
            ImVec2 radarSize(ImGui::GetContentRegionAvail().x, 320.0f);
            if (radarSize.x < 200.0f) radarSize.x = 200.0f;
            ImVec2 radarPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##skill_radar_canvas", radarSize);
            if (ImGui::IsItemVisible()) {
                DrawSkillRadarChart(skills, radarPos, ImGui::GetItemRectSize(), ImGui::GetWindowDrawList());
            }
            ImGui::Dummy(ImVec2(0.0f, 6.0f));

            ImGui::Separator();
            ImGui::TextUnformatted(u8"Последние действия");
            const auto logIt = state.activityLogs.find(state.active->id);
            if (logIt != state.activityLogs.end() && !logIt->second.empty()) {
                for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                    ImGui::BulletText("%s", it->c_str());
                }
            } else {
                ImGui::TextUnformatted(u8"Пока нет действий.");
            }

            ShowStatus(state);
        }
        ImGui::End();

        if (ImGui::Begin("Skill Catalog")) {
            const auto& catalogSkills = catalog.skills();
            if (state.selectedCatalogIndex >= static_cast<int>(catalogSkills.size())) {
                state.selectedCatalogIndex = -1;
            }
            for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                const std::string& skillName = catalogSkills[i];
                std::ostringstream label;
                label << skillName << " (" << std::fixed << std::setprecision(2) << catalog.weight(skillName) << ")";
                bool selected = state.selectedCatalogIndex == i;
                if (ImGui::Selectable(label.str().c_str(), selected)) {
                    state.selectedCatalogIndex = i;
                }
                if (ImGui::IsItemHovered()) {
                    const std::string desc = catalog.description(skillName);
                    if (!desc.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                        ImGui::TextUnformatted(desc.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
            }
            ImGui::Separator();
            if (state.selectedCatalogIndex >= 0 && state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
                const std::string& skillName = catalogSkills[state.selectedCatalogIndex];
                const std::string desc = catalog.description(skillName);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", skillName.c_str());
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                ImGui::TextWrapped("%s", desc.empty() ? "No description available." : desc.c_str());
                ImGui::PopTextWrapPos();
            } else {
                ImGui::TextUnformatted("Select a skill to see its description.");
            }
        }
        ImGui::End();

        if (ImGui::Begin("Pipeline")) {
            const int stepCount = static_cast<int>(kPipelineSteps.size());
            if (stepCount == 0) {
                ImGui::TextUnformatted("Pipeline is empty.");
            } else {
                if (state.selectedPipelineIndex < 0 || state.selectedPipelineIndex >= stepCount) {
                    state.selectedPipelineIndex = 0;
                }
                if (ImGui::BeginChild("pipeline_list", ImVec2(0, 140), true)) {
                    for (int i = 0; i < stepCount; ++i) {
                        bool selected = state.selectedPipelineIndex == i;
                        if (ImGui::Selectable(kPipelineSteps[i].title, selected)) {
                            state.selectedPipelineIndex = i;
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::Separator();
                const PipelineStep& step = kPipelineSteps[state.selectedPipelineIndex];
                ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), "%s", step.title);
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 45.0f);
                ImGui::TextUnformatted(step.description);
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::End();

        if (ImGui::Begin("Gameplay Rules")) {
            GameplayConfig& draft = state.rulesDraft;
            ImGui::TextUnformatted("Leveling curve");
            ImGui::InputInt("Base XP (level 1)", &draft.levelBaseXp);
            ImGui::InputInt("Linear gain per level", &draft.levelLinearXp);
            ImGui::InputInt("Quadratic gain per level", &draft.levelQuadraticXp);
            ImGui::Separator();
            ImGui::TextUnformatted("Category base XP");
            for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                std::string label = std::string("Tier ") + Profile::kCategoryLabels[idx] + " XP";
                int value = draft.categoryBaseXp[idx];
                if (ImGui::InputInt(label.c_str(), &value)) {
                    draft.categoryBaseXp[idx] = value;
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Bonuses & penalties");
            ImGui::InputFloat("Focus base bonus", &draft.focusBaseBonus, 0.05f, 0.5f, "%.2f");
            ImGui::InputFloat("Focus extra bonus", &draft.focusAdditionalBonus, 0.05f, 0.5f, "%.2f");
            ImGui::SliderFloat("Repeat reward factor", &draft.repeatRewardFactor, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Recovery reward factor", &draft.recoveryRewardFactor, 0.0f, 1.0f, "%.2f");
            ImGui::InputInt("Recovery warm-up tasks", &draft.recoveryWarmupTasks);
            ImGui::TextDisabled("Changes affect both CLI and GUI once saved.");
            if (ImGui::Button("Reset changes")) {
                draft = state.rulesConfig;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save && Apply")) {
                GameplayConfig sanitized = draft;
                sanitized.levelBaseXp = std::max(1, sanitized.levelBaseXp);
                sanitized.levelLinearXp = std::max(0, sanitized.levelLinearXp);
                sanitized.levelQuadraticXp = std::max(0, sanitized.levelQuadraticXp);
                for (auto& value : sanitized.categoryBaseXp) {
                    value = std::max(0, value);
                }
                sanitized.focusBaseBonus = std::clamp(sanitized.focusBaseBonus, 0.0f, 10.0f);
                sanitized.focusAdditionalBonus = std::clamp(sanitized.focusAdditionalBonus, 0.0f, 10.0f);
                sanitized.repeatRewardFactor = std::clamp(sanitized.repeatRewardFactor, 0.0f, 1.0f);
                sanitized.recoveryRewardFactor = std::clamp(sanitized.recoveryRewardFactor, 0.0f, 1.0f);
                sanitized.recoveryWarmupTasks = std::max(0, sanitized.recoveryWarmupTasks);
                if (SaveGameplayConfig(sanitized, state.storageDir)) {
                    state.rulesConfig = sanitized;
                    state.rulesDraft = sanitized;
                    SetGameplayConfig(sanitized);
                    ReapplyRulesToProfiles(state, *storage, catalog);
                    std::string keepId = state.active ? state.active->id : std::string{};
                    RefreshProfiles(state, *storage, catalog, keepId);
                    SetStatus(state, "Gameplay rules saved.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, "Failed to save gameplay rules.", 1.0f, 0.45f, 0.45f);
                }
            }
        }
        ImGui::End();

        // XP sheet modal
        if (state.xpPopupRequest) {
            ImGui::OpenPopup("Add Experience");
            state.xpPopupRequest = false;
        }
        bool xpPopupOpen = true;
        if (ImGui::BeginPopupModal("Add Experience", &xpPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto& catalogSkills = catalog.skills();
            if (state.xpEntries.size() != catalogSkills.size()) {
                PrepareXpEntries(state, catalog);
            }
            if (catalogSkills.empty()) {
                ImGui::TextUnformatted("Catalog is empty.");
            } else if (!state.active) {
                ImGui::TextUnformatted("No active profile.");
            } else {
                state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                const GameplayConfig& rules = GetGameplayConfig();

                ImGui::TextUnformatted("Task parameters");
                ImGui::Separator();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo("Classification", &state.taskCategoryIndex,
                                 Profile::kCategoryLabels.data(), Profile::kCategoryCount)) {
                    state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                }
                const int currentCategory = state.taskCategoryIndex;
                const int baseXp = rules.categoryBaseXp[currentCategory];
                const float scoreRatio = static_cast<float>(state.taskScore) / static_cast<float>(Profile::kMaxCategoryScore);
                const float scoreMultiplier = std::pow(std::max(0.1f, scoreRatio), 1.35f);
                const int currentBest = state.active->profile.category_best_score(static_cast<size_t>(currentCategory));
                ImGui::SameLine();
                ImGui::Text("Base XP: %d", baseXp);
                ImGui::TextDisabled("Category tier defines base XP; score adds a non-linear multiplier.");
                if (ImGui::SliderInt("Score", &state.taskScore, 1, Profile::kMaxCategoryScore)) {
                    state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                }
                ImGui::Text("Score multiplier: %.2f", scoreMultiplier);
                if (currentBest >= Profile::kMaxCategoryScore) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                       "Previous best: %d/10 (category mastered)", currentBest);
                } else {
                    ImGui::Text("Previous best: %d/10", currentBest);
                    if (state.taskScore <= currentBest) {
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                           "Repeat score => global XP limited to 35%% (skills still full).");
                    }
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Distribute skills (auto-balanced to 100%)");
                const float sliderWidth = ImGui::CalcTextSize("000").x + ImGui::GetStyle().FramePadding.x * 6.0f;
                int percentSum = 0;
                int maxSharePercent = 0;
                if (ImGui::BeginTable("xp_sheet", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Skill");
                    ImGui::TableSetupColumn("Share (%)");
                    ImGui::TableSetupColumn("XP");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(catalogSkills[i].c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(i);
                        int percent = state.xpEntries[i].percent;
                        ImGui::PushItemWidth(sliderWidth);
                        if (ImGui::SliderInt("##share", &percent, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                            AdjustSkillShare(state, i, percent);
                            percent = state.xpEntries[i].percent;
                        }
                        ImGui::PopItemWidth();
                        ImGui::PopID();
                        percentSum += state.xpEntries[i].percent;
                        if (state.xpEntries[i].percent > maxSharePercent) {
                            maxSharePercent = state.xpEntries[i].percent;
                        }
                        ImGui::TableSetColumnIndex(2);
                        const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                        ImGui::Text("%d", previewXp);
                    }
                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::Text("Assigned: %d%%", percentSum);
                const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * (static_cast<float>(maxSharePercent) / 100.0f);
                const int previewPool = static_cast<int>(std::round(baseXp * scoreMultiplier * focusBonus));
                ImGui::Text("Focus bonus: %.2f (max share %d%%)", focusBonus, maxSharePercent);
                ImGui::TextDisabled("Projected skill XP pool before penalties: %d", previewPool);

                if (ImGui::Button("Apply")) {
                    const int readyCategory = NormalizeCategoryIndex(state.taskCategoryIndex);
                    const int readyScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                    const int storedBest = state.active->profile.category_best_score(static_cast<size_t>(readyCategory));
                    int percentCheck = 0;
                    bool hasContribution = false;
                    for (const auto& entry : state.xpEntries) {
                        int clamped = ClampToRange(entry.percent, 0, 100);
                        percentCheck += clamped;
                        if (clamped > 0) hasContribution = true;
                    }
                    if (percentCheck != 100) {
                        SetStatus(state, "Distribution must stay at 100%.", 1.0f, 0.45f, 0.45f);
                    } else if (!hasContribution) {
                        SetStatus(state, "At least one skill must receive experience.", 1.0f, 0.45f, 0.45f);
                    } else {
                        float maxShare = 0.0f;
                        if (!state.xpEntries.empty()) {
                            const auto it = std::max_element(
                                state.xpEntries.begin(), state.xpEntries.end(),
                                [](const XpEntry& a, const XpEntry& b) { return a.percent < b.percent; });
                            maxShare = static_cast<float>(it->percent) / 100.0f;
                        }
                        const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * maxShare;
                        int basePool = static_cast<int>(std::round(
                            rules.categoryBaseXp[readyCategory] *
                            std::pow(std::max(0.1f, static_cast<float>(readyScore) / 10.0f), 1.35f) * focusBonus));
                        if (basePool < 0) basePool = 0;
                        std::vector<int> xpDistribution(state.xpEntries.size(), 0);
                        int remainder = basePool;
                        int fallbackIndex = -1;
                        for (int i = 0; i < static_cast<int>(state.xpEntries.size()); ++i) {
                            int percent = ClampToRange(state.xpEntries[i].percent, 0, 100);
                            if (percent <= 0) continue;
                            int shareXp = (basePool * percent) / 100;
                            xpDistribution[i] = shareXp;
                            remainder -= shareXp;
                            if (fallbackIndex == -1 || percent > state.xpEntries[fallbackIndex].percent) {
                                fallbackIndex = i;
                            }
                        }
                        if (remainder > 0 && fallbackIndex >= 0) {
                            xpDistribution[fallbackIndex] += remainder;
                        }
                        const char* categoryLabel = ClassificationLabel(readyCategory);
                        std::ostringstream skillsStream;
                        bool firstSkill = true;
                        for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                            int shareXp = xpDistribution[i];
                            if (shareXp <= 0) continue;
                            const std::string& skillName = catalogSkills[i];
                            double weight = catalog.weight(skillName);
                            state.active->profile.add_skill(skillName, 1, weight);
                            bool leveled = state.active->profile.grant_xp(skillName, shareXp);
                            if (!firstSkill) skillsStream << " | ";
                            skillsStream << skillName << " +" << shareXp << " XP (" << state.xpEntries[i].percent << "%)";
                            if (leveled) skillsStream << " lvl up";
                            firstSkill = false;
                        }
                        if (!firstSkill) {
                            AppendLog(state, state.active->id, skillsStream.str());
                        }
                        bool repeatPenalty = readyScore <= storedBest;
                        int effectiveXp = basePool;
                        if (repeatPenalty) {
                            effectiveXp = static_cast<int>(std::round(effectiveXp * rules.repeatRewardFactor));
                        }
                        const auto now = std::chrono::system_clock::now();
                        const std::int64_t nowSeconds =
                            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                        constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;
                        if (state.active->profile.last_task_timestamp() > 0 &&
                            (nowSeconds - state.active->profile.last_task_timestamp()) > kThirtyDays) {
                            state.active->profile.start_penalty_recovery(rules.recoveryWarmupTasks);
                        }
                        bool recoveryPenalty = false;
                        if (state.active->profile.penalty_active()) {
                            recoveryPenalty = true;
                            effectiveXp = static_cast<int>(std::round(effectiveXp * rules.recoveryRewardFactor));
                            state.active->profile.consume_penalty_task();
                        }
                        state.active->profile.set_last_task_timestamp(nowSeconds);
                        if (effectiveXp > 0) {
                            state.active->profile.grant_global_xp(effectiveXp);
                        }
                        if (readyScore > storedBest) {
                            state.active->profile.update_category_best_score(static_cast<size_t>(readyCategory), readyScore);
                            std::ostringstream bestStream;
                            bestStream << "Category " << categoryLabel << " best: " << readyScore << "/10";
                            AppendLog(state, state.active->id, bestStream.str());
                            if (readyScore == Profile::kMaxCategoryScore) {
                                std::ostringstream mastery;
                                mastery << "Category " << categoryLabel << " mastered!";
                                AppendLog(state, state.active->id, mastery.str());
                            }
                        }
                        state.active->profile.reset_category_cooldown(static_cast<size_t>(readyCategory));
                        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                            if (idx == static_cast<size_t>(readyCategory)) continue;
                            state.active->profile.tick_category_cooldown(idx);
                            if (state.active->profile.category_cooldown(idx) < 0) {
                                state.active->profile.update_category_best_score(
                                    idx, state.active->profile.category_best_score(idx) - 1);
                                state.active->profile.reset_category_cooldown(idx);
                                std::ostringstream decay;
                                decay << "Category " << Profile::kCategoryLabels[idx] << " decayed to "
                                      << state.active->profile.category_best_score(idx) << "/10";
                                AppendLog(state, state.active->id, decay.str());
                            }
                        }
                        int buffer = state.active->profile.category_cooldown(0);
                        for (size_t idx = 1; idx < Profile::kCategoryCount; ++idx) {
                            buffer = std::min(buffer, state.active->profile.category_cooldown(idx));
                        }
                        buffer = std::max(0, buffer);
                        state.active->profile.set_inactivity_tasks(buffer);
                        std::ostringstream summaryStream;
                        summaryStream << "Task [" << categoryLabel << "] score " << readyScore
                                      << " => +" << effectiveXp << " XP overall";
                        if (repeatPenalty) summaryStream << " (repeat 35%)";
                        if (recoveryPenalty) summaryStream << " (recovery 60%)";
                        const std::string summaryText = summaryStream.str();
                        AppendLog(state, state.active->id, summaryText);
                        storage->save_profile(state.active->profile);
                        PrepareXpEntries(state, catalog);
                        SetStatus(state, summaryText, 0.45f, 0.9f, 0.45f);
                        ImGui::CloseCurrentPopup();
                        xpPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!xpPopupOpen && ImGui::IsPopupOpen("Add Experience")) {
            ImGui::CloseCurrentPopup();
        }

        // Create profile modal
        if (state.createPopupRequest) {
            ImGui::OpenPopup("Create Profile");
            state.createPopupRequest = false;
        }
        bool createPopupOpen = true;
        if (ImGui::BeginPopupModal("Create Profile", &createPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", state.modalBuffer.data(), state.modalBuffer.size());
            if (ImGui::Button("Create")) {
                std::string name(state.modalBuffer.data());
                name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char c){ return !std::isspace(c); }));
                name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), name.end());
                if (name.empty()) {
                    SetStatus(state, "Name cannot be empty.", 1.0f, 0.45f, 0.45f);
                } else {
                    Profile profile(name);
                    SyncProfileWithCatalog(profile, catalog);
                    if (auto info = storage->create_profile(profile)) {
                        storage->save_profile(profile);
                        SetStatus(state, "Profile created.", 0.45f, 0.9f, 0.45f);
                        RefreshProfiles(state, *storage, catalog, info->id);
                        ImGui::CloseCurrentPopup();
                        createPopupOpen = false;
                    } else {
                        SetStatus(state, "Failed to create profile.", 1.0f, 0.45f, 0.45f);
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
                createPopupOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!createPopupOpen && ImGui::IsPopupOpen("Create Profile")) {
            ImGui::CloseCurrentPopup();
        }

        // Confirm modal
        if (state.confirmPopupRequest) {
            ImGui::OpenPopup("Confirm Action");
            state.confirmPopupRequest = false;
        }
        bool confirmPopupOpen = true;
        if (ImGui::BeginPopupModal("Confirm Action", &confirmPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
                const auto& info = state.profiles[state.selectedIndex];
                std::string action;
                switch (state.confirmAction) {
                    case ConfirmAction::Archive: action = "archive"; break;
                    case ConfirmAction::Restore: action = "restore"; break;
                    case ConfirmAction::Delete: action = "delete"; break;
                    default: action = ""; break;
                }
                ImGui::Text("Confirm %s of profile [%s] %s?", action.c_str(), info.id.c_str(), info.name.c_str());
                if (ImGui::Button("Yes")) {
                    bool ok = false;
                    if (state.confirmAction == ConfirmAction::Archive) {
                        ok = storage->set_archived(info.id, true);
                    } else if (state.confirmAction == ConfirmAction::Restore) {
                        ok = storage->set_archived(info.id, false);
                    } else if (state.confirmAction == ConfirmAction::Delete) {
                        ok = storage->delete_profile(info.id);
                    }
                    if (ok) {
                        SetStatus(state, "Operation complete.", 0.45f, 0.9f, 0.45f);
                        std::string newFocus;
                        if (state.confirmAction != ConfirmAction::Delete) {
                            newFocus = info.id;
                        }
                        RefreshProfiles(state, *storage, catalog, newFocus);
                    } else {
                        SetStatus(state, "Operation failed.", 1.0f, 0.45f, 0.45f);
                    }
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("No")) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            } else {
                ImGui::TextUnformatted("No profile selected.");
                if (ImGui::Button("Close")) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!confirmPopupOpen && ImGui::IsPopupOpen("Confirm Action")) {
            ImGui::CloseCurrentPopup();
            state.confirmAction = ConfirmAction::None;
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}



