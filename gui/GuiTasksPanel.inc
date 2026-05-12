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
        if (!participant.rollbackSnapshot.empty()) {
            if (!ApplyProfileTaskRollbackSnapshot(participant.rollbackSnapshot, profile)) {
                SetStatus(state, u8"Не удалось восстановить snapshot профиля для отката XP.", 1.0f, 0.45f, 0.45f);
                ok = false;
                break;
            }
        } else {
            const int newTotalXp = std::max(0, profile.total_xp() - participant.globalXp);
            const int newTasks = std::max(0, profile.tasks_completed() - 1);
            profile.set_total_xp(newTotalXp);
            profile.set_tasks_completed(newTasks);
        }
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

    const ImGuiStyle& tasksStyle = ImGui::GetStyle();
    auto TaskToneColor = [&](AppLogLevel level) -> ImVec4 {
        ImVec4 tone = LogLevelColor(level);
        const ImVec4 accent = tasksStyle.Colors[ImGuiCol_CheckMark];
        const ImVec4 text = tasksStyle.Colors[ImGuiCol_Text];
        if (level == AppLogLevel::Warning) {
            tone = BlendColor(tone, accent, 0.32f);
            tone = BlendColor(tone, text, 0.10f);
        } else if (level == AppLogLevel::Info) {
            tone = BlendColor(tone, accent, 0.45f);
            tone = BlendColor(tone, text, 0.06f);
        } else if (level == AppLogLevel::Error) {
            tone = BlendColor(tone, text, 0.18f);
        }
        tone.w = 1.0f;
        return tone;
    };
    auto TaskWarningColor = [&]() -> ImVec4 { return TaskToneColor(AppLogLevel::Warning); };
    auto TaskInfoColor = [&]() -> ImVec4 { return TaskToneColor(AppLogLevel::Info); };
    auto TaskErrorColor = [&]() -> ImVec4 { return TaskToneColor(AppLogLevel::Error); };
    auto TaskRowTint = [&](AppLogLevel level, float alpha) -> ImVec4 {
        ImVec4 tint = TaskToneColor(level);
        tint.w = std::clamp(alpha, 0.04f, 0.18f);
        return tint;
    };

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
        auto existingTask = std::find_if(state.tasks.begin(), state.tasks.end(),
                                         [&](const TaskEntry& item) { return item.id == taskId; });
        if (existingTask == state.tasks.end()) {
            SetStatus(state, u8"Задача не найдена.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (AppNormalizeTaskStatus(newStatus) == kTaskStatusDone && !TaskHasRecordedXp(*existingTask)) {
            PrepareXpEntriesFromTask(state, *existingTask, catalog);
            state.xpPopupRequest = true;
            SetStatus(state, u8"Перед закрытием распределите XP по задаче.", 0.45f, 0.9f, 0.45f);
            return true;
        }
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
            nextPipelineName = TaskPipelineStepLabel(state.pipelineSteps[static_cast<size_t>(idx)], idx);
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

    auto CollectTaskPipelineQuickTargets = [&](const TaskEntry& task) -> std::vector<int> {
        std::vector<int> targets;
        if (state.pipelineSteps.empty()) return targets;

        std::unordered_set<int> seen;
        const int currentIndex = ResolveTaskPipelineIndex(state, task);
        if (currentIndex >= 0 && currentIndex < static_cast<int>(state.pipelineSteps.size())) {
            const PipelineStep& step = state.pipelineSteps[static_cast<size_t>(currentIndex)];
            targets.reserve(step.nextIds.size());
            for (const auto& nextId : step.nextIds) {
                const int nextIndex = FindPipelineStepIndexById(state, nextId);
                if (nextIndex < 0 || nextIndex == currentIndex) continue;
                if (seen.insert(nextIndex).second) {
                    targets.push_back(nextIndex);
                }
            }
            return targets;
        }

        if (!task.pipelineStepId.empty() || !task.pipelineStep.empty()) {
            return targets;
        }

        targets.reserve(state.pipelineSteps.size());
        for (int i = 0; i < static_cast<int>(state.pipelineSteps.size()); ++i) {
            if (!CollectIncomingPipelineIndices(state, state.pipelineSteps[static_cast<size_t>(i)].id).empty()) {
                continue;
            }
            if (seen.insert(i).second) {
                targets.push_back(i);
            }
        }
        return targets;
    };

    auto DrawTaskPipelineQuickActionButtons = [&](const char* tableId,
                                                  const std::string& taskId,
                                                  const std::vector<int>& targetIndices) -> bool {
        if (targetIndices.empty()) return false;
        const float width = ImGui::GetContentRegionAvail().x;
        const int columns = (width >= 420.0f && targetIndices.size() > 1) ? 2 : 1;
        bool changed = false;
        if (columns == 1) {
            for (int idx : targetIndices) {
                if (idx < 0 || idx >= static_cast<int>(state.pipelineSteps.size())) continue;
                const std::string label = PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(idx)], idx);
                if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
                    changed = UpdateTaskPipeline(taskId, idx + 1) || changed;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    const std::string preview = BuildPipelineRoutePreview(state, idx, 3);
                    if (!preview.empty()) {
                        ImGui::TextUnformatted(preview.c_str());
                    } else {
                        ImGui::TextUnformatted(label.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }
            return changed;
        }

        if (ImGui::BeginTable(tableId, columns, ImGuiTableFlags_SizingStretchSame)) {
            for (size_t i = 0; i < targetIndices.size(); ++i) {
                const int idx = targetIndices[i];
                if (idx < 0 || idx >= static_cast<int>(state.pipelineSteps.size())) continue;
                if (static_cast<int>(i % static_cast<size_t>(columns)) == 0) {
                    ImGui::TableNextRow();
                }
                ImGui::TableSetColumnIndex(static_cast<int>(i % static_cast<size_t>(columns)));
                const std::string label = PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(idx)], idx);
                if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
                    changed = UpdateTaskPipeline(taskId, idx + 1) || changed;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    const std::string preview = BuildPipelineRoutePreview(state, idx, 3);
                    if (!preview.empty()) {
                        ImGui::TextUnformatted(preview.c_str());
                    } else {
                        ImGui::TextUnformatted(label.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndTable();
        }
        return changed;
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

    auto UpdateTaskText = [&](const std::string& taskId, const std::string& title, const std::string& description) -> bool {
        AppMutationResult mutation = AppUpdateTaskText(
            state.storageDir, state.tasks, taskId, title, description, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Текст задачи обновлён.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskCategory = [&](const std::string& taskId, int newCategory) -> bool {
        AppMutationResult mutation = AppUpdateTaskCategory(
            state.storageDir, state.tasks, taskId, newCategory, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Категория задачи обновлена.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto UpdateTaskPenalty = [&](const std::string& taskId, int newPenaltyPercent) -> bool {
        AppMutationResult mutation = AppUpdateTaskPenaltyPercent(
            state.storageDir, state.tasks, taskId, newPenaltyPercent, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Штраф дедлайна обновлён.", 0.45f, 0.9f, 0.45f);
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

    auto UpdateTaskSkills = [&](const std::string& taskId, const std::vector<std::string>& skillIds) -> bool {
        AppMutationResult mutation = AppUpdateTaskSkillIds(
            state.storageDir, state.tasks, taskId, skillIds, ResolveTaskActor(), &state.taskAudit);
        if (mutation.ok) {
            if (mutation.changed) {
                SetStatus(state, u8"Навыки задачи обновлены.", 0.45f, 0.9f, 0.45f);
            }
            return true;
        }
        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
        return false;
    };

    auto StartTaskXpFlow = [&](const std::string& taskId, bool closingTask) -> bool {
        auto it = std::find_if(state.tasks.begin(), state.tasks.end(),
                               [&](const TaskEntry& item) { return item.id == taskId; });
        if (it == state.tasks.end()) {
            SetStatus(state, u8"Задача не найдена.", 1.0f, 0.45f, 0.45f);
            return false;
        }
        if (TaskHasRecordedXp(*it)) {
            if (closingTask) {
                return UpdateTaskStatus(taskId, kTaskStatusDone);
            }
            SetStatus(state, u8"Для этой задачи XP уже записан.", 1.0f, 0.75f, 0.35f);
            return false;
        }
        PrepareXpEntriesFromTask(state, *it, catalog);
        state.xpPopupRequest = true;
        SetStatus(state, u8"Открыт контур выдачи XP по задаче.", 0.45f, 0.9f, 0.45f);
        return true;
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
    auto BuildTaskActionNeededSummary = [&state](const TaskEntry& task, std::int64_t nowSec) -> std::string {
        std::vector<std::string> reasons;
        const int status = NormalizeTaskStatus(task.status);
        if (TaskNeedsXpAward(task)) {
            reasons.push_back(u8"выдать XP");
        }
        if (task.deadlineAt > 0 && task.deadlineAt < nowSec && status != kTaskStatusDone) {
            reasons.push_back(u8"просрочен дедлайн");
        }
        const TaskPipelineProgressInfo progress = DescribeTaskPipelineProgress(state, task);
        if (progress.state == TaskPipelineProgressState::Missing) {
            reasons.push_back(u8"назначить этап");
        } else if (progress.state == TaskPipelineProgressState::Unknown) {
            reasons.push_back(u8"исправить этап");
        } else if (progress.state == TaskPipelineProgressState::Final && status != kTaskStatusDone) {
            reasons.push_back(u8"закрыть handoff");
        }
        if (reasons.empty()) return {};
        std::ostringstream out;
        out.imbue(std::locale::classic());
        for (size_t i = 0; i < reasons.size(); ++i) {
            if (i > 0) out << "; ";
            out << reasons[i];
        }
        return out.str();
    };
    auto TaskNeedsAdminAction = [&](const TaskEntry& task, std::int64_t nowSec) -> bool {
        return !BuildTaskActionNeededSummary(task, nowSec).empty();
    };

    auto ResetTaskFilters = [&state]() {
        state.tasksQuickFilter = 0;
        state.tasksFilterProfile = 0;
        state.tasksFilterProject = 0;
        state.tasksFilterPipeline = 0;
        state.tasksFilterStatus = 0;
        state.tasksFilterPriority = 0;
        state.tasksFilterDays = 0;
        state.tasksSortMode = 0;
        state.filters.tasks.fill('\0');
    };

    if (!state.isAdmin) {
        state.tasksViewMode = 0;
    }
    state.tasksViewMode = std::clamp(state.tasksViewMode, 0, state.isAdmin ? 1 : 0);
    bool tasksAdminMode = state.isAdmin && state.tasksViewMode == 1;
    auto SetTasksViewMode = [&](int mode) {
        state.tasksViewMode = state.isAdmin ? std::clamp(mode, 0, 1) : 0;
        if (state.tasksViewMode == 0) {
            state.showTaskCreateForm = false;
            state.taskSelectionIds.clear();
        }
        tasksAdminMode = state.isAdmin && state.tasksViewMode == 1;
    };
    if (!tasksAdminMode) {
        state.showTaskCreateForm = false;
    }

    if (tasksAdminMode && state.showTaskCreateForm) {
        if (BeginCard("tasks_create_card")) {
            DrawCardHeader(u8"Постановка");
            ImVec4 taskPlanAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            taskPlanAccent.w = 1.0f;
            const float taskPlanIconSize = ImGui::GetFrameHeight();
            EnsureTaskPlanAssigneesSize(state);
            EnsureTaskSkillFlagsSize(state.taskPlanSkillFlags, catalog.skills().size());
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
            state.taskPlanCategory = std::clamp(state.taskPlanCategory, 0, static_cast<int>(Profile::kCategoryCount - 1));
            state.taskPlanPenaltyPercent = std::clamp(state.taskPlanPenaltyPercent, 0, 100);
            constexpr int kTaskPlanStepMain = 0;
            constexpr int kTaskPlanStepAssignees = 1;
            constexpr int kTaskPlanStepSkills = 2;
            constexpr int kTaskPlanStepReview = 3;
            state.taskPlanStep = std::clamp(state.taskPlanStep, kTaskPlanStepMain, kTaskPlanStepReview);
            auto CountTaskPlanAssignees = [&]() {
                int count = 0;
                for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                    if (i < static_cast<int>(state.taskPlanAssignees.size()) &&
                        state.taskPlanAssignees[i] && !state.profiles[static_cast<size_t>(i)].archived) {
                        count += 1;
                    }
                }
                return count;
            };
            const int plannedAssigneeCount = CountTaskPlanAssignees();
            const std::vector<std::string> plannedSkillIds = CollectSelectedTaskSkillIds(catalog, state.taskPlanSkillFlags);

            const char* taskPlanStepLabels[] = {u8"Основное", u8"Люди", u8"Навыки", u8"Проверка"};
            const char* taskPlanStepShortLabels[] = {u8"Осн.", u8"Люди", u8"Нав.", u8"Пров."};
            if (ImGui::BeginTable("task_plan_steps", 4, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const bool selected = state.taskPlanStep == i;
                    if (selected) ImGui::BeginDisabled();
                    std::string stepLabel = std::to_string(i + 1) + ". " + taskPlanStepShortLabels[i];
                    if (ImGui::SmallButton(stepLabel.c_str())) {
                        state.taskPlanStep = i;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", taskPlanStepLabels[i]);
                    }
                    if (selected) ImGui::EndDisabled();
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled(u8"%d/4 %s", state.taskPlanStep + 1, taskPlanStepLabels[state.taskPlanStep]);

            if (state.taskPlanStep == kTaskPlanStepMain) {
            if (ImGui::BeginTable("tasks_create_form", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("create_left", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("create_right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(u8"Название задачи");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##task_plan_title", state.taskPlanTitle.data(), state.taskPlanTitle.size());
                ImGui::TextDisabled(u8"Проект");
                const float projectControlsAvail = ImGui::GetContentRegionAvail().x;
                const bool stackProjectControls = projectControlsAvail < 420.0f;
                const auto toggleTaskProjectInline = [&]() {
                    state.taskPlanCreateProjectInline = !state.taskPlanCreateProjectInline;
                    if (!state.taskPlanCreateProjectInline) {
                        state.taskPlanProjectName.fill('\0');
                        state.taskPlanProjectDesc.fill('\0');
                    }
                };
                if (stackProjectControls) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::Combo("##task_plan_project", &state.taskPlanProjectIndex,
                                 projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()));
                    if (state.isAdmin) {
                        const UiIcon projectToggleIcon = state.taskPlanCreateProjectInline ? UiIcon::Close : UiIcon::Plus;
                        const char* projectToggleTip = state.taskPlanCreateProjectInline
                            ? u8"Скрыть создание проекта"
                            : u8"Создать проект прямо из задачи";
                        if (IconButtonGui("task_plan_project_toggle_stack", projectToggleIcon, projectToggleTip,
                                          taskPlanAccent, taskPlanIconSize)) {
                            toggleTaskProjectInline();
                        }
                    }
                } else if (ImGui::BeginTable("task_plan_project_row", state.isAdmin ? 2 : 1,
                                             ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("task_plan_project_combo", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    if (state.isAdmin) {
                        ImGui::TableSetupColumn("task_plan_project_action", ImGuiTableColumnFlags_WidthFixed,
                                                taskPlanIconSize + ImGui::GetStyle().CellPadding.x * 2.0f);
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::Combo("##task_plan_project", &state.taskPlanProjectIndex,
                                 projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()));
                    if (state.isAdmin) {
                        ImGui::TableSetColumnIndex(1);
                        const UiIcon projectToggleIcon = state.taskPlanCreateProjectInline ? UiIcon::Close : UiIcon::Plus;
                        const char* projectToggleTip = state.taskPlanCreateProjectInline
                            ? u8"Скрыть создание проекта"
                            : u8"Создать проект прямо из задачи";
                        if (IconButtonGui("task_plan_project_toggle", projectToggleIcon, projectToggleTip,
                                          taskPlanAccent, taskPlanIconSize)) {
                            toggleTaskProjectInline();
                        }
                    }
                    ImGui::EndTable();
                }
                if (state.isAdmin && state.projects.empty() && !state.taskPlanCreateProjectInline) {
                    ImGui::TextColored(TaskWarningColor(),
                                       u8"Проектов пока нет. Создайте первый прямо из этой формы.");
                }
                ImGui::TextDisabled(u8"Этап пайплайна");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_pipeline", &state.taskPlanPipelineIndex,
                             pipelineLabelPtrs.data(), static_cast<int>(pipelineLabelPtrs.size()));
                ImGui::TextDisabled(u8"Категория XP");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_plan_category", &state.taskPlanCategory,
                             Profile::kCategoryLabels.data(), Profile::kCategoryCount);
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
                ImGui::TextDisabled(u8"Штраф дедлайна");
                ImGui::SliderInt("##task_plan_penalty", &state.taskPlanPenaltyPercent, 0, 100, "%d%%",
                                 ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndTable();
            }

            if (state.taskPlanCreateProjectInline) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::Separator();
                ImGui::TextDisabled(u8"Новый проект: сохранится и сразу выберется для задачи.");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##task_plan_project_name", u8"Например, Редизайн профиля",
                                         state.taskPlanProjectName.data(), state.taskPlanProjectName.size());
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextMultiline("##task_plan_project_desc", state.taskPlanProjectDesc.data(),
                                          state.taskPlanProjectDesc.size(), ImVec2(-1.0f, 48.0f));
                const float projectInlineAvail = ImGui::GetContentRegionAvail().x;
                const bool stackProjectInlineActions = projectInlineAvail < 360.0f;
                auto saveInlineProject = [&]() {
                    AppProjectSaveResult saveResult = AppSaveProjectEntry(
                        state.storageDir,
                        state.projects,
                        -1,
                        TrimStringGui(state.taskPlanProjectName.data()),
                        TrimStringGui(state.taskPlanProjectDesc.data()));
                    if (saveResult.ok) {
                        state.taskPlanProjectIndex = saveResult.projectIndex + 1;
                        state.taskPlanCreateProjectInline = false;
                        state.taskPlanProjectName.fill('\0');
                        state.taskPlanProjectDesc.fill('\0');
                        SetStatus(state, u8"Проект создан и выбран для задачи.", 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, saveResult.errorMessage, 1.0f, 0.45f, 0.45f);
                    }
                };
                if (stackProjectInlineActions) {
                    if (IconButtonGui("task_plan_project_save_stack", UiIcon::Save,
                                      u8"Сохранить проект и выбрать его", taskPlanAccent, taskPlanIconSize)) {
                        saveInlineProject();
                    }
                    ImGui::SameLine();
                    if (IconButtonGui("task_plan_project_cancel_stack", UiIcon::Close,
                                      u8"Отменить создание проекта", TaskWarningColor(), taskPlanIconSize)) {
                        state.taskPlanCreateProjectInline = false;
                        state.taskPlanProjectName.fill('\0');
                        state.taskPlanProjectDesc.fill('\0');
                    }
                } else if (ImGui::BeginTable("task_plan_project_inline_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (IconButtonGui("task_plan_project_save", UiIcon::Save,
                                      u8"Сохранить проект и выбрать его", taskPlanAccent, taskPlanIconSize)) {
                        saveInlineProject();
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (IconButtonGui("task_plan_project_cancel", UiIcon::Close,
                                      u8"Отменить создание проекта", TaskWarningColor(), taskPlanIconSize)) {
                        state.taskPlanCreateProjectInline = false;
                        state.taskPlanProjectName.fill('\0');
                        state.taskPlanProjectDesc.fill('\0');
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::TextDisabled(u8"Краткое описание");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextMultiline("##task_plan_desc", state.taskPlanDescription.data(),
                                      state.taskPlanDescription.size(), ImVec2(-1.0f, 56.0f));
            }

            if (state.taskPlanStep == kTaskPlanStepAssignees) {
            ImGui::TextDisabled(u8"Назначить профилям");
            ImGui::TextDisabled(u8"Выбрано исполнителей: %d", plannedAssigneeCount);
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
            }

            if (state.taskPlanStep == kTaskPlanStepSkills) {
            ImGui::TextDisabled(u8"Навыки задачи");
            const std::string plannedSkillSummary = BuildTaskSkillSummary(plannedSkillIds, catalog, 4);
            ImGui::TextWrapped("%s", plannedSkillSummary.c_str());
            ImGui::InputTextWithHint("##task_plan_skill_filter", u8"Фильтр навыков",
                                     state.taskPlanSkillFilter.data(), state.taskPlanSkillFilter.size());
            if (ImGui::BeginChild("task_plan_skills", ImVec2(0.0f, 112.0f), true)) {
                const auto& catalogSkills = catalog.skills();
                for (size_t i = 0; i < catalogSkills.size(); ++i) {
                    const std::string displayName = catalog.display_name(catalogSkills[i]);
                    if (!MatchesFilter(displayName, state.taskPlanSkillFilter.data())) continue;
                    bool checked = (i < state.taskPlanSkillFlags.size() && state.taskPlanSkillFlags[i] != 0);
                    if (ImGui::Checkbox((displayName + "##task_plan_skill").c_str(), &checked)) {
                        if (i < state.taskPlanSkillFlags.size()) {
                            state.taskPlanSkillFlags[i] = checked ? 1 : 0;
                        }
                    }
                }
            }
            ImGui::EndChild();
            }

            if (state.taskPlanStep == kTaskPlanStepReview) {
                const std::string titlePreview = TrimStringGui(state.taskPlanTitle.data());
                const std::string deadlinePreview = TrimStringGui(state.taskPlanDeadline.data());
                std::string projectPreview = u8"Без проекта";
                if (state.taskPlanProjectIndex > 0) {
                    const int projectIdx = state.taskPlanProjectIndex - 1;
                    if (projectIdx >= 0 && projectIdx < static_cast<int>(state.projects.size())) {
                        projectPreview = state.projects[static_cast<size_t>(projectIdx)].name;
                    }
                }
                std::string pipelinePreview = u8"Без этапа";
                if (state.taskPlanPipelineIndex > 0) {
                    const int pipelineIdx = state.taskPlanPipelineIndex - 1;
                    if (pipelineIdx >= 0 && pipelineIdx < static_cast<int>(state.pipelineSteps.size())) {
                        pipelinePreview = PipelineDisplayLabel(state.pipelineSteps[static_cast<size_t>(pipelineIdx)], pipelineIdx);
                    }
                }
                DrawCardHeader(u8"Проверка перед созданием");
                if (ImGui::BeginTable("task_plan_review", 4, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("review_label_l", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("review_value_l", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("review_label_r", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("review_value_r", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    auto DrawReviewRow = [&](const char* leftLabel, const std::string& leftValue,
                                             const char* rightLabel, const std::string& rightValue) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled("%s", leftLabel);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", leftValue.empty() ? u8"Не задано" : leftValue.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextDisabled("%s", rightLabel);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextWrapped("%s", rightValue.empty() ? u8"Не задано" : rightValue.c_str());
                    };
                    DrawReviewRow(u8"Название", titlePreview, u8"Дедлайн", deadlinePreview);
                    DrawReviewRow(u8"Проект", projectPreview, u8"Этап", pipelinePreview);
                    DrawReviewRow(u8"Категория", ClassificationLabel(state.taskPlanCategory),
                                  u8"Приоритет", TaskPriorityLabel(state.taskPlanPriority));
                    DrawReviewRow(u8"Исполнители", std::to_string(plannedAssigneeCount),
                                  u8"Навыки", BuildTaskSkillSummary(plannedSkillIds, catalog, 3));
                    ImGui::EndTable();
                }
                if (state.taskPlanPenaltyPercent > 0) {
                    ImGui::TextColored(TaskWarningColor(),
                                       u8"Штраф за дедлайн: -%d%% к итоговому XP.", state.taskPlanPenaltyPercent);
                }
                ImGui::TextDisabled(u8"Если задача создаётся сразу со статусом 'Выполнена', после создания откроется распределение XP.");
            }

            bool createTaskRequested = false;
            if (ImGui::BeginTable("task_plan_navigation", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("task_plan_back", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("task_plan_status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("task_plan_next", ImGuiTableColumnFlags_WidthFixed, 132.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (state.taskPlanStep <= kTaskPlanStepMain) ImGui::BeginDisabled();
                if (ImGui::Button(u8"< Назад", ImVec2(-1.0f, 0.0f))) {
                    state.taskPlanStep = std::max(kTaskPlanStepMain, state.taskPlanStep - 1);
                }
                if (state.taskPlanStep <= kTaskPlanStepMain) ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(u8"%d/4 %s", state.taskPlanStep + 1, taskPlanStepLabels[state.taskPlanStep]);
                ImGui::TableSetColumnIndex(2);
                if (state.taskPlanStep < kTaskPlanStepReview) {
                    if (ImGui::Button(u8"Далее >", ImVec2(-1.0f, 0.0f))) {
                        state.taskPlanStep = std::min(kTaskPlanStepReview, state.taskPlanStep + 1);
                    }
                } else {
                    createTaskRequested = ImGui::Button(u8"Создать", ImVec2(-1.0f, 0.0f));
                }
                ImGui::EndTable();
            }

            if (createTaskRequested) {
                const std::string title = TrimStringGui(state.taskPlanTitle.data());
                const std::string description = TrimStringGui(state.taskPlanDescription.data());
                const std::string deadlineInput = TrimStringGui(state.taskPlanDeadline.data());
                std::int64_t deadlineTs = 0;
                const std::vector<std::string> taskSkillIds = CollectSelectedTaskSkillIds(catalog, state.taskPlanSkillFlags);
                std::vector<std::string> assignees;
                for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                    if (i < static_cast<int>(state.taskPlanAssignees.size()) && state.taskPlanAssignees[i] && !state.profiles[i].archived) {
                        assignees.push_back(state.profiles[i].id);
                    }
                }
                if (title.empty()) {
                    state.taskPlanStep = kTaskPlanStepMain;
                    SetStatus(state, u8"Название задачи обязательно.", 1.0f, 0.45f, 0.45f);
                } else if (description.empty()) {
                    state.taskPlanStep = kTaskPlanStepMain;
                    SetStatus(state, u8"Краткое описание обязательно.", 1.0f, 0.45f, 0.45f);
                } else if (!ParseTaskDeadlineInput(deadlineInput, deadlineTs)) {
                    state.taskPlanStep = kTaskPlanStepMain;
                    SetStatus(state, u8"Укажите дедлайн в формате YYYY-MM-DD HH:MM.", 1.0f, 0.45f, 0.45f);
                } else if (assignees.empty()) {
                    state.taskPlanStep = kTaskPlanStepAssignees;
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
                    task.category = state.taskPlanCategory;
                    task.deadlinePenaltyPercent = state.taskPlanPenaltyPercent;
                    task.createdAt = nowSec;
                    task.assignees = assignees;
                    task.skillIds = taskSkillIds;
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
                        const std::string createdTaskId = task.id;
                        const bool launchXpAfterCreate = (NormalizeTaskStatus(task.status) == kTaskStatusDone);
                        SetStatus(state, u8"Задача создана.", 0.45f, 0.9f, 0.45f);
                        state.showTaskCreateForm = false;
                        state.taskPlanTitle.fill('\0');
                        state.taskPlanDescription.fill('\0');
                        state.taskPlanDeadline.fill('\0');
                        state.taskPlanProjectIndex = 0;
                        state.taskPlanPipelineIndex = 0;
                        state.taskPlanStatus = kTaskStatusNew;
                        state.taskPlanPriority = kTaskPriorityMedium;
                        state.taskPlanCategory = 0;
                        state.taskPlanPenaltyPercent = 0;
                        state.taskPlanCreateProjectInline = false;
                        state.taskPlanProjectName.fill('\0');
                        state.taskPlanProjectDesc.fill('\0');
                        state.taskPlanStep = kTaskPlanStepMain;
                        state.taskPlanSkillFilter.fill('\0');
                        std::fill(state.taskPlanSkillFlags.begin(), state.taskPlanSkillFlags.end(), 0);
                        for (auto& flag : state.taskPlanAssignees) flag = 0;
                        EnsureTaskPlanAssigneesSize(state);
                        if (launchXpAfterCreate) {
                            StartTaskXpFlow(createdTaskId, true);
                        }
                    } else {
                        SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button(u8"Добавить опыт без задачи", ImVec2(-1.0f, 0.0f))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
        }
        EndCard();
        ImGui::Spacing();
    }

    auto ComputeFilteredTasks = [&]() {
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
        bool filterWithoutPipeline = false;
        std::string filterPipelineId;
        std::string filterPipelineName;
        if (state.tasksFilterPipeline == 1) {
            filterWithoutPipeline = true;
        } else if (state.tasksFilterPipeline >= 2) {
            const int idx = state.tasksFilterPipeline - 2;
            if (idx >= 0 && idx < static_cast<int>(state.pipelineSteps.size())) {
                filterPipelineId = state.pipelineSteps[static_cast<size_t>(idx)].id;
                filterPipelineName = TaskPipelineStepLabel(state.pipelineSteps[static_cast<size_t>(idx)], idx);
            }
        }
        std::vector<const TaskEntry*> filtered;
        filtered.reserve(ordered.size());
        for (const TaskEntry* task : ordered) {
            const std::string resolvedProjectName = ResolveTaskProjectName(state, *task);
            const std::string pipelineLabel = ResolveTaskPipelineName(state, *task);
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
            if (filterWithoutPipeline) {
                if (!pipelineLabel.empty()) continue;
            } else if (!filterPipelineId.empty() || !filterPipelineName.empty()) {
                bool pipelineMatch = false;
                if (!filterPipelineId.empty() && task->pipelineStepId == filterPipelineId) pipelineMatch = true;
                if (!pipelineMatch && !filterPipelineName.empty() && pipelineLabel == filterPipelineName) pipelineMatch = true;
                if (!pipelineMatch) continue;
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
            } else if (state.tasksQuickFilter == 6) {
                if (!TaskNeedsXpAward(*task)) continue;
            } else if (state.tasksQuickFilter == 7) {
                if (NormalizeTaskStatus(task->status) == kTaskStatusDone) continue;
            } else if (state.tasksQuickFilter == 8) {
                if (!TaskNeedsAdminAction(*task, nowSec)) continue;
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
    std::vector<std::string> pipelineFilterLabels;
    pipelineFilterLabels.reserve(pipelineLabels.size() + 1);
    pipelineFilterLabels.push_back(u8"Все этапы");
    pipelineFilterLabels.push_back(u8"Без этапа");
    for (int i = 1; i < static_cast<int>(pipelineLabels.size()); ++i) {
        pipelineFilterLabels.push_back(pipelineLabels[static_cast<size_t>(i)]);
    }
    std::vector<const char*> pipelineFilterLabelPtrs;
    pipelineFilterLabelPtrs.reserve(pipelineFilterLabels.size());
    for (const auto& label : pipelineFilterLabels) {
        pipelineFilterLabelPtrs.push_back(label.c_str());
    }
    state.taskPlanPipelineIndex = std::clamp(state.taskPlanPipelineIndex, 0,
        static_cast<int>(pipelineLabelPtrs.size()) - 1);
    state.taskBulkPipelineIndex = std::clamp(state.taskBulkPipelineIndex, 0,
        static_cast<int>(pipelineLabelPtrs.size()) - 1);
    state.tasksFilterPipeline = std::clamp(state.tasksFilterPipeline, 0,
        static_cast<int>(pipelineFilterLabelPtrs.size()) - 1);

    const char* quickFilterLabels[] = {
        u8"Все",
        u8"Мне назначено",
        u8"Сегодня",
        u8"Просрочено",
        u8"7 дней",
        u8"Без проекта",
        u8"Ждут XP",
        u8"Активные",
        u8"Требуют действий"
    };
    state.tasksQuickFilter = std::clamp(state.tasksQuickFilter, 0, static_cast<int>(IM_ARRAYSIZE(quickFilterLabels)) - 1);
    auto ToggleTaskCreateFlow = [&]() {
        if (!state.isAdmin) return;
        if (!tasksAdminMode) {
            SetTasksViewMode(1);
            state.showTaskCreateForm = true;
            state.taskPlanStep = 0;
            return;
        }
        state.showTaskCreateForm = !state.showTaskCreateForm;
        if (state.showTaskCreateForm) {
            state.taskPlanStep = 0;
        }
    };
    auto TaskCreateButtonLabel = [&]() -> const char* {
        return (tasksAdminMode && state.showTaskCreateForm) ? u8"Скрыть форму" : u8"Поставить задачу";
    };
    auto TaskIconButton = [](const char* id, UiIcon icon, const char* tooltip,
                             bool enabled = true, bool selected = false) -> bool {
        ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        return IconButtonGui(id, icon, tooltip, accent, ImGui::GetFrameHeight(), enabled, selected);
    };
    auto DrawTasksModeSwitch = [&](bool fullWidth) {
        if (!state.isAdmin) {
            ImGui::TextDisabled(u8"Оперативно");
            return;
        }
        const float modeWidth = fullWidth
            ? std::max(88.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f)
            : 86.0f;
        auto drawMode = [&](const char* label, int mode) {
            const bool selected = state.tasksViewMode == mode;
            if (selected) ImGui::BeginDisabled();
            if (ImGui::Button(label, ImVec2(modeWidth, 0.0f))) {
                SetTasksViewMode(mode);
            }
            if (selected) ImGui::EndDisabled();
        };
        drawMode(u8"Опер.", 0);
        ImGui::SameLine();
        drawMode(u8"Админ", 1);
    };
    if (BeginCard("tasks_toolbar_card")) {
        DrawCardHeader(u8"Панель");
        const bool compactToolbar = ImGui::GetContentRegionAvail().x < 760.0f;
        int requestedQuickFilter = state.tasksQuickFilter;
        if (compactToolbar) {
            if (state.isAdmin) {
                ImGui::TextDisabled(u8"Режим");
                DrawTasksModeSwitch(true);
            }
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
            ImGui::TextDisabled(u8"Поиск");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##tasks_toolbar_search_compact",
                                     u8"Проект, задача или описание",
                                     state.filters.tasks.data(), state.filters.tasks.size());
            if (state.isAdmin) {
                const UiIcon createIcon = (tasksAdminMode && state.showTaskCreateForm) ? UiIcon::Stop : UiIcon::Plus;
                if (TaskIconButton("tasks_toolbar_create_compact", createIcon, TaskCreateButtonLabel(),
                                   true, tasksAdminMode && state.showTaskCreateForm)) {
                    ToggleTaskCreateFlow();
                }
                ImGui::SameLine();
            }
            if (TaskIconButton("tasks_toolbar_reset_compact", UiIcon::Clear, u8"Сбросить фильтры")) {
                ResetTaskFilters();
            }
        } else if (ImGui::BeginTable("tasks_toolbar_row", state.isAdmin ? 5 : 3,
                                     ImGuiTableFlags_SizingStretchProp)) {
            const float toolbarIconColumn = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f;
            if (state.isAdmin) {
                ImGui::TableSetupColumn("tasks_toolbar_mode", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            }
            ImGui::TableSetupColumn("tasks_toolbar_filter", ImGuiTableColumnFlags_WidthStretch, 0.85f);
            ImGui::TableSetupColumn("tasks_toolbar_search", ImGuiTableColumnFlags_WidthStretch, 1.45f);
            if (state.isAdmin) {
                ImGui::TableSetupColumn("tasks_toolbar_create", ImGuiTableColumnFlags_WidthFixed, toolbarIconColumn);
            }
            ImGui::TableSetupColumn("tasks_toolbar_reset", ImGuiTableColumnFlags_WidthFixed, toolbarIconColumn);
            ImGui::TableNextRow();
            int toolbarColumn = 0;
            if (state.isAdmin) {
                ImGui::TableSetColumnIndex(toolbarColumn++);
                DrawTasksModeSwitch(false);
            }
            ImGui::TableSetColumnIndex(toolbarColumn++);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##tasks_quick_filter", &requestedQuickFilter,
                             quickFilterLabels, IM_ARRAYSIZE(quickFilterLabels))) {
                if (requestedQuickFilter == 1 && !state.active) {
                    SetStatus(state, u8"Выберите активный профиль для фильтра 'Мне назначено'.", 1.0f, 0.75f, 0.35f);
                } else {
                    state.tasksQuickFilter = requestedQuickFilter;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(u8"Быстрый фильтр задач.");
            }
            ImGui::TableSetColumnIndex(toolbarColumn++);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##tasks_toolbar_search",
                                     u8"Проект, задача или описание",
                                     state.filters.tasks.data(), state.filters.tasks.size());
            if (state.isAdmin) {
                ImGui::TableSetColumnIndex(toolbarColumn++);
                const UiIcon createIcon = (tasksAdminMode && state.showTaskCreateForm) ? UiIcon::Stop : UiIcon::Plus;
                if (TaskIconButton("tasks_toolbar_create", createIcon, TaskCreateButtonLabel(),
                                   true, tasksAdminMode && state.showTaskCreateForm)) {
                    ToggleTaskCreateFlow();
                }
            }
            ImGui::TableSetColumnIndex(toolbarColumn++);
            if (TaskIconButton("tasks_toolbar_reset", UiIcon::Clear, u8"Сбросить фильтры")) {
                ResetTaskFilters();
            }
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::Spacing();

    if (tasksAdminMode) {
    std::string projectsHeader = u8"Проекты в задачах (" + std::to_string(static_cast<int>(state.projects.size())) + ")";
    if (ImGui::CollapsingHeader(projectsHeader.c_str())) {
        if (BeginCard("tasks_projects_bridge")) {
            std::vector<ProjectTaskStats> bridgeProjectStats;
            bridgeProjectStats.reserve(state.projects.size());
            for (const auto& project : state.projects) {
                bridgeProjectStats.push_back(BuildProjectTaskStats(state, project.id));
            }
            const ProjectPortfolioStats bridgePortfolio = BuildProjectPortfolioStats(bridgeProjectStats);

            std::string activePortfolioLabel = u8"Активно: " + std::to_string(bridgePortfolio.activeProjects) +
                u8" пр. / " + std::to_string(bridgePortfolio.totalActiveTasks) + u8" зад.";
            DrawStatusPillGui("bridge_portfolio_active", activePortfolioLabel.c_str(),
                              ImGui::GetStyle().Colors[ImGuiCol_CheckMark],
                              u8"Активные проекты и задачи", bridgePortfolio.totalActiveTasks > 0);
            if (bridgePortfolio.xpPendingProjects > 0) {
                ImGui::SameLine();
                std::string xpPortfolioLabel = u8"XP: " + std::to_string(bridgePortfolio.xpPendingProjects) +
                    u8" пр. / " + std::to_string(bridgePortfolio.totalXpPendingTasks) + u8" зад.";
                DrawStatusPillGui("bridge_portfolio_xp", xpPortfolioLabel.c_str(),
                                  TaskWarningColor(),
                                  u8"Проекты с задачами, ожидающими выдачи XP", true);
            }
            if (bridgePortfolio.overdueProjects > 0) {
                ImGui::SameLine();
                std::string overduePortfolioLabel = u8"Проср.: " + std::to_string(bridgePortfolio.overdueProjects) + u8" пр.";
                DrawStatusPillGui("bridge_portfolio_overdue", overduePortfolioLabel.c_str(),
                                  TaskWarningColor(),
                                  u8"Проекты с просроченными задачами", true);
            }
            if (state.tasksFilterProject > 0) {
                ImGui::SameLine();
                if (TaskIconButton("bridge_clear_focus", UiIcon::Clear, u8"Снять проектный фильтр")) {
                    state.tasksFilterProject = 0;
                }
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            if (state.projects.empty()) {
                if (DrawEmptyStateGui("tasks_projects_bridge_empty", UiIcon::Project,
                                      u8"Проектов пока нет",
                                      u8"Создайте проект здесь же, чтобы сразу привязать к нему задачи.",
                                      UiIcon::Plus, u8"Открыть форму создания проекта")) {
                    state.projectEditIndex = -1;
                    state.newProjectName.fill('\0');
                    state.newProjectDesc.fill('\0');
                    ImGui::SetNextItemOpen(true);
                }
            } else if (ImGui::BeginTable("tasks_projects_bridge_table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                const ImGuiStyle& bridgeStyle = ImGui::GetStyle();
                const float bridgeActionWidth = ImGui::GetFrameHeight() * 2.0f + bridgeStyle.ItemSpacing.x +
                                                bridgeStyle.CellPadding.x * 2.0f;
                ImGui::TableSetupColumn("bridge_project", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("bridge_state", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bridge_total", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("bridge_actions", ImGuiTableColumnFlags_WidthFixed, bridgeActionWidth);
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(state.projects.size()); ++i) {
                    const auto& project = state.projects[static_cast<size_t>(i)];
                    const ProjectTaskStats stats = bridgeProjectStats[static_cast<size_t>(i)];
                    ImGui::TableNextRow();
                    ApplyTableRowStateTintGui(state.projectEditIndex == i, state.tasksFilterProject == 2 + i);
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
                    ImVec4 neutralPill = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                    ImVec4 activePill = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                    std::string newLabel = u8"Н:" + std::to_string(stats.created);
                    std::string workLabel = u8"В:" + std::to_string(stats.inProgress);
                    std::string doneLabel = u8"Г:" + std::to_string(stats.done);
                    DrawStatusPillGui("bridge_project_new", newLabel.c_str(), neutralPill, u8"Новые задачи");
                    ImGui::SameLine();
                    DrawStatusPillGui("bridge_project_work", workLabel.c_str(), activePill,
                                      u8"Задачи в работе", stats.inProgress > 0);
                    ImGui::SameLine();
                    DrawStatusPillGui("bridge_project_done", doneLabel.c_str(), TaskInfoColor(),
                                      u8"Готовые задачи", stats.done > 0);
                    if (stats.overdue > 0) {
                        ImGui::SameLine();
                        std::string overdueLabel = u8"П:" + std::to_string(stats.overdue);
                        DrawStatusPillGui("bridge_project_overdue", overdueLabel.c_str(),
                                          TaskWarningColor(),
                                          u8"Просроченные задачи", true);
                    }
                    if (stats.xpPending > 0) {
                        ImGui::SameLine();
                        std::string xpLabel = "XP:" + std::to_string(stats.xpPending);
                        DrawStatusPillGui("bridge_project_xp", xpLabel.c_str(),
                                          TaskWarningColor(),
                                          u8"Handoff ждёт выдачи XP", true);
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", stats.total);
                    ImGui::TableSetColumnIndex(3);
                    if (TaskIconButton("bridge_focus", UiIcon::Search, u8"Показать задачи проекта")) {
                        state.tasksFilterProject = 2 + i;
                    }
                    ImGui::SameLine();
                    if (TaskIconButton("bridge_edit", UiIcon::Edit, u8"Редактировать проект")) {
                        state.projectEditIndex = i;
                        std::snprintf(state.newProjectName.data(), state.newProjectName.size(), "%s", project.name.c_str());
                        std::snprintf(state.newProjectDesc.data(), state.newProjectDesc.size(), "%s", project.description.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (state.isAdmin && ImGui::CollapsingHeader(u8"Создание и редактирование проекта")) {
                if (TaskIconButton("bridge_new_project", UiIcon::Plus, u8"Новый проект")) {
                    state.projectEditIndex = -1;
                    state.newProjectName.fill('\0');
                    state.newProjectDesc.fill('\0');
                }
                ImGui::SameLine();
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
                                          state.newProjectDesc.size(), ImVec2(-1.0f, 52.0f));
                if (TaskIconButton("bridge_save_project", UiIcon::Save, u8"Сохранить проект")) {
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
    state.tasksFilterPipeline = std::clamp(state.tasksFilterPipeline, 0,
        static_cast<int>(pipelineFilterLabelPtrs.size()) - 1);

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
    auto ExportFilteredTasks = [&](bool txt) {
        std::filesystem::path outPath;
        int exported = 0;
        std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
        const bool ok = txt
            ? ExportTasksTxt(state, filtered, nameById, outPath, exported)
            : ExportTasksCsv(state, filtered, nameById, outPath, exported);
        if (!ok) {
            SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
            return;
        }
        std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
        msg += outPath.string();
        msg += ")";
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
    };

    if (stackFilters) {
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
        ImGui::TextDisabled(u8"Этап пайплайна");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_pipeline", &state.tasksFilterPipeline,
                         pipelineFilterLabelPtrs.data(), static_cast<int>(pipelineFilterLabelPtrs.size()))) {
            state.tasksFilterPipeline = std::clamp(state.tasksFilterPipeline, 0,
                static_cast<int>(pipelineFilterLabelPtrs.size()) - 1);
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
        if (TaskIconButton("tasks_export_csv_stack", UiIcon::Export, u8"Экспортировать видимые задачи в CSV")) {
            ExportFilteredTasks(false);
        }
        ImGui::SameLine();
        if (TaskIconButton("tasks_export_txt_stack", UiIcon::FileText, u8"Экспортировать видимые задачи в TXT")) {
            ExportFilteredTasks(true);
        }
    } else {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("tasks_filters", 6, ControlTableFlagsGui())) {
        ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("filter_profile", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("filter_project", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("filter_pipeline", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("filter_status", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("filter_priority", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_profile", &state.tasksFilterProfile,
                         profileLabelPtrs.data(), static_cast<int>(profileLabelPtrs.size()))) {
            state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Фильтр по профилю.");
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_project", &state.tasksFilterProject,
                         projectFilterLabelPtrs.data(), static_cast<int>(projectFilterLabelPtrs.size()))) {
            state.tasksFilterProject = std::clamp(state.tasksFilterProject, 0,
                static_cast<int>(projectFilterLabelPtrs.size()) - 1);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Фильтр по проекту.");
        ImGui::TableSetColumnIndex(3);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_pipeline", &state.tasksFilterPipeline,
                         pipelineFilterLabelPtrs.data(), static_cast<int>(pipelineFilterLabelPtrs.size()))) {
            state.tasksFilterPipeline = std::clamp(state.tasksFilterPipeline, 0,
                static_cast<int>(pipelineFilterLabelPtrs.size()) - 1);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Фильтр по этапу пайплайна.");
        ImGui::TableSetColumnIndex(4);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_status", &state.tasksFilterStatus,
                         statusLabelPtrs, IM_ARRAYSIZE(statusLabelPtrs))) {
            state.tasksFilterStatus = std::clamp(state.tasksFilterStatus, 0,
                static_cast<int>(IM_ARRAYSIZE(statusLabelPtrs)) - 1);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Фильтр по статусу.");
        ImGui::TableSetColumnIndex(5);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_priority", &state.tasksFilterPriority,
                         priorityLabelPtrs, IM_ARRAYSIZE(priorityLabelPtrs))) {
            state.tasksFilterPriority = std::clamp(state.tasksFilterPriority, 0,
                static_cast<int>(IM_ARRAYSIZE(priorityLabelPtrs)) - 1);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Фильтр по приоритету.");
        ImGui::EndTable();
        }
        if (ImGui::BeginTable("tasks_filters_extra", 4, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("filter_period", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("filter_sort", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("filter_actions", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::Chart);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(u8"##tasks_days", &dayIndex, dayLabels, IM_ARRAYSIZE(dayLabels))) {
                state.tasksFilterDays = dayOptions[dayIndex];
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Период создания задач.");
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(u8"##tasks_sort", &state.tasksSortMode,
                             sortLabelPtrs, IM_ARRAYSIZE(sortLabelPtrs))) {
                state.tasksSortMode = std::clamp(state.tasksSortMode, 0,
                    static_cast<int>(IM_ARRAYSIZE(sortLabelPtrs)) - 1);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Порядок сортировки таблицы.");
            ImGui::TableSetColumnIndex(3);
            if (TaskIconButton("tasks_export_csv", UiIcon::Export, u8"Экспортировать видимые задачи в CSV")) {
                ExportFilteredTasks(false);
            }
            ImGui::SameLine();
            if (TaskIconButton("tasks_export_txt", UiIcon::FileText, u8"Экспортировать видимые задачи в TXT")) {
                ExportFilteredTasks(true);
            }
            ImGui::EndTable();
        }
    }
    }

    std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
    if (state.tasks.empty()) {
        if (DrawEmptyStateGui("tasks_empty_all", UiIcon::Task,
                              u8"Журнал задач пуст",
                              state.isAdmin
                                  ? u8"Начните с постановки первой задачи: проект, исполнители, навыки и дедлайн будут собраны в одном сценарии."
                                  : u8"Когда администратор назначит задачу, она появится здесь с проектом, этапом и статусом.",
                              UiIcon::Plus, state.isAdmin ? u8"Поставить первую задачу" : nullptr,
                              state.isAdmin)) {
            ToggleTaskCreateFlow();
        }
    }

    const std::int64_t workNowSec = NowSeconds();
    const std::int64_t workDayStart = StartOfLocalDay(workNowSec);
    const std::int64_t workDayEnd = workDayStart + 86400;
    int filteredActiveTasks = 0;
    int filteredOverdueTasks = 0;
    int filteredTodayTasks = 0;
    int filteredXpPendingTasks = 0;
    int filteredCriticalTasks = 0;
    int filteredPipelineMissing = 0;
    int filteredPipelineUnknown = 0;
    int filteredPipelineBranching = 0;
    int filteredPipelineFinalOpen = 0;
    const TaskEntry* workFocusTask = nullptr;
    auto ShouldPromoteWorkFocus = [&](const TaskEntry* candidate, const TaskEntry* current) {
        if (!candidate) return false;
        if (!current) return true;
        const bool candidateOverdue = candidate->deadlineAt > 0 && candidate->deadlineAt < workNowSec &&
                                      NormalizeTaskStatus(candidate->status) != kTaskStatusDone;
        const bool currentOverdue = current->deadlineAt > 0 && current->deadlineAt < workNowSec &&
                                    NormalizeTaskStatus(current->status) != kTaskStatusDone;
        if (candidateOverdue != currentOverdue) return candidateOverdue;
        const int candidatePriority = NormalizeTaskPriority(candidate->priority);
        const int currentPriority = NormalizeTaskPriority(current->priority);
        if (candidatePriority != currentPriority) return candidatePriority > currentPriority;
        const std::int64_t candidateDeadline = candidate->deadlineAt > 0
            ? candidate->deadlineAt
            : std::numeric_limits<std::int64_t>::max();
        const std::int64_t currentDeadline = current->deadlineAt > 0
            ? current->deadlineAt
            : std::numeric_limits<std::int64_t>::max();
        if (candidateDeadline != currentDeadline) return candidateDeadline < currentDeadline;
        const int candidateStatus = NormalizeTaskStatus(candidate->status);
        const int currentStatus = NormalizeTaskStatus(current->status);
        if (candidateStatus != currentStatus) return candidateStatus > currentStatus;
        return candidate->createdAt > current->createdAt;
    };
    for (const TaskEntry* task : filtered) {
        if (!task) continue;
        const int taskStatus = NormalizeTaskStatus(task->status);
        const bool taskDone = taskStatus == kTaskStatusDone;
        const bool taskOverdue = task->deadlineAt > 0 && task->deadlineAt < workNowSec && !taskDone;
        if (TaskNeedsXpAward(*task)) {
            filteredXpPendingTasks += 1;
        }
        if (taskDone) continue;
        ++filteredActiveTasks;
        if (taskOverdue) {
            filteredOverdueTasks += 1;
        }
        if (task->deadlineAt >= workDayStart && task->deadlineAt < workDayEnd) {
            filteredTodayTasks += 1;
        }
        if (NormalizeTaskPriority(task->priority) >= kTaskPriorityCritical) {
            filteredCriticalTasks += 1;
        }
        if (ShouldPromoteWorkFocus(task, workFocusTask)) {
            workFocusTask = task;
        }
        const TaskPipelineProgressInfo progress = DescribeTaskPipelineProgress(state, *task);
        if (progress.state == TaskPipelineProgressState::Missing) {
            filteredPipelineMissing += 1;
        } else if (progress.state == TaskPipelineProgressState::Unknown) {
            filteredPipelineUnknown += 1;
        } else if (progress.state == TaskPipelineProgressState::Branching) {
            filteredPipelineBranching += 1;
        } else if (progress.state == TaskPipelineProgressState::Final) {
            filteredPipelineFinalOpen += 1;
        }
    }
    const int filteredPipelineSignalCount = filteredPipelineMissing + filteredPipelineUnknown +
        filteredPipelineBranching + filteredPipelineFinalOpen;

    if (!tasksAdminMode) {
        if (BeginCard("tasks_work_queue_card")) {
            DrawCardHeader(u8"Очередь задач");
            const int queueColumns = ImGui::GetContentRegionAvail().x < 840.0f ? 2 : 6;
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("tasks_work_queue_metrics", queueColumns, ImGuiTableFlags_SizingStretchSame)) {
                int metricIndex = 0;
                auto DrawQueueMetric = [&](const char* title,
                                           int value,
                                           const char* hint,
                                           int quickFilter,
                                           const ImVec4& color) {
                    if ((metricIndex % queueColumns) == 0) {
                        ImGui::TableNextRow();
                    }
                    ImGui::TableSetColumnIndex(metricIndex % queueColumns);
                    ImVec4 bg = color;
                    bg.w = value > 0
                        ? std::clamp(bg.w * 0.12f, 0.06f, 0.16f)
                            : std::clamp(bg.w * 0.04f, 0.03f, 0.06f);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(bg));
                    ImGui::TextDisabled("%s", title);
                    ImGui::SameLine();
                    if (value > 0) {
                        ImGui::TextColored(color, "%d", value);
                    } else {
                        ImGui::TextDisabled("0");
                    }
                    if (quickFilter != -1) {
                        ImGui::SameLine();
                        ImGui::PushID(title);
                        if (TaskIconButton("queue_open", UiIcon::Search, u8"Открыть задачи по этому сигналу")) {
                            if (quickFilter >= 0) {
                                state.tasksQuickFilter = quickFilter;
                            } else if (quickFilter == -2) {
                                state.tasksQuickFilter = 0;
                                state.tasksFilterPriority = kTaskPriorityCritical + 1;
                            }
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", hint);
                    }
                    ++metricIndex;
                };
                DrawQueueMetric(u8"Активные", filteredActiveTasks, u8"в очереди", 7, TaskInfoColor());
                DrawQueueMetric(u8"Просрочка", filteredOverdueTasks, u8"срок сорван", 3, TaskWarningColor());
                DrawQueueMetric(u8"Сегодня", filteredTodayTasks, u8"дедлайн", 2, TaskInfoColor());
                DrawQueueMetric(u8"Ждут XP", filteredXpPendingTasks, u8"handoff", 6, TaskWarningColor());
                DrawQueueMetric(u8"Критичные", filteredCriticalTasks, u8"приоритет", -2, TaskErrorColor());
                DrawQueueMetric(u8"Pipeline", filteredPipelineSignalCount, u8"сигналы", -1,
                                filteredPipelineSignalCount > 0 ? TaskWarningColor() : TaskInfoColor());
                ImGui::EndTable();
            }
            if (workFocusTask) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextDisabled(u8"Фокус");
                ImGui::SameLine();
                const std::string focusTitle = TaskDisplayTitle(*workFocusTask).empty()
                    ? workFocusTask->id
                    : TaskDisplayTitle(*workFocusTask);
                ImGui::TextUnformatted(focusTitle.c_str());
                if (workFocusTask->deadlineAt > 0) {
                    ImGui::SameLine();
                    const std::string deadlineText = FormatTimestamp(workFocusTask->deadlineAt, "%d.%m %H:%M");
                    const bool overdue = workFocusTask->deadlineAt < workNowSec;
                    if (overdue) {
                        ImGui::TextColored(TaskWarningColor(), "%s", deadlineText.c_str());
                    } else {
                        ImGui::TextDisabled("%s", deadlineText.c_str());
                    }
                }
                ImGui::SameLine();
                if (TaskIconButton("tasks_focus_open", UiIcon::Search, u8"Открыть фокусную задачу")) {
                    state.tasksExpandedId = workFocusTask->id;
                }
            } else {
                ImGui::TextDisabled(u8"По текущему фильтру нет активных задач.");
            }
        }
        EndCard();
        ImGui::Spacing();
    }

    const bool drawPipelineRiskDetails = tasksAdminMode ||
        (filteredPipelineSignalCount > 0 &&
         ImGui::CollapsingHeader((u8"Pipeline-сигналы (" +
             std::to_string(filteredPipelineSignalCount) + ")").c_str()));
    if (drawPipelineRiskDetails) {
        if (BeginCard("tasks_pipeline_risks_card")) {
            DrawCardHeader(u8"Пайплайн-риски");
            if (state.pipelineSteps.empty()) {
                ImGui::TextDisabled(u8"Пайплайн не настроен.");
            } else if (filteredActiveTasks == 0) {
                ImGui::TextDisabled(u8"По текущему фильтру нет активных задач.");
            } else {
                ImGui::TextDisabled(u8"Активных задач по фильтру: %d | сигналов: %d",
                                    filteredActiveTasks, filteredPipelineSignalCount);
                const int riskColumns = ImGui::GetContentRegionAvail().x < 760.0f ? 2 : 4;
                if (ImGui::BeginTable("tasks_pipeline_risks_table", riskColumns, ImGuiTableFlags_SizingStretchSame)) {
                    int metricIndex = 0;
                    auto drawRiskMetric = [&](const char* title,
                                              int value,
                                              const ImVec4& baseColor,
                                              const char* hint) {
                        if ((metricIndex % riskColumns) == 0) {
                            ImGui::TableNextRow();
                        }
                        ImGui::TableSetColumnIndex(metricIndex % riskColumns);
                        ImVec4 bg = baseColor;
                        bg.w = value > 0
                            ? std::clamp(bg.w * 0.14f, 0.06f, 0.18f)
                            : std::clamp(bg.w * 0.05f, 0.03f, 0.06f);
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(bg));
                        ImGui::TextDisabled("%s", title);
                        if (value > 0) {
                            ImGui::TextColored(baseColor, "%d", value);
                        } else {
                            ImGui::TextDisabled("0");
                        }
                        ImGui::TextDisabled("%s", hint);
                        ++metricIndex;
                    };

                    drawRiskMetric(u8"Без этапа", filteredPipelineMissing,
                                   TaskWarningColor(), u8"назначить старт");
                    drawRiskMetric(u8"Вне схемы", filteredPipelineUnknown,
                                   TaskErrorColor(), u8"переназначить шаг");
                    drawRiskMetric(u8"Ветвление", filteredPipelineBranching,
                                   TaskInfoColor(), u8"нужен выбор next");
                    drawRiskMetric(u8"Финал открыт", filteredPipelineFinalOpen,
                                   TaskInfoColor(), u8"handoff не закрыт");
                    ImGui::EndTable();
                }
                if (filteredPipelineSignalCount == 0) {
                    ImGui::TextDisabled(u8"Критичных pipeline-сигналов по текущему фильтру нет.");
                }
            }
        }
        EndCard();
        ImGui::Spacing();
    }

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

        DrawDeadlineList(u8"Просроченные", overdueDeadlines, TaskErrorColor(), 3);
        DrawDeadlineList(u8"Ближайшие 2 часа", upcoming2h, TaskWarningColor(), 4);
        DrawDeadlineList(u8"Ближайшие 24 часа", upcoming24h, TaskInfoColor(), 4);

        if (overdueDeadlines.empty() && upcoming2h.empty() && upcoming24h.empty()) {
            ImGui::TextDisabled(u8"Критичных дедлайнов нет.");
        }
        }
        EndCard();
        ImGui::Spacing();
    }

    if (tasksAdminMode) {
    std::string auditHeader = u8"Последние изменения (" +
        std::to_string(static_cast<int>(state.taskAudit.size())) + ")";
    if (ImGui::CollapsingHeader(auditHeader.c_str())) {
        if (BeginCard("tasks_audit_card")) {
        constexpr int kTaskAuditPreview = 8;
        if (state.taskAudit.empty()) {
            ImGui::TextDisabled(u8"Изменений пока нет.");
        } else {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("tasks_audit_table", 5,
                                  ControlTableFlagsGui(ImGuiTableFlags_RowBg))) {
                ImGui::TableSetupColumn("audit_time", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("audit_actor", ImGuiTableColumnFlags_WidthFixed, 106.0f);
                ImGui::TableSetupColumn("audit_task", ImGuiTableColumnFlags_WidthStretch, 1.35f);
                ImGui::TableSetupColumn("audit_field", ImGuiTableColumnFlags_WidthFixed, 74.0f);
                ImGui::TableSetupColumn("audit_change", ImGuiTableColumnFlags_WidthStretch, 1.55f);
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
                    const std::string actorLabel = row.actor.empty() ? std::string(u8"system") : row.actor;
                    std::string changeLabel = row.oldValue;
                    changeLabel += " -> ";
                    changeLabel += row.newValue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(timeLabel.c_str());
                    ImGui::TableSetColumnIndex(1);
                    DrawClippedTextWithTooltip(actorLabel);
                    ImGui::TableSetColumnIndex(2);
                    DrawClippedTextWithTooltip(taskLabel);
                    ImGui::TableSetColumnIndex(3);
                    DrawClippedTextWithTooltip(row.field);
                    ImGui::TableSetColumnIndex(4);
                    DrawClippedTextWithTooltip(changeLabel);
                }
                ImGui::EndTable();
            }
        }
        }
        EndCard();
        ImGui::Spacing();
    }
    }

    auto ApplyBulkStatus = [&]() -> bool {
        if (NormalizeTaskStatus(state.taskBulkStatus) == kTaskStatusDone) {
            int pendingXpCount = 0;
            for (const auto& task : state.tasks) {
                if (state.taskSelectionIds.find(task.id) == state.taskSelectionIds.end()) continue;
                if (!TaskHasRecordedXp(task)) {
                    pendingXpCount += 1;
                }
            }
            if (pendingXpCount > 0) {
                SetStatus(state,
                          u8"Массовое закрытие отключено: сначала закройте задачи через detail-pane и выдайте XP.",
                          1.0f, 0.75f, 0.35f);
                return false;
            }
        }
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

    if (tasksAdminMode && !state.taskSelectionIds.empty()) {
        std::string bulkHeader = u8"Bulk (" +
            std::to_string(static_cast<int>(state.taskSelectionIds.size())) + ")";
        if (ImGui::CollapsingHeader(bulkHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (BeginCard("tasks_bulk_actions")) {
            const int selectedCount = static_cast<int>(state.taskSelectionIds.size());
            ImGui::TextDisabled(u8"%d выбрано", selectedCount);
            ImGui::SameLine();
            if (TaskIconButton("bulk_select_visible", UiIcon::Search, u8"Выбрать все видимые задачи")) {
                for (const TaskEntry* task : filtered) {
                    if (task) state.taskSelectionIds.insert(task->id);
                }
            }
            ImGui::SameLine();
            if (TaskIconButton("bulk_clear_selection", UiIcon::Clear, u8"Снять выбор")) {
                state.taskSelectionIds.clear();
            }
            ImGui::SameLine();
            if (TaskIconButton("bulk_delete_selection", UiIcon::Close, u8"Удалить выбранные задачи")) {
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
            CompactTableScopeGui compactTable;
            const float bulkIconColumn = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f;

            if (ImGui::BeginTable("tasks_bulk_compact_row1", 8, ControlTableFlagsGui())) {
                ImGui::TableSetupColumn("bulk_status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bulk_status_btn", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableSetupColumn("bulk_priority", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bulk_priority_btn", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableSetupColumn("bulk_project", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("bulk_project_btn", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableSetupColumn("bulk_pipeline", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("bulk_pipeline_btn", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_status", &state.taskBulkStatus, statusLabels, IM_ARRAYSIZE(statusLabels));

                ImGui::TableSetColumnIndex(1);
                if (TaskIconButton("bulk_apply_status", UiIcon::Play, u8"Применить статус")) {
                    ApplyBulkStatus();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_priority", &state.taskBulkPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels));

                ImGui::TableSetColumnIndex(3);
                if (TaskIconButton("bulk_apply_priority", UiIcon::Apply, u8"Применить приоритет")) {
                    ApplyBulkPriority();
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_project", &state.taskBulkProjectIndex,
                             projectLabelPtrs.data(), static_cast<int>(projectLabelPtrs.size()));

                ImGui::TableSetColumnIndex(5);
                if (TaskIconButton("bulk_apply_project", UiIcon::Project, u8"Применить проект")) {
                    ApplyBulkProject();
                }

                ImGui::TableSetColumnIndex(6);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##task_bulk_pipeline", &state.taskBulkPipelineIndex,
                             pipelineLabelPtrs.data(), static_cast<int>(pipelineLabelPtrs.size()));

                ImGui::TableSetColumnIndex(7);
                if (TaskIconButton("bulk_apply_pipeline", UiIcon::Flow, u8"Применить этап pipeline")) {
                    ApplyBulkPipeline();
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginTable("tasks_bulk_compact_row2", 4, ControlTableFlagsGui())) {
                ImGui::TableSetupColumn("bulk_deadline", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("bulk_deadline_set", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableSetupColumn("bulk_deadline_clear", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableSetupColumn("bulk_assignees", ImGuiTableColumnFlags_WidthFixed, bulkIconColumn);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##task_bulk_deadline", u8"YYYY-MM-DD HH:MM",
                                         state.taskBulkDeadline.data(), state.taskBulkDeadline.size());

                ImGui::TableSetColumnIndex(1);
                if (TaskIconButton("bulk_apply_deadline", UiIcon::Calendar, u8"Установить дедлайн")) {
                    ApplyBulkDeadline(false);
                }

                ImGui::TableSetColumnIndex(2);
                if (TaskIconButton("bulk_clear_deadline", UiIcon::Clear, u8"Очистить дедлайн")) {
                    ApplyBulkDeadline(true);
                }

                ImGui::TableSetColumnIndex(3);
                if (TaskIconButton("bulk_open_assignees", UiIcon::Assign, u8"Назначить исполнителей")) {
                    ImGui::OpenPopup("tasks_bulk_assignees_popup");
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginPopup("tasks_bulk_assignees_popup")) {
                ImGui::TextDisabled(u8"Массовое назначение исполнителей");
                {
                    CompactTableScopeGui assigneesCompactTable;
                    if (ImGui::BeginTable("bulk_assignees_table", 2, ControlTableFlagsGui())) {
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
                }
                if (TaskIconButton("bulk_assignees_apply", UiIcon::Assign, u8"Применить исполнителей")) {
                    if (ApplyBulkAssignees()) {
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (TaskIconButton("bulk_assignees_cancel", UiIcon::Close, u8"Закрыть без изменений")) {
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
    std::unordered_map<std::string, int> pipelineCounts;
    std::string topPipelineLabel;
    int topPipelineCount = 0;
    int unassignedPipelineCount = 0;
    int xpPendingCount = 0;
    std::string lastStamp;
    if (!filtered.empty()) {
        lastStamp = FormatTimestamp(filtered.front()->createdAt, "%Y-%m-%d %H:%M");
        uniqueProfiles.reserve(state.profiles.size());
        pipelineCounts.reserve(filtered.size());
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
            if (TaskNeedsXpAward(*task)) {
                xpPendingCount += 1;
            }
            const std::string pipelineName = ResolveTaskPipelineName(state, *task);
            if (pipelineName.empty()) {
                unassignedPipelineCount += 1;
            } else {
                const int nextCount = ++pipelineCounts[pipelineName];
                if (nextCount >= topPipelineCount) {
                    topPipelineCount = nextCount;
                    topPipelineLabel = pipelineName;
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
            DrawEmptyStateGui("tasks_summary_empty_filter", UiIcon::Search,
                              u8"Нет задач по фильтру",
                              u8"Сбросьте фильтры или переключите быстрый режим, чтобы вернуть сводку.");
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
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("tasks_summary_table", 5, ControlTableFlagsGui())) {
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
                ImGui::TextDisabled(u8"Посл.");
                ImGui::Text("%s", lastStamp.empty() ? "-" : lastStamp.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Последняя задача по текущему фильтру.");
                ImGui::TableSetColumnIndex(2);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Фокус");
                DrawClippedTextWithTooltip(focusText);
                ImGui::TableSetColumnIndex(3);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Ср.");
                ImGui::Text("%.1f", avgScore);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Средняя оценка задач.");
                ImGui::TableSetColumnIndex(4);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Люди");
                ImGui::Text("%d", static_cast<int>(uniqueProfiles.size()));
                ImGui::EndTable();
            }
            if (hasAttention) {
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled(u8"Зона внимания");
                ImGui::SameLine();
                std::string attentionLabel = std::string(ClassificationLabel(minIdx)) + " (" + std::to_string(minCount) + ")";
                DrawStatusPillGui("tasks_summary_attention", attentionLabel.c_str(),
                                  TaskWarningColor(),
                                  u8"Категория с заметным недобором задач", true);
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"XP ср.: %.0f", avgPool);
            if (xpPendingCount > 0) {
                ImGui::SameLine();
                std::string handoffLabel = "handoff:" + std::to_string(xpPendingCount);
                DrawStatusPillGui("tasks_summary_handoff", handoffLabel.c_str(),
                                  TaskWarningColor(),
                                  u8"Задачи закрыты, но XP ещё не выдан.", true);
            }
            if (!topPipelineLabel.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled(u8"Этап:");
                ImGui::SameLine();
                ImGui::Text("%s (%d)", topPipelineLabel.c_str(), topPipelineCount);
                if (unassignedPipelineCount > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(u8"без:%d", unassignedPipelineCount);
                }
            } else if (unassignedPipelineCount > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(u8"без этапа:%d", unassignedPipelineCount);
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"14 дней");
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
                DrawEmptyStateGui("tasks_activity_empty_period", UiIcon::Task,
                                  u8"Нет активности за период",
                                  u8"Новые задачи не создавались в последние дни.");
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
    float taskSplitPaneHeight = 0.0f;
    if (useTaskSplitView) {
        const float listPanelWidth = std::max(0.0f, taskLayoutWidth - detailPanelWidth - taskLayoutGap);
        taskSplitPaneHeight = std::max(420.0f, ImGui::GetContentRegionAvail().y);
        ImGui::BeginChild("tasks_master_column", ImVec2(listPanelWidth, taskSplitPaneHeight), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_HorizontalScrollbar);
    }

    auto CollectTaskTableAssigneeNames = [&](const TaskEntry& task) -> std::vector<std::string> {
        std::vector<std::string> ids = task.assignees;
        if (ids.empty()) {
            for (const auto& participant : task.participants) {
                if (!participant.profileId.empty()) {
                    ids.push_back(participant.profileId);
                }
            }
        }
        std::vector<std::string> names;
        names.reserve(ids.size());
        for (const auto& profileId : ids) {
            auto it = nameById.find(profileId);
            names.push_back((it == nameById.end()) ? profileId : it->second);
        }
        return names;
    };
    auto BuildTaskTableAssigneeSummary = [&](const TaskEntry& task, int maxVisible) -> std::string {
        const std::vector<std::string> names = CollectTaskTableAssigneeNames(task);
        if (names.empty()) return u8"Не назначена";
        std::ostringstream out;
        out.imbue(std::locale::classic());
        const int visible = std::min(maxVisible, static_cast<int>(names.size()));
        for (int i = 0; i < visible; ++i) {
            if (i > 0) out << ", ";
            out << names[static_cast<size_t>(i)];
        }
        if (static_cast<int>(names.size()) > visible) {
            out << u8" +" << (static_cast<int>(names.size()) - visible);
        }
        return out.str();
    };
    auto TaskPriorityCode = [](int priority) -> const char* {
        switch (NormalizeTaskPriority(priority)) {
            case kTaskPriorityLow: return "P1";
            case kTaskPriorityMedium: return "P2";
            case kTaskPriorityHigh: return "P3";
            case kTaskPriorityCritical: return "P4";
            default: return "P?";
        }
    };
    auto TaskStatusShortLabel = [](int status) -> const char* {
        switch (NormalizeTaskStatus(status)) {
            case kTaskStatusNew: return u8"Нов.";
            case kTaskStatusInProgress: return u8"Раб.";
            case kTaskStatusDone: return u8"Гот.";
            default: return "?";
        }
    };

    int visibleCount = 0;
    const bool allowBulkSelection = tasksAdminMode;
    const int tableColumns = allowBulkSelection ? 9 : 8;
    if (allowBulkSelection && !state.taskSelectionIds.empty()) {
        ImGui::TextDisabled(u8"Выбрано: %d", static_cast<int>(state.taskSelectionIds.size()));
        ImGui::SameLine();
        if (TaskIconButton("tasks_table_clear_selection", UiIcon::Close, u8"Снять выбор задач")) {
            state.taskSelectionIds.clear();
        }
        ImGui::Spacing();
    }
    CompactTableScopeGui compactTable;
    if (ImGui::BeginTable("tasks_table", tableColumns, ControlTableFlagsGui(ImGuiTableFlags_RowBg))) {
        if (allowBulkSelection) {
            ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        }
        ImGui::TableSetupColumn(u8"Дата", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn(u8"Дедл.", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn(u8"Этап", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(u8"Исполнители", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("P", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn(u8"Ст.", ImGuiTableColumnFlags_WidthFixed, tasksAdminMode ? 66.0f : 58.0f);
        ImGui::TableHeadersRow();
        const std::int64_t nowSec = NowSeconds();
        for (const TaskEntry* task : filtered) {
            ++visibleCount;
            ImGui::TableNextRow();
            const bool isExpanded = (state.tasksExpandedId == task->id);
            const bool isFocusTask = workFocusTask && workFocusTask->id == task->id;
            const bool overdue = task->deadlineAt > 0 && task->deadlineAt < nowSec &&
                                 NormalizeTaskStatus(task->status) != kTaskStatusDone;
            if (isExpanded) {
                ApplyTableRowStateTintGui(true);
            } else if (isFocusTask) {
                ApplyTableRowStateTintGui(false, true);
            } else if (overdue) {
                ImVec4 rowHighlight = TaskRowTint(AppLogLevel::Warning, 0.12f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
            }
            TaskEntry* editableTask = tasksAdminMode ? FindTaskMutable(task->id) : nullptr;
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

            const bool xpPending = TaskNeedsXpAward(*task);
            const int taskPipelineIndex = ResolveTaskPipelineIndex(state, *task);
            const std::string actionNeededSummary = BuildTaskActionNeededSummary(*task, nowSec);

            ImGui::TableSetColumnIndex(col++);
            const std::string stamp = FormatTimestamp(task->createdAt, "%d.%m");
            ImGui::TextDisabled("%s", stamp.empty() ? "-" : stamp.c_str());
            if (ImGui::IsItemHovered()) {
                const std::string fullStamp = FormatTimestamp(task->createdAt, "%Y-%m-%d %H:%M");
                ImGui::SetTooltip("%s", fullStamp.c_str());
            }

            ImGui::TableSetColumnIndex(col++);
            if (tasksAdminMode && editableTask) {
                const std::string deadlineText = task->deadlineAt > 0 ? FormatTimestamp(task->deadlineAt, "%Y-%m-%d %H:%M") : std::string();
                std::string buttonLabel = task->deadlineAt > 0
                    ? FormatTimestamp(task->deadlineAt, "%d.%m %H:%M")
                    : std::string(u8"+");
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    std::snprintf(state.taskInlineDeadlineBuffer.data(), state.taskInlineDeadlineBuffer.size(), "%s", deadlineText.c_str());
                    state.taskInlineDeadlineEditId = task->id;
                    ImGui::OpenPopup("deadline_popup");
                }
                if (!deadlineText.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", deadlineText.c_str());
                }
                if (ImGui::BeginPopup("deadline_popup")) {
                    ImGui::TextDisabled(u8"Дедлайн (YYYY-MM-DD HH:MM)");
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::InputTextWithHint("##deadline_input", u8"2026-02-14 18:00",
                                             state.taskInlineDeadlineBuffer.data(), state.taskInlineDeadlineBuffer.size());
                    if (TaskIconButton("deadline_popup_save", UiIcon::Save, u8"Сохранить дедлайн")) {
                        if (UpdateTaskDeadline(task->id, state.taskInlineDeadlineBuffer.data())) {
                            state.taskInlineDeadlineEditId.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (TaskIconButton("deadline_popup_clear", UiIcon::Clear, u8"Очистить дедлайн")) {
                        if (UpdateTaskDeadline(task->id, "")) {
                            state.taskInlineDeadlineEditId.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (TaskIconButton("deadline_popup_cancel", UiIcon::Close, u8"Закрыть без изменений")) {
                        state.taskInlineDeadlineEditId.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            } else if (task->deadlineAt > 0) {
                const std::string deadline = FormatTimestamp(task->deadlineAt, "%d.%m %H:%M");
                if (overdue) {
                    ImGui::TextColored(TaskWarningColor(), "%s", deadline.c_str());
                } else {
                    ImGui::TextUnformatted(deadline.c_str());
                }
                if (ImGui::IsItemHovered()) {
                    const std::string fullDeadline = FormatTimestamp(task->deadlineAt, "%Y-%m-%d %H:%M");
                    ImGui::SetTooltip("%s", fullDeadline.c_str());
                }
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableSetColumnIndex(col++);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            const std::string projectName = ResolveTaskProjectName(state, *task);
            if (tasksAdminMode && editableTask) {
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
            } else {
                DrawClippedTextWithTooltip(projectName, u8"Без проекта", projectName.empty());
            }

            ImGui::TableSetColumnIndex(col++);
            ImU32 pipelineAttentionBg = 0;
            if (TryGetTaskPipelineAttentionCellBg(state, *task, pipelineAttentionBg)) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, pipelineAttentionBg);
            }
            const std::string pipelineName = ResolveTaskPipelineName(state, *task);
            if (tasksAdminMode && editableTask) {
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
                DrawTaskPipelineProgressHint(state, *task);
            } else if (taskPipelineIndex >= 0 && taskPipelineIndex < static_cast<int>(state.pipelineSteps.size())) {
                const PipelineStep& step = state.pipelineSteps[static_cast<size_t>(taskPipelineIndex)];
                const std::string stageCode = TaskPipelineStepCode(step, taskPipelineIndex);
                const std::string stageTitle = step.title.empty() ? pipelineName : step.title;
                ImGui::Text("%s", stageCode.c_str());
                if (ImGui::IsItemHovered()) {
                    const std::string tooltip = stageTitle.empty() ? pipelineName : stageTitle;
                    if (!tooltip.empty()) ImGui::SetTooltip("%s", tooltip.c_str());
                }
                DrawTaskPipelineProgressHint(state, *task);
            } else {
                DrawClippedTextWithTooltip(pipelineName, u8"Без этапа", pipelineName.empty());
                DrawTaskPipelineProgressHint(state, *task);
            }

            ImGui::TableSetColumnIndex(col++);
            std::string taskTitle = task->title;
            if (taskTitle.empty()) taskTitle = u8"Без названия";
            std::string taskTitleLabel = taskTitle + "##task_title";
            if (ImGui::Selectable(taskTitleLabel.c_str(), isExpanded,
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAvailWidth)) {
                state.tasksExpandedId = isExpanded ? std::string() : task->id;
            }
            const bool taskTitleHovered = ImGui::IsItemHovered();
            ImGui::TextDisabled("%s", ClassificationLabel(task->category));
            if (xpPending) {
                ImGui::SameLine();
                ImGui::TextColored(TaskWarningColor(), u8"XP");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(u8"Ожидает выдачи XP");
                }
            }
            if (!actionNeededSummary.empty()) {
                ImGui::SameLine();
                DrawStatusPillGui("task_action_needed_pill", "!", TaskWarningColor(),
                                  actionNeededSummary.c_str(), true);
            }
            if (!task->description.empty() && taskTitleHovered) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(task->description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(col++);
            const std::string participants = BuildParticipantLabel(*task, nameById);
            const std::string participantSummary = BuildTaskTableAssigneeSummary(*task, 2);
            DrawClippedTextWithTooltip(participantSummary);
            if (!participants.empty() && participants != participantSummary && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", participants.c_str());
            }
            if (tasksAdminMode && editableTask && task->participants.empty()) {
                ImGui::SameLine();
                if (TaskIconButton("task_row_assignee_edit", UiIcon::Assign, u8"Редактировать исполнителей")) {
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
                    if (TaskIconButton("row_assignee_save", UiIcon::Save, u8"Сохранить исполнителей")) {
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
                    if (TaskIconButton("row_assignee_cancel", UiIcon::Close, u8"Закрыть без изменений")) {
                        state.taskInlineAssigneeEditId.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::TableSetColumnIndex(col++);
            const int normalizedPriority = NormalizeTaskPriority(task->priority);
            if (tasksAdminMode) {
                int draftPriority = normalizedPriority;
                ImVec4 priorityAccent = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                if (normalizedPriority >= kTaskPriorityCritical) {
                    priorityAccent = TaskErrorColor();
                } else if (normalizedPriority >= kTaskPriorityHigh) {
                    priorityAccent = TaskWarningColor();
                } else if (normalizedPriority == kTaskPriorityMedium) {
                    priorityAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                }
                ImVec4 priorityFrame = BlendColor(ImGui::GetStyle().Colors[ImGuiCol_FrameBg], priorityAccent, 0.20f);
                priorityFrame.w = std::max(priorityFrame.w, ImGui::GetStyle().Colors[ImGuiCol_FrameBg].w);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, priorityFrame);
                const char* priorityLabels[] = {"P1", "P2", "P3", "P4"};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_priority_row", &draftPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels))) {
                    UpdateTaskPriority(task->id, draftPriority);
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", TaskPriorityLabel(draftPriority));
                }
            } else {
                ImVec4 priorityAccent = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                if (normalizedPriority >= kTaskPriorityCritical) {
                    priorityAccent = TaskErrorColor();
                } else if (normalizedPriority >= kTaskPriorityHigh) {
                    priorityAccent = TaskWarningColor();
                } else if (normalizedPriority == kTaskPriorityMedium) {
                    priorityAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                }
                DrawStatusPillGui("task_priority_pill", TaskPriorityCode(normalizedPriority),
                                  priorityAccent, TaskPriorityLabel(normalizedPriority),
                                  normalizedPriority >= kTaskPriorityHigh);
            }

            ImGui::TableSetColumnIndex(col++);
            const int normalizedStatus = NormalizeTaskStatus(task->status);
            if (tasksAdminMode) {
                int draftStatus = normalizedStatus;
                ImVec4 statusAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                if (normalizedStatus == kTaskStatusDone) {
                    statusAccent = TaskInfoColor();
                } else if (overdue) {
                    statusAccent = TaskWarningColor();
                }
                ImVec4 statusFrame = BlendColor(ImGui::GetStyle().Colors[ImGuiCol_FrameBg], statusAccent, 0.20f);
                statusFrame.w = std::max(statusFrame.w, ImGui::GetStyle().Colors[ImGuiCol_FrameBg].w);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, statusFrame);
                const char* statusLabels[] = {u8"Нов.", u8"Раб.", u8"Гот."};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##task_status_row", &draftStatus, statusLabels, IM_ARRAYSIZE(statusLabels))) {
                    UpdateTaskStatus(task->id, draftStatus);
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", TaskStatusLabel(draftStatus));
                }
            } else {
                ImVec4 statusAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                if (normalizedStatus == kTaskStatusDone) {
                    statusAccent = TaskInfoColor();
                } else if (overdue) {
                    statusAccent = TaskWarningColor();
                }
                DrawStatusPillGui("task_status_pill", TaskStatusShortLabel(normalizedStatus),
                                  statusAccent, TaskStatusLabel(normalizedStatus),
                                  normalizedStatus == kTaskStatusDone || overdue);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const int totalTasks = static_cast<int>(state.tasks.size());
    ImGui::TextDisabled(u8"Показано: %d из %d (всего: %d)", visibleCount, filteredCount, totalTasks);
    if (!state.tasks.empty() && filteredCount == 0) {
        ImGui::Spacing();
        if (DrawEmptyStateGui("tasks_empty_filtered", UiIcon::Search,
                              u8"По текущим фильтрам задач нет",
                              u8"Сбросьте фильтры или переключите быстрый режим, чтобы вернуть полный журнал.",
                              UiIcon::Refresh, u8"Сбросить фильтры задач")) {
            state.filters.tasks.fill('\0');
            state.tasksFilterProfile = 0;
            state.tasksFilterProject = 0;
            state.tasksFilterPipeline = 0;
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
        ImGui::BeginChild("tasks_detail_column", ImVec2(0.0f, taskSplitPaneHeight), ImGuiChildFlags_Border);
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
    auto CollectTaskDisplayAssigneeNames = [&](const TaskEntry& task) -> std::vector<std::string> {
        std::vector<std::string> ids = task.assignees;
        if (ids.empty()) {
            for (const auto& participant : task.participants) {
                if (!participant.profileId.empty()) {
                    ids.push_back(participant.profileId);
                }
            }
        }
        std::vector<std::string> names;
        names.reserve(ids.size());
        for (const auto& profileId : ids) {
            auto it = nameById.find(profileId);
            names.push_back((it == nameById.end()) ? profileId : it->second);
        }
        return names;
    };
    auto BuildTaskAssigneeSummary = [&](const TaskEntry& task, int maxVisible) -> std::string {
        const std::vector<std::string> names = CollectTaskDisplayAssigneeNames(task);
        if (names.empty()) return u8"Не назначены";
        std::ostringstream out;
        out.imbue(std::locale::classic());
        const int visible = std::min(maxVisible, static_cast<int>(names.size()));
        for (int i = 0; i < visible; ++i) {
            if (i > 0) out << ", ";
            out << names[static_cast<size_t>(i)];
        }
        if (static_cast<int>(names.size()) > visible) {
            out << u8" +" << (static_cast<int>(names.size()) - visible);
        }
        return out.str();
    };
    if (expanded) {
        TaskEntry* editableTask = nullptr;
        if (tasksAdminMode) {
            for (auto& task : state.tasks) {
                if (task.id == expanded->id) {
                    editableTask = &task;
                    break;
                }
            }
        }
        if (BeginCard("tasks_detail_card")) {
            DrawCardHeader(u8"Детали");
            std::string title = expanded->title;
            if (title.empty()) title = u8"Без названия";
            const float detailHeadlineWidth = ImGui::GetContentRegionAvail().x;
            if (detailHeadlineWidth < 300.0f) {
                ImGui::TextWrapped("%s", title.c_str());
                ImGui::SameLine();
                if (TaskIconButton("task_detail_close_compact", UiIcon::Close, u8"Скрыть детали задачи")) {
                    state.tasksExpandedId.clear();
                }
            } else if (ImGui::BeginTable("task_detail_headline", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("task_detail_title", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("task_detail_close", ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(title.c_str());
                ImGui::PopTextWrapPos();
                ImGui::TableSetColumnIndex(1);
                if (TaskIconButton("task_detail_close", UiIcon::Close, u8"Скрыть детали задачи")) {
                    state.tasksExpandedId.clear();
                }
                ImGui::EndTable();
            }
            const std::string projectName = ResolveTaskProjectName(state, *expanded);
            const std::string pipelineName = ResolveTaskPipelineName(state, *expanded);
            const TaskPipelineProgressInfo expandedPipelineProgress = DescribeTaskPipelineProgress(state, *expanded);
            const int detailPriority = NormalizeTaskPriority(expanded->priority);
            const bool detailOverdue = expanded->deadlineAt > 0 &&
                                       expanded->deadlineAt < NowSeconds() &&
                                       NormalizeTaskStatus(expanded->status) != kTaskStatusDone;
            const std::vector<std::string> detailAssigneeNames = CollectTaskDisplayAssigneeNames(*expanded);
            auto DrawTaskDetailMetaRow = [&](const char* leftLabel, auto drawLeft, const char* rightLabel, auto drawRight) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", leftLabel);
                ImGui::TableSetColumnIndex(1);
                drawLeft();
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", rightLabel);
                ImGui::TableSetColumnIndex(3);
                drawRight();
            };
            if (ImGui::BeginTable("task_detail_meta", 4, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("label_left", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("value_left", ImGuiTableColumnFlags_WidthStretch, 1.15f);
                ImGui::TableSetupColumn("label_right", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("value_right", ImGuiTableColumnFlags_WidthStretch, 1.15f);

                DrawTaskDetailMetaRow(
                    u8"Проект",
                    [&]() {
                        DrawClippedTextWithTooltip(projectName, u8"Без проекта", projectName.empty());
                    },
                    u8"Ст.",
                    [&]() {
                        ImGui::TextUnformatted(TaskStatusShortLabel(expanded->status));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TaskStatusLabel(expanded->status));
                        if (TaskNeedsXpAward(*expanded)) {
                            DrawTaskXpPendingBadge(*expanded);
                        }
                    });

                DrawTaskDetailMetaRow(
                    u8"Этап",
                    [&]() {
                        DrawClippedTextWithTooltip(pipelineName, u8"Без этапа", pipelineName.empty());
                        DrawTaskPipelineProgressHint(state, *expanded);
                    },
                    "P",
                    [&]() {
                        ImVec4 priorityAccent = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                        if (detailPriority >= kTaskPriorityCritical) {
                            priorityAccent = TaskErrorColor();
                        } else if (detailPriority >= kTaskPriorityHigh) {
                            priorityAccent = TaskWarningColor();
                        } else if (detailPriority == kTaskPriorityMedium) {
                            priorityAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                        }
                        DrawStatusPillGui("task_detail_priority", TaskPriorityCode(detailPriority),
                                          priorityAccent, TaskPriorityLabel(detailPriority),
                                          detailPriority >= kTaskPriorityHigh);
                    });

                DrawTaskDetailMetaRow(
                    u8"Дедл.",
                    [&]() {
                        if (expanded->deadlineAt > 0) {
                            const std::string deadlineText = FormatTimestamp(expanded->deadlineAt, "%d.%m %H:%M");
                            if (detailOverdue) {
                                ImGui::TextColored(TaskWarningColor(), "%s", deadlineText.c_str());
                            } else {
                                ImGui::TextUnformatted(deadlineText.c_str());
                            }
                            if (ImGui::IsItemHovered()) {
                                const std::string fullDeadline = FormatTimestamp(expanded->deadlineAt, "%Y-%m-%d %H:%M");
                                ImGui::SetTooltip("%s", fullDeadline.c_str());
                            }
                        } else {
                            ImGui::TextDisabled("-");
                        }
                    },
                    u8"Кат.",
                    [&]() {
                        ImGui::TextUnformatted(ClassificationLabel(expanded->category));
                    });

                DrawTaskDetailMetaRow(
                    u8"Штраф XP",
                    [&]() {
                        if (expanded->deadlinePenaltyPercent > 0) {
                            std::string penaltyLabel = "-" + std::to_string(expanded->deadlinePenaltyPercent) + "%";
                            DrawStatusPillGui("task_detail_penalty", penaltyLabel.c_str(),
                                              TaskWarningColor(),
                                              u8"Штраф за срыв дедлайна.", true);
                        } else {
                            ImGui::TextDisabled("-");
                        }
                    },
                    u8"Люди",
                    [&]() {
                        const std::string assigneeSummary = BuildTaskAssigneeSummary(*expanded, 2);
                        ImGui::TextUnformatted(assigneeSummary.c_str());
                    });
                ImGui::EndTable();
            }
            if (expandedPipelineProgress.state == TaskPipelineProgressState::Missing) {
                ImGui::Spacing();
                DrawStatusPillGui("task_detail_pipeline_missing", u8"Вне пайплайна",
                                  TaskWarningColor(),
                                  u8"Задача пока вне пайплайна.", true);
                ImGui::TextWrapped(u8"Назначьте стартовый этап, чтобы отслеживать движение задачи по pipeline.");
            } else if (expandedPipelineProgress.state == TaskPipelineProgressState::Unknown) {
                ImGui::Spacing();
                DrawStatusPillGui("task_detail_pipeline_unknown", u8"Этап не найден",
                                  TaskErrorColor(),
                                  u8"Этап задачи не найден в текущей схеме.", true);
                ImGui::TextWrapped(u8"Лучше переназначить шаг вручную, иначе быстрые переходы pipeline будут непредсказуемы.");
            }
            if (NormalizeTaskStatus(expanded->status) == kTaskStatusDone && !TaskHasRecordedXp(*expanded)) {
                ImGui::Spacing();
                DrawStatusPillGui("task_detail_xp_pending", u8"XP не выдан",
                                  TaskWarningColor(),
                                  u8"Задача закрыта, но XP ещё не выдан.", true);
            }

            const int taskPipelineIndex = ResolveTaskPipelineIndex(state, *expanded);
            const int detailStatus = NormalizeTaskStatus(expanded->status);
            const bool detailHasRecordedXp = TaskHasRecordedXp(*expanded);
            const bool detailNeedsXpAward = TaskNeedsXpAward(*expanded);
            const std::vector<int> detailQuickPipelineTargets = (tasksAdminMode && editableTask)
                ? CollectTaskPipelineQuickTargets(*editableTask)
                : CollectTaskPipelineQuickTargets(*expanded);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled(u8"Следующее действие");
            if (tasksAdminMode && editableTask && !editableTask->id.empty()) {
                if (detailNeedsXpAward) {
                    ImGui::TextDisabled(u8"Закрыта без XP.");
                    if (TaskIconButton("task_detail_award_xp", UiIcon::Award, u8"Выдать XP по задаче")) {
                        StartTaskXpFlow(editableTask->id, true);
                    }
                } else if (expandedPipelineProgress.state == TaskPipelineProgressState::Missing &&
                           !detailQuickPipelineTargets.empty()) {
                    ImGui::TextDisabled(u8"Назначьте стартовый этап.");
                    DrawTaskPipelineQuickActionButtons("task_detail_next_missing_pipeline",
                                                       editableTask->id,
                                                       detailQuickPipelineTargets);
                } else if (expandedPipelineProgress.state == TaskPipelineProgressState::Unknown) {
                    ImGui::TextColored(TaskWarningColor(),
                                       u8"Текущий этап отсутствует в схеме. Ниже нужен ручной выбор корректного этапа.");
                } else if (detailStatus == kTaskStatusNew) {
                    ImGui::TextDisabled(u8"Ожидает старта.");
                    if (TaskIconButton("task_detail_start_work", UiIcon::Play, u8"Перевести в работу")) {
                        UpdateTaskStatus(editableTask->id, kTaskStatusInProgress);
                    }
                } else if (detailStatus == kTaskStatusInProgress) {
                    if (!detailQuickPipelineTargets.empty()) {
                        ImGui::TextDisabled(u8"Следующий pipeline-шаг.");
                        DrawTaskPipelineQuickActionButtons("task_detail_next_pipeline",
                                                           editableTask->id,
                                                           detailQuickPipelineTargets);
                    } else {
                        ImGui::TextDisabled(u8"Переходов дальше нет.");
                        if (TaskIconButton("task_detail_close_xp", UiIcon::Award, u8"Закрыть и выдать XP")) {
                            StartTaskXpFlow(editableTask->id, true);
                        }
                    }
                } else if (detailStatus == kTaskStatusDone && detailHasRecordedXp) {
                    ImGui::TextDisabled(u8"Закрыта, XP записан.");
                    if (TaskIconButton("task_detail_reopen", UiIcon::Play, u8"Вернуть в работу")) {
                        UpdateTaskStatus(editableTask->id, kTaskStatusInProgress);
                    }
                } else {
                    ImGui::TextDisabled(u8"Нет доступного shortcut для текущего состояния.");
                }
            } else {
                if (detailNeedsXpAward) {
                    ImGui::TextColored(TaskWarningColor(), u8"Ожидает выдачи XP администратором.");
                } else if (detailStatus == kTaskStatusNew) {
                    ImGui::TextDisabled(u8"Ожидает старта администратором.");
                } else if (detailStatus == kTaskStatusInProgress) {
                    ImGui::TextDisabled(u8"В работе. Следите за дедлайном и этапом pipeline.");
                } else {
                    ImGui::TextDisabled(detailHasRecordedXp ? u8"Закрыта, XP записан." : u8"Закрыта, XP ещё не записан.");
                }
            }

            if (ImGui::CollapsingHeader(u8"Контекст задачи")) {
                if (!expanded->description.empty()) {
                    ImGui::TextDisabled(u8"Описание");
                    ImGui::TextWrapped("%s", expanded->description.c_str());
                    ImGui::Spacing();
                }

                ImGui::TextDisabled(u8"Навыки задачи");
                ImGui::TextWrapped("%s", BuildTaskSkillSummary(expanded->skillIds, catalog, 6).c_str());

                ImGui::Spacing();
                ImGui::TextDisabled(u8"Исполнители");
                if (detailAssigneeNames.empty()) {
                    DrawEmptyStateGui("tasks_detail_assignees_empty", UiIcon::Users,
                                      u8"Нет назначенных профилей",
                                      u8"Назначьте исполнителей, чтобы задача появилась в профилях участников.");
                } else {
                    ImGui::TextWrapped("%s", BuildTaskAssigneeSummary(*expanded, 4).c_str());
                }

                if (taskPipelineIndex >= 0 && taskPipelineIndex < static_cast<int>(state.pipelineSteps.size())) {
                    const PipelineStep& step = state.pipelineSteps[static_cast<size_t>(taskPipelineIndex)];
                    const std::string nextSummary = BuildPipelineNextSummary(state, taskPipelineIndex);
                    if (!nextSummary.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled(u8"Следующий шаг");
                        ImGui::TextWrapped("%s", nextSummary.c_str());
                    }
                    if (!step.hints.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled(u8"Подсказки этапа");
                        const int hintLimit = std::min(3, static_cast<int>(step.hints.size()));
                        for (int i = 0; i < hintLimit; ++i) {
                            ImGui::BulletText("%s", step.hints[static_cast<size_t>(i)].c_str());
                        }
                        if (static_cast<int>(step.hints.size()) > hintLimit) {
                            ImGui::TextDisabled(u8"Еще: %d", static_cast<int>(step.hints.size()) - hintLimit);
                        }
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
            }

            if (tasksAdminMode && editableTask && !editableTask->id.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled(u8"Админские изменения применяются сразу.");

                const TaskPipelineProgressInfo editablePipelineProgress = DescribeTaskPipelineProgress(state, *editableTask);
                const int currentEditableStatus = NormalizeTaskStatus(editableTask->status);

                if (ImGui::CollapsingHeader(u8"Название и описание",
                                            state.taskInlineTextEditId == editableTask->id ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                    if (state.taskInlineTextEditId != editableTask->id) {
                        ImGui::TextDisabled(u8"Название");
                        ImGui::TextWrapped("%s", editableTask->title.empty() ? u8"Без названия" : editableTask->title.c_str());
                        ImGui::Spacing();
                        ImGui::TextDisabled(u8"Описание");
                        ImGui::TextWrapped("%s", editableTask->description.empty() ? u8"Без описания" : editableTask->description.c_str());
                        ImGui::Spacing();
                        if (TaskIconButton("task_detail_text_edit", UiIcon::Edit, u8"Редактировать название и описание")) {
                            std::snprintf(state.taskInlineTitleBuffer.data(), state.taskInlineTitleBuffer.size(),
                                          "%s", editableTask->title.c_str());
                            std::snprintf(state.taskInlineDescriptionBuffer.data(), state.taskInlineDescriptionBuffer.size(),
                                          "%s", editableTask->description.c_str());
                            state.taskInlineTextEditId = editableTask->id;
                        }
                    } else {
                        ImGui::TextDisabled(u8"Название");
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputTextWithHint("##task_detail_title_edit", u8"Что нужно сделать",
                                                 state.taskInlineTitleBuffer.data(), state.taskInlineTitleBuffer.size());
                        ImGui::TextDisabled(u8"Описание");
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputTextMultiline("##task_detail_description_edit",
                                                  state.taskInlineDescriptionBuffer.data(),
                                                  state.taskInlineDescriptionBuffer.size(),
                                                  ImVec2(-1.0f, 110.0f));
                        if (ImGui::BeginTable("task_detail_text_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            if (TaskIconButton("task_detail_text_save", UiIcon::Save, u8"Сохранить текст задачи")) {
                                if (UpdateTaskText(editableTask->id,
                                                   state.taskInlineTitleBuffer.data(),
                                                   state.taskInlineDescriptionBuffer.data())) {
                                    state.taskInlineTextEditId.clear();
                                }
                            }
                            ImGui::TableSetColumnIndex(1);
                            if (TaskIconButton("task_detail_text_cancel", UiIcon::Close, u8"Отменить редактирование текста")) {
                                state.taskInlineTextEditId.clear();
                                state.taskInlineTitleBuffer.fill('\0');
                                state.taskInlineDescriptionBuffer.fill('\0');
                            }
                            ImGui::EndTable();
                        }
                    }
                }

                if (ImGui::CollapsingHeader(u8"Параметры задачи")) {
                    const char* statusLabels[] = {u8"Новая", u8"В работе", u8"Выполнена"};
                    const char* priorityLabels[] = {u8"Низкий", u8"Средний", u8"Высокий", u8"Критический"};
                    if (ImGui::BeginTable("task_detail_admin_meta", 2, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("task_detail_admin_left", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableSetupColumn("task_detail_admin_right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled(u8"Статус");
                        int draftStatus = NormalizeTaskStatus(editableTask->status);
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::Combo("##task_detail_status", &draftStatus, statusLabels, IM_ARRAYSIZE(statusLabels))) {
                            UpdateTaskStatus(editableTask->id, draftStatus);
                        }

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextDisabled(u8"Приоритет");
                        int draftPriority = NormalizeTaskPriority(editableTask->priority);
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::Combo("##task_detail_priority", &draftPriority, priorityLabels, IM_ARRAYSIZE(priorityLabels))) {
                            UpdateTaskPriority(editableTask->id, draftPriority);
                        }

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled(u8"Категория XP");
                        int draftCategory = std::clamp(editableTask->category, 0, static_cast<int>(Profile::kCategoryCount - 1));
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::Combo("##task_detail_category", &draftCategory,
                                         Profile::kCategoryLabels.data(), Profile::kCategoryCount)) {
                            UpdateTaskCategory(editableTask->id, draftCategory);
                        }

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextDisabled(u8"Штраф дедлайна");
                        int draftPenalty = std::clamp(editableTask->deadlinePenaltyPercent, 0, 100);
                        if (ImGui::SliderInt("##task_detail_penalty", &draftPenalty, 0, 100, "%d%%",
                                             ImGuiSliderFlags_AlwaysClamp)) {
                            UpdateTaskPenalty(editableTask->id, draftPenalty);
                        }
                        ImGui::EndTable();
                    }
                    if (editableTask->deadlinePenaltyPercent > 0) {
                        ImGui::TextDisabled(u8"Штраф уменьшит итоговый XP-пул задачи на %d%%.", editableTask->deadlinePenaltyPercent);
                    }
                }

                if (ImGui::CollapsingHeader(u8"Навыки задачи",
                                            state.taskInlineSkillEditId == editableTask->id ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                    ImGui::TextWrapped("%s", BuildTaskSkillSummary(editableTask->skillIds, catalog, 6).c_str());
                    if (state.taskInlineSkillEditId != editableTask->id) {
                        if (TaskIconButton("task_detail_skill_edit", UiIcon::Edit, u8"Редактировать навыки задачи")) {
                            state.taskInlineSkillEditId = editableTask->id;
                            state.taskInlineSkillFilter.fill('\0');
                            ApplyTaskSkillIdsToFlags(catalog, editableTask->skillIds, state.taskInlineSkillFlags);
                        }
                    } else {
                        ImGui::Spacing();
                        ImGui::InputTextWithHint("##task_detail_skill_filter", u8"Фильтр навыков",
                                                 state.taskInlineSkillFilter.data(), state.taskInlineSkillFilter.size());
                        if (ImGui::BeginChild("task_detail_skills", ImVec2(0.0f, 150.0f), true)) {
                            const auto& catalogSkills = catalog.skills();
                            for (size_t i = 0; i < catalogSkills.size(); ++i) {
                                const std::string displayName = catalog.display_name(catalogSkills[i]);
                                if (!MatchesFilter(displayName, state.taskInlineSkillFilter.data())) continue;
                                bool checked = (i < state.taskInlineSkillFlags.size() && state.taskInlineSkillFlags[i] != 0);
                                if (ImGui::Checkbox((displayName + "##task_detail_skill").c_str(), &checked)) {
                                    if (i < state.taskInlineSkillFlags.size()) {
                                        state.taskInlineSkillFlags[i] = checked ? 1 : 0;
                                    }
                                }
                            }
                        }
                        ImGui::EndChild();
                        if (ImGui::BeginTable("task_detail_skill_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            if (TaskIconButton("task_detail_skill_save", UiIcon::Save, u8"Сохранить навыки задачи")) {
                                const std::vector<std::string> skillIds =
                                    CollectSelectedTaskSkillIds(catalog, state.taskInlineSkillFlags);
                                if (UpdateTaskSkills(editableTask->id, skillIds)) {
                                    state.taskInlineSkillEditId.clear();
                                }
                            }
                            ImGui::TableSetColumnIndex(1);
                            if (TaskIconButton("task_detail_skill_cancel", UiIcon::Close, u8"Отменить редактирование навыков")) {
                                state.taskInlineSkillEditId.clear();
                                state.taskInlineSkillFilter.fill('\0');
                            }
                            ImGui::EndTable();
                        }
                    }
                }

                const bool openPipelineAdmin =
                    editablePipelineProgress.state == TaskPipelineProgressState::Missing ||
                    editablePipelineProgress.state == TaskPipelineProgressState::Unknown ||
                    editablePipelineProgress.state == TaskPipelineProgressState::Final ||
                    TaskNeedsXpAward(*editableTask);
                if (ImGui::CollapsingHeader(u8"Пайплайн и handoff",
                                            openPipelineAdmin ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                    if (editablePipelineProgress.state == TaskPipelineProgressState::Final) {
                        ImGui::TextDisabled(u8"Статус финального handoff");
                        if (currentEditableStatus == kTaskStatusNew) {
                            if (TaskIconButton("task_detail_handoff_start", UiIcon::Play, u8"Начать handoff")) {
                                UpdateTaskStatus(editableTask->id, kTaskStatusInProgress);
                            }
                            ImGui::TextDisabled(u8"Shortcut не пропускает этапы: сначала 'В работе', затем 'Выполнена'.");
                        } else if (currentEditableStatus == kTaskStatusInProgress) {
                            if (TaskIconButton("task_detail_handoff_close", UiIcon::Award, u8"Закрыть после handoff и выдать XP")) {
                                StartTaskXpFlow(editableTask->id, true);
                            }
                            ImGui::TextDisabled(u8"Используйте это после завершения финального handoff.");
                        } else {
                            if (TaskIconButton("task_detail_handoff_reopen", UiIcon::Play, u8"Вернуть в работу")) {
                                UpdateTaskStatus(editableTask->id, kTaskStatusInProgress);
                            }
                            ImGui::TextDisabled(u8"Новая из 'Выполнена' недоступна по правилам, поэтому shortcut возвращает в 'В работе'.");
                        }
                        ImGui::Spacing();
                    }

                    const int editablePipelineIndex = ResolveTaskPipelineIndex(state, *editableTask);
                    const std::vector<int> quickPipelineTargets = CollectTaskPipelineQuickTargets(*editableTask);
                    if (!state.pipelineSteps.empty()) {
                        if (editablePipelineIndex >= 0 && editablePipelineIndex < static_cast<int>(state.pipelineSteps.size())) {
                            if (!quickPipelineTargets.empty()) {
                                ImGui::TextDisabled(u8"Быстрый переход по пайплайну");
                                DrawTaskPipelineQuickActionButtons("task_detail_pipeline_actions",
                                                                   editableTask->id,
                                                                   quickPipelineTargets);
                            } else {
                                ImGui::TextDisabled(u8"Пайплайн: финальный этап, дальше переходов нет.");
                                if (NormalizeTaskStatus(editableTask->status) != kTaskStatusDone) {
                                    ImGui::TextDisabled(u8"Если handoff завершён, переведите задачу в статус 'Выполнена'.");
                                }
                            }
                        } else if (editableTask->pipelineStepId.empty() && editableTask->pipelineStep.empty()) {
                            if (!quickPipelineTargets.empty()) {
                                ImGui::TextDisabled(u8"Стартовый этап");
                                DrawTaskPipelineQuickActionButtons("task_detail_pipeline_start_actions",
                                                                   editableTask->id,
                                                                   quickPipelineTargets);
                            } else {
                                ImGui::TextDisabled(u8"Стартовый этап не найден. Используйте ручной выбор ниже.");
                            }
                        } else {
                            ImGui::TextDisabled(u8"Текущий этап не найден в схеме. Используйте ручной выбор ниже.");
                        }
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled(u8"Ручной выбор этапа");
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

                    if (NormalizeTaskStatus(editableTask->status) != kTaskStatusDone || !TaskHasRecordedXp(*editableTask)) {
                        ImGui::Spacing();
                        if (TaskIconButton("task_detail_pipeline_close_xp", UiIcon::Award, u8"Закрыть и выдать XP")) {
                            StartTaskXpFlow(editableTask->id, true);
                        }
                    }
                }

                if (ImGui::CollapsingHeader(u8"Удаление и откат")) {
                    bool hasXp = false;
                    for (const auto& participant : editableTask->participants) {
                        if (participant.globalXp > 0 || participant.skillXp > 0) {
                            hasXp = true;
                            break;
                        }
                    }
                    if (hasXp) {
                        ImGui::TextColored(TaskWarningColor(),
                                           u8"Удаление откатит уже записанный XP по участникам задачи.");
                    } else {
                        ImGui::TextDisabled(u8"Удаление уберёт задачу из журнала без XP-отката.");
                    }
                    const char* deleteLabel = hasXp ? u8"Удалить задачу и откатить XP" : u8"Удалить задачу";
                    if (ImGui::Button(deleteLabel, ImVec2(-1.0f, 0.0f))) {
                        state.pendingTaskDeleteId = editableTask->id;
                        state.taskDeletePopupRequest = true;
                    }
                }
            }
        }
        EndCard();
    }
    if (useTaskSplitView) {
        if (!expanded) {
            if (BeginCard("tasks_detail_placeholder")) {
                DrawCardHeader(u8"Детали задачи");
                DrawEmptyStateGui("tasks_detail_empty_selection", UiIcon::Task,
                                  u8"Задача не выбрана",
                                  u8"Выберите задачу по названию в таблице, чтобы открыть описание, исполнителей, XP и админские действия.");
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
