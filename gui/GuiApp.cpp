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

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

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
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <vector>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

constexpr const char* kAdminPassword = "admin123";

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
    bool addSkillPopupRequest = false;
    bool deleteSkillPopupRequest = false;
    bool mergeSkillPopupRequest = false;
    bool adminPopupRequest = false;
    bool isAdmin = false;
    ConfirmAction confirmAction = ConfirmAction::None;
    std::array<char, 128> modalBuffer{};
    std::array<char, 128> newSkillName{};
    std::array<char, 256> newSkillDesc{};
    std::array<char, 128> editSkillName{};
    std::array<char, 256> editSkillDesc{};
    float newSkillWeight = 1.0f;
    std::array<char, 64> adminPassword{};
    std::string pendingSkillDelete;
    std::string pendingMergeFromId;
    std::string pendingMergeToId;
    std::string pendingMergeName;
    std::string pendingMergeDesc;
    float pendingMergeWeight = 1.0f;
    float editedSkillWeight = 1.0f;
    std::array<char, 128> achTitle{};
    std::array<char, 128> achIcon{};
    float achBonus = 0.0f;
    int achDurationDays = 0;
    int selectedAchievementIndex = -1;
    std::vector<XpEntry> xpEntries;
    std::unordered_map<std::string, std::deque<std::string>> activityLogs;
    int taskScore = 10;
    int taskCategoryIndex = 0;
    int selectedCatalogIndex = -1;
    int selectedPipelineIndex = 0;
    int selectedRankIndex = 0;
    int lastCatalogSelection = -1;
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

std::string NormalizeSkillNameGui(const std::string& name) {
    auto decode_utf8 = [](const std::string& s, size_t& i, uint32_t& out) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80) {
            out = c0;
            ++i;
            return true;
        }
        if ((c0 >> 5) == 0x6 && i + 1 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c1 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            i += 2;
            return true;
        }
        if ((c0 >> 4) == 0xE && i + 2 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            i += 3;
            return true;
        }
        if ((c0 >> 3) == 0x1E && i + 3 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            i += 4;
            return true;
        }
        return false;
    };
    auto append_utf8 = [](std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    auto lower_codepoint = [](uint32_t cp) {
        if (cp >= 'A' && cp <= 'Z') return cp + 32;
        if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
        if (cp == 0x0401) return static_cast<uint32_t>(0x0451);
        return cp;
    };

    std::string out;
    out.reserve(name.size());
    size_t i = 0;
    while (i < name.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(name, i, cp)) break;
        if (cp <= 0x7F && std::isspace(static_cast<unsigned char>(cp))) continue;
        append_utf8(out, lower_codepoint(cp));
    }
    return out;
}

std::string TrimStringGui(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct IconTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

struct IconCache {
    std::unordered_map<std::string, IconTexture> textures;
    std::unordered_set<std::string> missing;
};

IconCache& GetIconCache() {
    static IconCache cache;
    return cache;
}

std::string NormalizeIconKey(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = path.lexically_normal();
    if (!normalized.is_absolute()) {
        normalized = std::filesystem::absolute(normalized, ec);
        if (ec) {
            normalized = path.lexically_normal();
        }
    }
    auto key = normalized.generic_string();
    if (key.empty()) key = path.generic_string();
    return key;
}

std::optional<std::filesystem::path> ResolveIconPath(const std::string& icon, const std::filesystem::path& storageDir) {
    if (icon.empty()) return std::nullopt;
    std::filesystem::path path(icon);
    std::error_code ec;
    if (path.is_relative()) {
        const std::filesystem::path candidates[] = {
            storageDir / path,
            std::filesystem::current_path() / path,
            storageDir.parent_path() / path
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    if (std::filesystem::exists(path, ec)) {
        return path;
    }
    return std::nullopt;
}

bool LoadIconTexture(const std::filesystem::path& path, IconTexture& out) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_set_flip_vertically_on_load(0);
    std::string pathStr = path.string();
    unsigned char* data = stbi_load(pathStr.c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return false;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    GLint lastTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTex);
    GLint lastUnpack = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &lastUnpack);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, lastUnpack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTex));

    stbi_image_free(data);
    out.id = tex;
    out.width = w;
    out.height = h;
    return true;
}

const IconTexture* GetIconTexture(const std::filesystem::path& path) {
    auto& cache = GetIconCache();
    const std::string key = NormalizeIconKey(path);
    if (auto it = cache.textures.find(key); it != cache.textures.end()) {
        return &it->second;
    }
    if (cache.missing.find(key) != cache.missing.end()) {
        return nullptr;
    }
    IconTexture tex;
    if (!LoadIconTexture(path, tex)) {
        cache.missing.insert(key);
        return nullptr;
    }
    auto [it, inserted] = cache.textures.emplace(key, tex);
    return &it->second;
}

ImVec2 FitIconSize(const IconTexture& tex, float maxSize) {
    if (tex.width <= 0 || tex.height <= 0) return ImVec2(maxSize, maxSize);
    const float maxDim = static_cast<float>(std::max(tex.width, tex.height));
    const float scale = maxDim > 0.0f ? (maxSize / maxDim) : 1.0f;
    return ImVec2(tex.width * scale, tex.height * scale);
}

void ReleaseIconTextures() {
    auto& cache = GetIconCache();
    for (auto& kv : cache.textures) {
        if (kv.second.id != 0) {
            glDeleteTextures(1, &kv.second.id);
            kv.second.id = 0;
        }
    }
    cache.textures.clear();
    cache.missing.clear();
}

struct RankOption {
    const char* label;
    int level;
};

