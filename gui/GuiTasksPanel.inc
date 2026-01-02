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
    ImGui::InputTextWithHint(u8"##tasks_filter", u8"Поиск по проекту, задаче или описанию",
                             state.filters.tasks.data(), state.filters.tasks.size());

    std::vector<const TaskEntry*> ordered;
    ordered.reserve(state.tasks.size());
    for (const auto& task : state.tasks) {
        ordered.push_back(&task);
    }
    std::sort(ordered.begin(), ordered.end(), [](const TaskEntry* a, const TaskEntry* b) {
        return a->createdAt > b->createdAt;
    });

    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);

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
        for (const TaskEntry* task : ordered) {
            const std::string filterText = task->project + " " + task->title + " " + task->description;
            if (!MatchesFilter(filterText, state.filters.tasks.data())) {
                continue;
            }
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
