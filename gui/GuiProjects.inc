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
    int xpPending = 0;
};

struct ProjectPortfolioStats {
    int totalProjects = 0;
    int activeProjects = 0;
    int overdueProjects = 0;
    int xpPendingProjects = 0;
    int totalActiveTasks = 0;
    int totalOverdueTasks = 0;
    int totalXpPendingTasks = 0;
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
        if (TaskNeedsXpAward(task)) {
            stats.xpPending += 1;
        }
        if (task.deadlineAt > 0 && task.deadlineAt < nowSec && status != kTaskStatusDone) {
            stats.overdue += 1;
        }
    }
    return stats;
}

static ProjectPortfolioStats BuildProjectPortfolioStats(const std::vector<ProjectTaskStats>& projectStats) {
    ProjectPortfolioStats summary{};
    summary.totalProjects = static_cast<int>(projectStats.size());
    for (const auto& stats : projectStats) {
        const int activeTasks = stats.created + stats.inProgress;
        if (activeTasks > 0) {
            summary.activeProjects += 1;
            summary.totalActiveTasks += activeTasks;
        }
        if (stats.overdue > 0) {
            summary.overdueProjects += 1;
            summary.totalOverdueTasks += stats.overdue;
        }
        if (stats.xpPending > 0) {
            summary.xpPendingProjects += 1;
            summary.totalXpPendingTasks += stats.xpPending;
        }
    }
    return summary;
}