const std::vector<RankOption>& RankOptions() {
    static const std::vector<RankOption> opts = {
        {u8"Стажёр", 1},
        {u8"Джуниор I", 10},
        {u8"Джуниор II", 20},
        {u8"Джуниор III", 30},
        {u8"Джуниор IV", 40},
        {u8"Мидл I", 50},
        {u8"Мидл II", 60},
        {u8"Мидл III", 70},
        {u8"Мидл IV", 80},
        {u8"Мидл V", 90},
        {u8"Мидл VI", 100},
        {u8"Сеньор I", 150},
        {u8"Сеньор II", 160},
        {u8"Сеньор III", 170},
        {u8"Сеньор IV", 180},
        {u8"Сеньор V", 190}
    };
    return opts;
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

bool RemoveSkillFromProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& skillName) {
    std::string target = skillName;
    if (!catalog.contains_id(target)) {
        if (auto id = catalog.id_for_name(skillName)) {
            target = *id;
        }
    }
    bool removedAny = false;
    auto list = storage.list_profiles();
    std::string restoreId = state.active ? state.active->id : std::string{};
    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            auto skills = profile->list_skills();
            auto before = skills.size();
            skills.erase(std::remove_if(skills.begin(), skills.end(), [&](const Skill& s) {
                return s.name == target;
            }), skills.end());
            if (skills.size() != before) {
                profile->set_skills(skills);
                storage.save_profile(*profile);
                removedAny = true;
            }
        }
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    return removedAny;
}

int TotalSkillXp(const Skill& skill) {
    int total = skill.xp;
    for (int lvl = 2; lvl <= skill.level; ++lvl) {
        total += Skill::required_xp_for(lvl);
    }
    return total;
}

