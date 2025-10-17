#include "AppUtils.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"

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
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <deque>
#include <optional>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

struct GuiProfile {
    Profile profile;
    std::string id;
};

struct XpEntry {
    int add = 0;
    int sub = 0;
};

enum class ConfirmAction {
    None,
    Archive,
    Restore,
    Delete
};

struct GuiState {
    std::vector<IJobStorage::ProfileInfo> profiles;
    int selectedIndex = -1;
    std::optional<GuiProfile> active;
    std::string statusMessage;
    float statusColor[4] = {0.6f, 0.7f, 1.0f, 1.0f};

    bool createPopupRequest = false;
    bool confirmPopupRequest = false;
    bool xpPopupRequest = false;
    ConfirmAction confirmAction = ConfirmAction::None;
    std::array<char, 128> modalBuffer{};
    std::vector<XpEntry> xpEntries;
    std::unordered_map<std::string, std::deque<std::string>> activityLogs;
};

std::vector<IJobStorage::ProfileInfo> LoadProfiles(IJobStorage& storage) {
    auto list = storage.list_profiles();
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return list;
}

void SetStatus(GuiState& state, const std::string& msg, float r, float g, float b, float a = 1.0f) {
    state.statusMessage = msg;
    state.statusColor[0] = r;
    state.statusColor[1] = g;
    state.statusColor[2] = b;
    state.statusColor[3] = a;
}

void PrepareXpEntries(GuiState& state, const SkillCatalog& catalog) {
    state.xpEntries.assign(catalog.skills().size(), {});
}

void AppendLog(GuiState& state, const std::string& profileId, const std::string& message) {
    auto& log = state.activityLogs[profileId];
    if (log.size() == 3) {
        log.pop_front();
    }
    log.push_back(message);
}

int ClampToRange(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int ComputeAppliedXp(const XpEntry& entry) {
    int add = entry.add;
    if (add < 0) add = 0;
    const int percent = ClampToRange(entry.sub, 0, 100);
    if (add == 0) return 0;
    const double remaining = add * (1.0 - static_cast<double>(percent) / 100.0);
    int result = static_cast<int>(std::round(remaining));
    if (result < 0) result = 0;
    return result;
}

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

} // namespace

