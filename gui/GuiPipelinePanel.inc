void DrawPipelinePanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Pipeline, state.storageDir);
    const int stepCount = static_cast<int>(kPipelineSteps.size());
    if (stepCount == 0) {
        DrawCardHeader(u8"Пайплайн");
        ImGui::TextDisabled(u8"Пайплайн пуст.");
        return;
    }
    DrawCardHeader(u8"Пайплайн");
    ImGui::TextDisabled(u8"Пайплайн — последовательность шагов развития.");
    ImGui::TextDisabled(u8"Выберите этап, чтобы увидеть детали.");
    const float filterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilter = filterWidth < 360.0f;
    if (stackFilter) {
        ImGui::TextDisabled(u8"Фильтр");
        ImGui::InputTextWithHint(u8"##pipeline_filter", u8"Фильтр пайплайна",
                                 state.filters.pipeline.data(), state.filters.pipeline.size());
        if (ImGui::SmallButton(u8"Сброс##pipeline_filter")) {
            state.filters.pipeline.fill('\0');
        }
    } else if (ImGui::BeginTable("pipeline_filter_row", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("pipeline_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("pipeline_filter", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("pipeline_reset", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::TextDisabled(u8"Фильтр");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##pipeline_filter", u8"Фильтр пайплайна",
                                 state.filters.pipeline.data(), state.filters.pipeline.size());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            state.filters.pipeline.fill('\0');
        }
        ImGui::EndTable();
    }
    int visibleCount = 0;
    int focusIndex = -1;
    for (int i = 0; i < stepCount; ++i) {
        if (MatchesFilter(kPipelineSteps[i].title, state.filters.pipeline.data())) {
            visibleCount += 1;
            if (focusIndex == -1) {
                focusIndex = i;
            }
        }
    }
    ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, stepCount);
    if (visibleCount == 0) {
        ImGui::TextDisabled(u8"Нет этапов по фильтру.");
        if (ImGui::SmallButton(u8"Сбросить фильтр")) {
            state.filters.pipeline.fill('\0');
        }
    }
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
    const float listHeight = std::max(220.0f, ImGui::GetTextLineHeightWithSpacing() * 12.0f);
    if (ImGui::BeginTable("pipeline_split", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("pipeline_list", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("pipeline_detail", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("pipeline_list", ImVec2(0, listHeight), true)) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
        ImGuiTableFlags listFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_SizingStretchProp;
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        if (ImGui::BeginTable("pipeline_table", 2, listFlags)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(u8"Этап", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int rowRank = 0;
            for (int i = 0; i < stepCount; ++i) {
                if (!MatchesFilter(kPipelineSteps[i].title, state.filters.pipeline.data())) {
                    continue;
                }
                bool selected = state.selectedPipelineIndex == i;
                if (focusIndex == i) {
                    ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                    rowHighlight.w = std::clamp(rowHighlight.w * 0.16f, 0.08f, 0.22f);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string rowId = std::string("##pipeline_") + std::to_string(i);
                if (ImGui::Selectable(rowId.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    state.selectedPipelineIndex = i;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%d", rowRank + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                if (focusIndex == i) {
                    ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
                    warnText.w = 0.95f;
                    ImGui::TextColored(warnText, "%s", kPipelineSteps[i].title);
                } else {
                    ImGui::TextUnformatted(kPipelineSteps[i].title);
                }
                ++rowRank;
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        DrawCardHeader(u8"Описание этапа");
        if (visibleCount > 0) {
            const PipelineStep& step = kPipelineSteps[state.selectedPipelineIndex];
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImGui::TextColored(accent, "%s", step.title);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            const float wrapPos = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::PushTextWrapPos(wrapPos);
            ImGui::TextUnformatted(step.description);
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextDisabled(u8"Выберите фильтр или сбросьте его, чтобы увидеть этапы.");
        }
        if (focusIndex >= 0) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"Зона внимания");
            ImGui::SameLine();
            ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", kPipelineSteps[focusIndex].title);
        }
        ImGui::EndTable();
    }
}

