static void SyncPipelineEditBuffers(GuiState& state, int index) {
    if (index < 0 | index >= static_cast<int>(state.pipelineSteps.size())) return;
    if (state.pipelineEditIndex == index) return;
    std::snprintf(state.pipelineTitle.data(), state.pipelineTitle.size(), "%s",
                  state.pipelineSteps[index].title.c_str());
    std::snprintf(state.pipelineDescription.data(), state.pipelineDescription.size(), "%s",
                  state.pipelineSteps[index].description.c_str());
    state.pipelineEditIndex = index;
}

void DrawPipelinePanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Pipeline, state.storageDir);
    if (!state.modules.pipeline) {
        DrawCardHeader(u8"Пайплайн");
        ImGui::TextDisabled(u8"Модуль пайплайна отключён.");
        return;
    }
    const int stepCount = static_cast<int>(state.pipelineSteps.size());
    if (stepCount == 0) {
        DrawCardHeader(u8"Пайплайн");
        ImGui::TextDisabled(u8"Пайплайн пуст.");
        if (state.isAdmin) {
            if (ImGui::Button(u8"Добавить этап")) {
                AppPipelineMutationResult addResult = AppAddPipelineStep(state.storageDir, state.pipelineSteps, stepCount - 1);
                if (addResult.ok) {
                    state.selectedPipelineIndex = addResult.selectedIndex;
                    state.pipelineEditIndex = -1;
                    SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
                    SetStatus(state, u8"Этап добавлен.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, addResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }
        }
        return;
    }
    DrawCardHeader(u8"Пайплайн");
    ImGui::TextDisabled(u8"Пайплайн - последовательность шагов развития.");
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
        if (MatchesFilter(state.pipelineSteps[i].title, state.filters.pipeline.data())) {
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
    if (state.selectedPipelineIndex < 0 | state.selectedPipelineIndex >= stepCount) {
        state.selectedPipelineIndex = 0;
    }
    if (!MatchesFilter(state.pipelineSteps[state.selectedPipelineIndex].title, state.filters.pipeline.data())) {
        for (int i = 0; i < stepCount; ++i) {
            if (MatchesFilter(state.pipelineSteps[i].title, state.filters.pipeline.data())) {
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
        ImGuiTableFlags listFlags = ImGuiTableFlags_RowBg |
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
                if (!MatchesFilter(state.pipelineSteps[i].title, state.filters.pipeline.data())) {
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
                    ImGui::TextColored(warnText, "%s", state.pipelineSteps[i].title.c_str());
                } else {
                    ImGui::TextUnformatted(state.pipelineSteps[i].title.c_str());
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
            const PipelineStep& step = state.pipelineSteps[state.selectedPipelineIndex];
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImGui::TextColored(accent, "%s", step.title.c_str());
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            const float wrapPos = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::PushTextWrapPos(wrapPos);
            ImGui::TextUnformatted(step.description.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextDisabled(u8"Выберите фильтр или сбросьте его, чтобы увидеть этапы.");
        }
        if (focusIndex >= 0) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"Зона внимания");
            ImGui::SameLine();
            ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", state.pipelineSteps[focusIndex].title.c_str());
        }
        if (state.isAdmin) {
            ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
            DrawCardHeader(u8"Редактирование");
            SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
            ImGui::TextDisabled(u8"Название этапа");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText(u8"##pipeline_title", state.pipelineTitle.data(), state.pipelineTitle.size());
            ImGui::TextDisabled(u8"Описание");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextMultiline(u8"##pipeline_desc", state.pipelineDescription.data(),
                                      state.pipelineDescription.size(), ImVec2(-1.0f, 160.0f));

            bool saveRequested = false;
            bool addRequested = false;
            bool deleteRequested = false;
            int moveFrom = -1;
            int moveTo = -1;

            if (ImGui::BeginTable("pipeline_edit_actions", 5, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                    saveRequested = true;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Добавить", ImVec2(-1.0f, 0.0f))) {
                    addRequested = true;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, 0.0f))) {
                    deleteRequested = true;
                }
                ImGui::TableNextColumn();
                const bool canMoveUp = state.selectedPipelineIndex > 0;
                if (!canMoveUp) ImGui::BeginDisabled();
                if (ImGui::Button(u8"Вверх", ImVec2(-1.0f, 0.0f))) {
                    moveFrom = state.selectedPipelineIndex;
                    moveTo = state.selectedPipelineIndex - 1;
                }
                if (!canMoveUp) ImGui::EndDisabled();
                ImGui::TableNextColumn();
                const bool canMoveDown = state.selectedPipelineIndex + 1 < stepCount;
                if (!canMoveDown) ImGui::BeginDisabled();
                if (ImGui::Button(u8"Вниз", ImVec2(-1.0f, 0.0f))) {
                    moveFrom = state.selectedPipelineIndex;
                    moveTo = state.selectedPipelineIndex + 1;
                }
                if (!canMoveDown) ImGui::EndDisabled();
                ImGui::EndTable();
            }

            if (saveRequested) {
                AppPipelineMutationResult saveResult = AppUpdatePipelineStep(
                    state.storageDir,
                    state.pipelineSteps,
                    state.selectedPipelineIndex,
                    TrimStringGui(state.pipelineTitle.data()),
                    TrimStringGui(state.pipelineDescription.data()));
                if (saveResult.ok) {
                    state.selectedPipelineIndex = saveResult.selectedIndex;
                    SetStatus(state, u8"Пайплайн сохранён.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, saveResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }

            if (addRequested) {
                AppPipelineMutationResult addResult = AppAddPipelineStep(
                    state.storageDir,
                    state.pipelineSteps,
                    state.selectedPipelineIndex);
                if (addResult.ok) {
                    state.selectedPipelineIndex = addResult.selectedIndex;
                    state.pipelineEditIndex = -1;
                    SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
                    SetStatus(state, u8"Этап добавлен.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, addResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }

            if (deleteRequested) {
                AppPipelineMutationResult deleteResult = AppDeletePipelineStep(
                    state.storageDir,
                    state.pipelineSteps,
                    state.selectedPipelineIndex);
                if (deleteResult.ok) {
                    state.selectedPipelineIndex = deleteResult.selectedIndex;
                    state.pipelineEditIndex = -1;
                    SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
                    SetStatus(state, u8"Этап удалён.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, deleteResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }

            if (moveFrom >= 0 && moveTo >= 0 &&
                moveFrom < static_cast<int>(state.pipelineSteps.size()) &&
                moveTo < static_cast<int>(state.pipelineSteps.size())) {
                AppPipelineMutationResult moveResult = AppMovePipelineStep(
                    state.storageDir,
                    state.pipelineSteps,
                    moveFrom,
                    moveTo);
                if (moveResult.ok) {
                    state.selectedPipelineIndex = moveResult.selectedIndex;
                    state.pipelineEditIndex = -1;
                    SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
                    SetStatus(state, u8"Порядок этапов обновлён.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, moveResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }
        }
        ImGui::EndTable();
    }
}

