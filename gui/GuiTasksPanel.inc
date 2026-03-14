static bool RollbackTask(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const TaskEntry& task) {
    const std::string previousActive = state.active ? state.active->id : std::string{};
    bool ok = true;
    for (const auto& participant : task.participants) {
        if (participant.profileId.empty()) continue;
        if (!storage.set_active_profile(participant.profileId)) {
            SetStatus(state, u8"Не удалось активировать профиль для отката XP.", 1.0f, 0.45f, 0.45f);
            ok = false;
            break;
        }
        auto profileOpt = storage.load_profile();
        if (!profileOpt) {
            SetStatus(state, u8"Не удалось загрузить профиль для отката XP.", 1.0f, 0.45f, 0.45f);
            ok = false;
            break;
        }
        Profile profile = *profileOpt;
        const int newTotalXp = std::max(0, profile.total_xp() - participant.globalXp);
        const int newTasks = std::max(0, profile.tasks_completed() - 1);
        profile.set_total_xp(newTotalXp);
        profile.set_tasks_completed(newTasks);
        AppProfileMutationResult mutation = AppSaveProfileSnapshot(storage, previousActive, participant.profileId, profile);
        if (!mutation.ok) {
            SetStatus(state,
                      mutation.errorMessage.empty() ? u8"Не удалось сохранить профиль при откате XP." : mutation.errorMessage,
                      1.0f, 0.45f, 0.45f);
            ok = false;
            break;
        }
    }
    if (!previousActive.empty()) {
        SelectProfileById(state, storage, catalog, previousActive);
    }
    return ok;
}

void DrawTasksPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Tasks, state.storageDir);
    if (!state.modules.tasks) {
        DrawCardHeader(u8"Задачи");
        ImGui::TextDisabled(u8"Модуль задач отключён.");
        return;
    }
    DrawCardHeader(u8"Задачи");
    ImGui::TextDisabled(u8"Планирование и выполненные задачи в одном журнале.");

    auto ResolveTaskActor = [&state]() -> std::string {
        if (state.isAdmin) return "admin";
        if (state.active) {
            return "[" + state.active->id + "] " + state.active->profile.name();
        }
        return "system";
    };

    auto FindTaskMutable = [&state](const std::string& taskId) -> TaskEntry* {
        auto it = std::find_if(state.tasks.begin(), state.tasks.end(),
                               [&](const TaskEntry& item) { return item.id == taskId; });
        return (it == state.tasks.end()) ? nullptr : &(*it);
    };

    auto UpdateTaskStatus = [&](const std::string& taskId, int newStatus) -> bool {
        AppMutationResult mutation = AppUpdateTaskStatus(
            state.storageDir, state.tasks, taskId, newStatus, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Статус задачи обновлен.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        const bool warn = (mutation.errorMessage == u8"Недопустимый переход статуса (Выполнена -> Новая).");
        SetStatus(state, mutation.errorMessage, warn ? 1.0f : 1.0f, warn ? 0.75f : 0.45f, warn ? 0.35f : 0.45f);
        return false;
    };

    auto UpdateTaskPriority = [&](const std::string& taskId, int newPriority) -> bool {
        AppMutationResult mutation = AppUpdateTaskPriority(
            state.storageDir, state.tasks, taskId, newPriority, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Приоритет задачи обновлен.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskProject = [&](const std::string& taskId, int projectIndex) -> bool {
        std::string nextProjectId;
        std::string nextProjectName;
        if (projectIndex > 0) {
            const int idx = projectIndex - 1;
            if (idx < 0 || idx >= static_cast<int>(state.projects.size())) {
                SetStatus(state, u8"Проект не найден.", 1.0f, 0.45f, 0.45f);
                return false;
            }
            nextProjectId = state.projects[idx].id;
            nextProjectName = state.projects[idx].name;
        }
        AppMutationResult mutation = AppUpdateTaskProject(
            state.storageDir, state.tasks, taskId, nextProjectId, nextProjectName, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Проект задачи обновлен.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto ResolveTaskPipelineName = [&state](const TaskEntry& task) -> std::string {
        if (!task.pipelineStepId.empty()) {
            const int idx = FindPipelineStepIndexById(state, task.pipelineStepId);
            if (idx >= 0 && idx < static_cast<int>(state.pipelineSteps.size())) {
                return PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(idx)], idx);
            }
        }
        return task.pipelineStep;
    };

    auto UpdateTaskPipeline = [&](const std::string& taskId, int pipelineIndex) -> bool {
        std::string nextPipelineId;
        std::string nextPipelineName;
        if (pipelineIndex > 0) {
            const int idx = pipelineIndex - 1;
            if (idx < 0 || idx >= static_cast<int>(state.pipelineSteps.size())) {
                SetStatus(state, u8"Этап пайплайна не найден.", 1.0f, 0.45f, 0.45f);
                return false;
            }
            nextPipelineId = state.pipelineSteps[static_cast<size_t>(idx)].id;
            nextPipelineName = PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(idx)], idx);
        }
        AppMutationResult mutation = AppUpdateTaskPipelineStep(
            state.storageDir, state.tasks, taskId, nextPipelineId, nextPipelineName, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Этап задачи обновлен.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskDeadline = [&](const std::string& taskId, const std::string& deadlineInput) -> bool {
        const std::string trimmed = TrimStringGui(deadlineInput);
        std::optional<std::int64_t> deadlineAt;
        if (!trimmed.empty()) {
            std::int64_t parsed = 0;
            if (!AppParseTaskDeadlineInput(trimmed, parsed)) {
                SetStatus(state, u8"Формат дедлайна: YYYY-MM-DD HH:MM.", 1.0f, 0.45f, 0.45f);
                return false;
            }
            deadlineAt = parsed;
        }
        AppMutationResult mutation = AppUpdateTaskDeadline(
            state.storageDir, state.tasks, taskId, deadlineAt, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Дедлайн задачи обновлен.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskAssignees = [&](const std::string& taskId, const std::vector<std::string>& assignees) -> bool {
        AppMutationResult mutation = AppUpdateTaskAssignees(
            state.storageDir, state.tasks, taskId, assignees, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Исполнители задачи обновлены.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        const bool warn = (mutation.errorMessage == u8"Для этой задачи исполнители зафиксированы по записи XP.");
        SetStatus(state, mutation.errorMessage, 1.0f, warn ? 0.75f : 0.45f, warn ? 0.35f : 0.45f);
        return false;
    };

    auto StartOfLocalDay = [](std::int64_t ts) -> std::int64_t {
        std::time_t tv = static_cast<std::time_t>(ts);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &tv);
#else
        localtime_r(&tv, &localTm);
#endif
        localTm.tm_hour = 0;
        localTm.tm_min = 0;
        localTm.tm_sec = 0;
        localTm.tm_isdst = -1;
        return static_cast<std::int64_t>(std::mktime(&localTm));
    };

    auto ResetTaskFilters = [&state]() {
        state.tasksQuickFilter = 0;
        state.tasksFilterProfile = 0;
        state.tasksFilterProject = 0;
        state.tasksFilterStatus = 0;
        state.tasksFilterPriority = 0;
        state.tasksFilterDays = 0;
        state.tasksSortMode = 0;
        state.filters.tasks.fill('\0');
    };

    if (state.isAdmin && state.showTaskCreateForm) {
        if (BeginCard("tasks_create_card")) {
            DrawCardHeader(u8"Создать задачу");
            EnsureTaskPlanAssigneesSize(state);
            std::vector<std::string> projectLabels;
            projectLabels.reserve(state.projects.size() + 1);
            projectLabels.push_back(u8"Без проекта");
            for (const auto& project : state.projects) {
                projectLabels.push_back(project.name);
            }
            std::vector<const char*> projectLabelPtrs;
            projectLabelPtrs.reserve(projectLabels.size());
            for (const auto& label : projectLabels) {
                projectLabelPtrs.push_back(label.c_str());
            }
            std::vector<std::string> pipelineLabels;
            pipelineLabels.reserve(state.pipelineSteps.size() + 1);
            pipelineLabels.push_back(u8"Без этапа");
            for (int i = 0; i < static_cast<int>(state.pipelineSteps.size()); ++i) {
                pipelineLabels.push_back(PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(i)], i));
            }
            std::vector<const char*> pipelineLabelPtrs;
            pipelineLabelPtrs.reserve(pipelineLabels.size());
            for (const auto& label : pipelineLabels) {
                pipelineLabelPtrs.push_back(label.c_str());
            }
            state.taskPlanProjectIndex = std::clamp(state.taskPlanProjectIndex, 0,
                static_cast<int>(projectLabelPtrs.size()) - 1);
            state.taskPlanPipelineIndex = std::clamp(state.taskPlanPipelineIndex, 0,
                static_cast<int>(pipelineLabelPtrs.size()) - 1);
            state.taskPlanStatus = NormalizeTaskStatus(state.taskPlanStatus);
            state.taskPlanPriority = NormalizeTaskPriority(state.taskPlanPriority);

            if (ImGui::BeginTable("tasks_create_form", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("create_left", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("create_right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Название задачи");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##task_plan_title", state.taskPlanTitle.data(), state.taskPlanTitle.size());
                ImGui::TextDisabled(u8"Проект");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_project", &state.taskPlanProjectIndex,
                             projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()));
                ImGui::TextDisabled(u8"Этап пайплайна");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_pipeline", &state.taskPlanPipelineIndex,
                             pipelineLabelPtrs.data(), static_cast<int>(pipelineLabelPtrs.size()));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(u8"Дедлайн (YYYY-MM-DD HH:MM)");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##task_plan_deadline", u8"2026-02-14 18:00",
                                         state.taskPlanDeadline.data(), state.taskPlanDeadline.size());
                ImGui::TextDisabled(u8"Статус");
                const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_status", &state.taskPlanStatus, statusLabels, IM_ARRAYSIZE(statusLabels));
                ImGui::TextDisabled(u8"Приоритет");
                const char* priorityLabels[] = {u8"Низкий", u8"Средний", u8"Высокий", u8"Критический"};
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_priority", &state.taskPlanPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels));
                ImGui::EndTable();
            }

            ImGui::TextDisabled(u8"Краткое описание");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextMultiline("##task_plan_desc", state.taskPlanDescription.data(),
                                      state.taskPlanDescription.size(), ImVec2(-1.0f, 70.0f));

            ImGui::TextDisabled(u8"Назначить профилям");
            if (ImGui::BeginTable("task_plan_assignees", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(u8"№", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn(u8"Профиль", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn(u8"Назначить", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                    const auto& info = state.profiles[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%d", i + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info.name.c_str());
                    if (info.archived) {
                        ImGui::SameLine();
                        ImGui::TextDisabled(u8"(архив)");
                    }
                    ImGui::TableSetColumnIndex(2);
                    bool checked = (i < static_cast<int>(state.taskPlanAssignees.size()) && state.taskPlanAssignees[i] != 0);
                    if (info.archived) ImGui::BeginDisabled();
                    ImGui::PushID(info.id.c_str());
                    if (ImGui::Checkbox("##task_plan_assign", &checked)) {
                        if (i < static_cast<int>(state.taskPlanAssignees.size())) {
                            state.taskPlanAssignees[i] = checked ? 1 : 0;
                        }
                    }
                    ImGui::PopID();
                    if (info.archived) ImGui::EndDisabled();
                }
                ImGui::EndTable();
            }

            if (ImGui::Button(u8"Создать задачу", ImVec2(-1.0f, 0.0f))) {
                const std::string title = TrimStringGui(state.taskPlanTitle.data());
                const std::string description = TrimStringGui(state.taskPlanDescription.data());
                const std::string deadlineInput = TrimStringGui(state.taskPlanDeadline.data());
                std::int64_t deadlineTs = 0;
                std::vector<std::string> assignees;
                for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                    if (i < static_cast<int>(state.taskPlanAssignees.size()) && state.taskPlanAssignees[i] && !state.profiles[i].archived) {
                        assignees.push_back(state.profiles[i].id);
                    }
                }
                if (title.empty()) {
                    SetStatus(state, u8"Название задачи обязательно.", 1.0f, 0.45f, 0.45f);
                } else if (description.empty()) {
                    SetStatus(state, u8"Краткое описание обязательно.", 1.0f, 0.45f, 0.45f);
                } else if (!ParseTaskDeadlineInput(deadlineInput, deadlineTs)) {
                    SetStatus(state, u8"Укажите дедлайн в формате YYYY-MM-DD HH:MM.", 1.0f, 0.45f, 0.45f);
                } else if (assignees.empty()) {
                    SetStatus(state, u8"Выберите хотя бы один профиль.", 1.0f, 0.45f, 0.45f);
                } else {
                    TaskEntry task;
                    const std::int64_t nowSec = NowSeconds();
                    task.id = AppGenerateTaskId(nowSec, state.tasks.size() + 1);
                    task.title = title;
                    task.description = description;
                    task.deadlineAt = deadlineTs;
                    task.status = AppNormalizeTaskStatus(state.taskPlanStatus);
                    task.priority = AppNormalizeTaskPriority(state.taskPlanPriority);
                    task.createdAt = nowSec;
                    task.assignees = assignees;
                    if (state.taskPlanProjectIndex > 0) {
                        const int projectIdx = state.taskPlanProjectIndex - 1;
                        if (projectIdx >= 0 && projectIdx < static_cast<int>(state.projects.size())) {
                            task.projectId = state.projects[projectIdx].id;
                            task.project = state.projects[projectIdx].name;
                        }
                    }
                    if (state.taskPlanPipelineIndex > 0) {
                        const int pipelineIdx = state.taskPlanPipelineIndex - 1;
                        if (pipelineIdx >= 0 && pipelineIdx < static_cast<int>(state.pipelineSteps.size())) {
                            task.pipelineStepId = state.pipelineSteps[static_cast<size_t>(pipelineIdx)].id;
                            task.pipelineStep = PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(pipelineIdx)], pipelineIdx);
                        }
                    }
                    AppMutationResult mutation = AppCreateTaskEntry(
                        state.storageDir, state.tasks, task, ResolveTaskActor(), &state.taskAudit);
                    if (mutation.ok) {
                        SetStatus(state, u8"Задача создана.", 0.45f, 0.9f, 0.45f);
                        state.showTaskCreateForm = false;
                        state.taskPlanTitle.fill('\0');
                        state.taskPlanDescription.fill('\0');
                        state.taskPlanDeadline.fill('\0');
                        state.taskPlanProjectIndex = 0;
                        state.taskPlanPipelineIndex = 0;
                        state.taskPlanStatus = kTaskStatusNew;
                        state.taskPlanPriority = kTaskPriorityMedium;
                        for (auto& flag : state.taskPlanAssignees) flag = 0;
                        EnsureTaskPlanAssigneesSize(state);
                    } else {
                        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
                    }
                }
            }

            if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, 0.0f))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
        }
        EndCard();
        ImGui::Spacing();
    }

    if (state.tasks.empty()) {
        ImGui::TextDisabled(u8"Задач пока нет.");
    }

    auto ComputeFilteredTasks = [&state, &StartOfLocalDay, &ResolveTaskPipelineName]() {
        std::vector<const TaskEntry*> ordered;
        ordered.reserve(state.tasks.size());
        for (const auto& task : state.tasks) {
            ordered.push_back(&task);
        }
        const std::int64_t nowSec = NowSeconds();
        const std::int64_t minTs = state.tasksFilterDays > 0
            ? nowSec - static_cast<std::int64_t>(state.tasksFilterDays) * 86400
            : 0;
        const std::int64_t dayStart = StartOfLocalDay(nowSec);
        const std::int64_t dayEnd = dayStart + 86400;
        const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
            state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
            ? state.profiles[state.tasksFilterProfile - 1].id
            : std::string();
        bool filterWithoutProject = false;
        std::string filterProjectId;
        std::string filterProjectName;
        if (state.tasksFilterProject == 1) {
            filterWithoutProject = true;
        } else if (state.tasksFilterProject >= 2) {
            const int idx = state.tasksFilterProject - 2;
            if (idx >= 0 && idx < static_cast<int>(state.projects.size())) {
                filterProjectId = state.projects[static_cast<size_t>(idx)].id;
                filterProjectName = state.projects[static_cast<size_t>(idx)].name;
            }
        }
        std::vector<const TaskEntry*> filtered;
        filtered.reserve(ordered.size());
        for (const TaskEntry* task : ordered) {
            const std::string resolvedProjectName = ResolveTaskProjectName(state, *task);
            const std::string pipelineLabel = ResolveTaskPipelineName(*task);
            const std::string filterText = resolvedProjectName + " " + pipelineLabel + " " + task->title + " " + task->description;
            if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
            if (state.tasksFilterStatus > 0 && NormalizeTaskStatus(task->status) != (state.tasksFilterStatus - 1)) continue;
            if (state.tasksFilterPriority > 0 && NormalizeTaskPriority(task->priority) != (state.tasksFilterPriority - 1)) continue;
            if (!filterProfileId.empty() && !TaskHasAssignee(*task, filterProfileId)) continue;
            const bool quickNoProject = (state.tasksQuickFilter == 5);
            if (quickNoProject) {
                if (!resolvedProjectName.empty()) continue;
            } else if (filterWithoutProject) {
                if (!resolvedProjectName.empty()) continue;
            } else if (!filterProjectId.empty() || !filterProjectName.empty()) {
                bool projectMatch = false;
                if (!filterProjectId.empty() && task->projectId == filterProjectId) projectMatch = true;
                if (!projectMatch && !filterProjectName.empty() && resolvedProjectName == filterProjectName) projectMatch = true;
                if (!projectMatch) continue;
            }
            if (minTs > 0 && task->createdAt < minTs) continue;

            if (state.tasksQuickFilter == 1) {
                if (!state.active || !TaskHasAssignee(*task, state.active->id)) continue;
            } else if (state.tasksQuickFilter == 2) {
                if (task->deadlineAt <= 0 || task->deadlineAt < dayStart || task->deadlineAt >= dayEnd) continue;
            } else if (state.tasksQuickFilter == 3) {
                if (task->deadlineAt <= 0 || task->deadlineAt >= nowSec || NormalizeTaskStatus(task->status) == kTaskStatusDone) continue;
            } else if (state.tasksQuickFilter == 4) {
                const std::int64_t nextWeekEnd = dayStart + 7 * 86400;
                if (task->deadlineAt <= 0 || task->deadlineAt < dayStart || task->deadlineAt >= nextWeekEnd) continue;
            }

            filtered.push_back(task);
        }

        if (state.tasksSortMode == 1) {
            std::sort(filtered.begin(), filtered.end(), [](const TaskEntry* a, const TaskEntry* b) {
                const std::int64_t ad = (a->deadlineAt > 0) ? a->deadlineAt : std::numeric_limits<std::int64_t>::max();
                const std::int64_t bd = (b->deadlineAt > 0) ? b->deadlineAt : std::numeric_limits<std::int64_t>::max();
                if (ad != bd) return ad < bd;
                return a->createdAt > b->createdAt;
            });
        } else if (state.tasksSortMode == 2) {
            std::sort(filtered.begin(), filtered.end(), [](const TaskEntry* a, const TaskEntry* b) {
                const int ap = NormalizeTaskPriority(a->priority);
                const int bp = NormalizeTaskPriority(b->priority);
                if (ap != bp) return ap > bp;
                return a->createdAt > b->createdAt;
            });
        } else {
            std::sort(filtered.begin(), filtered.end(), [](const TaskEntry* a, const TaskEntry* b) {
                return a->createdAt > b->createdAt;
            });
        }
        return filtered;
    };

    std::unordered_map<std::string, std::string> nameById;
    nameById.reserve(state.profiles.size());
    for (const auto& info : state.profiles) {
        nameById[info.id] = info.name;
    }

    std::vector<std::string> projectLabels;
    projectLabels.reserve(state.projects.size() + 1);
    projectLabels.push_back(u8"Без проекта");
    for (const auto& project : state.projects) {
        projectLabels.push_back(project.name);
    }
    std::vector<const char*> projectLabelPtrs;
    projectLabelPtrs.reserve(projectLabels.size());
    for (const auto& label : projectLabels) {
        projectLabelPtrs.push_back(label.c_str());
    }
    state.taskBulkProjectIndex = std::clamp(state.taskBulkProjectIndex, 0,
        static_cast<int>(projectLabelPtrs.size()) - 1);

    std::vector<std::string> pipelineLabels;
    pipelineLabels.reserve(state.pipelineSteps.size() + 1);
    pipelineLabels.push_back(u8"Без этапа");
    for (int i = 0; i < static_cast<int>(state.pipelineSteps.size()); ++i) {
        pipelineLabels.push_back(PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(i)], i));
    }
    std::vector<const char*> pipelineLabelPtrs;
    pipelineLabelPtrs.reserve(pipelineLabels.size());
    for (const auto& label : pipelineLabels) {
        pipelineLabelPtrs.push_back(label.c_str());
    }
    state.taskPlanPipelineIndex = std::clamp(state.taskPlanPipelineIndex, 0,
        static_cast<int>(pipelineLabelPtrs.size()) - 1);
    state.taskBulkPipelineIndex = std::clamp(state.taskBulkPipelineIndex, 0,
        static_cast<int>(pipelineLabelPtrs.size()) - 1);

    const char* quickFilterLabels[] = {u8"Все", u8"Мне назначено", u8"Сегодня", u8"Просрочено", u8"7 дней", u8"Без проекта"};
    state.tasksQuickFilter = std::clamp(state.tasksQuickFilter, 0, static_cast<int>(IM_ARRAYSIZE(quickFilterLabels)) - 1);
    if (BeginCard("tasks_toolbar_card")) {
        DrawCardHeader(u8"Быстрый доступ");
        const bool compactToolbar = ImGui::GetContentRegionAvail().x < 760.0f;
        int requestedQuickFilter = state.tasksQuickFilter;
        if (compactToolbar) {
            ImGui::TextDisabled(u8"Быстрый фильтр");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##tasks_quick_filter", &requestedQuickFilter,
                             quickFilterLabels, IM_ARRAYSIZE(quickFilterLabels))) {
                if (requestedQuickFilter == 1 && !state.active) {
                    SetStatus(state, u8"Выберите активный профиль для фильтра 'Мне назначено'.", 1.0f, 0.75f, 0.35f);
                } else {
                    state.tasksQuickFilter = requestedQuickFilter;
                }
            }
            if (state.isAdmin) {
                const char* createLabel = state.showTaskCreateForm ? u8"Скрыть форму" : u8"Новая задача";
                if (ImGui::Button(createLabel, ImVec2(-1.0f, 0.0f))) {
                    state.showTaskCreateForm = !state.showTaskCreateForm;
                }
            }
            if (ImGui::Button(u8"Сбросить фильтры", ImVec2(-1.0f, 0.0f))) {
                ResetTaskFilters();
            }
        } else if (ImGui::BeginTable("tasks_toolbar_row", state.isAdmin ? 4 : 3,
                                     ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("tasks_toolbar_filter", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("tasks_toolbar_hint", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            if (state.isAdmin) {
                ImGui::TableSetupColumn("tasks_toolbar_create", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            }
            ImGui::TableSetupColumn("tasks_toolbar_reset", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Быстрый фильтр");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##tasks_quick_filter", &requestedQuickFilter,
                             quickFilterLabels, IM_ARRAYSIZE(quickFilterLabels))) {
                if (requestedQuickFilter == 1 && !state.active) {
                    SetStatus(state, u8"Выберите активный профиль для фильтра 'Мне назначено'.", 1.0f, 0.75f, 0.35f);
                } else {
                    state.tasksQuickFilter = requestedQuickFilter;
                }
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(u8"Сейчас");
            ImGui::TextUnformatted(quickFilterLabels[state.tasksQuickFilter]);
            if (state.isAdmin) {
                ImGui::TableSetColumnIndex(2);
                const char* createLabel = state.showTaskCreateForm ? u8"Скрыть форму" : u8"Новая задача";
                if (ImGui::Button(createLabel, ImVec2(-1.0f, 0.0f))) {
                    state.showTaskCreateForm = !state.showTaskCreateForm;
                }
                ImGui::TableSetColumnIndex(3);
            } else {
                ImGui::TableSetColumnIndex(2);
            }
            if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                ResetTaskFilters();
            }
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::Spacing();

    std::string projectsHeader = u8"Проекты в задачах (" + std::to_string(static_cast<int>(state.projects.size())) + ")";
    if (ImGui::CollapsingHeader(projectsHeader.c_str())) {
        if (BeginCard("tasks_projects_bridge")) {
            if (state.tasksFilterProject > 0) {
                ImGui::TextDisabled(u8"Активный проектный фокус");
                if (ImGui::Button(u8"Снять проектный фильтр", ImVec2(-1.0f, 0.0f))) {
                    state.tasksFilterProject = 0;
                }
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
            }

            if (state.projects.empty()) {
                ImGui::TextDisabled(u8"Проектов пока нет.");
            } else if (ImGui::BeginTable("tasks_projects_bridge_table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("bridge_project", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("bridge_state", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bridge_total", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("bridge_actions", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(state.projects.size()); ++i) {
                    const auto& project = state.projects[static_cast<size_t>(i)];
                    const ProjectTaskStats stats = BuildProjectTaskStats(state, project.id);
                    ImGui::TableNextRow();
                    ImGui::PushID(project.id.c_str());
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(project.name.c_str(), state.projectEditIndex == i, ImGuiSelectableFlags_SpanAvailWidth)) {
                        state.projectEditIndex = i;
                        std::snprintf(state.newProjectName.data(), state.newProjectName.size(), "%s", project.name.c_str());
                        std::snprintf(state.newProjectDesc.data(), state.newProjectDesc.size(), "%s", project.description.c_str());
                    }
                    if (!project.description.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", project.description.c_str());
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled(u8"Н:%d  В:%d  Г:%d", stats.created, stats.inProgress, stats.done);
                    if (stats.overdue > 0) {
                        ImGui::SameLine();
                        ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"П:%d", stats.overdue);
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", stats.total);
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton(u8"Фокус")) {
                        state.tasksFilterProject = 2 + i;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(u8"Ред.")) {
                        state.projectEditIndex = i;
                        std::snprintf(state.newProjectName.data(), state.newProjectName.size(), "%s", project.name.c_str());
                        std::snprintf(state.newProjectDesc.data(), state.newProjectDesc.size(), "%s", project.description.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (state.isAdmin) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::Separator();
                if (ImGui::Button(u8"Новый проект", ImVec2(-1.0f, 0.0f))) {
                    state.projectEditIndex = -1;
                    state.newProjectName.fill('\0');
                    state.newProjectDesc.fill('\0');
                }
                if (state.projectEditIndex >= 0 && state.projectEditIndex < static_cast<int>(state.projects.size())) {
                    ImGui::TextDisabled(u8"Редактирование проекта: %s", state.projects[static_cast<size_t>(state.projectEditIndex)].name.c_str());
                } else {
                    ImGui::TextDisabled(u8"Быстрое создание проекта");
                }
                ImGui::TextDisabled(u8"Название");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##tasks_bridge_project_name", state.newProjectName.data(), state.newProjectName.size());
                ImGui::TextDisabled(u8"Описание");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextMultiline("##tasks_bridge_project_desc", state.newProjectDesc.data(),
                                          state.newProjectDesc.size(), ImVec2(-1.0f, 70.0f));
                if (ImGui::Button(u8"Сохранить проект", ImVec2(-1.0f, 0.0f))) {
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
        }
        EndCard();
        ImGui::Spacing();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);

    if (ImGui::CollapsingHeader(u8"Фильтры и экспорт")) {
        DrawCardHeader(u8"Фильтр");
    const float filtersWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilters = filtersWidth < 620.0f;
    std::vector<std::string> profileLabels;
    profileLabels.reserve(state.profiles.size() + 1);
    profileLabels.push_back(u8"Все профили");
    for (const auto& info : state.profiles) {
        profileLabels.push_back("[" + info.id + "] " + info.name);
    }
    std::vector<const char*> profileLabelPtrs;
    profileLabelPtrs.reserve(profileLabels.size());
    for (const auto& label : profileLabels) {
        profileLabelPtrs.push_back(label.c_str());
    }
    state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);

    std::vector<std::string> projectFilterLabels;
    projectFilterLabels.reserve(state.projects.size() + 2);
    projectFilterLabels.push_back(u8"Все проекты");
    projectFilterLabels.push_back(u8"Без проекта");
    for (const auto& project : state.projects) {
        projectFilterLabels.push_back(project.name);
    }
    std::vector<const char*> projectFilterLabelPtrs;
    projectFilterLabelPtrs.reserve(projectFilterLabels.size());
    for (const auto& label : projectFilterLabels) {
        projectFilterLabelPtrs.push_back(label.c_str());
    }
    state.tasksFilterProject = std::clamp(state.tasksFilterProject, 0,
        static_cast<int>(projectFilterLabelPtrs.size()) - 1);

    const char* statusLabelPtrs[] = {u8"Все статусы", u8"Новая", u8"В работе", u8"Выполнена"};
    state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0, static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);

    const char* priorityLabelPtrs[] = {u8"Все приоритеты", u8"Низкий", u8"Средний", u8"Высокий", u8"Критический"};
    state.tasksFilterPriority = std::clamp(state.tasksFilterPriority, 0, static_cast<int>(IM_ARRAYSIZE(priorityLabelPtrs)) - 1);

    const char* sortLabelPtrs[] = {u8"Сначала новые", u8"Ближайший дедлайн", u8"Высокий приоритет"};
    state.tasksSortMode = std::clamp(state.tasksSortMode, 0, static_cast<int>(IM_ARRAYSIZE(sortLabelPtrs)) - 1);

    const int dayOptions[] = {0, 7, 30, 90, 365};
    const char* dayLabels[] = {u8"Все время", u8"7 дней", u8"30 дней", u8"90 дней", u8"365 дней"};
    int dayIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(dayOptions); ++i) {
        if (dayOptions[i] == state.tasksFilterDays) {
            dayIndex = i;
            break;
        }
    }

    if (stackFilters) {
        ImGui::TextDisabled(u8"Поиск");
        ImGui::InputTextWithHint(u8"##tasks_filter", u8"Поиск по проекту, задаче или описанию",
                                 state.filters.tasks.data(), state.filters.tasks.size());
        ImGui::TextDisabled(u8"Профили");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_profile", &state.tasksFilterProfile,
                         profileLabelPtrs.data(), static_cast<int>(profileLabelPtrs.size()))) {
            state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);
        }
        ImGui::TextDisabled(u8"Проекты");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_project", &state.tasksFilterProject,
                         projectFilterLabelPtrs.data(), static_cast<int>(projectFilterLabelPtrs.size()))) {
            state.tasksFilterProject = std::clamp(state.tasksFilterProject, 0,
                static_cast<int>(projectFilterLabelPtrs.size()) - 1);
        }
        ImGui::TextDisabled(u8"Статус");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_status", &state.tasksFilterStatus,
                         statusLabelPtrs, IM_ARRAYSIZE(statusLabelPtrs))) {
            state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0,
                static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);
        }
        ImGui::TextDisabled(u8"Приоритет");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_priority", &state.tasksFilterPriority,
                         priorityLabelPtrs, IM_ARRAYSIZE(priorityLabelPtrs))) {
            state.tasksFilterPriority = std::clamp(state.tasksFilterPriority, 0,
                static_cast<int>(IM_ARRAYSIZE(priorityLabelPtrs)) - 1);
        }
        ImGui::TextDisabled(u8"Сортировка");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_sort", &state.tasksSortMode,
                         sortLabelPtrs, IM_ARRAYSIZE(sortLabelPtrs))) {
            state.tasksSortMode = std::clamp(state.tasksSortMode, 0,
                static_cast<int>(IM_ARRAYSIZE(sortLabelPtrs)) - 1);
        }
        ImGui::TextDisabled(u8"Период");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_days", &dayIndex, dayLabels, IM_ARRAYSIZE(dayLabels))) {
            state.tasksFilterDays = dayOptions[dayIndex];
        }
        ImGui::TextDisabled(u8"Экспорт");
        if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
            std::filesystem::path outPath;
            int exported = 0;
            std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
            if (!ExportTasksCsv(state, filtered, nameById, outPath, exported)) {
                SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
            } else {
                std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                msg += outPath.string();
                msg += ")";
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
            }
        }
        if (ImGui::Button(u8"Экспорт TXT", ImVec2(-1.0f, 0.0f))) {
            std::filesystem::path outPath;
            int exported = 0;
            std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
            if (!ExportTasksTxt(state, filtered, nameById, outPath, exported)) {
                SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
            } else {
                std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                msg += outPath.string();
                msg += ")";
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
            }
        }
    } else if (ImGui::BeginTable("tasks_filters", 6, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("filter_search", ImGuiTableColumnFlags_WidthStretch, 1.05f);
        ImGui::TableSetupColumn("filter_profile", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableSetupColumn("filter_project", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableSetupColumn("filter_status", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn("filter_priority", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::TextDisabled(u8"Поиск");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##tasks_filter", u8"Поиск по проекту, задаче или описанию",
                                 state.filters.tasks.data(), state.filters.tasks.size());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"Профили");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_profile", &state.tasksFilterProfile,
                         profileLabelPtrs.data(), static_cast<int>(profileLabelPtrs.size()))) {
            state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled(u8"Проекты");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_project", &state.tasksFilterProject,
                         projectFilterLabelPtrs.data(), static_cast<int>(projectFilterLabelPtrs.size()))) {
            state.tasksFilterProject = std::clamp(state.tasksFilterProject, 0,
                static_cast<int>(projectFilterLabelPtrs.size()) - 1);
        }
        ImGui::TableSetColumnIndex(4);
        ImGui::TextDisabled(u8"Статус");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_status", &state.tasksFilterStatus,
                         statusLabelPtrs, IM_ARRAYSIZE(statusLabelPtrs))) {
            state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0,
                static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);
        }
        ImGui::TableSetColumnIndex(5);
        ImGui::TextDisabled(u8"Приоритет");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_priority", &state.tasksFilterPriority,
                         priorityLabelPtrs, IM_ARRAYSIZE(priorityLabelPtrs))) {
            state.tasksFilterPriority = std::clamp(state.tasksFilterPriority, 0,
                static_cast<int>(IM_ARRAYSIZE(priorityLabelPtrs)) - 1);
        }
        ImGui::EndTable();
        if (ImGui::BeginTable("tasks_filters_extra", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("filter_period", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("filter_sort", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("filter_actions", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::Chart);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Период");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(u8"##tasks_days", &dayIndex, dayLabels, IM_ARRAYSIZE(dayLabels))) {
                state.tasksFilterDays = dayOptions[dayIndex];
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(u8"##tasks_sort", &state.tasksSortMode,
                             sortLabelPtrs, IM_ARRAYSIZE(sortLabelPtrs))) {
                state.tasksSortMode = std::clamp(state.tasksSortMode, 0,
                    static_cast<int>(IM_ARRAYSIZE(sortLabelPtrs)) - 1);
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled(u8"Экспорт");
            if (ImGui::Button(u8"CSV", ImVec2(-1.0f, 0.0f))) {
                std::filesystem::path outPath;
                int exported = 0;
                std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
                if (!ExportTasksCsv(state, filtered, nameById, outPath, exported)) {
                    SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
                } else {
                    std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                    msg += outPath.string();
                    msg += ")";
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
                }
            }
            if (ImGui::Button(u8"TXT", ImVec2(-1.0f, 0.0f))) {
                std::filesystem::path outPath;
                int exported = 0;
                std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
                if (!ExportTasksTxt(state, filtered, nameById, outPath, exported)) {
                    SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
                } else {
                    std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                    msg += outPath.string();
                    msg += ")";
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
                }
            }
            ImGui::EndTable();
        }
    }
    }

    std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();

    for (auto it = state.taskSelectionIds.begin(); it != state.taskSelectionIds.end();) {
        const bool exists = std::any_of(state.tasks.begin(), state.tasks.end(),
                                        [&](const TaskEntry& item) { return item.id == *it; });
        if (!exists) {
            it = state.taskSelectionIds.erase(it);
        } else {
            ++it;
        }
    }

    if (state.taskBulkAssignees.size() != state.profiles.size()) {
        std::vector<char> next(state.profiles.size(), 0);
        for (size_t i = 0; i < std::min(next.size(), state.taskBulkAssignees.size()); ++i) {
            next[i] = state.taskBulkAssignees[i] ? 1 : 0;
        }
        state.taskBulkAssignees = std::move(next);
    }

    const std::int64_t nowSec = NowSeconds();
    std::vector<const TaskEntry*> overdueDeadlines;
    std::vector<const TaskEntry*> upcoming2h;
    std::vector<const TaskEntry*> upcoming24h;
    overdueDeadlines.reserve(state.tasks.size());
    upcoming2h.reserve(state.tasks.size());
    upcoming24h.reserve(state.tasks.size());
    std::unordered_set<std::string> reminderCandidates;
    reminderCandidates.reserve(state.tasks.size());

    for (const auto& task : state.tasks) {
        if (task.id.empty()) continue;
        if (task.deadlineAt <= 0) continue;
        if (NormalizeTaskStatus(task.status) == kTaskStatusDone) continue;
        reminderCandidates.insert(task.id);
        if (task.deadlineAt < nowSec) {
            overdueDeadlines.push_back(&task);
        } else if (task.deadlineAt <= (nowSec + 2 * 3600)) {
            upcoming2h.push_back(&task);
        } else if (task.deadlineAt <= (nowSec + 24 * 3600)) {
            upcoming24h.push_back(&task);
        }
    }
    auto byDeadlineAsc = [](const TaskEntry* a, const TaskEntry* b) {
        if (a->deadlineAt != b->deadlineAt) return a->deadlineAt < b->deadlineAt;
        return a->createdAt > b->createdAt;
    };
    std::sort(overdueDeadlines.begin(), overdueDeadlines.end(), byDeadlineAsc);
    std::sort(upcoming2h.begin(), upcoming2h.end(), byDeadlineAsc);
    std::sort(upcoming24h.begin(), upcoming24h.end(), byDeadlineAsc);

    auto pruneReminderSet = [&](std::unordered_set<std::string>& setRef) {
        for (auto it = setRef.begin(); it != setRef.end();) {
            if (reminderCandidates.find(*it) == reminderCandidates.end()) {
                it = setRef.erase(it);
            } else {
                ++it;
            }
        }
    };
    pruneReminderSet(state.tasksReminderShown24h);
    pruneReminderSet(state.tasksReminderShown2h);

    if (state.tasksReminderLastAt <= 0 || (nowSec - state.tasksReminderLastAt) >= 30) {
        state.tasksReminderLastAt = nowSec;
        const TaskEntry* notifyTask = nullptr;
        bool criticalWindow = false;
        for (const TaskEntry* task : upcoming2h) {
            if (!task) continue;
            if (state.tasksReminderShown2h.find(task->id) == state.tasksReminderShown2h.end()) {
                notifyTask = task;
                criticalWindow = true;
                break;
            }
        }
        if (!notifyTask) {
            for (const TaskEntry* task : upcoming24h) {
                if (!task) continue;
                if (state.tasksReminderShown24h.find(task->id) == state.tasksReminderShown24h.end()) {
                    notifyTask = task;
                    criticalWindow = false;
                    break;
                }
            }
        }
        if (notifyTask) {
            const std::string taskTitle = TaskDisplayTitle(*notifyTask).empty()
                ? notifyTask->id
                : TaskDisplayTitle(*notifyTask);
            const std::string deadlineText = FormatTimestamp(notifyTask->deadlineAt, "%d.%m.%Y %H:%M");
            std::string msg;
            if (criticalWindow) {
                msg = u8"Скоро дедлайн (<2ч): " + taskTitle + u8" до " + deadlineText;
                SetStatus(state, msg, 1.0f, 0.75f, 0.35f, 1.0f, AppLogLevel::Warning, u8"Задачи");
                state.tasksReminderShown2h.insert(notifyTask->id);
                state.tasksReminderShown24h.insert(notifyTask->id);
            } else {
                msg = u8"Дедлайн в течение 24ч: " + taskTitle + u8" до " + deadlineText;
                SetStatus(state, msg, 0.75f, 0.85f, 1.0f, 1.0f, AppLogLevel::Info, u8"Задачи");
                state.tasksReminderShown24h.insert(notifyTask->id);
            }
        }
    }

    std::string deadlinesHeader = u8"Скоро дедлайн (" +
        std::to_string(static_cast<int>(overdueDeadlines.size() + upcoming2h.size() + upcoming24h.size())) + ")";
    if (ImGui::CollapsingHeader(deadlinesHeader.c_str())) {
        if (BeginCard("tasks_upcoming_deadlines")) {
        ImGui::TextDisabled(u8"Просрочено: %d | <2ч: %d | <24ч: %d",
                            static_cast<int>(overdueDeadlines.size()),
                            static_cast<int>(upcoming2h.size()),
                            static_cast<int>(upcoming24h.size()));

        auto DrawDeadlineList = [&](const char* caption, const std::vector<const TaskEntry*>& items,
                                    const ImVec4& color, int maxItems) {
            if (items.empty()) return;
            ImGui::Spacing();
            ImGui::TextColored(color, "%s", caption);
            const int limit = std::min(maxItems, static_cast<int>(items.size()));
            for (int i = 0; i < limit; ++i) {
                const TaskEntry* task = items[static_cast<size_t>(i)];
                if (!task) continue;
                const std::string title = TaskDisplayTitle(*task).empty() ? task->id : TaskDisplayTitle(*task);
                const std::string deadlineText = FormatTimestamp(task->deadlineAt, "%d.%m.%Y %H:%M");
                std::string leftText;
                if (task->deadlineAt < nowSec) {
                    leftText = u8"просрочено";
                } else {
                    const std::int64_t leftSec = task->deadlineAt - nowSec;
                    const int leftHours = static_cast<int>(leftSec / 3600);
                    const int leftMinutes = static_cast<int>((leftSec % 3600) / 60);
                    std::ostringstream leftStream;
                    leftStream.imbue(std::locale::classic());
                    leftStream << leftHours << u8"ч " << std::setw(2) << std::setfill('0') << leftMinutes << u8"м";
                    leftText = leftStream.str();
                }
                ImGui::BulletText("%s | %s | %s | %s",
                                  title.c_str(),
                                  deadlineText.c_str(),
                                  TaskPriorityLabel(task->priority),
                                  leftText.c_str());
            }
            if (static_cast<int>(items.size()) > limit) {
                ImGui::TextDisabled(u8"Еще: %d", static_cast<int>(items.size()) - limit);
            }
        };

        DrawDeadlineList(u8"Просроченные", overdueDeadlines, LogLevelColor(AppLogLevel::Error), 3);
        DrawDeadlineList(u8"Ближайшие 2 часа", upcoming2h, LogLevelColor(AppLogLevel::Warning), 4);
        DrawDeadlineList(u8"Ближайшие 24 часа", upcoming24h, LogLevelColor(AppLogLevel::Info), 4);

        if (overdueDeadlines.empty() && upcoming2h.empty() && upcoming24h.empty()) {
            ImGui::TextDisabled(u8"Критичных дедлайнов нет.");
        }
        }
        EndCard();
        ImGui::Spacing();
    }

    std::string auditHeader = u8"Последние изменения (" +
        std::to_string(static_cast<int>(state.taskAudit.size())) + ")";
    if (ImGui::CollapsingHeader(auditHeader.c_str())) {
        if (BeginCard("tasks_audit_card")) {
        constexpr int kTaskAuditPreview = 8;
        if (state.taskAudit.empty()) {
            ImGui::TextDisabled(u8"Изменений пока нет.");
        } else if (ImGui::BeginTable("tasks_audit_table", 5,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("audit_time", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("audit_actor", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("audit_task", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("audit_field", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("audit_change", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableHeadersRow();
            int shown = 0;
            for (auto it = state.taskAudit.rbegin(); it != state.taskAudit.rend() && shown < kTaskAuditPreview; ++it, ++shown) {
                const TaskAuditEntry& row = *it;
                std::string taskLabel = row.taskId;
                auto taskIt = std::find_if(state.tasks.begin(), state.tasks.end(),
                    [&](const TaskEntry& task) { return task.id == row.taskId; });
                if (taskIt != state.tasks.end()) {
                    const std::string title = TaskDisplayTitle(*taskIt);
                    if (!title.empty()) taskLabel = title;
                }
                const std::string timeLabel = row.timestamp > 0
                    ? FormatTimestamp(row.timestamp, "%d.%m %H:%M")
                    : std::string(u8"—");
                std::string changeLabel = row.oldValue;
                changeLabel += " -> ";
                changeLabel += row.newValue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(timeLabel.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.actor.empty() ? u8"system" : row.actor.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(taskLabel.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.field.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(changeLabel.c_str());
            }
            ImGui::EndTable();
        }
        }
        EndCard();
        ImGui::Spacing();
    }

    auto ApplyBulkStatus = [&]() -> bool {
        AppMutationResult mutation = AppBulkUpdateTaskStatus(
            state.storageDir, state.tasks, state.taskSelectionIds, state.taskBulkStatus, ResolveTaskActor(), &state.taskAudit);
        if (!mutation.ok) {
            SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (!mutation.changed) {
            if (mutation.skippedCount > 0) {
                std::string msg = u8"Пропущено переходов статуса: " + std::to_string(mutation.skippedCount);
                SetStatus(state, msg, 1.0f, 0.75f, 0.35f);
            } else {
                SetStatus(state, u8"Нет задач для изменения статуса.", 1.0f, 0.75f, 0.35f);
            }
            return false;
        }
        std::string msg = u8"Статус обновлен для задач: " + std::to_string(mutation.changedCount);
        if (mutation.skippedCount > 0) {
            msg += u8" (пропущено: " + std::to_string(mutation.skippedCount) + ")";
        }
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
        return true;
    };

    auto ApplyBulkPriority = [&]() -> bool {
        AppMutationResult mutation = AppBulkUpdateTaskPriority(
            state.storageDir, state.tasks, state.taskSelectionIds, state.taskBulkPriority, ResolveTaskActor(), &state.taskAudit);
        if (!mutation.ok) {
            SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (!mutation.changed) {
            SetStatus(state, u8"Нет задач для изменения приоритета.", 1.0f, 0.75f, 0.35f);
            return false;
        }
        std::string msg = u8"Приоритет обновлен для задач: " + std::to_string(mutation.changedCount);
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
        return true;
    };

    auto ApplyBulkProject = [&]() -> bool {
        const std::string nextProjectId = (state.taskBulkProjectIndex > 0 &&
            (state.taskBulkProjectIndex - 1) < static_cast<int>(state.projects.size()))
            ? state.projects[state.taskBulkProjectIndex - 1].id
            : std::string();
        const std::string nextProjectName = (state.taskBulkProjectIndex > 0 &&
            (state.taskBulkProjectIndex - 1) < static_cast<int>(state.projects.size()))
            ? state.projects[state.taskBulkProjectIndex - 1].name
            : std::string();
        AppMutationResult mutation = AppBulkUpdateTaskProject(
            state.storageDir, state.tasks, state.taskSelectionIds, nextProjectId, nextProjectName, ResolveTaskActor(), &state.taskAudit);
        if (!mutation.ok) {
            SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (!mutation.changed) {
            SetStatus(state, u8"Нет задач для изменения проекта.", 1.0f, 0.75f, 0.35f);
            return false;
        }
        std::string msg = u8"Проект обновлен для задач: " + std::to_string(mutation.changedCount);
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
        return true;
    };

    auto ApplyBulkPipeline = [&]() -> bool {
        const std::string nextPipelineId = (state.taskBulkPipelineIndex > 0 &&
            (state.taskBulkPipelineIndex - 1) < static_cast<int>(state.pipelineSteps.size()))
            ? state.pipelineSteps[static_cast<size_t>(state.taskBulkPipelineIndex - 1)].id
            : std::string();
        const std::string nextPipelineName = (state.taskBulkPipelineIndex > 0 &&
            (state.taskBulkPipelineIndex - 1) < static_cast<int>(state.pipelineSteps.size()))
            ? PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(state.taskBulkPipelineIndex - 1)],
                                   state.taskBulkPipelineIndex - 1)
            : std::string();
        AppMutationResult mutation = AppBulkUpdateTaskPipelineStep(
            state.storageDir, state.tasks, state.taskSelectionIds, nextPipelineId, nextPipelineName,
            ResolveTaskActor(), &state.taskAudit);
        if (!mutation.ok) {
            SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (!mutation.changed) {
            SetStatus(state, u8"Нет задач для изменения этапа.", 1.0f, 0.75f, 0.35f);
            return false;
        }
        std::string msg = u8"Этап обновлен для задач: " + std::to_string(mutation.changedCount);
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
        return true;
    };

    auto ApplyBulkDeadline = [&](bool clearDeadline) -> bool {
        const std::string deadlineInput = clearDeadline ? std::string() : TrimStringGui(state.taskBulkDeadline.data());
        std::optional<std::int64_t> deadlineAt;
        if (!deadlineInput.empty()) {
            std::int64_t parsedDeadline = 0;
            if (!AppParseTaskDeadlineInput(deadlineInput, parsedDeadline)) {
                SetStatus(state, u8"Формат дедлайна: YYYY-MM-DD HH:MM.", 1.0f, 0.45f, 0.45f);
                return false;
            }
            deadlineAt = parsedDeadline;
        }
        AppMutationResult mutation = AppBulkUpdateTaskDeadline(
            state.storageDir, state.tasks, state.taskSelectionIds, deadlineAt, ResolveTaskActor(), &state.taskAudit);
        if (!mutation.ok) {
            SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (!mutation.changed) {
            SetStatus(state, u8"Нет задач для изменения дедлайна.", 1.0f, 0.75f, 0.35f);
            return false;
        }
        std::string msg = u8"Дедлайн обновлен для задач: " + std::to_string(mutation.changedCount);
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
        return true;
    };

    auto ApplyBulkAssignees = [&]() -> bool {
        std::vector<std::string> assignees;
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            if (i < static_cast<int>(state.taskBulkAssignees.size()) && state.taskBulkAssignees[i] && !state.profiles[i].archived) {
                assignees.push_back(state.profiles[i].id);
            }
        }
        AppMutationResult mutation = AppBulkUpdateTaskAssignees(
            state.storageDir, state.tasks, state.taskSelectionIds, assignees, ResolveTaskActor(), &state.taskAudit);
        if (!mutation.ok) {
            const bool warn = (mutation.errorMessage == u8"Выберите хотя бы одного исполнителя.");
            SetStatus(state, mutation.errorMessage, 1.0f, warn ? 0.45f : 0.45f, warn ? 0.45f : 0.45f);
            return false;
        }
        if (!mutation.changed) {
            if (mutation.skippedCount > 0) {
                std::string msg = u8"Пропущено задач с XP-участниками: " + std::to_string(mutation.skippedCount);
                SetStatus(state, msg, 1.0f, 0.75f, 0.35f);
            } else {
                SetStatus(state, u8"Нет задач для изменения исполнителей.", 1.0f, 0.75f, 0.35f);
            }
            return false;
        }
        std::string msg = u8"Исполнители обновлены для задач: " + std::to_string(mutation.changedCount);
        if (mutation.skippedCount > 0) {
            msg += u8" (пропущено: " + std::to_string(mutation.skippedCount) + ")";
        }
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
        return true;
    };

    if (state.isAdmin && !state.taskSelectionIds.empty()) {
        std::string bulkHeader = u8"Массовые действия (" +
            std::to_string(static_cast<int>(state.taskSelectionIds.size())) + ")";
        if (ImGui::CollapsingHeader(bulkHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (BeginCard("tasks_bulk_actions")) {
            const int selectedCount = static_cast<int>(state.taskSelectionIds.size());
            ImGui::TextDisabled(u8"Выбрано: %d", selectedCount);
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Видимые")) {
                for (const TaskEntry* task : filtered) {
                    if (task) state.taskSelectionIds.insert(task->id);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Снять")) {
                state.taskSelectionIds.clear();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Удалить")) {
                state.taskBulkDeletePopupRequest = true;
            }

            const bool hasSelection = !state.taskSelectionIds.empty();
            if (!hasSelection) {
                ImGui::TextDisabled(u8"Отметьте задачи чекбоксами в таблице.");
            }
            if (!hasSelection) ImGui::BeginDisabled();

            const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
            const char* priorityLabels[] = {u8"Низкий", u8"Средний", u8"Высокий", u8"Критический"};
            state.taskBulkStatus = std::clamp(state.taskBulkStatus, kTaskStatusNew, kTaskStatusDone);
            state.taskBulkPriority = std::clamp(state.taskBulkPriority, kTaskPriorityLow, kTaskPriorityCritical);

            if (ImGui::BeginTable("tasks_bulk_compact_row1", 8, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("bulk_status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bulk_status_btn", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("bulk_priority", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bulk_priority_btn", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("bulk_project", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bulk_project_btn", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("bulk_pipeline", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("bulk_pipeline_btn", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_status", &state.taskBulkStatus, statusLabels, IM_ARRAYSIZE(statusLabels));

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button(u8"Статус", ImVec2(-1.0f, 0.0f))) {
                    ApplyBulkStatus();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_priority", &state.taskBulkPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels));

                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button(u8"Приоритет", ImVec2(-1.0f, 0.0f))) {
                    ApplyBulkPriority();
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_project", &state.taskBulkProjectIndex,
                             projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()));

                ImGui::TableSetColumnIndex(5);
                if (ImGui::Button(u8"Проект", ImVec2(-1.0f, 0.0f))) {
                    ApplyBulkProject();
                }

                ImGui::TableSetColumnIndex(6);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_pipeline", &state.taskBulkPipelineIndex,
                             pipelineLabelPtrs.data(), static_cast<int>(pipelineLabelPtrs.size()));

                ImGui::TableSetColumnIndex(7);
                if (ImGui::Button(u8"Этап", ImVec2(-1.0f, 0.0f))) {
                    ApplyBulkPipeline();
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginTable("tasks_bulk_compact_row2", 4, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("bulk_deadline", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("bulk_deadline_set", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("bulk_deadline_clear", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("bulk_assignees", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##task_bulk_deadline", u8"YYYY-MM-DD HH:MM",
                                         state.taskBulkDeadline.data(), state.taskBulkDeadline.size());

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button(u8"Дедлайн", ImVec2(-1.0f, 0.0f))) {
                    ApplyBulkDeadline(false);
                }

                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Очистить", ImVec2(-1.0f, 0.0f))) {
                    ApplyBulkDeadline(true);
                }

                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button(u8"Исполнители", ImVec2(-1.0f, 0.0f))) {
                    ImGui::OpenPopup("tasks_bulk_assignees_popup");
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginPopup("tasks_bulk_assignees_popup")) {
                ImGui::TextDisabled(u8"Массовое назначение исполнителей");
                if (ImGui::BeginTable("bulk_assignees_table", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("bulk_assignee_col1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("bulk_assignee_col2", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                        const auto& info = state.profiles[i];
                        if (i % 2 == 0) {
                            ImGui::TableNextRow();
                        }
                        ImGui::TableSetColumnIndex(i % 2);
                        bool checked = (i < static_cast<int>(state.taskBulkAssignees.size()) && state.taskBulkAssignees[i] != 0);
                        if (info.archived) ImGui::BeginDisabled();
                        ImGui::PushID(info.id.c_str());
                        if (ImGui::Checkbox("##bulk_assign", &checked)) {
                            if (i < static_cast<int>(state.taskBulkAssignees.size())) {
                                state.taskBulkAssignees[i] = checked ? 1 : 0;
                            }
                        }
                        ImGui::PopID();
                        ImGui::SameLine();
                        ImGui::TextUnformatted(info.name.c_str());
                        if (info.archived) {
                            ImGui::SameLine();
                            ImGui::TextDisabled(u8"(архив)");
                            ImGui::EndDisabled();
                        }
                    }
                    ImGui::EndTable();
                }
                if (ImGui::Button(u8"Применить", ImVec2(120.0f, 0.0f))) {
                    if (ApplyBulkAssignees()) {
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"Отмена", ImVec2(120.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (!hasSelection) ImGui::EndDisabled();
            }
            EndCard();
            ImGui::Spacing();
        }
    }

    std::array<int, Profile::kCategoryCount> categoryCounts{};
    const int filteredCount = static_cast<int>(filtered.size());
    int topIdx = 0;
    int topCount = 0;
    int minIdx = -1;
    int minCount = 0;
    int activeCategories = 0;
    int scoreSum = 0;
    int poolSum = 0;
    std::unordered_set<std::string> uniqueProfiles;
    std::string lastStamp;
    if (!filtered.empty()) {
        lastStamp = FormatTimestamp(filtered.front()->createdAt, "%Y-%m-%d %H:%M");
        uniqueProfiles.reserve(state.profiles.size());
        for (const TaskEntry* task : filtered) {
            int idx = std::clamp(task->category, 0, static_cast<int>(Profile::kCategoryCount - 1));
            categoryCounts[idx] += 1;
            if (categoryCounts[idx] >= topCount) {
                topCount = categoryCounts[idx];
                topIdx = idx;
            }
            scoreSum += task->score;
            poolSum += task->basePool;
            if (!task->participants.empty()) {
                for (const auto& participant : task->participants) {
                    uniqueProfiles.insert(participant.profileId);
                }
            } else {
                for (const auto& assignee : task->assignees) {
                    uniqueProfiles.insert(assignee);
                }
            }
        }
        minIdx = topIdx;
        minCount = topCount;
        for (size_t i = 0; i < categoryCounts.size(); ++i) {
            if (categoryCounts[i] <= 0) {
                continue;
            }
            activeCategories += 1;
            if (categoryCounts[i] < minCount) {
                minCount = categoryCounts[i];
                minIdx = static_cast<int>(i);
            }
        }
    }
    const bool hasAttention = (activeCategories >= 2) && (topCount - minCount >= 2) && (minIdx >= 0);

    std::string summaryHeader = u8"Сводка (" + std::to_string(filteredCount) + ")";
    if (ImGui::CollapsingHeader(summaryHeader.c_str())) {
        if (BeginCard("tasks_summary_card")) {
        if (filtered.empty()) {
            ImGui::TextDisabled(u8"Нет задач по фильтру.");
        } else {
            std::ostringstream focusStream;
            focusStream.imbue(std::locale::classic());
            float pct = filteredCount > 0 ? (static_cast<float>(topCount) / static_cast<float>(filteredCount)) * 100.0f : 0.0f;
            focusStream << std::fixed << std::setprecision(0) << pct;
            std::string focusText = std::string(ClassificationLabel(topIdx)) +
                " (" + std::to_string(topCount) + ", " + focusStream.str() + "%)";
            const float avgScore = filteredCount > 0
                ? static_cast<float>(scoreSum) / static_cast<float>(filteredCount)
                : 0.0f;
            const float avgPool = filteredCount > 0
                ? static_cast<float>(poolSum) / static_cast<float>(filteredCount)
                : 0.0f;
            if (ImGui::BeginTable("tasks_summary_table", 5, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("sum_total", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("sum_last", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("sum_focus", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("sum_score", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("sum_people", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Всего");
                ImGui::Text("%d", filteredCount);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Последняя");
                ImGui::Text("%s", lastStamp.empty() ? "-" : lastStamp.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Фокус");
                ImGui::Text("%s", focusText.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Оценка ср.");
                ImGui::Text("%.1f", avgScore);
                ImGui::TableSetColumnIndex(4);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Участники");
                ImGui::Text("%d", static_cast<int>(uniqueProfiles.size()));
                ImGui::EndTable();
            }
            if (hasAttention) {
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled(u8"Зона внимания");
                ImGui::SameLine();
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s (%d)",
                                   ClassificationLabel(minIdx), minCount);
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"Средний пул XP: %.0f", avgPool);

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextDisabled(u8"Активность за 14 дней");
            constexpr int kDays = 14;
            std::array<float, kDays> dayCounts{};
            const std::int64_t nowSec = NowSeconds();
            const std::int64_t today = nowSec / 86400;
            const std::int64_t startDay = today - (kDays - 1);
            int maxCount = 0;
            for (const TaskEntry* task : filtered) {
                if (!task) continue;
                const std::int64_t day = task->createdAt / 86400;
                const int idx = static_cast<int>(day - startDay);
                if (idx < 0 || idx >= kDays) continue;
                dayCounts[idx] += 1.0f;
                maxCount = std::max(maxCount, static_cast<int>(dayCounts[idx]));
            }
            if (maxCount == 0) {
                ImGui::TextDisabled(u8"Нет активности за период.");
            } else {
                ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
                accent.w = 1.0f;
                DrawSparkline("tasks_activity", dayCounts.data(), kDays,
                              static_cast<float>(maxCount), accent, 36.0f);
            }
        }
        }
        EndCard();
        ImGui::Spacing();
    }

    const float taskLayoutWidth = ImGui::GetContentRegionAvail().x;
    const float taskLayoutGap = style.ItemSpacing.x;
    const float detailPanelWidth = std::clamp(taskLayoutWidth * 0.30f, 320.0f, 400.0f);
    const bool useTaskSplitView = (taskLayoutWidth - detailPanelWidth - taskLayoutGap) >= 1040.0f;
    if (useTaskSplitView) {
        const float listPanelWidth = std::max(0.0f, taskLayoutWidth - detailPanelWidth - taskLayoutGap);
        ImGui::BeginChild("tasks_master_column", ImVec2(listPanelWidth, 0.0f), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar);
    }

    int visibleCount = 0;
    const bool allowBulkSelection = state.isAdmin;
    const int tableColumns = allowBulkSelection ? 9 : 8;
    if (allowBulkSelection && !state.taskSelectionIds.empty()) {
        ImGui::TextDisabled(u8"Выбрано задач: %d", static_cast<int>(state.taskSelectionIds.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Снять выбор")) {
            state.taskSelectionIds.clear();
        }
        ImGui::Spacing();
    }
    if (ImGui::BeginTable("tasks_table", tableColumns, ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp)) {
        if (allowBulkSelection) {
            ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        }
        ImGui::TableSetupColumn(u8"Дата", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Дедлайн", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(u8"Этап", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(u8"Исполнители", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(u8"Приоритет", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Статус", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();
        const std::int64_t nowSec = NowSeconds();
        for (const TaskEntry* task : filtered) {
            ++visibleCount;
            ImGui::TableNextRow();
            const bool overdue = task->deadlineAt > 0 && task->deadlineAt < nowSec &&
                                 NormalizeTaskStatus(task->status) != kTaskStatusDone;
            if (overdue) {
                ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                rowHighlight.w = std::clamp(rowHighlight.w * 0.16f, 0.08f, 0.22f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
            }
            TaskEntry* editableTask = state.isAdmin ? FindTaskMutable(task->id) : nullptr;
            int col = 0;
            ImGui::PushID(task->id.c_str());

            if (allowBulkSelection) {
                ImGui::TableSetColumnIndex(col++);
                bool selected = state.taskSelectionIds.find(task->id) != state.taskSelectionIds.end();
                if (ImGui::Checkbox("##row_select", &selected)) {
                    if (selected) {
                        state.taskSelectionIds.insert(task->id);
                    } else {
                        state.taskSelectionIds.erase(task->id);
                    }
                }
            }

            const bool isExpanded = (state.tasksExpandedId == task->id);

            ImGui::TableSetColumnIndex(col++);
            const std::string stamp = FormatTimestamp(task->createdAt, "%d.%m.%Y %H:%M");
            ImGui::TextDisabled("%s", stamp.empty() ? "-" : stamp.c_str());

            ImGui::TableSetColumnIndex(col++);
            if (state.isAdmin && editableTask) {
                const std::string deadlineText = task->deadlineAt > 0 ? FormatTimestamp(task->deadlineAt, "%Y-%m-%d %H:%M") : std::string();
                std::string buttonLabel = deadlineText.empty() ? std::string(u8"Установить") : deadlineText;
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    std::snprintf(state.taskInlineDeadlineBuffer.data(), state.taskInlineDeadlineBuffer.size(), "%s", deadlineText.c_str());
                    state.taskInlineDeadlineEditId = task->id;
                    ImGui::OpenPopup("deadline_popup");
                }
                if (ImGui::BeginPopup("deadline_popup")) {
                    ImGui::TextDisabled(u8"Дедлайн (YYYY-MM-DD HH:MM)");
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::InputTextWithHint("##deadline_input", u8"2026-02-14 18:00",
                                             state.taskInlineDeadlineBuffer.data(), state.taskInlineDeadlineBuffer.size());
                    if (ImGui::Button(u8"Сохранить", ImVec2(100.0f, 0.0f))) {
                        if (UpdateTaskDeadline(task->id, state.taskInlineDeadlineBuffer.data())) {
                            state.taskInlineDeadlineEditId.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Очистить", ImVec2(100.0f, 0.0f))) {
                        if (UpdateTaskDeadline(task->id, "")) {
                            state.taskInlineDeadlineEditId.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Отмена", ImVec2(100.0f, 0.0f))) {
                        state.taskInlineDeadlineEditId.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            } else if (task->deadlineAt > 0) {
                const std::string deadline = FormatTimestamp(task->deadlineAt, "%d.%m.%Y %H:%M");
                if (overdue) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", deadline.c_str());
                } else {
                    ImGui::TextUnformatted(deadline.c_str());
                }
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableSetColumnIndex(col++);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            const std::string projectName = ResolveTaskProjectName(state, *task);
            if (state.isAdmin && editableTask) {
                int projectIndex = 0;
                if (!editableTask->projectId.empty()) {
                    for (int i = 0; i < static_cast<int>(state.projects.size()); ++i) {
                        if (state.projects[i].id == editableTask->projectId) {
                            projectIndex = i + 1;
                            break;
                        }
                    }
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_project_row", &projectIndex,
                                 projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()))) {
                    UpdateTaskProject(task->id, projectIndex);
                }
            } else if (projectName.empty()) {
                ImGui::TextDisabled(u8"Без проекта");
            } else {
                ImGui::TextUnformatted(projectName.c_str());
            }

            ImGui::TableSetColumnIndex(col++);
            const std::string pipelineName = ResolveTaskPipelineName(*task);
            if (state.isAdmin && editableTask) {
                int pipelineIndex = 0;
                if (!editableTask->pipelineStepId.empty()) {
                    const int currentPipelineIndex = FindPipelineStepIndexById(state, editableTask->pipelineStepId);
                    if (currentPipelineIndex >= 0) {
                        pipelineIndex = currentPipelineIndex + 1;
                    }
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_pipeline_row", &pipelineIndex,
                                 pipelineLabelPtrs.data(), static_cast<int>(pipelineLabelPtrs.size()))) {
                    UpdateTaskPipeline(task->id, pipelineIndex);
                }
            } else if (pipelineName.empty()) {
                ImGui::TextDisabled(u8"Без этапа");
            } else {
                ImGui::TextUnformatted(pipelineName.c_str());
            }

            ImGui::TableSetColumnIndex(col++);
            std::string taskTitle = task->title;
            if (taskTitle.empty()) taskTitle = u8"Без названия";
            std::string taskTitleLabel = taskTitle + "##task_title";
            if (ImGui::Selectable(taskTitleLabel.c_str(), isExpanded,
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAvailWidth)) {
                state.tasksExpandedId = isExpanded ? std::string() : task->id;
            }
            if (!task->description.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(task->description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(col++);
            const std::string participants = BuildParticipantLabel(*task, nameById);
            if (participants.empty()) {
                ImGui::TextDisabled(u8"Не назначена");
            } else {
                ImGui::TextUnformatted(participants.c_str());
            }
            if (state.isAdmin && editableTask && task->participants.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Изм.")) {
                    state.taskInlineAssigneeEditId = task->id;
                    if (state.taskInlineAssigneeFlags.size() != state.profiles.size()) {
                        state.taskInlineAssigneeFlags.assign(state.profiles.size(), 0);
                    }
                    for (size_t i = 0; i < state.profiles.size(); ++i) {
                        const auto& info = state.profiles[i];
                        const bool assigned = std::find(editableTask->assignees.begin(), editableTask->assignees.end(), info.id) != editableTask->assignees.end();
                        state.taskInlineAssigneeFlags[i] = assigned ? 1 : 0;
                    }
                    ImGui::OpenPopup("assignee_popup");
                }
                if (ImGui::BeginPopup("assignee_popup")) {
                    ImGui::TextDisabled(u8"Исполнители");
                    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                        const auto& info = state.profiles[i];
                        bool checked = (i < static_cast<int>(state.taskInlineAssigneeFlags.size()) && state.taskInlineAssigneeFlags[i] != 0);
                        if (info.archived) ImGui::BeginDisabled();
                        if (ImGui::Checkbox((std::string("##row_assignee_") + info.id).c_str(), &checked)) {
                            if (i < static_cast<int>(state.taskInlineAssigneeFlags.size())) {
                                state.taskInlineAssigneeFlags[i] = checked ? 1 : 0;
                            }
                        }
                        ImGui::SameLine();
                        ImGui::TextUnformatted(info.name.c_str());
                        if (info.archived) {
                            ImGui::SameLine();
                            ImGui::TextDisabled(u8"(архив)");
                            ImGui::EndDisabled();
                        }
                    }
                    if (ImGui::Button(u8"Сохранить", ImVec2(110.0f, 0.0f))) {
                        std::vector<std::string> nextAssignees;
                        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                            if (i < static_cast<int>(state.taskInlineAssigneeFlags.size()) && state.taskInlineAssigneeFlags[i] && !state.profiles[i].archived) {
                                nextAssignees.push_back(state.profiles[i].id);
                            }
                        }
                        if (UpdateTaskAssignees(task->id, nextAssignees)) {
                            state.taskInlineAssigneeEditId.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Отмена", ImVec2(110.0f, 0.0f))) {
                        state.taskInlineAssigneeEditId.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::TableSetColumnIndex(col++);
            const int normalizedPriority = NormalizeTaskPriority(task->priority);
            if (state.isAdmin) {
                int draftPriority = normalizedPriority;
                const char* priorityLabels[] = {u8"Низкий", u8"Средний", u8"Высокий", u8"Критический"};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_priority_row", &draftPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels))) {
                    UpdateTaskPriority(task->id, draftPriority);
                }
            } else {
                if (normalizedPriority >= kTaskPriorityCritical) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Error), "%s", TaskPriorityLabel(normalizedPriority));
                } else if (normalizedPriority >= kTaskPriorityHigh) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", TaskPriorityLabel(normalizedPriority));
                } else if (normalizedPriority <= kTaskPriorityLow) {
                    ImGui::TextDisabled("%s", TaskPriorityLabel(normalizedPriority));
                } else {
                    ImGui::TextUnformatted(TaskPriorityLabel(normalizedPriority));
                }
            }

            ImGui::TableSetColumnIndex(col++);
            const int normalizedStatus = NormalizeTaskStatus(task->status);
            if (state.isAdmin) {
                int draftStatus = normalizedStatus;
                const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_status_row", &draftStatus, statusLabels, IM_ARRAYSIZE(statusLabels))) {
                    UpdateTaskStatus(task->id, draftStatus);
                }
            } else if (normalizedStatus == kTaskStatusDone) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Info), "%s", TaskStatusLabel(normalizedStatus));
            } else if (overdue) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", TaskStatusLabel(normalizedStatus));
            } else {
                ImGui::TextUnformatted(TaskStatusLabel(normalizedStatus));
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const int totalTasks = static_cast<int>(state.tasks.size());
    ImGui::TextDisabled(u8"Показано: %d из %d (всего: %d)", visibleCount, filteredCount, totalTasks);
    if (filteredCount == 0) {
        ImGui::TextDisabled(u8"Нет задач по фильтру.");
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Сбросить фильтр")) {
            state.filters.tasks.fill('\0');
            state.tasksFilterProfile = 0;
            state.tasksFilterProject = 0;
            state.tasksFilterStatus = 0;
            state.tasksFilterPriority = 0;
            state.tasksFilterDays = 0;
            state.tasksSortMode = 0;
            state.tasksQuickFilter = 0;
        }
    }

    if (useTaskSplitView) {
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("tasks_detail_column", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar);
    }

    const TaskEntry* expanded = nullptr;
    if (!state.tasksExpandedId.empty()) {
        for (const TaskEntry* task : filtered) {
            if (task && task->id == state.tasksExpandedId) {
                expanded = task;
                break;
            }
        }
        if (!expanded) {
            state.tasksExpandedId.clear();
        }
    }
    if (expanded) {
        TaskEntry* editableTask = nullptr;
        for (auto& task : state.tasks) {
            if (task.id == expanded->id) {
                editableTask = &task;
                break;
            }
        }
        if (BeginCard("tasks_detail_card")) {
            DrawCardHeader(u8"Детали задачи");
            ImGui::TextDisabled(u8"Клик по названию задачи обновляет панель.");
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Скрыть")) {
                state.tasksExpandedId.clear();
            }
            std::string title = expanded->title;
            if (title.empty()) title = u8"Без названия";
            ImGui::TextUnformatted(title.c_str());
            const std::string projectName = ResolveTaskProjectName(state, *expanded);
            const std::string pipelineName = ResolveTaskPipelineName(*expanded);
            if (ImGui::BeginTable("task_detail_meta", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 88.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Проект");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(projectName.empty() ? u8"Без проекта" : projectName.c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Статус");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(TaskStatusLabel(expanded->status));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Этап");
                ImGui::TableSetColumnIndex(1);
                if (pipelineName.empty()) {
                    ImGui::TextDisabled(u8"Без этапа");
                } else {
                    ImGui::TextUnformatted(pipelineName.c_str());
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Приоритет");
                ImGui::TableSetColumnIndex(1);
                const int detailPriority = NormalizeTaskPriority(expanded->priority);
                if (detailPriority >= kTaskPriorityCritical) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Error), "%s", TaskPriorityLabel(detailPriority));
                } else if (detailPriority >= kTaskPriorityHigh) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", TaskPriorityLabel(detailPriority));
                } else {
                    ImGui::TextUnformatted(TaskPriorityLabel(detailPriority));
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Дедлайн");
                ImGui::TableSetColumnIndex(1);
                if (expanded->deadlineAt > 0) {
                    const std::string deadlineText = FormatTimestamp(expanded->deadlineAt, "%Y-%m-%d %H:%M");
                    const bool overdue = expanded->deadlineAt < NowSeconds() &&
                                         NormalizeTaskStatus(expanded->status) != kTaskStatusDone;
                    if (overdue) {
                        ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", deadlineText.c_str());
                    } else {
                        ImGui::TextUnformatted(deadlineText.c_str());
                    }
                } else {
                    ImGui::TextDisabled(u8"Не задан");
                }
                ImGui::EndTable();
            }
            if (!expanded->description.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled(u8"Описание");
                ImGui::TextWrapped("%s", expanded->description.c_str());
            }

            ImGui::Spacing();
            ImGui::TextDisabled(u8"Исполнители");
            std::vector<std::string> assignees = expanded->assignees;
            if (assignees.empty()) {
                for (const auto& participant : expanded->participants) {
                    if (!participant.profileId.empty()) {
                        assignees.push_back(participant.profileId);
                    }
                }
            }
            if (assignees.empty()) {
                ImGui::TextDisabled(u8"Нет назначенных профилей.");
            } else {
                for (const auto& profileId : assignees) {
                    auto it = nameById.find(profileId);
                    const std::string name = (it == nameById.end()) ? profileId : it->second;
                    ImGui::BulletText("%s", name.c_str());
                }
            }

            if (!expanded->participants.empty()) {
                int totalGlobalXp = 0;
                int totalSkillXp = 0;
                for (const auto& participant : expanded->participants) {
                    totalGlobalXp += participant.globalXp;
                    totalSkillXp += participant.skillXp;
                }
                ImGui::Spacing();
                ImGui::TextDisabled(u8"Итоговый XP: %d", totalGlobalXp);
                ImGui::TextDisabled(u8"Навыки (суммарно): %d XP", totalSkillXp);
            }

            if (state.isAdmin && editableTask && !editableTask->id.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled(u8"Действия");

                int draftStatus = NormalizeTaskStatus(editableTask->status);
                const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_detail_status", &draftStatus, statusLabels, IM_ARRAYSIZE(statusLabels))) {
                    UpdateTaskStatus(editableTask->id, draftStatus);
                }

                int draftPriority = NormalizeTaskPriority(editableTask->priority);
                const char* priorityLabels[] = {u8"Низкий", u8"Средний", u8"Высокий", u8"Критический"};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_detail_priority", &draftPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels))) {
                    UpdateTaskPriority(editableTask->id, draftPriority);
                }

                int draftPipelineIndex = 0;
                if (!editableTask->pipelineStepId.empty()) {
                    const int currentPipelineIndex = FindPipelineStepIndexById(state, editableTask->pipelineStepId);
                    if (currentPipelineIndex >= 0) {
                        draftPipelineIndex = currentPipelineIndex + 1;
                    }
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_detail_pipeline", &draftPipelineIndex,
                                 pipelineLabelPtrs.data(), static_cast<int>(pipelineLabelPtrs.size()))) {
                    UpdateTaskPipeline(editableTask->id, draftPipelineIndex);
                }

                bool hasXp = false;
                for (const auto& participant : editableTask->participants) {
                    if (participant.globalXp > 0 || participant.skillXp > 0) {
                        hasXp = true;
                        break;
                    }
                }
                const char* deleteLabel = hasXp ? u8"Удалить задачу и откатить XP" : u8"Удалить задачу";
                if (ImGui::Button(deleteLabel, ImVec2(-1.0f, 0.0f))) {
                    state.pendingTaskDeleteId = editableTask->id;
                    state.taskDeletePopupRequest = true;
                }
            }
        }
        EndCard();
    }
    if (useTaskSplitView) {
        if (!expanded) {
            if (BeginCard("tasks_detail_placeholder")) {
                DrawCardHeader(u8"Детали задачи");
                ImGui::TextDisabled(u8"Выберите задачу по названию в таблице.");
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextWrapped(u8"Справа будут показаны описание, исполнители, XP и админские действия выбранной задачи.");
            }
            EndCard();
        }
        ImGui::EndChild();
    }

    if (state.taskBulkDeletePopupRequest) {
        ImGui::OpenPopup(u8"Удалить выбранные задачи?");
        state.taskBulkDeletePopupRequest = false;
    }
    bool bulkDeleteOpen = true;
    if (ImGui::BeginPopupModal(u8"Удалить выбранные задачи?", &bulkDeleteOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        ImGui::TextWrapped(u8"Удалить выбранные задачи: %d шт.?", static_cast<int>(state.taskSelectionIds.size()));
        ImGui::Spacing();
        if (ImGui::Button(u8"Удалить", ImVec2(140.0f, 0.0f))) {
            int removed = 0;
            int rollbackFailed = 0;
            std::vector<std::string> idsToErase;
            std::vector<TaskEntry> removedTasks;
            idsToErase.reserve(state.taskSelectionIds.size());
            removedTasks.reserve(state.taskSelectionIds.size());
            for (const auto& taskId : state.taskSelectionIds) {
                auto it = std::find_if(state.tasks.begin(), state.tasks.end(),
                                       [&](const TaskEntry& item) { return item.id == taskId; });
                if (it == state.tasks.end()) continue;
                TaskEntry copy = *it;
                bool hasXp = false;
                for (const auto& participant : copy.participants) {
                    if (participant.globalXp > 0 || participant.skillXp > 0) {
                        hasXp = true;
                        break;
                    }
                }
                if (hasXp && !RollbackTask(state, storage, catalog, copy)) {
                    rollbackFailed += 1;
                    continue;
                }
                idsToErase.push_back(copy.id);
                removedTasks.push_back(std::move(copy));
            }
            if (!idsToErase.empty()) {
                AppMutationResult deleteResult = AppDeleteTasksByIds(
                    state.storageDir, state.tasks, idsToErase, ResolveTaskActor(), &state.taskAudit, nullptr);
                if (deleteResult.ok) {
                    removed = deleteResult.changedCount;
                    for (const auto& id : idsToErase) {
                        state.taskSelectionIds.erase(id);
                        if (state.tasksExpandedId == id) {
                            state.tasksExpandedId.clear();
                        }
                    }
                } else {
                    SetStatus(state, deleteResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }
            std::string msg = u8"Удалено задач: " + std::to_string(removed);
            if (rollbackFailed > 0) {
                msg += u8", не удалено из-за ошибки отката XP: " + std::to_string(rollbackFailed);
                SetStatus(state, msg, 1.0f, 0.75f, 0.35f);
            } else {
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
            }
            ImGui::CloseCurrentPopup();
            bulkDeleteOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"Отмена", ImVec2(140.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            bulkDeleteOpen = false;
        }
        ImGui::EndPopup();
    }
    if (!bulkDeleteOpen && ImGui::IsPopupOpen(u8"Удалить выбранные задачи?")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.taskDeletePopupRequest) {
        ImGui::OpenPopup(u8"Удалить задачу?");
        state.taskDeletePopupRequest = false;
    }
    bool deleteOpen = true;
    if (ImGui::BeginPopupModal(u8"Удалить задачу?", &deleteOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        TaskEntry* target = nullptr;
        for (auto& task : state.tasks) {
            if (task.id == state.pendingTaskDeleteId) {
                target = &task;
                break;
            }
        }
        if (!target) {
            ImGui::TextDisabled(u8"Задача не найдена.");
        } else {
            std::string title = TaskDisplayTitle(*target);
            if (title.empty()) title = target->id;
            bool hasXp = false;
            for (const auto& participant : target->participants) {
                if (participant.globalXp > 0 || participant.skillXp > 0) {
                    hasXp = true;
                    break;
                }
            }
            if (hasXp) {
                ImGui::TextWrapped(u8"Удалить задачу \"%s\" и откатить начисленный XP для участников?", title.c_str());
            } else {
                ImGui::TextWrapped(u8"Удалить задачу \"%s\"?", title.c_str());
            }
            ImGui::Spacing();
            if (ImGui::Button(u8"Удалить", ImVec2(140.0f, 0.0f))) {
                TaskEntry copy = *target;
                bool rollbackOk = true;
                if (hasXp) {
                    rollbackOk = RollbackTask(state, storage, catalog, copy);
                }
                if (rollbackOk) {
                    AppMutationResult deleteResult = AppDeleteTasksByIds(
                        state.storageDir, state.tasks, std::vector<std::string>{copy.id}, ResolveTaskActor(), &state.taskAudit, nullptr);
                    if (deleteResult.ok) {
                        if (state.tasksExpandedId == copy.id) {
                            state.tasksExpandedId.clear();
                        }
                        if (hasXp) {
                            SetStatus(state, u8"Задача удалена, XP откатан.", 0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state, u8"Задача удалена.", 0.45f, 0.9f, 0.45f);
                        }
                    } else {
                        SetStatus(state, deleteResult.errorMessage, 1.0f, 0.45f, 0.45f);
                    }
                }
                state.pendingTaskDeleteId.clear();
                ImGui::CloseCurrentPopup();
                deleteOpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена", ImVec2(140.0f, 0.0f))) {
                state.pendingTaskDeleteId.clear();
                ImGui::CloseCurrentPopup();
                deleteOpen = false;
            }
        }
        ImGui::EndPopup();
    }
    if (!deleteOpen && ImGui::IsPopupOpen(u8"Удалить задачу?")) {
        ImGui::CloseCurrentPopup();
    }
}
