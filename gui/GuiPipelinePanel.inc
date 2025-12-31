void DrawPipelinePanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Pipeline, state.storageDir);
    const int stepCount = static_cast<int>(kPipelineSteps.size());
    if (stepCount == 0) {
        ImGui::TextUnformatted(u8"Пайплайн пуст.");
        return;
    }
    const float filterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilter = filterWidth < 360.0f;
    if (stackFilter) {
        ImGui::TextDisabled(u8"Фильтр");
        ImGui::InputTextWithHint(u8"##pipeline_filter", u8"Фильтр пайплайна",
                                 state.filters.pipeline.data(), state.filters.pipeline.size());
        if (ImGui::SmallButton(u8"Сброс##pipeline_filter")) {
            state.filters.pipeline.fill('\0');
        }
    } else if (ImGui::BeginTable("pipeline_filter_row", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("pipeline_filter", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("pipeline_reset", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled(u8"Фильтр");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##pipeline_filter", u8"Фильтр пайплайна",
                                 state.filters.pipeline.data(), state.filters.pipeline.size());
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            state.filters.pipeline.fill('\0');
        }
        ImGui::EndTable();
    }
    int visibleCount = 0;
    for (int i = 0; i < stepCount; ++i) {
        if (MatchesFilter(kPipelineSteps[i].title, state.filters.pipeline.data())) {
            visibleCount += 1;
        }
    }
    ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, stepCount);
    if (state.selectedPipelineIndex < 0 || state.selectedPipelineIndex >= stepCount) {
        state.selectedPipelineIndex = 0;
    }
    if (!MatchesFilter(kPipelineSteps[state.selectedPipelineIndex].title, state.filters.pipeline.data())) {
        for (int i = 0; i < stepCount; ++i) {
            if (MatchesFilter(kPipelineSteps[i].title, state.filters.pipeline.data())) {
                state.selectedPipelineIndex = i;
                break;
            }
        }
    }
    if (ImGui::BeginChild("pipeline_list", ImVec2(0, 140), true)) {
        for (int i = 0; i < stepCount; ++i) {
            if (!MatchesFilter(kPipelineSteps[i].title, state.filters.pipeline.data())) {
                continue;
            }
            bool selected = state.selectedPipelineIndex == i;
            if (ImGui::Selectable(kPipelineSteps[i].title, selected)) {
                state.selectedPipelineIndex = i;
            }
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (visibleCount > 0) {
        const PipelineStep& step = kPipelineSteps[state.selectedPipelineIndex];
        ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        ImGui::TextColored(accent, "%s", step.title);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 45.0f);
        ImGui::TextUnformatted(step.description);
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextDisabled(u8"Нет этапов по фильтру.");
    }
}