static void OpenTasksFromProjectHub(GuiState& state, int projectFilterIndex, int quickFilter) {
    state.showProjects = false;
    state.showTasks = true;
    state.tasksQuickFilter = quickFilter;
    state.tasksFilterProfile = 0;
    state.tasksFilterProject = projectFilterIndex;
    state.tasksFilterPipeline = 0;
    state.tasksFilterStatus = 0;
    state.tasksFilterPriority = 0;
    state.tasksFilterDays = 0;
    state.tasksSortMode = 0;
    state.filters.tasks.fill('\0');
    state.tasksExpandedId.clear();
    state.taskSelectionIds.clear();
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
    auto ProjectIconButton = [](const char* id, UiIcon icon, const char* tooltip,
                                bool enabled = true, bool selected = false) -> bool {
        ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        return IconButtonGui(id, icon, tooltip, accent, ImGui::GetFrameHeight(), enabled, selected);
    };

    std::vector<ProjectTaskStats> projectStats;
    projectStats.reserve(state.projects.size());
    std::vector<std::string> projectNamesLower;
    projectNamesLower.reserve(state.projects.size());
    std::vector<int> projectOrder;
    projectOrder.reserve(state.projects.size());
    for (int i = 0; i < static_cast<int>(state.projects.size()); ++i) {
        const auto& project = state.projects[static_cast<size_t>(i)];
        projectStats.push_back(BuildProjectTaskStats(state, project.id));
        projectNamesLower.push_back(ToLowerUtf8(project.name));
        projectOrder.push_back(i);
    }
    const ProjectPortfolioStats portfolio = BuildProjectPortfolioStats(projectStats);

    const float avail = ImGui::GetContentRegionAvail().x;
    const bool stackFilters = avail < 780.0f;
    bool projectsFilterChanged = false;
    bool overdueChanged = false;
    bool xpPendingChanged = false;
    if (stackFilters) {
        ImGui::SetNextItemWidth(-1.0f);
        projectsFilterChanged = ImGui::InputTextWithHint("##projects_filter", u8"Поиск проекта", state.filters.projects.data(), state.filters.projects.size());
        if (ImGui::SmallButton(u8"Сброс##projects_filter")) {
            state.filters.projects.fill('\0');
            state.projectsFilterOverdueOnly = false;
            state.projectsFilterXpPendingOnly = false;
            projectsFilterChanged = true;
        }
        overdueChanged = ImGui::Checkbox(u8"Только просроченные", &state.projectsFilterOverdueOnly);
        xpPendingChanged = ImGui::Checkbox(u8"Только ждут XP", &state.projectsFilterXpPendingOnly);
    } else {
        const float overdueWidth = 210.0f;
        const float xpPendingWidth = 170.0f;
        const float resetWidth = 72.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float inputWidth = std::max(160.0f, avail - overdueWidth - xpPendingWidth - resetWidth - spacing * 3.0f);
        ImGui::SetNextItemWidth(inputWidth);
        projectsFilterChanged = ImGui::InputTextWithHint("##projects_filter", u8"Поиск проекта", state.filters.projects.data(), state.filters.projects.size());
        ImGui::SameLine();
        overdueChanged = ImGui::Checkbox(u8"Только просроченные", &state.projectsFilterOverdueOnly);
        ImGui::SameLine();
        xpPendingChanged = ImGui::Checkbox(u8"Только ждут XP", &state.projectsFilterXpPendingOnly);
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Сброс##projects_filter")) {
            state.filters.projects.fill('\0');
            state.projectsFilterOverdueOnly = false;
            state.projectsFilterXpPendingOnly = false;
            projectsFilterChanged = true;
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::TextDisabled(u8"Сортировка");
    ImGui::SetNextItemWidth(220.0f);
    const char* projectSortLabels[] = {u8"По названию", u8"По задачам", u8"По просроченным", u8"По XP handoff"};
    state.projectsSortMode = std::clamp(state.projectsSortMode, 0, 3);
    bool sortChanged = ImGui::Combo("##projects_sort", &state.projectsSortMode, projectSortLabels, IM_ARRAYSIZE(projectSortLabels));
    if (projectsFilterChanged || overdueChanged || xpPendingChanged || sortChanged) {
        SyncFiltersToUi(state);
        state.uiDirty = true;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("projects_status_slice", 4, ControlTableFlagsGui())) {
        ImGui::TableSetupColumn("projects_total", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn("projects_active", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("projects_xp_pending", ImGuiTableColumnFlags_WidthStretch, 1.05f);
        ImGui::TableSetupColumn("projects_overdue", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled(u8"Проектов");
        ImGui::Text("%d", portfolio.totalProjects);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled(u8"Активный поток");
        {
            std::string activeLabel = std::to_string(portfolio.activeProjects) + u8" пр. / " +
                std::to_string(portfolio.totalActiveTasks) + u8" зад.";
            DrawStatusPillGui("projects_summary_active", activeLabel.c_str(),
                              ImGui::GetStyle().Colors[ImGuiCol_CheckMark],
                              u8"Активные проекты и задачи", portfolio.totalActiveTasks > 0);
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"XP handoff");
        if (portfolio.xpPendingProjects > 0) {
            std::string xpLabel = std::to_string(portfolio.xpPendingProjects) + u8" пр. / " +
                std::to_string(portfolio.totalXpPendingTasks) + u8" зад.";
            DrawStatusPillGui("projects_summary_xp", xpLabel.c_str(),
                              LogLevelColor(AppLogLevel::Warning),
                              u8"Проекты с задачами, ожидающими XP", true);
        } else {
            ImGui::TextDisabled(u8"Нет ожидания");
        }

        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled(u8"Просрочка");
        if (portfolio.overdueProjects > 0) {
            std::string overdueLabel = std::to_string(portfolio.overdueProjects) + u8" пр.";
            DrawStatusPillGui("projects_summary_overdue", overdueLabel.c_str(),
                              LogLevelColor(AppLogLevel::Warning),
                              u8"Проекты с просроченными задачами", true);
        } else {
            ImGui::TextDisabled(u8"Нет риска");
        }
            ImGui::EndTable();
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("projects_hub_actions", 4, ImGuiTableFlags_SizingStretchSame)) {
        const bool canUseTasksHub = state.modules.tasks;
        ImGui::TableSetupColumn("projects_open_all");
        ImGui::TableSetupColumn("projects_open_active");
        ImGui::TableSetupColumn("projects_open_xp");
        ImGui::TableSetupColumn("projects_open_overdue");
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (!canUseTasksHub) ImGui::BeginDisabled();
        if (ProjectIconButton("projects_open_all", UiIcon::Task, u8"Открыть все задачи проектов")) {
            OpenTasksFromProjectHub(state, 0, 0);
        }
        if (!canUseTasksHub) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(u8"Все");
        ImGui::TextDisabled(u8"%d проектов", portfolio.totalProjects);

        ImGui::TableSetColumnIndex(1);
        if (!canUseTasksHub || portfolio.totalActiveTasks <= 0) ImGui::BeginDisabled();
        if (ProjectIconButton("projects_open_active", UiIcon::Play, u8"Открыть активные задачи проектов")) {
            OpenTasksFromProjectHub(state, 0, 7);
        }
        if (!canUseTasksHub || portfolio.totalActiveTasks <= 0) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(u8"Актив");
        ImGui::TextDisabled(u8"%d задач", portfolio.totalActiveTasks);

        ImGui::TableSetColumnIndex(2);
        if (!canUseTasksHub || portfolio.totalXpPendingTasks <= 0) ImGui::BeginDisabled();
        if (ProjectIconButton("projects_open_xp", UiIcon::Award, u8"Открыть задачи, ожидающие XP")) {
            OpenTasksFromProjectHub(state, 0, 6);
        }
        if (!canUseTasksHub || portfolio.totalXpPendingTasks <= 0) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("XP");
        ImGui::TextDisabled(u8"%d задач", portfolio.totalXpPendingTasks);

        ImGui::TableSetColumnIndex(3);
        if (!canUseTasksHub || portfolio.totalOverdueTasks <= 0) ImGui::BeginDisabled();
        if (ProjectIconButton("projects_open_overdue", UiIcon::Calendar, u8"Открыть просроченные задачи проектов")) {
            OpenTasksFromProjectHub(state, 0, 3);
        }
        if (!canUseTasksHub || portfolio.totalOverdueTasks <= 0) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(u8"Срок");
        ImGui::TextDisabled(u8"%d задач", portfolio.totalOverdueTasks);

            ImGui::EndTable();
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    int shownProjects = 0;
    {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("projects_table", 5, ControlTableFlagsGui(ImGuiTableFlags_RowBg))) {
        const ImGuiStyle& projectStyle = ImGui::GetStyle();
        const float projectActionWidth = ImGui::GetFrameHeight() * 4.0f + projectStyle.ItemSpacing.x * 3.0f +
                                         projectStyle.CellPadding.x * 2.0f;
        ImGui::TableSetupColumn("idx", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("tasks", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch, 1.25f);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, projectActionWidth);
        ImGui::TableHeadersRow();

        const int sortMode = std::clamp(state.projectsSortMode, 0, 3);
        if (sortMode == 1) {
            std::sort(projectOrder.begin(), projectOrder.end(), [&](int a, int b) {
                const auto& sa = projectStats[a];
                const auto& sb = projectStats[b];
                if (sa.total != sb.total) return sa.total > sb.total;
                return projectNamesLower[a] < projectNamesLower[b];
            });
        } else if (sortMode == 2) {
            std::sort(projectOrder.begin(), projectOrder.end(), [&](int a, int b) {
                const auto& sa = projectStats[a];
                const auto& sb = projectStats[b];
                if (sa.overdue != sb.overdue) return sa.overdue > sb.overdue;
                if (sa.xpPending != sb.xpPending) return sa.xpPending > sb.xpPending;
                return projectNamesLower[a] < projectNamesLower[b];
            });
        } else if (sortMode == 3) {
            std::sort(projectOrder.begin(), projectOrder.end(), [&](int a, int b) {
                const auto& sa = projectStats[a];
                const auto& sb = projectStats[b];
                if (sa.xpPending != sb.xpPending) return sa.xpPending > sb.xpPending;
                if (sa.done != sb.done) return sa.done > sb.done;
                if (sa.overdue != sb.overdue) return sa.overdue > sb.overdue;
                return projectNamesLower[a] < projectNamesLower[b];
            });
        } else {
            std::sort(projectOrder.begin(), projectOrder.end(), [&](int a, int b) {
                return projectNamesLower[a] < projectNamesLower[b];
            });
        }
        for (int idx : projectOrder) {
            const auto& project = state.projects[idx];
            const auto& taskStats = projectStats[idx];
            const int projectFilterIndex = 2 + idx;
            const int activeTasks = taskStats.created + taskStats.inProgress;
            const std::string hay = project.name + " " + project.description;
            if (!MatchesFilter(hay, state.filters.projects.data())) {
                continue;
            }
            if (state.projectsFilterOverdueOnly && taskStats.overdue <= 0) {
                continue;
            }
            if (state.projectsFilterXpPendingOnly && taskStats.xpPending <= 0) {
                continue;
            }
            shownProjects += 1;
            ImGui::TableNextRow();
            const bool selected = state.projectEditIndex == idx;
            const bool focusedByTasks = state.tasksFilterProject == projectFilterIndex;
            if (selected) {
                ApplyTableRowStateTintGui(true);
            } else if (focusedByTasks) {
                ApplyTableRowStateTintGui(false, true);
            } else if (taskStats.overdue > 0) {
                ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                rowHighlight.w = std::clamp(rowHighlight.w * 0.16f, 0.08f, 0.22f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
            } else if (taskStats.xpPending > 0) {
                ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                rowHighlight.w = std::clamp(rowHighlight.w * 0.08f, 0.04f, 0.10f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
            }
            ImGui::PushID(project.id.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", shownProjects);
            ImGui::TableSetColumnIndex(1);
            const std::string projectRowLabel = (project.name.empty() ? std::string(u8"Без названия") : project.name) +
                "##project_row_" + project.id;
            if (ImGui::Selectable(projectRowLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                state.projectEditIndex = idx;
                std::snprintf(state.newProjectName.data(), state.newProjectName.size(), "%s", project.name.c_str());
                std::snprintf(state.newProjectDesc.data(), state.newProjectDesc.size(), "%s", project.description.c_str());
            }
            if (ImGui::IsItemHovered() && (!project.description.empty() ||
                                           ImGui::CalcTextSize(project.name.c_str()).x > ImGui::GetContentRegionAvail().x)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(project.name.empty() ? u8"Без названия" : project.name.c_str());
                if (!project.description.empty()) {
                    ImGui::Separator();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                    ImGui::TextUnformatted(project.description.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", taskStats.total);

            ImGui::TableSetColumnIndex(3);
            ImVec4 neutralPill = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            ImVec4 activePill = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            DrawStatusPillGui("project_new_pill", (std::string("Н:") + std::to_string(taskStats.created)).c_str(),
                              neutralPill, u8"Новые задачи");
            ImGui::SameLine();
            DrawStatusPillGui("project_work_pill", (std::string("В:") + std::to_string(taskStats.inProgress)).c_str(),
                              activePill, u8"Задачи в работе", taskStats.inProgress > 0);
            ImGui::SameLine();
            DrawStatusPillGui("project_done_pill", (std::string("Г:") + std::to_string(taskStats.done)).c_str(),
                              LogLevelColor(AppLogLevel::Info), u8"Готовые задачи", taskStats.done > 0);
            if (taskStats.overdue > 0) {
                ImGui::SameLine();
                DrawStatusPillGui("project_overdue_pill", (std::string(u8"П:") + std::to_string(taskStats.overdue)).c_str(),
                                  LogLevelColor(AppLogLevel::Warning), u8"Просроченные задачи", true);
            }
            if (taskStats.xpPending > 0) {
                ImGui::SameLine();
                DrawStatusPillGui("project_xp_pill", (std::string("XP:") + std::to_string(taskStats.xpPending)).c_str(),
                                  LogLevelColor(AppLogLevel::Warning), u8"Задачи ждут выдачи опыта", true);
            }
            if (taskStats.xpPending > 0) {
                ImGui::TextDisabled(u8"Handoff ждёт");
            } else if (taskStats.done > 0) {
                ImGui::TextDisabled(u8"Handoff закрыт");
            }

            ImGui::TableSetColumnIndex(4);
            const bool canOpenTasks = state.modules.tasks;
            if (!canOpenTasks) ImGui::BeginDisabled();
            if (ProjectIconButton("open_tasks_project", UiIcon::Task, u8"Открыть задачи проекта")) {
                OpenTasksFromProjectHub(state, projectFilterIndex, 0);
            }
            if (canOpenTasks && taskStats.xpPending > 0) {
                ImGui::SameLine();
                if (ProjectIconButton("open_tasks_project_xp", UiIcon::Award, u8"Открыть задачи проекта, ожидающие XP")) {
                    OpenTasksFromProjectHub(state, projectFilterIndex, 6);
                }
            } else if (canOpenTasks && taskStats.overdue > 0) {
                ImGui::SameLine();
                if (ProjectIconButton("open_tasks_project_deadline", UiIcon::Calendar, u8"Открыть просроченные задачи проекта")) {
                    OpenTasksFromProjectHub(state, projectFilterIndex, 3);
                }
            } else if (canOpenTasks && activeTasks > 0) {
                ImGui::SameLine();
                if (ProjectIconButton("open_tasks_project_active", UiIcon::Play, u8"Открыть активные задачи проекта")) {
                    OpenTasksFromProjectHub(state, projectFilterIndex, 7);
                }
            }
            if (!canOpenTasks) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ProjectIconButton("del_project", UiIcon::Close, u8"Удалить проект")) {
                state.pendingDeleteProjectIndex = idx;
                state.pendingDeleteProjectId = project.id;
                state.projectDeletePopupRequest = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
            ImGui::EndTable();
        }
    }
    ImGui::TextDisabled(u8"Показано проектов: %d из %d", shownProjects, static_cast<int>(state.projects.size()));
    if (shownProjects <= 0) {
        if (state.projects.empty()) {
            if (DrawEmptyStateGui("projects_empty_all", UiIcon::Project,
                                  u8"Проектов пока нет",
                                  u8"Создайте первый проект, чтобы группировать задачи, видеть handoff и просрочки по направлению.",
                                  UiIcon::Plus, u8"Перейти к созданию проекта")) {
                state.projectEditIndex = -1;
                state.newProjectName.fill('\0');
                state.newProjectDesc.fill('\0');
            }
        } else {
            if (DrawEmptyStateGui("projects_empty_filter", UiIcon::Search,
                                  u8"Нет проектов по фильтру",
                                  u8"Текущие фильтры скрыли все проекты. Сброс вернет полный список.",
                                  UiIcon::Refresh, u8"Сбросить фильтры проектов")) {
                state.filters.projects.fill('\0');
                state.projectsFilterOverdueOnly = false;
                state.projectsFilterXpPendingOnly = false;
                SyncFiltersToUi(state);
                state.uiDirty = true;
            }
        }
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
            std::string actor;
            if (state.isAdmin) {
                actor = "admin";
            } else if (state.active) {
                actor = "[" + state.active->id + "] " + state.active->profile.name();
            } else {
                actor = "system";
            }
            AppProjectDeleteResult deleteResult = AppDeleteProjectAndDetachTasks(
                state.storageDir, state.projects, state.tasks, projectId, actor, &state.taskAudit);
            if (deleteResult.ok) {
                state.projectEditIndex = -1;
                state.newProjectName.fill('\0');
                state.newProjectDesc.fill('\0');
                state.pendingDeleteProjectIndex = -1;
                state.pendingDeleteProjectId.clear();
                std::string msg = u8"Проект удален";
                if (deleteResult.detachedTasks > 0) {
                    msg += u8", задач отвязано: " + std::to_string(deleteResult.detachedTasks);
                }
                msg += ".";
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                ImGui::CloseCurrentPopup();
                deleteOpen = false;
            } else if (deleteResult.errorMessage == u8"Не удалось записать task-audit.log") {
                AppendAppLog(state, AppLogLevel::Warning, u8"Проекты", u8"Не удалось записать task-audit.log");
                SetStatus(state, u8"Проект удален, но task-audit.log не обновлен.", 1.0f, 0.75f, 0.35f);
                state.projectEditIndex = -1;
                state.newProjectName.fill('\0');
                state.newProjectDesc.fill('\0');
                state.pendingDeleteProjectIndex = -1;
                state.pendingDeleteProjectId.clear();
                ImGui::CloseCurrentPopup();
                deleteOpen = false;
            } else {
                SetStatus(state, deleteResult.errorMessage, 1.0f, 0.45f, 0.45f);
            }
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
    if (ProjectIconButton("project_editor_new", UiIcon::Plus, u8"Новый проект")) {
        state.projectEditIndex = -1;
        state.newProjectName.fill('\0');
        state.newProjectDesc.fill('\0');
    }
    ImGui::SameLine();
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

    if (ProjectIconButton("project_editor_save", UiIcon::Save, u8"Сохранить проект")) {
        AppProjectSaveResult saveResult = AppSaveProjectEntry(
            state.storageDir,
            state.projects,
            state.projectEditIndex,
            TrimStringGui(state.newProjectName.data()),
            TrimStringGui(state.newProjectDesc.data()));
        if (saveResult.ok) {
            state.projectEditIndex = saveResult.projectIndex;
            SetStatus(state, u8"Проект сохранен.", 0.45f, 0.9f, 0.45f);
        } else {
            SetStatus(state, saveResult.errorMessage, 1.0f, 0.45f, 0.45f);
        }
    }
}
