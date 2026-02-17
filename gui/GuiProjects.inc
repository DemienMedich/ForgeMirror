static std::filesystem::path ProjectsStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "projects.json";
}

static std::string GenerateProjectId(const std::vector<ProjectEntry>& projects) {
    const std::int64_t now = NowSeconds();
    int suffix = static_cast<int>(projects.size()) + 1;
    std::string base = "prj_" + std::to_string(now);
    auto exists = [&](const std::string& id) {
        return std::any_of(projects.begin(), projects.end(), [&](const ProjectEntry& p) { return p.id == id; });
    };
    std::string candidate = base;
    while (exists(candidate)) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

static std::vector<ProjectEntry> LoadProjects(const std::filesystem::path& storageDir) {
    std::vector<ProjectEntry> out;
    const std::string content = tasks_read_all(ProjectsStoragePath(storageDir));
    if (content.empty()) return out;
    const auto objects = tasks_parse_object_array(content);
    out.reserve(objects.size());
    for (const auto& obj : objects) {
        auto find_value = [&](const char* key) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            return it->second;
        };
        ProjectEntry project;
        if (auto v = find_value("id")) project.id = *v;
        if (auto v = find_value("name")) project.name = *v;
        if (auto v = find_value("description")) project.description = *v;
        if (auto v = find_value("createdAt")) project.createdAt = tasks_parse_int64(*v, 0);
        if (project.id.empty() || project.name.empty()) continue;
        out.push_back(std::move(project));
    }
    std::sort(out.begin(), out.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
        return a.name < b.name;
    });
    return out;
}

static bool SaveProjects(const std::filesystem::path& storageDir, const std::vector<ProjectEntry>& projects) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < projects.size(); ++i) {
        const auto& p = projects[i];
        if (p.id.empty() || p.name.empty()) continue;
        out << "  {\"id\":\"" << tasks_json_escape(p.id)
            << "\",\"name\":\"" << tasks_json_escape(p.name)
            << "\",\"description\":\"" << tasks_json_escape(p.description)
            << "\",\"createdAt\":" << p.createdAt << "}";
        if (i + 1 < projects.size()) out << ",";
        out << "\n";
    }
    out << "]";
    std::string data = out.str();
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    data.insert(0, reinterpret_cast<const char*>(bom), sizeof(bom));
    return tasks_write_all(ProjectsStoragePath(storageDir), data);
}

static const ProjectEntry* FindProjectById(const GuiState& state, const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& project : state.projects) {
        if (project.id == id) return &project;
    }
    return nullptr;
}

static std::string ResolveTaskProjectName(const GuiState& state, const TaskEntry& task) {
    if (!task.projectId.empty()) {
        if (const ProjectEntry* project = FindProjectById(state, task.projectId)) {
            return project->name;
        }
    }
    return task.project;
}


struct ProjectTaskStats {
    int total = 0;
    int created = 0;
    int inProgress = 0;
    int done = 0;
    int overdue = 0;
};

static ProjectTaskStats BuildProjectTaskStats(const GuiState& state, const std::string& projectId) {
    ProjectTaskStats stats{};
    if (projectId.empty()) return stats;
    const std::int64_t nowSec = NowSeconds();
    for (const auto& task : state.tasks) {
        if (task.projectId != projectId) {
            continue;
        }
        stats.total += 1;
        const int status = NormalizeTaskStatus(task.status);
        if (status == kTaskStatusDone) {
            stats.done += 1;
        } else if (status == kTaskStatusInProgress) {
            stats.inProgress += 1;
        } else {
            stats.created += 1;
        }
        if (task.deadlineAt > 0 && task.deadlineAt < nowSec && status != kTaskStatusDone) {
            stats.overdue += 1;
        }
    }
    return stats;
}

