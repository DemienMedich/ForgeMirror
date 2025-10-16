#include "AppUtils.h"
#include "IJobStorage.h"
#include "Profile.h"
#include "SkillCatalog.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>

#include <algorithm>
#include <array>
#include <cmath>
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

std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

std::vector<IJobStorage::ProfileInfo> load_profiles(IJobStorage& storage) {
    auto profiles = storage.list_profiles();
    std::sort(profiles.begin(), profiles.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return profiles;
}

void show_status(const std::string& status, float color[4]) {
    if (!status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color[0], color[1], color[2], color[3]));
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::PopStyleColor();
    }
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

    std::vector<IJobStorage::ProfileInfo> profiles = load_profiles(*storage);
    int selectedIndex = profiles.empty() ? -1 : 0;
    std::optional<GuiProfile> active;
    if (selectedIndex >= 0) {
        if (storage->set_active_profile(profiles[selectedIndex].id)) {
            if (auto p = storage->load_profile()) {
                active = GuiProfile{*p, profiles[selectedIndex].id};
            }
        }
    }

    std::array<char, 64> xpSkillBuffer{};
    int xpAmount = 10;
    std::string statusMessage;
    float statusColor[4] = {0.6f, 0.7f, 1.0f, 1.0f};

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Profiles");
        if (ImGui::Button("Refresh")) {
            profiles = load_profiles(*storage);
            selectedIndex = profiles.empty() ? -1 : 0;
            if (selectedIndex >= 0 && storage->set_active_profile(profiles[selectedIndex].id)) {
                if (auto p = storage->load_profile()) {
                    active = GuiProfile{*p, profiles[selectedIndex].id};
                }
            } else {
                active.reset();
            }
        }
        ImGui::Separator();
        if (ImGui::BeginChild("profile_list", ImVec2(0, 0), false)) {
            for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
                const auto& info = profiles[i];
                std::string label = "[" + info.id + "] " + info.name + (info.archived ? " (archived)" : "");
                if (ImGui::Selectable(label.c_str(), selectedIndex == i)) {
                    selectedIndex = i;
                    if (storage->set_active_profile(info.id)) {
                        if (auto p = storage->load_profile()) {
                            active = GuiProfile{*p, info.id};
                            statusMessage.clear();
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::Begin("Profile Details");
        if (!active) {
            ImGui::TextUnformatted("Select a profile to view details.");
        } else {
            ImGui::Text("Name: %s", active->profile.name().c_str());
            ImGui::Text("ID: %s", active->id.c_str());
            ImGui::Text("Overall Level: %d (%s)", active->profile.overall_level(), DescribeOverallRank(active->profile.overall_level()).c_str());
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

            auto skills = active->profile.list_skills();
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
            ImGui::TextUnformatted("Grant XP");
            ImGui::InputText("Skill", xpSkillBuffer.data(), xpSkillBuffer.size());
            ImGui::InputInt("Amount", &xpAmount);
            if (ImGui::Button("Add XP")) {
                std::string skillInput = trim_copy(xpSkillBuffer.data());
                if (skillInput.empty()) {
                    statusMessage = "Enter a skill name.";
                    statusColor[0] = 1.0f; statusColor[1] = 0.45f; statusColor[2] = 0.45f; statusColor[3] = 1.0f;
                } else if (xpAmount <= 0) {
                    statusMessage = "Amount must be positive.";
                    statusColor[0] = 1.0f; statusColor[1] = 0.45f; statusColor[2] = 0.45f; statusColor[3] = 1.0f;
                } else {
                    auto canonical = catalog.canonical(skillInput);
                    if (!canonical) {
                        statusMessage = "Skill not in catalog.";
                        statusColor[0] = 1.0f; statusColor[1] = 0.45f; statusColor[2] = 0.45f; statusColor[3] = 1.0f;
                    } else {
                        const std::string skillName = *canonical;
                        const double weight = catalog.weight(skillName);
                        active->profile.add_skill(skillName, 1, weight);
                        bool leveled = active->profile.grant_xp(skillName, xpAmount);
                        storage->save_profile(active->profile);
                        statusMessage = leveled ? "Level up!" : "XP added.";
                        statusColor[0] = 0.45f; statusColor[1] = 0.9f; statusColor[2] = 0.45f; statusColor[3] = 1.0f;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Sync")) {
                if (active) {
                    if (storage->save_profile(active->profile)) {
                        statusMessage = "Profile saved.";
                        statusColor[0] = 0.45f; statusColor[1] = 0.9f; statusColor[2] = 0.45f; statusColor[3] = 1.0f;
                    } else {
                        statusMessage = "Failed to save profile.";
                        statusColor[0] = 1.0f; statusColor[1] = 0.45f; statusColor[2] = 0.45f; statusColor[3] = 1.0f;
                    }
                }
            }

            show_status(statusMessage, statusColor);
        }
        ImGui::End();

        ImGui::Begin("Skill Catalog");
        for (const auto& skill : catalog.skills()) {
            ImGui::Text("%s (%.2f)", skill.c_str(), catalog.weight(skill));
        }
        ImGui::End();

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