bool MergeSkillInProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& fromId, const std::string& toId) {
    if (fromId == toId) return false;
    bool changedAny = false;
    auto list = storage.list_profiles();
    std::string restoreId = state.active ? state.active->id : std::string{};
    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            bool changed = false;
            auto skills = profile->list_skills();
            int fromIndex = -1;
            int toIndex = -1;
            for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                if (skills[i].name == fromId) fromIndex = i;
                if (skills[i].name == toId) toIndex = i;
            }
            if (fromIndex >= 0) {
                if (toIndex >= 0 && toIndex != fromIndex) {
                    int totalFrom = TotalSkillXp(skills[fromIndex]);
                    skills[toIndex].add_xp(totalFrom);
                    if (fromIndex > toIndex) {
                        skills.erase(skills.begin() + fromIndex);
                    } else {
                        skills.erase(skills.begin() + fromIndex);
                        toIndex -= 1;
                    }
                } else {
                    skills[fromIndex].name = toId;
                }
                profile->set_skills(skills);
                changed = true;
            }

            auto ach = profile->achievements();
            bool achChanged = false;
            for (auto& a : ach) {
                if (a.skill == fromId) {
                    a.skill = toId;
                    achChanged = true;
                }
            }
            if (achChanged) {
                profile->set_achievements(ach);
            }

            if (changed || achChanged) {
                SyncProfileWithCatalog(*profile, catalog);
                storage.save_profile(*profile);
                changedAny = true;
            }
        }
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    return changedAny;
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
        ImGui::TextUnformatted(state.isAdmin ? u8"[Режим администратора]" : u8"[Режим просмотра]");
        if (!state.isAdmin) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        if (ImGui::Button(state.isAdmin ? u8"Выйти из админа" : u8"Войти как админ")) {
            if (state.isAdmin) {
                state.isAdmin = false;
            } else {
                state.adminPopupRequest = true;
                state.adminPassword.fill('\0');
            }
        }
        if (!state.isAdmin) ImGui::PopStyleColor();
        ImGui::Separator();
        if (ImGui::Button(u8"Обновить")) {
            RefreshProfiles(state, *storage, catalog);
        }
        ImGui::SameLine();
        if (!state.isAdmin) ImGui::BeginDisabled();
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
        if (!state.isAdmin) ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::BeginChild("profiles", ImVec2(0, 0), false)) {
            for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                const auto& info = state.profiles[i];
                std::string label = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
                if (ImGui::Selectable(label.c_str(), state.selectedIndex == i)) {
                    state.selectedIndex = i;
        RefreshActiveProfile(state, *storage, catalog);
        // Align rank selector to current level
        const auto& opts = RankOptions();
        int bestIdx = 0;
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            if (state.active && state.active->profile.overall_level() >= opts[i].level) {
                bestIdx = i;
            }
        }
        state.selectedRankIndex = bestIdx;
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
            const std::string levelText = u8"Уровень " + std::to_string(overallLevel);
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
            if (!profile.achievements().empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted(u8"Ачивки");
                auto formatDate = [](std::int64_t ts) -> std::string {
                    if (ts <= 0) return std::string(u8"без срока");
                    std::time_t t = static_cast<std::time_t>(ts);
                    std::tm tm{};
#if defined(_WIN32)
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    char buf[32];
                    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm)) {
                        return std::string(buf);
                    }
                    return std::string(u8"срок указан");
                };
                auto formatRemaining = [](std::int64_t seconds) -> std::string {
                    if (seconds <= 0) return std::string("00:00:00");
                    constexpr std::int64_t daySeconds = 24 * 3600;
                    const std::int64_t days = seconds / daySeconds;
                    seconds %= daySeconds;
                    const std::int64_t hours = seconds / 3600;
                    seconds %= 3600;
                    const std::int64_t minutes = seconds / 60;
                    const std::int64_t secs = seconds % 60;
                    std::ostringstream out;
                    out << std::setfill('0');
                    if (days > 0) {
                        out << days << u8"д ";
                    }
                    out << std::setw(2) << hours << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
                    return out.str();
                };
                const auto nowSec = NowSeconds();
                constexpr int kIconsPerRow = 6;
                const float iconCell = 110.0f;
                int iconIndex = 0;
                for (const auto& a : profile.achievements()) {
                    bool active = a.is_active(nowSec);
                    const std::string expires = formatDate(a.expiresAt);
                    const auto iconPath = ResolveIconPath(a.icon, state.storageDir);
                    ImGui::PushID(iconIndex);
                    bool hovered = false;
                    if (iconPath) {
                        if (const auto* iconTex = GetIconTexture(*iconPath)) {
                            ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)iconTex->id), ImVec2(iconCell, iconCell));
                            hovered = ImGui::IsItemHovered();
                        } else {
                            ImGui::BeginDisabled();
                            ImGui::Button("?", ImVec2(iconCell, iconCell));
                            ImGui::EndDisabled();
                            hovered = ImGui::IsItemHovered();
                        }
                    } else {
                        ImGui::BeginDisabled();
                        ImGui::Button("?", ImVec2(iconCell, iconCell));
                        ImGui::EndDisabled();
                        hovered = ImGui::IsItemHovered();
                    }
                    if (hovered) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(a.title.c_str());
                        const std::string achSkillName = catalog.display_name(a.skill);
                        ImGui::Text(u8"%s, %+0.1f%% XP", achSkillName.c_str(), a.bonusPercent);
                        if (a.expiresAt == 0) {
                            ImGui::TextUnformatted(u8"Срок: без срока");
                        } else {
                            ImGui::Text(u8"Срок до: %s", expires.c_str());
                            if (active) {
                                const std::string remaining = formatRemaining(a.expiresAt - nowSec);
                                ImGui::Text(u8"Осталось: %s", remaining.c_str());
                            } else {
                                ImGui::TextUnformatted(u8"Срок: истекла");
                            }
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                    ++iconIndex;
                    if (iconIndex % kIconsPerRow != 0) {
                        ImGui::SameLine();
                    }
                }
                if (iconIndex % kIconsPerRow != 0) {
                    ImGui::NewLine();
                }
            }
            ImGui::Text(u8"Всего XP: %d", totalXp);
            ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f), progressLabel.c_str());
            ImGui::Separator();

            if (state.isAdmin) {
                const auto& opts = RankOptions();
                std::vector<const char*> labels;
                labels.reserve(opts.size());
                for (const auto& o : opts) labels.push_back(o.label);
                ImGui::TextUnformatted(u8"Назначить ранг");
                if (state.selectedRankIndex >= static_cast<int>(opts.size())) state.selectedRankIndex = 0;
                if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(), static_cast<int>(labels.size()))) {
                    state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Применить ранг")) {
                    const int newLevel = opts[state.selectedRankIndex].level;
                    state.active->profile.set_level_and_progress(newLevel, 0);
                    storage->save_profile(state.active->profile);
                    SetStatus(state, u8"Ранг обновлён.", 0.45f, 0.9f, 0.45f);
                }
                ImGui::Separator();
            }

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
            std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                return catalog.display_name(a.name) < catalog.display_name(b.name);
            });
            for (const auto& skill : skills) {
                const std::string displayName = catalog.display_name(skill.name);
                ImGui::TextUnformatted(displayName.c_str());
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
                auto radarSkills = skills;
                for (auto& skill : radarSkills) {
                    skill.name = catalog.display_name(skill.name);
                }
                DrawSkillRadarChart(radarSkills, radarPos, ImGui::GetItemRectSize(), ImGui::GetWindowDrawList());
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

        if (ImGui::Begin(u8"Каталог навыков")) {
            const auto& catalogSkills = catalog.skills();
        if (state.selectedCatalogIndex >= static_cast<int>(catalogSkills.size())) {
            state.selectedCatalogIndex = -1;
        }
        if (state.selectedCatalogIndex != state.lastCatalogSelection && state.selectedCatalogIndex >= 0 && state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
            const std::string& selectedId = catalogSkills[state.selectedCatalogIndex];
            const std::string selectedName = catalog.display_name(selectedId);
            const std::string selectedDesc = catalog.description(selectedId);
            state.editedSkillWeight = static_cast<float>(catalog.weight(selectedId));
            state.lastCatalogSelection = state.selectedCatalogIndex;
            state.selectedAchievementIndex = -1;
            state.achTitle.fill('\0');
            state.achIcon.fill('\0');
            state.achBonus = 0.0f;
            state.achDurationDays = 0;
            std::snprintf(state.editSkillName.data(), state.editSkillName.size(), "%s", selectedName.c_str());
            std::snprintf(state.editSkillDesc.data(), state.editSkillDesc.size(), "%s", selectedDesc.c_str());
        }

            ImGui::BeginGroup();
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Добавить навык")) {
                state.addSkillPopupRequest = true;
                std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
                std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
                state.newSkillWeight = 1.0f;
            }
            ImGui::SameLine();
            const bool canDeleteSkill = state.selectedCatalogIndex >= 0 && state.selectedCatalogIndex < static_cast<int>(catalogSkills.size());
            if (!canDeleteSkill) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить навык")) {
                state.deleteSkillPopupRequest = true;
                state.pendingSkillDelete = catalogSkills[state.selectedCatalogIndex];
            }
            if (!canDeleteSkill) ImGui::EndDisabled();
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::EndGroup();

            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                const std::string& skillId = catalogSkills[i];
                const std::string displayName = catalog.display_name(skillId);
                std::ostringstream label;
                label << displayName << " (" << std::fixed << std::setprecision(2) << catalog.weight(skillId) << ")";
                bool selected = state.selectedCatalogIndex == i;
                if (ImGui::Selectable(label.str().c_str(), selected)) {
                    state.selectedCatalogIndex = i;
                }
                if (ImGui::IsItemHovered()) {
                    const std::string desc = catalog.description(skillId);
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
            const std::int64_t nowSecGlobal = NowSeconds();
            if (state.selectedCatalogIndex >= 0 && state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
                const std::string& skillId = catalogSkills[state.selectedCatalogIndex];
                const std::string displayName = catalog.display_name(skillId);
                const std::string desc = catalog.description(skillId);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", displayName.c_str());
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                ImGui::TextWrapped("%s", desc.empty() ? u8"Описание отсутствует." : desc.c_str());
                ImGui::PopTextWrapPos();
                if (state.isAdmin) {
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    ImGui::TextUnformatted(u8"Вес навыка");
                    ImGui::SliderFloat("##edit_weight", &state.editedSkillWeight, 0.5f, 1.6f, "%.2f");
                    if (ImGui::Button(u8"Сохранить вес")) {
                        double newW = static_cast<double>(state.editedSkillWeight);
                        catalog.update_skill(skillId, displayName, newW, desc);
                        PrepareXpEntries(state, catalog);
                        RefreshProfiles(state, *storage, catalog, state.active ? state.active->id : std::string{});
                        SetStatus(state, u8"Вес навыка обновлён.", 0.45f, 0.9f, 0.45f);
                    }
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    ImGui::TextUnformatted(u8"Редактирование навыка");
                    ImGui::InputText(u8"Название", state.editSkillName.data(), state.editSkillName.size());
                    ImGui::InputTextMultiline(u8"Описание", state.editSkillDesc.data(), state.editSkillDesc.size(), ImVec2(-1.0f, 80.0f));
                    if (ImGui::Button(u8"Сохранить навык")) {
                        const std::string newName = TrimStringGui(state.editSkillName.data());
                        const std::string newDesc = TrimStringGui(state.editSkillDesc.data());
                        if (newName.empty()) {
                            SetStatus(state, u8"Название не может быть пустым.", 1.0f, 0.45f, 0.45f);
                        } else {
                            auto existingId = catalog.id_for_name(newName);
                            if (existingId && *existingId != skillId) {
                                state.mergeSkillPopupRequest = true;
                                state.pendingMergeFromId = skillId;
                                state.pendingMergeToId = *existingId;
                                state.pendingMergeName = newName;
                                state.pendingMergeDesc = newDesc;
                                state.pendingMergeWeight = state.editedSkillWeight;
                            } else {
                                bool changed = catalog.update_skill(skillId, newName,
                                                                    static_cast<double>(state.editedSkillWeight),
                                                                    newDesc);
                                if (!changed) {
                                    SetStatus(state, u8"Изменений нет.", 0.9f, 0.8f, 0.5f);
                                } else {
                                    PrepareXpEntries(state, catalog);
                                    RefreshProfiles(state, *storage, catalog, state.active ? state.active->id : std::string{});
                                    state.lastCatalogSelection = -1;
                                    SetStatus(state, u8"Навык обновлён.", 0.45f, 0.9f, 0.45f);
                                }
                            }
                        }
                    }
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    ImGui::TextUnformatted(u8"Ачивки навыка");
                    if (state.active) {
                        auto& ach = state.active->profile.achievements();
                        std::vector<int> filtered;
                        filtered.reserve(ach.size());
                        for (size_t idx = 0; idx < ach.size(); ++idx) {
                            if (ach[idx].skill != skillId) continue;
                            filtered.push_back(static_cast<int>(idx));
                        }
                        if (ImGui::BeginChild("ach_list", ImVec2(0, 140), true)) {
                            for (int fi = 0; fi < static_cast<int>(filtered.size()); ++fi) {
                                int idx = filtered[fi];
                                const auto& a = ach[idx];
                                const bool expired = !a.is_active(nowSecGlobal);
                                bool selected = state.selectedAchievementIndex == idx;
                                std::string row = a.title + " (" + std::to_string(a.bonusPercent) + "%)";
                                if (expired) row += u8" [истекла]";
                                ImGui::PushID(idx);
                                if (ImGui::Selectable(row.c_str(), selected)) {
                                    state.selectedAchievementIndex = idx;
                                    std::snprintf(state.achTitle.data(), state.achTitle.size(), "%s", a.title.c_str());
                                    std::snprintf(state.achIcon.data(), state.achIcon.size(), "%s", a.icon.c_str());
                                    state.achBonus = static_cast<float>(a.bonusPercent);
                                    if (a.expiresAt > 0 && a.awardedAt > 0) {
                                        auto dur = a.expiresAt - a.awardedAt;
                                        state.achDurationDays = static_cast<int>(dur / (24 * 3600));
                                    } else {
                                        state.achDurationDays = 0;
                                    }
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                        ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        ImGui::InputText(u8"Название ачивки", state.achTitle.data(), state.achTitle.size());
                        ImGui::InputText(u8"Иконка (путь)", state.achIcon.data(), state.achIcon.size());
                        ImGui::InputFloat(u8"Бонус к XP (%)", &state.achBonus, 0.5f, 2.0f, "%.1f");
                        ImGui::InputInt(u8"Срок (дней, 0 = без срока)", &state.achDurationDays);
                        if (state.achDurationDays < 0) state.achDurationDays = 0;
                        if (ImGui::Button(u8"Выдать ачивку")) {
                            Achievement a;
                            a.title = state.achTitle.data();
                            a.skill = skillId;
                            a.bonusPercent = static_cast<double>(state.achBonus);
                            a.icon = state.achIcon.data();
                            a.awardedAt = nowSecGlobal;
                            if (state.achDurationDays > 0) {
                                a.expiresAt = nowSecGlobal + static_cast<std::int64_t>(state.achDurationDays) * 24 * 3600;
                            } else {
                                a.expiresAt = 0;
                            }
                            state.active->profile.add_achievement(a);
                            storage->save_profile(state.active->profile);
                            SetStatus(state, u8"Ачивка выдана.", 0.45f, 0.9f, 0.45f);
                            RefreshProfiles(state, *storage, catalog, state.active->id);
                            state.selectedAchievementIndex = -1;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(u8"Сохранить изменения")) {
                            if (state.selectedAchievementIndex >= 0 && state.selectedAchievementIndex < static_cast<int>(state.active->profile.achievements().size())) {
                                auto achIdx = state.selectedAchievementIndex;
                                auto achList = state.active->profile.achievements();
                                Achievement& a = achList[achIdx];
                                a.title = state.achTitle.data();
                                a.icon = state.achIcon.data();
                                a.bonusPercent = static_cast<double>(state.achBonus);
                                if (state.achDurationDays > 0) {
                                    if (a.awardedAt == 0) a.awardedAt = nowSecGlobal;
                                    a.expiresAt = a.awardedAt + static_cast<std::int64_t>(state.achDurationDays) * 24 * 3600;
                                } else {
                                    a.expiresAt = 0;
                                }
                                state.active->profile.set_achievements(achList);
                                storage->save_profile(state.active->profile);
                                SetStatus(state, u8"Ачивка обновлена.", 0.45f, 0.9f, 0.45f);
                                RefreshProfiles(state, *storage, catalog, state.active->id);
                            } else {
                                SetStatus(state, u8"Сначала выберите ачивку.", 1.0f, 0.45f, 0.45f);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(u8"Удалить ачивку")) {
                            if (state.selectedAchievementIndex >= 0 && state.selectedAchievementIndex < static_cast<int>(state.active->profile.achievements().size())) {
                                auto achList = state.active->profile.achievements();
                                achList.erase(achList.begin() + state.selectedAchievementIndex);
                                state.active->profile.set_achievements(achList);
                                storage->save_profile(state.active->profile);
                                state.selectedAchievementIndex = -1;
                                SetStatus(state, u8"Ачивка удалена.", 0.45f, 0.9f, 0.45f);
                                RefreshProfiles(state, *storage, catalog, state.active->id);
                            } else {
                                SetStatus(state, u8"Сначала выберите ачивку.", 1.0f, 0.45f, 0.45f);
                            }
                        }
                    } else {
                        ImGui::TextDisabled(u8"Ачивки отображаются при выборе профиля.");
                    }
                }
            } else {
                ImGui::TextUnformatted(u8"Выберите навык, чтобы увидеть описание.");
            }
        }
        ImGui::End();

        // Admin login modal
        if (state.adminPopupRequest) {
            ImGui::OpenPopup(u8"Вход администратора");
            state.adminPopupRequest = false;
        }
        bool adminPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Вход администратора", &adminPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText(u8"Пароль", state.adminPassword.data(), state.adminPassword.size(), ImGuiInputTextFlags_Password);
            if (ImGui::Button(u8"Войти")) {
                if (std::string(state.adminPassword.data()) == kAdminPassword) {
                    state.isAdmin = true;
                    SetStatus(state, "Администратор: доступ открыт.", 0.45f, 0.9f, 0.45f);
                    ImGui::CloseCurrentPopup();
                    adminPopupOpen = false;
                } else {
                    SetStatus(state, "Неверный пароль администратора.", 1.0f, 0.45f, 0.45f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена")) {
                ImGui::CloseCurrentPopup();
                adminPopupOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!adminPopupOpen && ImGui::IsPopupOpen(u8"Вход администратора")) {
            ImGui::CloseCurrentPopup();
        }

        // Skill Catalog modals
        auto trimStr = [](std::string s) {
            auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
            return s;
        };

        if (state.addSkillPopupRequest) {
            ImGui::OpenPopup(u8"Добавить навык");
            state.addSkillPopupRequest = false;
        }
        bool addSkillOpen = true;
        if (ImGui::BeginPopupModal(u8"Добавить навык", &addSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText(u8"Название", state.newSkillName.data(), state.newSkillName.size());
            ImGui::InputFloat(u8"Вес (0.5 - 1.6)", &state.newSkillWeight, 0.05f, 0.2f, "%.2f");
            ImGui::InputTextMultiline(u8"Описание", state.newSkillDesc.data(), state.newSkillDesc.size(), ImVec2(360, 120));
            if (ImGui::Button(u8"Сохранить")) {
                std::string name = trimStr(state.newSkillName.data());
                std::string desc = trimStr(state.newSkillDesc.data());
                if (name.empty()) {
                    SetStatus(state, u8"Название навыка не может быть пустым.", 1.0f, 0.45f, 0.45f);
                } else {
                    double weight = static_cast<double>(state.newSkillWeight);
                    bool added = catalog.add_skill(name, weight, desc);
                    if (added) {
                        PrepareXpEntries(state, catalog);
                        std::string keepId = state.active ? state.active->id : std::string{};
                        RefreshProfiles(state, *storage, catalog, keepId);
                        if (auto newId = catalog.id_for_name(name)) {
                            const auto& skills = catalog.skills();
                            for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                                if (skills[i] == *newId) {
                                    state.selectedCatalogIndex = i;
                                    break;
                                }
                            }
                        }
                        SetStatus(state, u8"Навык добавлен.", 0.45f, 0.9f, 0.45f);
                        ImGui::CloseCurrentPopup();
                        addSkillOpen = false;
                    } else {
                        SetStatus(state, u8"Не удалось добавить навык.", 1.0f, 0.45f, 0.45f);
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена")) {
                ImGui::CloseCurrentPopup();
                addSkillOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!addSkillOpen && ImGui::IsPopupOpen(u8"Добавить навык")) {
            ImGui::CloseCurrentPopup();
        }

        if (state.deleteSkillPopupRequest) {
            ImGui::OpenPopup(u8"Удалить навык");
            state.deleteSkillPopupRequest = false;
        }
        bool deleteSkillOpen = true;
        if (ImGui::BeginPopupModal(u8"Удалить навык", &deleteSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!state.pendingSkillDelete.empty()) {
                const std::string displayName = catalog.display_name(state.pendingSkillDelete);
                ImGui::Text(u8"Удалить навык '%s' из каталога?", displayName.c_str());
                if (ImGui::Button(u8"Удалить")) {
                    bool removed = catalog.remove_skill(state.pendingSkillDelete);
                    bool stripped = RemoveSkillFromProfiles(state, *storage, catalog, state.pendingSkillDelete);
                    PrepareXpEntries(state, catalog);
                    std::string keepId = state.active ? state.active->id : std::string{};
                    RefreshProfiles(state, *storage, catalog, keepId);
                    if (state.selectedCatalogIndex >= static_cast<int>(catalog.skills().size())) {
                        state.selectedCatalogIndex = static_cast<int>(catalog.skills().size()) - 1;
                    }
                    if (removed) {
                        std::string msg = u8"Навык удалён.";
                        if (stripped) msg += u8" Удалён из профилей.";
                        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, u8"Навык не найден.", 1.0f, 0.45f, 0.45f);
                    }
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                    state.pendingSkillDelete.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Отмена")) {
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                    state.pendingSkillDelete.clear();
                }
            } else {
                ImGui::TextUnformatted(u8"Навык не выбран.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!deleteSkillOpen && ImGui::IsPopupOpen(u8"Удалить навык")) {
            ImGui::CloseCurrentPopup();
        }

        if (state.mergeSkillPopupRequest) {
            ImGui::OpenPopup(u8"Слияние навыков");
            state.mergeSkillPopupRequest = false;
        }
        bool mergeSkillOpen = true;
        if (ImGui::BeginPopupModal(u8"Слияние навыков", &mergeSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!state.pendingMergeFromId.empty() && !state.pendingMergeToId.empty()) {
                const std::string fromName = catalog.display_name(state.pendingMergeFromId);
                const std::string toName = catalog.display_name(state.pendingMergeToId);
                ImGui::Text(u8"Навык '%s' уже существует.", toName.c_str());
                ImGui::Text(u8"Слить опыт из '%s' в '%s'?", fromName.c_str(), toName.c_str());
                if (ImGui::Button(u8"Слить")) {
                    MergeSkillInProfiles(state, *storage, catalog, state.pendingMergeFromId, state.pendingMergeToId);
                    catalog.remove_skill(state.pendingMergeFromId);
                    catalog.update_skill(state.pendingMergeToId,
                                         state.pendingMergeName,
                                         static_cast<double>(state.pendingMergeWeight),
                                         state.pendingMergeDesc);
                    PrepareXpEntries(state, catalog);
                    std::string keepId = state.active ? state.active->id : std::string{};
                    RefreshProfiles(state, *storage, catalog, keepId);
                    const auto& skills = catalog.skills();
                    for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                        if (skills[i] == state.pendingMergeToId) {
                            state.selectedCatalogIndex = i;
                            break;
                        }
                    }
                    state.lastCatalogSelection = -1;
                    SetStatus(state, u8"Навыки объединены.", 0.45f, 0.9f, 0.45f);
                    state.pendingMergeFromId.clear();
                    state.pendingMergeToId.clear();
                    state.pendingMergeName.clear();
                    state.pendingMergeDesc.clear();
                    state.pendingMergeWeight = 1.0f;
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Отмена")) {
                    state.pendingMergeFromId.clear();
                    state.pendingMergeToId.clear();
                    state.pendingMergeName.clear();
                    state.pendingMergeDesc.clear();
                    state.pendingMergeWeight = 1.0f;
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
            } else {
                ImGui::TextUnformatted(u8"Нет данных для слияния.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!mergeSkillOpen && ImGui::IsPopupOpen(u8"Слияние навыков")) {
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Begin(u8"Пайплайн")) {
            const int stepCount = static_cast<int>(kPipelineSteps.size());
            if (stepCount == 0) {
                ImGui::TextUnformatted(u8"Пайплайн пуст.");
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

        if (state.isAdmin && ImGui::Begin(u8"Правила")) {
            GameplayConfig& draft = state.rulesDraft;
            ImGui::TextUnformatted(u8"Кривая уровней");
            ImGui::InputInt(u8"Базовый XP (уровень 1)", &draft.levelBaseXp);
            ImGui::InputInt(u8"Линейный прирост за уровень", &draft.levelLinearXp);
            ImGui::InputInt(u8"Квадратичный прирост за уровень", &draft.levelQuadraticXp);
            ImGui::Separator();
            ImGui::TextUnformatted(u8"Базовый XP категорий");
            for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[idx] + " XP";
                int value = draft.categoryBaseXp[idx];
                if (ImGui::InputInt(label.c_str(), &value)) {
                    draft.categoryBaseXp[idx] = value;
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted(u8"Бонусы и штрафы");
            ImGui::InputFloat(u8"Базовый фокус-бонус", &draft.focusBaseBonus, 0.05f, 0.5f, "%.2f");
            ImGui::InputFloat(u8"Доп. фокус-бонус", &draft.focusAdditionalBonus, 0.05f, 0.5f, "%.2f");
            ImGui::SliderFloat(u8"Коэффициент награды при повторе", &draft.repeatRewardFactor, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat(u8"Коэффициент награды при прогреве", &draft.recoveryRewardFactor, 0.0f, 1.0f, "%.2f");
            ImGui::InputInt(u8"Задач прогрева", &draft.recoveryWarmupTasks);
            ImGui::TextDisabled(u8"Изменения применяются в CLI и GUI после сохранения.");
            if (ImGui::Button(u8"Сбросить")) {
                draft = state.rulesConfig;
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Сохранить и применить")) {
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
                    SetStatus(state, u8"Правила сохранены.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить правила.", 1.0f, 0.45f, 0.45f);
                }
            }
        }
        if (state.isAdmin) ImGui::End();

        // XP sheet modal
        if (state.xpPopupRequest) {
            ImGui::OpenPopup(u8"Добавление опыта");
            state.xpPopupRequest = false;
        }
        bool xpPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Добавление опыта", &xpPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!state.isAdmin) {
                ImGui::TextUnformatted(u8"Доступно только администратору.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
                }
                ImGui::EndPopup();
                if (!xpPopupOpen && ImGui::IsPopupOpen(u8"Добавление опыта")) {
                    ImGui::CloseCurrentPopup();
                }
                goto skip_xp_body;
            }
            const auto& catalogSkills = catalog.skills();
            if (state.xpEntries.size() != catalogSkills.size()) {
                PrepareXpEntries(state, catalog);
            }
            if (catalogSkills.empty()) {
                ImGui::TextUnformatted(u8"Каталог пуст.");
            } else if (!state.active) {
                ImGui::TextUnformatted(u8"Нет активного профиля.");
            } else {
                state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                const GameplayConfig& rules = GetGameplayConfig();

                ImGui::TextUnformatted(u8"Параметры задачи");
                ImGui::Separator();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo(u8"Категория", &state.taskCategoryIndex,
                                 Profile::kCategoryLabels.data(), Profile::kCategoryCount)) {
                    state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                }
                const int currentCategory = state.taskCategoryIndex;
                const int baseXp = rules.categoryBaseXp[currentCategory];
                const float scoreRatio = static_cast<float>(state.taskScore) / static_cast<float>(Profile::kMaxCategoryScore);
                const float scoreMultiplier = std::pow(std::max(0.1f, scoreRatio), 1.35f);
                const int currentBest = state.active->profile.category_best_score(static_cast<size_t>(currentCategory));
                ImGui::SameLine();
                ImGui::Text(u8"Базовый XP: %d", baseXp);
                ImGui::TextDisabled(u8"Категория задаёт базовый XP; оценка — нелинейный множитель.");
                if (ImGui::SliderInt(u8"Оценка", &state.taskScore, 1, Profile::kMaxCategoryScore)) {
                    state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                }
                ImGui::Text(u8"Множитель за оценку: %.2f", scoreMultiplier);
                if (currentBest >= Profile::kMaxCategoryScore) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                       u8"Лучший результат: %d/10 (категория освоена)", currentBest);
                } else {
                    ImGui::Text(u8"Лучший результат: %d/10", currentBest);
                    if (state.taskScore <= currentBest) {
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                           u8"Повтор оценки — глобальный XP ограничен 35%% (навыки без штрафа).");
                    }
                }

                ImGui::Separator();
                ImGui::TextUnformatted(u8"Распределите навыки (автоподбор до 100%)");
                const float sliderWidth = ImGui::CalcTextSize("000").x + ImGui::GetStyle().FramePadding.x * 6.0f;
                int percentSum = 0;
                int maxSharePercent = 0;
                const auto now = std::chrono::system_clock::now();
                const std::int64_t nowSeconds =
                    std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                if (ImGui::BeginTable("xp_sheet", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn(u8"Навык");
                    ImGui::TableSetupColumn(u8"Доля (%)");
                    ImGui::TableSetupColumn(u8"Ачивки");
                    ImGui::TableSetupColumn("XP");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                        ImGui::TableNextRow();
                        const std::string& skillId = catalogSkills[i];
                        const std::string displayName = catalog.display_name(skillId);
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(displayName.c_str());
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
                        double mult = state.active->profile.skill_bonus_multiplier(skillId, nowSeconds);
                        double bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
                        if (bonusPercent > 0.01) {
                            ImGui::Text("+%.1f%%", bonusPercent);
                        } else {
                            ImGui::TextUnformatted("-");
                        }
                        ImGui::TableSetColumnIndex(3);
                        const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                        const int finalPreviewXp = static_cast<int>(std::round(previewXp * mult));
                        if (finalPreviewXp != previewXp) {
                            ImGui::Text("%d -> %d", previewXp, finalPreviewXp);
                        } else {
                            ImGui::Text("%d", previewXp);
                        }
                    }
                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::Text(u8"Назначено: %d%%", percentSum);
                const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * (static_cast<float>(maxSharePercent) / 100.0f);
                const int previewPool = static_cast<int>(std::round(baseXp * scoreMultiplier * focusBonus));
                ImGui::Text(u8"Фокус-бонус: %.2f (макс. доля %d%%)", focusBonus, maxSharePercent);
                ImGui::TextDisabled(u8"Прогноз XP по навыкам до штрафов: %d", previewPool);

                if (ImGui::Button(u8"Применить")) {
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
                        SetStatus(state, u8"Сумма долей должна быть 100%.", 1.0f, 0.45f, 0.45f);
                    } else if (!hasContribution) {
                        SetStatus(state, u8"Нужно выбрать хотя бы один навык.", 1.0f, 0.45f, 0.45f);
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
                        skillsStream.imbue(std::locale::classic());
                        bool firstSkill = true;
                        for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                            int shareXp = xpDistribution[i];
                            if (shareXp <= 0) continue;
                            const std::string& skillId = catalogSkills[i];
                            const std::string displayName = catalog.display_name(skillId);
                            double weight = catalog.weight(skillId);
                            state.active->profile.add_skill(skillId, 1, weight);
                            double mult = state.active->profile.skill_bonus_multiplier(skillId, nowSeconds);
                            double bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
                            int finalSkillXp = static_cast<int>(std::round(shareXp * mult));
                            bool leveled = state.active->profile.grant_xp(skillId, finalSkillXp);
                            if (!firstSkill) skillsStream << " | ";
                            skillsStream << displayName << " +" << finalSkillXp << " XP (" << state.xpEntries[i].percent << "%";
                            if (bonusPercent > 0.01) {
                                skillsStream << ", +" << std::fixed << std::setprecision(1) << bonusPercent << "%";
                                skillsStream << std::defaultfloat;
                            }
                            skillsStream << ")";
                            if (leveled) skillsStream << u8" уровень вверх";
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
                            bestStream.imbue(std::locale::classic());
                            bestStream << u8"Категория " << categoryLabel << u8": лучший результат " << readyScore << "/10";
                            AppendLog(state, state.active->id, bestStream.str());
                            if (readyScore == Profile::kMaxCategoryScore) {
                                std::ostringstream mastery;
                                mastery.imbue(std::locale::classic());
                                mastery << u8"Категория " << categoryLabel << u8" освоена!";
                                AppendLog(state, state.active->id, mastery.str());
                            }
                        }
                        constexpr bool kDecayEnabled = false;
                        if (kDecayEnabled) {
                            state.active->profile.reset_category_cooldown(static_cast<size_t>(readyCategory));
                            for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                                if (idx == static_cast<size_t>(readyCategory)) continue;
                                state.active->profile.tick_category_cooldown(idx);
                                if (state.active->profile.category_cooldown(idx) < 0) {
                                    state.active->profile.update_category_best_score(
                                        idx, state.active->profile.category_best_score(idx) - 1);
                                    state.active->profile.reset_category_cooldown(idx);
                                    std::ostringstream decay;
                                    decay.imbue(std::locale::classic());
                                    decay << u8"Категория " << Profile::kCategoryLabels[idx] << u8" снижена до "
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
                        }
                        std::ostringstream summaryStream;
                        summaryStream.imbue(std::locale::classic());
                        summaryStream << u8"Задача [" << categoryLabel << u8"] с оценкой " << readyScore
                                      << u8" => +" << effectiveXp << " XP";
                        const int repeatPercent = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
                        const int recoveryPercent = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
                        if (repeatPenalty) summaryStream << u8" (повтор " << repeatPercent << "%)";
                        if (recoveryPenalty) summaryStream << u8" (прогрев " << recoveryPercent << "%)";
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
                if (ImGui::Button(u8"Отмена")) {
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
                }
            }
            ImGui::EndPopup();
        }
skip_xp_body:
        if (!xpPopupOpen && ImGui::IsPopupOpen(u8"Добавление опыта")) {
            ImGui::CloseCurrentPopup();
        }

        // Create profile modal
        if (state.createPopupRequest) {
            ImGui::OpenPopup(u8"Создать профиль");
            state.createPopupRequest = false;
        }
        bool createPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Создать профиль", &createPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText(u8"Имя", state.modalBuffer.data(), state.modalBuffer.size());
            if (ImGui::Button(u8"Создать")) {
                std::string name(state.modalBuffer.data());
                name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char c){ return !std::isspace(c); }));
                name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), name.end());
                if (name.empty()) {
                    SetStatus(state, u8"Имя не может быть пустым.", 1.0f, 0.45f, 0.45f);
                } else {
                    Profile profile(name);
                    SyncProfileWithCatalog(profile, catalog);
                    if (auto info = storage->create_profile(profile)) {
                        storage->save_profile(profile);
                        SetStatus(state, u8"Профиль создан.", 0.45f, 0.9f, 0.45f);
                        RefreshProfiles(state, *storage, catalog, info->id);
                        ImGui::CloseCurrentPopup();
                        createPopupOpen = false;
                    } else {
                        SetStatus(state, u8"Не удалось создать профиль.", 1.0f, 0.45f, 0.45f);
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена")) {
                ImGui::CloseCurrentPopup();
                createPopupOpen = false;
            }
            ImGui::EndPopup();
        }
        if (!createPopupOpen && ImGui::IsPopupOpen(u8"Создать профиль")) {
            ImGui::CloseCurrentPopup();
        }

        // Confirm modal
        if (state.confirmPopupRequest) {
            ImGui::OpenPopup(u8"Подтвердите действие");
            state.confirmPopupRequest = false;
        }
        bool confirmPopupOpen = true;
        if (ImGui::BeginPopupModal(u8"Подтвердите действие", &confirmPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
                const auto& info = state.profiles[state.selectedIndex];
                std::string action;
                switch (state.confirmAction) {
                    case ConfirmAction::Archive: action = u8"архивирование"; break;
                    case ConfirmAction::Restore: action = u8"восстановление"; break;
                    case ConfirmAction::Delete: action = u8"удаление"; break;
                    default: action = ""; break;
                }
                ImGui::Text(u8"Подтвердить %s профиля [%s] %s?", action.c_str(), info.id.c_str(), info.name.c_str());
                if (ImGui::Button(u8"Да")) {
                    bool ok = false;
                    if (state.confirmAction == ConfirmAction::Archive) {
                        ok = storage->set_archived(info.id, true);
                    } else if (state.confirmAction == ConfirmAction::Restore) {
                        ok = storage->set_archived(info.id, false);
                    } else if (state.confirmAction == ConfirmAction::Delete) {
                        ok = storage->delete_profile(info.id);
                    }
                    if (ok) {
                        SetStatus(state, u8"Операция выполнена.", 0.45f, 0.9f, 0.45f);
                        std::string newFocus;
                        if (state.confirmAction != ConfirmAction::Delete) {
                            newFocus = info.id;
                        }
                        RefreshProfiles(state, *storage, catalog, newFocus);
                    } else {
                        SetStatus(state, u8"Не удалось выполнить операцию.", 1.0f, 0.45f, 0.45f);
                    }
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Нет")) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            } else {
                ImGui::TextUnformatted(u8"Профиль не выбран.");
                if (ImGui::Button(u8"Закрыть")) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            }
            ImGui::EndPopup();
        }
        if (!confirmPopupOpen && ImGui::IsPopupOpen(u8"Подтвердите действие")) {
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

    ReleaseIconTextures();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}



