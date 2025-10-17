#include "AppUtils.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
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
#include <optional>
#include <string>
#include <vector>

namespace {

struct GuiProfile {
    Profile profile;
    std::string id;
};

enum class ModalAction {
    None,
    Create,
    Archive,
    Restore,
    Delete
};

struct GuiState {
    std::vector<IJobStorage::ProfileInfo> profiles;
    int selectedIndex = -1;
    std::optional<GuiProfile> active;
    int xpSkillIndex = 0;
    int addSkillIndex = 0;
    int xpAmount = 10;
    std::string statusMessage;
    float statusColor[4] = {0.6f, 0.7f, 1.0f, 1.0f};

    ModalAction modal = ModalAction::None;
    std::array<char, 128> modalBuffer{};
    std::string pendingId;
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

void RefreshActiveProfile(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    state.active.reset();
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.profiles.size())) return;
    const auto& info = state.profiles[state.selectedIndex];
    if (!storage.set_active_profile(info.id)) return;
    if (auto loaded = storage.load_profile()) {
        SyncProfileWithCatalog(*loaded, catalog);
        storage.save_profile(*loaded);
        state.active = GuiProfile{*loaded, info.id};
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
            state.modal = ModalAction::Create;
            state.modalBuffer.fill('\0');
            ImGui::OpenPopup("Create Profile");
        }
        ImGui::SameLine();
        bool canArchive = state.selectedIndex >= 0 && !state.profiles.empty() && !state.profiles[state.selectedIndex].archived;
        if (!canArchive) ImGui::BeginDisabled();
        if (ImGui::Button("Archive")) {
            state.modal = ModalAction::Archive;
            ImGui::OpenPopup("Confirm Action");
        }
        if (!canArchive) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canRestore = state.selectedIndex >= 0 && !state.profiles.empty() && state.profiles[state.selectedIndex].archived;
        if (!canRestore) ImGui::BeginDisabled();
        if (ImGui::Button("Restore")) {
            state.modal = ModalAction::Restore;
            ImGui::OpenPopup("Confirm Action");
        }
        if (!canRestore) ImGui::EndDisabled();
        ImGui::SameLine();
        bool canDelete = state.selectedIndex >= 0;
        if (!canDelete) ImGui::BeginDisabled();
        if (ImGui::Button("Delete")) {
            state.modal = ModalAction::Delete;
            ImGui::OpenPopup("Confirm Action");
        }
        if (!canDelete) ImGui::EndDisabled();

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
            const auto& catalogSkills = catalog.skills();

            ImGui::TextUnformatted("Grant XP");
            if (!catalogSkills.empty()) {
                if (state.xpSkillIndex >= static_cast<int>(catalogSkills.size())) state.xpSkillIndex = 0;
                const char* preview = catalogSkills[state.xpSkillIndex].c_str();
                if (ImGui::BeginCombo("Skill##grant", preview)) {
                    for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                        bool selected = (state.xpSkillIndex == i);
                        if (ImGui::Selectable(catalogSkills[i].c_str(), selected)) {
                            state.xpSkillIndex = i;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextUnformatted("Catalog is empty.");
            }
            ImGui::InputInt("Amount", &state.xpAmount);
            if (ImGui::Button("Add XP")) {
                if (catalogSkills.empty()) {
                    SetStatus(state, "Catalog is empty.", 1.0f, 0.45f, 0.45f);
                } else if (state.xpAmount <= 0) {
                    SetStatus(state, "Amount must be positive.", 1.0f, 0.45f, 0.45f);
                } else {
                    const std::string& skillName = catalogSkills[state.xpSkillIndex];
                    double weight = catalog.weight(skillName);
                    state.active->profile.add_skill(skillName, 1, weight);
                    bool leveled = state.active->profile.grant_xp(skillName, state.xpAmount);
                    storage->save_profile(state.active->profile);
                    SetStatus(state, leveled ? "Level up!" : "XP added.", 0.45f, 0.9f, 0.45f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                if (storage->save_profile(state.active->profile)) {
                    SetStatus(state, "Profile saved.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, "Failed to save profile.", 1.0f, 0.45f, 0.45f);
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Add Skill (level 0)");
            if (!catalogSkills.empty()) {
                if (state.addSkillIndex >= static_cast<int>(catalogSkills.size())) state.addSkillIndex = 0;
                const char* preview = catalogSkills[state.addSkillIndex].c_str();
                if (ImGui::BeginCombo("Skill##add", preview)) {
                    for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                        bool selected = (state.addSkillIndex == i);
                        if (ImGui::Selectable(catalogSkills[i].c_str(), selected)) {
                            state.addSkillIndex = i;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Add Skill")) {
                    const std::string& skillName = catalogSkills[state.addSkillIndex];
                    double weight = catalog.weight(skillName);
                    state.active->profile.add_skill(skillName, 0, weight);
                    storage->save_profile(state.active->profile);
                    SetStatus(state, "Skill added with level 0.", 0.45f, 0.9f, 0.45f);
                }
            } else {
                ImGui::TextUnformatted("Catalog is empty.");
            }

            ShowStatus(state);
        }
        ImGui::End();

        ImGui::Begin("Skill Catalog");
        for (const auto& skill : catalog.skills()) {
            ImGui::Text("%s (%.2f)", skill.c_str(), catalog.weight(skill));
        }
        ImGui::End();

        // Modal dialogs
        if (state.modal == ModalAction::Create && ImGui::BeginPopupModal("Create Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
                        state.modal = ModalAction::None;
                    } else {
                        SetStatus(state, "Failed to create profile.", 1.0f, 0.45f, 0.45f);
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                state.modal = ModalAction::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (state.modal == ModalAction::Archive || state.modal == ModalAction::Restore || state.modal == ModalAction::Delete) {
            if (ImGui::BeginPopupModal("Confirm Action", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
                    const auto& info = state.profiles[state.selectedIndex];
                    std::string action = (state.modal == ModalAction::Archive) ? "archive" : (state.modal == ModalAction::Restore) ? "restore" : "delete";
                    ImGui::Text("Confirm %s of profile [%s] %s?", action.c_str(), info.id.c_str(), info.name.c_str());
                    if (ImGui::Button("Yes")) {
                        bool ok = false;
                        if (state.modal == ModalAction::Archive) {
                            ok = storage->set_archived(info.id, true);
                        } else if (state.modal == ModalAction::Restore) {
                            ok = storage->set_archived(info.id, false);
                        } else {
                            ok = storage->delete_profile(info.id);
                        }
                        if (ok) {
                            SetStatus(state, "Operation complete.", 0.45f, 0.9f, 0.45f);
                            std::string newFocus;
                            if (state.modal != ModalAction::Delete) {
                                newFocus = info.id;
                            }
                            RefreshProfiles(state, *storage, catalog, newFocus);
                        } else {
                            SetStatus(state, "Operation failed.", 1.0f, 0.45f, 0.45f);
                        }
                        state.modal = ModalAction::None;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("No")) {
                        state.modal = ModalAction::None;
                        ImGui::CloseCurrentPopup();
                    }
                } else {
                    ImGui::TextUnformatted("No profile selected.");
                    if (ImGui::Button("Close")) {
                        state.modal = ModalAction::None;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
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