int main() {
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    extern IJobStorage* CreateFileStorage(const std::filesystem::path& dir);
    auto storageDir = ResolveStorageDirectory();
    SkillCatalog catalog(storageDir);
    std::unique_ptr<IJobStorage> storage(CreateFileStorage(storageDir));
    EnsureAdminProfile(*storage, catalog);

    GuiState state;
    RefreshProfiles(state, *storage, catalog);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Main menu window
        ImGui::Begin("Main Menu");
        if (ImGui::Button("Refresh")) {
            RefreshProfiles(state, *storage, catalog);
        }
        ImGui::SameLine();
        if (ImGui::Button("Create")) {
            state.modalBuffer.fill('\0');
            state.createPopupRequest = true;
        }
        ImGui::SameLine();
        bool canArchive = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size()) && !state.profiles[state.selectedIndex].archived;
        if (!canArchive) ImGui::BeginDisabled();
        if (ImGui::Button("Archive")) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Archive;
        }
        if (!canArchive) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canRestore = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size()) && state.profiles[state.selectedIndex].archived;
        if (!canRestore) ImGui::BeginDisabled();
        if (ImGui::Button("Restore")) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Restore;
        }
        if (!canRestore) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canDelete = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size());
        if (!canDelete) ImGui::BeginDisabled();
        if (ImGui::Button("Delete")) {
            state.confirmPopupRequest = true;
            state.confirmAction = ConfirmAction::Delete;
        }
        if (!canDelete) ImGui::EndDisabled();
        ImGui::SameLine();
        bool hasActive = state.active.has_value();
        if (!hasActive) ImGui::BeginDisabled();
        if (ImGui::Button("Add Experience")) {
            PrepareXpEntries(state, catalog);
            state.xpPopupRequest = true;
        }
        if (!hasActive) ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::BeginChild("profiles", ImVec2(0, 0), false)) {
            for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                const auto& info = state.profiles[i];
                std::string label = "[" + info.id + "] " + info.name + (info.archived ? " (archived)" : "");
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
        ImGui::Begin("Profile Details");
        if (!state.active) {
            ImGui::TextUnformatted("Select a profile to view details.");
        } else {
            ImGui::Text("Name: %s", state.active->profile.name().c_str());
            ImGui::Text("ID: %s", state.active->id.c_str());
            ImGui::Text("Overall Level: %d (%s)", state.active->profile.overall_level(), DescribeOverallRank(state.active->profile.overall_level()).c_str());
            ImGui::Separator();

            ImGui::TextUnformatted("Skills");
            ImGui::Columns(4, "skill_table");
            ImGui::TextUnformatted("Skill");
            ImGui::NextColumn();
            ImGui::TextUnformatted("Level");
            ImGui::NextColumn();
            ImGui::TextUnformatted("XP");
            ImGui::NextColumn();
            ImGui::TextUnformatted("Weight");
            ImGui::NextColumn();
            ImGui::Separator();

            auto skills = state.active->profile.list_skills();
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
            ImGui::TextUnformatted("Recent Activity");
            const auto logIt = state.activityLogs.find(state.active->id);
            if (logIt != state.activityLogs.end() && !logIt->second.empty()) {
                for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                    ImGui::BulletText("%s", it->c_str());
                }
            } else {
                ImGui::TextUnformatted("No activity yet.");
            }

            ShowStatus(state);
        }
        ImGui::End();

        ImGui::Begin("Skill Catalog");
        for (const auto& skill : catalog.skills()) {
            ImGui::Text("%s (%.2f)", skill.c_str(), catalog.weight(skill));
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
            } else if (ImGui::BeginTable("xp_sheet", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Skill");
                ImGui::TableSetupColumn("Plus");
                ImGui::TableSetupColumn("Minus");
                ImGui::TableSetupColumn("Result");
                ImGui::TableHeadersRow();
                const float inputWidth = ImGui::CalcTextSize("00000").x + ImGui::GetStyle().FramePadding.x * 4.0f;
                int total = 0;
                for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(catalogSkills[i].c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID(i * 2);
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::InputInt("##plus", &state.xpEntries[i].add, 0)) {
                        if (state.xpEntries[i].add < 0) state.xpEntries[i].add = 0;
                    }
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(i * 2 + 1);
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::InputInt("##minus", &state.xpEntries[i].sub, 0)) {
                        if (state.xpEntries[i].sub < 0) state.xpEntries[i].sub = 0;
                    }
                    state.xpEntries[i].sub = ClampToRange(state.xpEntries[i].sub, 0, 100);
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(3);
                    int delta = ComputeAppliedXp(state.xpEntries[i]);
                    total += delta;
                    ImGui::Text("%d", delta);
                }
                ImGui::EndTable();
                ImGui::Separator();
                ImGui::Text("Total XP: %d", total);

                if (ImGui::Button("Apply")) {
                    int applied = 0;
                    for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                        int rawAdd = state.xpEntries[i].add;
                        if (rawAdd < 0) rawAdd = 0;
                        if (rawAdd <= 0) continue;
                        const int percent = ClampToRange(state.xpEntries[i].sub, 0, 100);
                        const int delta = ComputeAppliedXp(state.xpEntries[i]);
                        const std::string& skillName = catalogSkills[i];
                        double weight = catalog.weight(skillName);
                        state.active->profile.add_skill(skillName, 1, weight);
                        bool leveled = false;
                        if (delta > 0) {
                            leveled = state.active->profile.grant_xp(skillName, delta);
                            applied += delta;
                        }
                        std::ostringstream entry;
                        entry << skillName << ": +" << rawAdd << " XP";
                        if (percent > 0) {
                            entry << " (-" << percent << "%)";
                        }
                        if (delta > 0 && delta != rawAdd) {
                            entry << " => +" << delta << " XP";
                        } else if (delta <= 0) {
                            entry << " => 0 XP";
                        }
                        if (leveled) entry << " (level up)";
                        AppendLog(state, state.active->id, entry.str());
                    }
                    storage->save_profile(state.active->profile);
                    SetStatus(state, applied > 0 ? "XP sheet applied." : "Nothing to apply.",
                              applied > 0 ? 0.45f : 1.0f,
                              applied > 0 ? 0.9f : 0.45f,
                              applied > 0 ? 0.45f : 0.45f);
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
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



