void DrawTasksPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Tasks, state.storageDir);
    DrawCardHeader(u8"Задачи");
    ImGui::TextDisabled(u8"Все выполненные задачи с распределением вклада.");
    if (state.tasks.empty()) {
        ImGui::TextDisabled(u8"Задач пока нет.");
        return;
    }

    std::unordered_map<std::string, std::string> nameById;
    nameById.reserve(state.profiles.size());
    for (const auto& info : state.profiles) {
        nameById[info.id] = info.name;
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

    std::vector<std::string> categoryLabels;
    categoryLabels.reserve(Profile::kCategoryCount + 1);
    categoryLabels.push_back(u8"Все категории");
    for (const char* label : Profile::kCategoryLabels) {
        categoryLabels.push_back(label ? label : "");
    }
    std::vector<const char*> categoryLabelPtrs;
    categoryLabelPtrs.reserve(categoryLabels.size());
    for (const auto& label : categoryLabels) {
        categoryLabelPtrs.push_back(label.c_str());
    }
    state.tasksFilterCategory = std::clamp(state.tasksFilterCategory, 0,
                                           static_cast<int>(categoryLabelPtrs.size()) - 1);

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
        ImGui::TextDisabled(u8"Категория");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_category", &state.tasksFilterCategory,
                         categoryLabelPtrs.data(), static_cast<int>(categoryLabelPtrs.size()))) {
            state.tasksFilterCategory = std::clamp(state.tasksFilterCategory, 0, static_cast<int>(categoryLabelPtrs.size()) - 1);
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
            std::vector<const TaskEntry*> filtered;
            filtered.reserve(state.tasks.size());
            const std::int64_t nowSec = NowSeconds();
            const std::int64_t minTs = state.tasksFilterDays > 0
                ? nowSec - static_cast<std::int64_t>(state.tasksFilterDays) * 86400
                : 0;
            const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
                state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
                ? state.profiles[state.tasksFilterProfile - 1].id
                : std::string();
            for (const auto& task : state.tasks) {
                const std::string filterText = task.project + " " + task.title + " " + task.description;
                if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
                if (state.tasksFilterCategory > 0 && task.category != state.tasksFilterCategory - 1) continue;
                if (!filterProfileId.empty() && !FindTaskParticipant(task, filterProfileId)) continue;
                if (minTs > 0 && task.createdAt < minTs) continue;
                filtered.push_back(&task);
            }
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
            std::vector<const TaskEntry*> filtered;
            filtered.reserve(state.tasks.size());
            const std::int64_t nowSec = NowSeconds();
            const std::int64_t minTs = state.tasksFilterDays > 0
                ? nowSec - static_cast<std::int64_t>(state.tasksFilterDays) * 86400
                : 0;
            const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
                state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
                ? state.profiles[state.tasksFilterProfile - 1].id
                : std::string();
            for (const auto& task : state.tasks) {
                const std::string filterText = task.project + " " + task.title + " " + task.description;
                if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
                if (state.tasksFilterCategory > 0 && task.category != state.tasksFilterCategory - 1) continue;
                if (!filterProfileId.empty() && !FindTaskParticipant(task, filterProfileId)) continue;
                if (minTs > 0 && task.createdAt < minTs) continue;
                filtered.push_back(&task);
            }
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
        ImGui::TableSetupColumn("filter_category", ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
        ImGui::TextDisabled(u8"Категория");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_category", &state.tasksFilterCategory,
                         categoryLabelPtrs.data(), static_cast<int>(categoryLabelPtrs.size()))) {
            state.tasksFilterCategory = std::clamp(state.tasksFilterCategory, 0, static_cast<int>(categoryLabelPtrs.size()) - 1);
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
                std::vector<const TaskEntry*> filtered;
                filtered.reserve(state.tasks.size());
                const std::int64_t nowSec = NowSeconds();
                const std::int64_t minTs = state.tasksFilterDays > 0
                    ? nowSec - static_cast<std::int64_t>(state.tasksFilterDays) * 86400
                    : 0;
                const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
                    state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
                    ? state.profiles[state.tasksFilterProfile - 1].id
                    : std::string();
                for (const auto& task : state.tasks) {
                    const std::string filterText = task.project + " " + task.title + " " + task.description;
                    if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
                    if (state.tasksFilterCategory > 0 && task.category != state.tasksFilterCategory - 1) continue;
                    if (!filterProfileId.empty() && !FindTaskParticipant(task, filterProfileId)) continue;
                    if (minTs > 0 && task.createdAt < minTs) continue;
                    filtered.push_back(&task);
                }
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
                std::vector<const TaskEntry*> filtered;
                filtered.reserve(state.tasks.size());
                const std::int64_t nowSec = NowSeconds();
                const std::int64_t minTs = state.tasksFilterDays > 0
                    ? nowSec - static_cast<std::int64_t>(state.tasksFilterDays) * 86400
                    : 0;
                const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
                    state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
                    ? state.profiles[state.tasksFilterProfile - 1].id
                    : std::string();
                for (const auto& task : state.tasks) {
                    const std::string filterText = task.project + " " + task.title + " " + task.description;
                    if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
                    if (state.tasksFilterCategory > 0 && task.category != state.tasksFilterCategory - 1) continue;
                    if (!filterProfileId.empty() && !FindTaskParticipant(task, filterProfileId)) continue;
                    if (minTs > 0 && task.createdAt < minTs) continue;
                    filtered.push_back(&task);
                }
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
    const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
        state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
        ? state.profiles[state.tasksFilterProfile - 1].id
        : std::string();

    std::vector<const TaskEntry*> filtered;
    filtered.reserve(ordered.size());
    for (const TaskEntry* task : ordered) {
        const std::string filterText = task->project + " " + task->title + " " + task->description;
        if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
        if (state.tasksFilterCategory > 0 && task->category != state.tasksFilterCategory - 1) continue;
        if (!filterProfileId.empty() && !FindTaskParticipant(*task, filterProfileId)) continue;
        if (minTs > 0 && task->createdAt < minTs) continue;
        filtered.push_back(task);
    }

    int visibleCount = 0;
    if (ImGui::BeginTable("tasks_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"Дата", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(u8"Участники", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(u8"Категория", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(u8"Оценка", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (const TaskEntry* task : filtered) {
            ++visibleCount;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string stamp = FormatTimestamp(task->createdAt, "%d.%m.%Y %H:%M");
            ImGui::TextDisabled("%s", stamp.empty() ? "-" : stamp.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextUnformatted(task->project.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(task->title.c_str());
            if (!task->description.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextWrapped("%s", task->description.c_str());
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(3);
            const std::string participants = BuildParticipantLabel(*task, nameById);
            ImGui::TextUnformatted(participants.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(ClassificationLabel(task->category));
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d/10", task->score);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, static_cast<int>(state.tasks.size()));
    if (visibleCount == 0) {
        ImGui::TextDisabled(u8"Нет задач по фильтру.");
    }
}