static void EnsureTaskPlanAssigneesSize(GuiState& state) {
    const size_t profileCount = state.profiles.size();
    if (state.taskPlanAssignees.size() == profileCount) return;
    std::vector<char> next(profileCount, 0);
    for (size_t i = 0; i < std::min(profileCount, state.taskPlanAssignees.size()); ++i) {
        next[i] = state.taskPlanAssignees[i] ? 1 : 0;
    }
    bool hasSelected = false;
    for (char flag : next) {
        if (flag) {
            hasSelected = true;
            break;
        }
    }
    if (!hasSelected && state.active) {
        for (size_t i = 0; i < profileCount; ++i) {
            if (state.profiles[i].id == state.active->id && !state.profiles[i].archived) {
                next[i] = 1;
                break;
            }
        }
    }
    state.taskPlanAssignees = std::move(next);
}

static bool ParseTaskDeadlineInput(const std::string& input, std::int64_t& outTs) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if (std::sscanf(input.c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) != 5) {
        return false;
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t timeValue = std::mktime(&tm);
    if (timeValue <= 0) return false;
    std::tm verify{};
#if defined(_WIN32)
    localtime_s(&verify, &timeValue);
#else
    localtime_r(&timeValue, &verify);
#endif
    if (verify.tm_year != tm.tm_year || verify.tm_mon != tm.tm_mon || verify.tm_mday != tm.tm_mday ||
        verify.tm_hour != tm.tm_hour || verify.tm_min != tm.tm_min) {
        return false;
    }
    outTs = static_cast<std::int64_t>(timeValue);
    return true;
}

void DrawProjectsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Projects, state.storageDir);
    DrawCardHeader(u8"Проекты");
    if (!state.isAdmin) {
        ImGui::TextDisabled(u8"Только для администратора.");
        return;
    }
    ImGui::TextDisabled(u8"Хранятся в meta/projects.json, синхронизируются через облако.");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    if (ImGui::BeginTable("projects_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("tasks", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(state.projects.size()); ++i) {
            const auto& project = state.projects[i];
            const ProjectTaskStats taskStats = BuildProjectTaskStats(state, project.id);
            ImGui::TableNextRow();
            ImGui::PushID(project.id.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", i + 1);
            ImGui::TableSetColumnIndex(1);
            const bool selected = state.projectEditIndex == i;
            if (ImGui::Selectable(project.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                state.projectEditIndex = i;
                std::snprintf(state.newProjectName.data(), state.newProjectName.size(), "%s", project.name.c_str());
                std::snprintf(state.newProjectDesc.data(), state.newProjectDesc.size(), "%s", project.description.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", taskStats.total);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled(u8"Н:%d  В:%d  Г:%d", taskStats.created, taskStats.inProgress, taskStats.done);
            if (taskStats.overdue > 0) {
                ImGui::SameLine();
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"П:%d", taskStats.overdue);
            }

            ImGui::TableSetColumnIndex(4);
            const bool canOpenTasks = state.modules.tasks;
            if (!canOpenTasks) ImGui::BeginDisabled();
            if (ImGui::SmallButton(u8"К задачам##open_tasks_project")) {
                state.showProjects = false;
                state.showTasks = true;
                state.tasksFilterProject = 2 + i;
            }
            if (!canOpenTasks) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Х##del_project")) {
                state.pendingDeleteProjectIndex = i;
                state.pendingDeleteProjectId = project.id;
                state.projectDeletePopupRequest = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (state.projectDeletePopupRequest) {
        ImGui::OpenPopup(u8"Удалить проект?");
        state.projectDeletePopupRequest = false;
    }
    bool deleteOpen = true;
    if (ImGui::BeginPopupModal(u8"Удалить проект?", &deleteOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        std::string projectName;
        std::string projectId = state.pendingDeleteProjectId;
        if (state.pendingDeleteProjectIndex >= 0 &&
            state.pendingDeleteProjectIndex < static_cast<int>(state.projects.size())) {
            const auto& project = state.projects[state.pendingDeleteProjectIndex];
            if (projectId.empty() || project.id == projectId) {
                projectId = project.id;
                projectName = project.name;
            }
        }
        if (projectName.empty() && !projectId.empty()) {
            for (const auto& p : state.projects) {
                if (p.id == projectId) {
                    projectName = p.name;
                    break;
                }
            }
        }
        if (projectName.empty()) projectName = u8"(не найден)";
        ImGui::TextWrapped(u8"Удалить проект и снять его со всех задач?");
        ImGui::TextDisabled("%s", projectName.c_str());
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button(u8"Удалить", ImVec2(180.0f, 0.0f))) {
            state.projects.erase(std::remove_if(state.projects.begin(), state.projects.end(),
                                                [&](const ProjectEntry& p) { return p.id == projectId; }),
                                 state.projects.end());
            for (auto& task : state.tasks) {
                if (task.projectId == projectId) {
                    task.projectId.clear();
                    task.project.clear();
                }
            }
            SaveProjects(state.storageDir, state.projects);
            SaveTasks(state.storageDir, state.tasks);
            state.projectEditIndex = -1;
            state.newProjectName.fill('\0');
            state.newProjectDesc.fill('\0');
            state.pendingDeleteProjectIndex = -1;
            state.pendingDeleteProjectId.clear();
            SetStatus(state, u8"Проект удален.", 0.45f, 0.9f, 0.45f);
            ImGui::CloseCurrentPopup();
            deleteOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"Отмена", ImVec2(180.0f, 0.0f))) {
            state.pendingDeleteProjectIndex = -1;
            state.pendingDeleteProjectId.clear();
            ImGui::CloseCurrentPopup();
            deleteOpen = false;
        }
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawCardHeader(u8"Редактирование проекта");
    if (ImGui::Button(u8"Новый проект", ImVec2(-1.0f, 0.0f))) {
        state.projectEditIndex = -1;
        state.newProjectName.fill('\0');
        state.newProjectDesc.fill('\0');
    }
    if (state.projectEditIndex >= 0 && state.projectEditIndex < static_cast<int>(state.projects.size())) {
        ImGui::TextDisabled(u8"Редактирование: %s", state.projects[state.projectEditIndex].name.c_str());
    } else {
        ImGui::TextDisabled(u8"Создание нового проекта");
    }
    ImGui::TextDisabled(u8"Название");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##project_name", state.newProjectName.data(), state.newProjectName.size());
    ImGui::TextDisabled(u8"Описание");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##project_desc", state.newProjectDesc.data(), state.newProjectDesc.size(), ImVec2(-1.0f, 90.0f));

    if (ImGui::Button(u8"Сохранить проект", ImVec2(-1.0f, 0.0f))) {
        const std::string name = TrimStringGui(state.newProjectName.data());
        const std::string description = TrimStringGui(state.newProjectDesc.data());
        if (name.empty()) {
            SetStatus(state, u8"Название проекта обязательно.", 1.0f, 0.45f, 0.45f);
        } else {
            int editIndex = state.projectEditIndex;
            if (editIndex >= 0 && editIndex < static_cast<int>(state.projects.size())) {
                auto& project = state.projects[editIndex];
                project.name = name;
                project.description = description;
            } else {
                ProjectEntry project;
                project.id = GenerateProjectId(state.projects);
                project.name = name;
                project.description = description;
                project.createdAt = NowSeconds();
                state.projects.push_back(std::move(project));
                std::sort(state.projects.begin(), state.projects.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
                    return a.name < b.name;
                });
                for (int i = 0; i < static_cast<int>(state.projects.size()); ++i) {
                    if (state.projects[i].name == name) {
                        editIndex = i;
                        break;
                    }
                }
            }
            if (SaveProjects(state.storageDir, state.projects)) {
                state.projectEditIndex = editIndex;
                SetStatus(state, u8"Проект сохранен.", 0.45f, 0.9f, 0.45f);
            } else {
                SetStatus(state, u8"Не удалось сохранить проект.", 1.0f, 0.45f, 0.45f);
            }
        }
    }
}


