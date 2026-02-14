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
        storage.save_profile(profile);
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

    auto UpdateTaskStatus = [&state](const std::string& taskId, int newStatus) -> bool {
        auto it = std::find_if(state.tasks.begin(), state.tasks.end(),
                               [&](const TaskEntry& item) { return item.id == taskId; });
        if (it == state.tasks.end()) {
            SetStatus(state, u8"Задача не найдена.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        const int prev = NormalizeTaskStatus(it->status);
        const int next = NormalizeTaskStatus(newStatus);
        if (prev == next) {
            return true;
        }
        it->status = next;
        if (SaveTasks(state.storageDir, state.tasks)) {
            SetStatus(state, u8"Статус задачи обновлен.", 0.45f, 0.9f, 0.45f);
            return true;
        }
        it->status = prev;
        SetStatus(state, u8"Не удалось сохранить статус задачи.", 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto FindTaskMutable = [&state](const std::string& taskId) -> TaskEntry* {
        auto it = std::find_if(state.tasks.begin(), state.tasks.end(),
                               [&](const TaskEntry& item) { return item.id == taskId; });
        return (it == state.tasks.end()) ? nullptr : &(*it);
    };

    auto UpdateTaskProject = [&](const std::string& taskId, int projectIndex) -> bool {
        TaskEntry* task = FindTaskMutable(taskId);
        if (!task) {
            SetStatus(state, u8"Задача не найдена.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        const std::string prevId = task->projectId;
        const std::string prevName = task->project;
        if (projectIndex <= 0) {
            task->projectId.clear();
            task->project.clear();
        } else {
            const int idx = projectIndex - 1;
            if (idx < 0 || idx >= static_cast<int>(state.projects.size())) {
                SetStatus(state, u8"Проект не найден.", 1.0f, 0.45f, 0.45f);
                return false;
            }
            task->projectId = state.projects[idx].id;
            task->project = state.projects[idx].name;
        }
        if (task->projectId == prevId && task->project == prevName) {
            return true;
        }
        if (SaveTasks(state.storageDir, state.tasks)) {
            SetStatus(state, u8"Проект задачи обновлен.", 0.45f, 0.9f, 0.45f);
            return true;
        }
        task->projectId = prevId;
        task->project = prevName;
        SetStatus(state, u8"Не удалось сохранить проект задачи.", 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskDeadline = [&](const std::string& taskId, const std::string& deadlineInput) -> bool {
        TaskEntry* task = FindTaskMutable(taskId);
        if (!task) {
            SetStatus(state, u8"Задача не найдена.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        const std::int64_t prevDeadline = task->deadlineAt;
        const std::string trimmed = TrimStringGui(deadlineInput);
        if (trimmed.empty()) {
            task->deadlineAt = 0;
        } else {
            std::int64_t parsed = 0;
            if (!ParseTaskDeadlineInput(trimmed, parsed)) {
                SetStatus(state, u8"Формат дедлайна: YYYY-MM-DD HH:MM.", 1.0f, 0.45f, 0.45f);
                return false;
            }
            task->deadlineAt = parsed;
        }
        if (task->deadlineAt == prevDeadline) {
            return true;
        }
        if (SaveTasks(state.storageDir, state.tasks)) {
            SetStatus(state, u8"Дедлайн задачи обновлен.", 0.45f, 0.9f, 0.45f);
            return true;
        }
        task->deadlineAt = prevDeadline;
        SetStatus(state, u8"Не удалось сохранить дедлайн задачи.", 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskAssignees = [&](const std::string& taskId, const std::vector<std::string>& assignees) -> bool {
        TaskEntry* task = FindTaskMutable(taskId);
        if (!task) {
            SetStatus(state, u8"Задача не найдена.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (assignees.empty()) {
            SetStatus(state, u8"Выберите хотя бы одного исполнителя.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (!task->participants.empty()) {
            SetStatus(state, u8"Для этой задачи исполнители зафиксированы по записи XP.", 1.0f, 0.75f, 0.35f);
            return false;
        }
        const std::vector<std::string> prevAssignees = task->assignees;
        task->assignees = assignees;
        if (task->assignees == prevAssignees) {
            return true;
        }
        if (SaveTasks(state.storageDir, state.tasks)) {
            SetStatus(state, u8"Исполнители задачи обновлены.", 0.45f, 0.9f, 0.45f);
            return true;
        }
        task->assignees = prevAssignees;
        SetStatus(state, u8"Не удалось сохранить исполнителей задачи.", 1.0f, 0.45f, 0.45f);
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

    if (state.isAdmin) {
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
            state.taskPlanProjectIndex = std::clamp(state.taskPlanProjectIndex, 0,
                static_cast<int>(projectLabelPtrs.size()) - 1);
            state.taskPlanStatus = NormalizeTaskStatus(state.taskPlanStatus);

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
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(u8"Дедлайн (YYYY-MM-DD HH:MM)");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##task_plan_deadline", u8"2026-02-14 18:00",
                                         state.taskPlanDeadline.data(), state.taskPlanDeadline.size());
                ImGui::TextDisabled(u8"Статус");
                const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_status", &state.taskPlanStatus, statusLabels, IM_ARRAYSIZE(statusLabels));
                ImGui::EndTable();
            }

            ImGui::TextDisabled(u8"Краткое описание");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextMultiline("##task_plan_desc", state.taskPlanDescription.data(),
                                      state.taskPlanDescription.size(), ImVec2(-1.0f, 70.0f));

            ImGui::TextDisabled(u8"Назначить профилям");
            if (ImGui::BeginTable("task_plan_assignees", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("assignee_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("assignee_name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("assignee_set", ImGuiTableColumnFlags_WidthFixed, 90.0f);
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
                    task.id = GenerateTaskId(nowSec, state.tasks.size() + 1);
                    task.title = title;
                    task.description = description;
                    task.deadlineAt = deadlineTs;
                    task.status = NormalizeTaskStatus(state.taskPlanStatus);
                    task.createdAt = nowSec;
                    task.assignees = assignees;
                    if (state.taskPlanProjectIndex > 0) {
                        const int projectIdx = state.taskPlanProjectIndex - 1;
                        if (projectIdx >= 0 && projectIdx < static_cast<int>(state.projects.size())) {
                            task.projectId = state.projects[projectIdx].id;
                            task.project = state.projects[projectIdx].name;
                        }
                    }
                    state.tasks.push_back(std::move(task));
                    if (SaveTasks(state.storageDir, state.tasks)) {
                        SetStatus(state, u8"Задача создана.", 0.45f, 0.9f, 0.45f);
                        state.taskPlanTitle.fill('\0');
                        state.taskPlanDescription.fill('\0');
                        state.taskPlanStatus = kTaskStatusNew;
                        for (auto& flag : state.taskPlanAssignees) flag = 0;
                        EnsureTaskPlanAssigneesSize(state);
                    } else {
                        SetStatus(state, u8"Не удалось сохранить задачу.", 1.0f, 0.45f, 0.45f);
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

    auto ComputeFilteredTasks = [&state, &StartOfLocalDay]() {
        std::vector<const TaskEntry*> ordered;
        ordered.reserve(state.tasks.size());
        for (const auto& task : state.tasks) {
            ordered.push_back(&task);
        }
        std::sort(ordered.begin(), ordered.end(), [](const TaskEntry* a, const TaskEntry* b) {
            return a->createdAt > b->createdAt;
        });
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
        std::vector<const TaskEntry*> filtered;
        filtered.reserve(ordered.size());
        for (const TaskEntry* task : ordered) {
            const std::string filterText = ResolveTaskProjectName(state, *task) + " " + task->title + " " + task->description;
            if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
            if (state.tasksFilterStatus > 0 && NormalizeTaskStatus(task->status) != (state.tasksFilterStatus - 1)) continue;
            if (!filterProfileId.empty() && !TaskHasAssignee(*task, filterProfileId)) continue;
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

    DrawCardHeader(u8"Быстрые фильтры");
    if (ImGui::Button(u8"Все", ImVec2(96.0f, 0.0f))) {
        state.tasksQuickFilter = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Мне назначено", ImVec2(140.0f, 0.0f))) {
        if (state.active) {
            state.tasksQuickFilter = 1;
        } else {
            SetStatus(state, u8"Выберите активный профиль для фильтра 'Мне назначено'.", 1.0f, 0.75f, 0.35f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сегодня", ImVec2(96.0f, 0.0f))) {
        state.tasksQuickFilter = 2;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Просрочено", ImVec2(116.0f, 0.0f))) {
        state.tasksQuickFilter = 3;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"7 дней", ImVec2(96.0f, 0.0f))) {
        state.tasksQuickFilter = 4;
    }

    DrawCardHeader(u8"Фильтр");
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
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

    const char* statusLabelPtrs[] = {u8"Все статусы", u8"Новая", u8"В работе", u8"Выполнена"};
    state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0, static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);

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
        ImGui::TextDisabled(u8"Статус");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_status", &state.tasksFilterStatus,
                         statusLabelPtrs, IM_ARRAYSIZE(statusLabelPtrs))) {
            state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0,
                static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);
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
    } else if (ImGui::BeginTable("tasks_filters", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("filter_search", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("filter_profile", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("filter_status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
        ImGui::TextDisabled(u8"Статус");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_status", &state.tasksFilterStatus,
                         statusLabelPtrs, IM_ARRAYSIZE(statusLabelPtrs))) {
            state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0,
                static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);
        }
        ImGui::EndTable();
        if (ImGui::BeginTable("tasks_filters_extra", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("filter_period", ImGuiTableColumnFlags_WidthStretch, 1.0f);
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

    if (state.isAdmin) {
        if (BeginCard("tasks_bulk_actions")) {
            DrawCardHeader(u8"Массовые действия");
            const int selectedCount = static_cast<int>(state.taskSelectionIds.size());
            ImGui::TextDisabled(u8"Выбрано задач: %d", selectedCount);

            if (ImGui::Button(u8"Выбрать видимые", ImVec2(170.0f, 0.0f))) {
                for (const TaskEntry* task : filtered) {
                    if (task) state.taskSelectionIds.insert(task->id);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Снять выбор", ImVec2(170.0f, 0.0f))) {
                state.taskSelectionIds.clear();
            }

            const bool hasSelection = !state.taskSelectionIds.empty();
            if (!hasSelection) {
                ImGui::TextDisabled(u8"Отметьте задачи чекбоксами в таблице ниже.");
            }
            if (!hasSelection) ImGui::BeginDisabled();

            ImGui::Separator();
            ImGui::TextDisabled(u8"Статус");
            ImGui::SetNextItemWidth(160.0f);
            const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
            state.taskBulkStatus = std::clamp(state.taskBulkStatus, kTaskStatusNew, kTaskStatusDone);
            ImGui::Combo("##task_bulk_status", &state.taskBulkStatus, statusLabels, IM_ARRAYSIZE(statusLabels));
            ImGui::SameLine();
            if (ImGui::Button(u8"Применить статус", ImVec2(170.0f, 0.0f))) {
                int ok = 0;
                for (const auto& taskId : state.taskSelectionIds) {
                    ok += UpdateTaskStatus(taskId, state.taskBulkStatus) ? 1 : 0;
                }
                std::string msg = u8"Статус обновлен для задач: " + std::to_string(ok);
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
            }

            ImGui::TextDisabled(u8"Проект");
            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("##task_bulk_project", &state.taskBulkProjectIndex,
                         projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()));
            ImGui::SameLine();
            if (ImGui::Button(u8"Назначить проект", ImVec2(170.0f, 0.0f))) {
                int ok = 0;
                for (const auto& taskId : state.taskSelectionIds) {
                    ok += UpdateTaskProject(taskId, state.taskBulkProjectIndex) ? 1 : 0;
                }
                std::string msg = u8"Проект обновлен для задач: " + std::to_string(ok);
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
            }

            ImGui::TextDisabled(u8"Дедлайн (YYYY-MM-DD HH:MM)");
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##task_bulk_deadline", u8"2026-02-14 18:00",
                                     state.taskBulkDeadline.data(), state.taskBulkDeadline.size());
            ImGui::SameLine();
            if (ImGui::Button(u8"Установить дедлайн", ImVec2(170.0f, 0.0f))) {
                int ok = 0;
                for (const auto& taskId : state.taskSelectionIds) {
                    ok += UpdateTaskDeadline(taskId, state.taskBulkDeadline.data()) ? 1 : 0;
                }
                std::string msg = u8"Дедлайн обновлен для задач: " + std::to_string(ok);
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
            }

            if (ImGui::TreeNode(u8"Исполнители")) {
                for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                    const auto& info = state.profiles[i];
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
                if (ImGui::Button(u8"Назначить исполнителей", ImVec2(170.0f, 0.0f))) {
                    std::vector<std::string> assignees;
                    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                        if (i < static_cast<int>(state.taskBulkAssignees.size()) && state.taskBulkAssignees[i] && !state.profiles[i].archived) {
                            assignees.push_back(state.profiles[i].id);
                        }
                    }
                    if (assignees.empty()) {
                        SetStatus(state, u8"Выберите хотя бы одного исполнителя.", 1.0f, 0.45f, 0.45f);
                    } else {
                        int ok = 0;
                        for (const auto& taskId : state.taskSelectionIds) {
                            ok += UpdateTaskAssignees(taskId, assignees) ? 1 : 0;
                        }
                        std::string msg = u8"Исполнители обновлены для задач: " + std::to_string(ok);
                        SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::Button(u8"Удалить выбранные", ImVec2(170.0f, 0.0f))) {
                state.taskBulkDeletePopupRequest = true;
            }
            if (!hasSelection) ImGui::EndDisabled();
        }
        EndCard();
        ImGui::Spacing();
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

    if (BeginCard("tasks_summary_card")) {
        DrawCardHeader(u8"Сводка");
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
                if (idx < 0 | idx >= kDays) continue;
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

    int visibleCount = 0;
    const bool allowBulkSelection = state.isAdmin;
    const int tableColumns = allowBulkSelection ? 7 : 6;
    if (ImGui::BeginTable("tasks_table", tableColumns, ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp)) {
        if (allowBulkSelection) {
            ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        }
        ImGui::TableSetupColumn(u8"Дата", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Дедлайн", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(u8"Исполнители", ImGuiTableColumnFlags_WidthStretch, 1.2f);
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

            ImGui::TableSetColumnIndex(col++);
            const bool isExpanded = (state.tasksExpandedId == task->id);
            if (ImGui::Selectable("##row_expand", isExpanded, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(16.0f, 0.0f))) {
                state.tasksExpandedId = isExpanded ? std::string() : task->id;
            }
            ImGui::SameLine();
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
            ImGui::TextUnformatted(task->title.c_str());
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
            state.tasksFilterStatus = 0;
            state.tasksFilterDays = 0;
            state.tasksQuickFilter = 0;
        }
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
            ImGui::TextDisabled(u8"Клик по строке задачи раскрывает детали.");
            std::string title = TaskDisplayTitle(*expanded);
            if (!title.empty()) {
                ImGui::TextUnformatted(title.c_str());
            }
            const std::string projectName = ResolveTaskProjectName(state, *expanded);
            ImGui::TextDisabled(u8"Проект: %s", projectName.empty() ? u8"Без проекта" : projectName.c_str());
            ImGui::TextDisabled(u8"Статус: %s", TaskStatusLabel(expanded->status));
            if (expanded->deadlineAt > 0) {
                const std::string deadlineText = FormatTimestamp(expanded->deadlineAt, "%Y-%m-%d %H:%M");
                const bool overdue = expanded->deadlineAt < NowSeconds() && NormalizeTaskStatus(expanded->status) != kTaskStatusDone;
                if (overdue) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Дедлайн: %s", deadlineText.c_str());
                } else {
                    ImGui::TextDisabled(u8"Дедлайн: %s", deadlineText.c_str());
                }
            } else {
                ImGui::TextDisabled(u8"Дедлайн: не задан");
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
            idsToErase.reserve(state.taskSelectionIds.size());
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
            }
            if (!idsToErase.empty()) {
                state.tasks.erase(std::remove_if(state.tasks.begin(), state.tasks.end(),
                                                 [&](const TaskEntry& item) {
                                                     return std::find(idsToErase.begin(), idsToErase.end(), item.id) != idsToErase.end();
                                                 }),
                                  state.tasks.end());
                if (SaveTasks(state.storageDir, state.tasks)) {
                    removed = static_cast<int>(idsToErase.size());
                    for (const auto& id : idsToErase) {
                        state.taskSelectionIds.erase(id);
                        if (state.tasksExpandedId == id) {
                            state.tasksExpandedId.clear();
                        }
                    }
                } else {
                    SetStatus(state, u8"Не удалось сохранить удаление задач.", 1.0f, 0.45f, 0.45f);
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
                    state.tasks.erase(std::remove_if(state.tasks.begin(), state.tasks.end(),
                                                     [&](const TaskEntry& t) { return t.id == copy.id; }),
                                      state.tasks.end());
                    SaveTasks(state.storageDir, state.tasks);
                    if (state.tasksExpandedId == copy.id) {
                        state.tasksExpandedId.clear();
                    }
                    if (hasXp) {
                        SetStatus(state, u8"Задача удалена, XP откатан.", 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, u8"Задача удалена.", 0.45f, 0.9f, 0.45f);
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
