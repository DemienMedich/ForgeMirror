static void SyncPipelineEditBuffers(GuiState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.pipelineSteps.size())) return;
    if (state.pipelineEditIndex == index) return;
    std::snprintf(state.pipelineTitle.data(), state.pipelineTitle.size(), "%s",
                  state.pipelineSteps[index].title.c_str());
    std::snprintf(state.pipelineDescription.data(), state.pipelineDescription.size(), "%s",
                  state.pipelineSteps[index].description.c_str());
    state.pipelineEditIndex = index;
}

static bool BeginPipelinePane(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f)) {
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, CardRounding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, CardPadding());
    return ImGui::BeginChild(id, size, ImGuiChildFlags_Border);
}

static void EndPipelinePane() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

static std::string PipelineDisplayCode(const PipelineStep& step, int index) {
    if (!step.stageCode.empty()) return step.stageCode;
    return std::to_string(index + 1);
}

static std::string PipelineDisplayLabel(const PipelineStep& step, int index) {
    const std::string code = PipelineDisplayCode(step, index);
    if (code.empty()) return step.title;
    return code + "  " + step.title;
}

static std::string PipelineGroupLabel(const PipelineStep& step) {
    return step.branch.empty() ? std::string(u8"Без группы") : step.branch;
}

static std::string PipelineFilterText(const PipelineStep& step, int index) {
    std::string text = PipelineDisplayCode(step, index);
    text += " ";
    text += step.branch;
    text += " ";
    text += step.title;
    text += " ";
    text += step.description;
    text += " ";
    text += step.input;
    text += " ";
    text += step.output;
    text += " ";
    text += step.owner;
    text += " ";
    text += step.doneCriteria;
    text += " ";
    text += step.engineCheck;
    text += " ";
    text += step.risk;
    text += " ";
    text += step.nextStageLabel;
    text += " ";
    text += step.legacyNotes;
    return text;
}

static bool MatchesPipelineStepFilter(const PipelineStep& step, int index, const char* filter) {
    return MatchesFilter(PipelineFilterText(step, index), filter);
}

static int FindPipelineStepIndexById(const GuiState& state, const std::string& id) {
    if (id.empty()) return -1;
    for (int i = 0; i < static_cast<int>(state.pipelineSteps.size()); ++i) {
        if (state.pipelineSteps[i].id == id) return i;
    }
    return -1;
}

static std::vector<int> CollectIncomingPipelineIndices(const GuiState& state, const std::string& id) {
    std::vector<int> incoming;
    if (id.empty()) return incoming;
    for (int i = 0; i < static_cast<int>(state.pipelineSteps.size()); ++i) {
        const auto& step = state.pipelineSteps[i];
        for (const auto& nextId : step.nextIds) {
            if (nextId == id) {
                incoming.push_back(i);
                break;
            }
        }
    }
    return incoming;
}

static std::string BuildPipelineRoutePreview(const GuiState& state, int startIndex, int maxDepth = 3) {
    if (startIndex < 0 || startIndex >= static_cast<int>(state.pipelineSteps.size())) return {};
    std::string preview;
    int currentIndex = startIndex;
    for (int depth = 0; depth < maxDepth; ++depth) {
        if (currentIndex < 0 || currentIndex >= static_cast<int>(state.pipelineSteps.size())) break;
        const PipelineStep& step = state.pipelineSteps[currentIndex];
        if (!preview.empty()) preview += " -> ";
        preview += PipelineDisplayLabel(step, currentIndex);
        if (step.nextIds.size() != 1) {
            if (step.nextIds.size() > 1 && depth + 1 < maxDepth) {
                preview += " -> ...";
            }
            break;
        }
        const int nextIndex = FindPipelineStepIndexById(state, step.nextIds.front());
        if (nextIndex < 0 || nextIndex == currentIndex) break;
        currentIndex = nextIndex;
    }
    return preview;
}

static std::string BuildPipelineNextSummary(const GuiState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.pipelineSteps.size())) return {};
    const PipelineStep& step = state.pipelineSteps[index];
    if (!step.nextStageLabel.empty()) return step.nextStageLabel;
    if (step.nextIds.empty()) return u8"Финишный handoff";
    if (step.nextIds.size() == 1) {
        const int nextIndex = FindPipelineStepIndexById(state, step.nextIds.front());
        if (nextIndex >= 0) return BuildPipelineRoutePreview(state, nextIndex, 3);
        return u8"Следующий блок не найден";
    }
    std::string summary = u8"Ветвление: ";
    bool first = true;
    for (const auto& nextId : step.nextIds) {
        const int nextIndex = FindPipelineStepIndexById(state, nextId);
        if (nextIndex < 0) continue;
        if (!first) summary += " | ";
        first = false;
        summary += PipelineDisplayLabel(state.pipelineSteps[nextIndex], nextIndex);
    }
    return summary;
}

static void DrawPipelineWrappedText(const std::string& text) {
    if (text.empty()) {
        ImGui::TextDisabled(u8"Не заполнено.");
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

static void DrawPipelineFieldRow(const char* label, const std::string& value) {
    if (value.empty()) return;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    DrawPipelineWrappedText(value);
}

static bool DrawPipelineStageRow(const GuiState& state, int index, bool selected) {
    if (index < 0 || index >= static_cast<int>(state.pipelineSteps.size())) return false;
    const PipelineStep& step = state.pipelineSteps[index];
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = std::max(140.0f, ImGui::GetContentRegionAvail().x);
    const float height = 66.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, height);

    ImGui::PushID(index);
    ImGui::InvisibleButton("pipeline_stage_row", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 bg = CardBackgroundColor();
    bg.w = std::max(bg.w, 0.88f);
    if (selected) {
        bg = BlendColor(bg, accent, 0.24f);
        bg.w = 0.96f;
    } else if (hovered) {
        bg = BlendColor(bg, accent, 0.12f);
        bg.w = 0.92f;
    }
    ImVec4 border = selected ? accent : CardBorderColor();
    border.w = selected ? 0.70f : 0.26f;

    const ImVec2 p1(pos.x + size.x, pos.y + size.y);
    drawList->AddRectFilled(pos, p1, ImGui::GetColorU32(bg), CardRounding());
    drawList->AddRect(pos, p1, ImGui::GetColorU32(border), CardRounding(), 0, selected ? 2.0f : 1.0f);

    const float padX = 12.0f;
    const float padY = 10.0f;
    const float chipWidth = 52.0f;
    const ImVec2 chipMin(pos.x + padX, pos.y + padY);
    const ImVec2 chipMax(chipMin.x + chipWidth, chipMin.y + 24.0f);
    ImVec4 chipBg = selected ? accent : BlendColor(accent, style.Colors[ImGuiCol_FrameBg], 0.55f);
    chipBg.w = selected ? 0.92f : 0.54f;
    drawList->AddRectFilled(chipMin, chipMax, ImGui::GetColorU32(chipBg), 8.0f);
    drawList->AddRect(chipMin, chipMax, ImGui::GetColorU32(border), 8.0f, 0, 1.0f);

    const std::string code = PipelineDisplayCode(step, index);
    const std::string subtitle = BuildPipelineNextSummary(state, index);
    const float textX = chipMax.x + 12.0f;
    const ImVec4 titleColor = selected ? accent : style.Colors[ImGuiCol_Text];
    const ImVec4 metaColor = BlendColor(style.Colors[ImGuiCol_TextDisabled], accent, selected ? 0.24f : 0.10f);

    drawList->PushClipRect(pos, p1, true);
    drawList->AddText(ImVec2(chipMin.x + 10.0f, chipMin.y + 5.0f), ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), code.c_str());
    drawList->AddText(ImVec2(textX, pos.y + padY - 1.0f), ImGui::GetColorU32(titleColor), step.title.c_str());
    drawList->AddText(ImVec2(textX, pos.y + padY + 24.0f), ImGui::GetColorU32(metaColor), subtitle.c_str());
    drawList->PopClipRect();

    ImGui::PopID();
    return clicked;
}

static bool DrawPipelineJumpButton(const char* id,
                                   const std::string& label,
                                   const std::string& preview,
                                   bool accentButton = false) {
    bool pressed = false;
    ImGui::PushID(id);
    if (accentButton) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        ImVec4 bg = accent;
        ImVec4 hover = BlendColor(accent, style.Colors[ImGuiCol_ButtonHovered], 0.35f);
        ImVec4 active = BlendColor(accent, style.Colors[ImGuiCol_ButtonActive], 0.25f);
        bg.w = 0.85f;
        hover.w = 0.92f;
        active.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
        pressed = ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
        ImGui::PopStyleColor(3);
    } else {
        pressed = ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
    }
    if (!preview.empty()) {
        ImGui::TextDisabled("%s", preview.c_str());
    }
    ImGui::PopID();
    return pressed;
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
            if (ImGui::Button(u8"Добавить блок")) {
                AppPipelineMutationResult addResult = AppAddPipelineStep(state.storageDir, state.pipelineSteps, stepCount - 1);
                if (addResult.ok) {
                    state.selectedPipelineIndex = addResult.selectedIndex;
                    state.pipelineEditIndex = -1;
                    SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
                    SetStatus(state, u8"Блок добавлен.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, addResult.errorMessage, 1.0f, 0.45f, 0.45f);
                }
            }
        }
        return;
    }

    DrawCardHeader(u8"Пайплайн Forge Mirror");
    ImGui::TextDisabled(u8"Объединенный production-flow: текущий пайплайн Forge Mirror + схема из Excel.");
    ImGui::TextDisabled(u8"Выберите блок, чтобы увидеть подсказки, контроль в движке и следующий путь.");

    const float filterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilter = filterWidth < 420.0f;
    if (stackFilter) {
        ImGui::TextDisabled(u8"Фильтр");
        ImGui::InputTextWithHint(u8"##pipeline_filter", u8"Название, код, ветка, контроль",
                                 state.filters.pipeline.data(), state.filters.pipeline.size());
        if (ImGui::SmallButton(u8"Сброс фильтра")) {
            state.filters.pipeline.fill('\0');
        }
    } else if (ImGui::BeginTable("pipeline_filter_row", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("pipeline_filter_label", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("pipeline_filter_value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("pipeline_filter_action", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled(u8"Фильтр");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##pipeline_filter", u8"Название, код, ветка, контроль",
                                 state.filters.pipeline.data(), state.filters.pipeline.size());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
            state.filters.pipeline.fill('\0');
        }
        ImGui::EndTable();
    }

    std::vector<int> visibleIndices;
    visibleIndices.reserve(stepCount);
    std::vector<std::string> groupOrder;
    groupOrder.reserve(8);
    for (int i = 0; i < stepCount; ++i) {
        const auto& step = state.pipelineSteps[i];
        if (!MatchesPipelineStepFilter(step, i, state.filters.pipeline.data())) continue;
        visibleIndices.push_back(i);
        const std::string group = PipelineGroupLabel(step);
        bool exists = false;
        for (const auto& item : groupOrder) {
            if (item == group) {
                exists = true;
                break;
            }
        }
        if (!exists) groupOrder.push_back(group);
    }

    ImGui::TextDisabled(u8"Показано блоков: %d из %d", static_cast<int>(visibleIndices.size()), stepCount);
    if (visibleIndices.empty()) {
        ImGui::TextDisabled(u8"По текущему фильтру ничего не найдено.");
        return;
    }

    if (state.selectedPipelineIndex < 0 || state.selectedPipelineIndex >= stepCount) {
        state.selectedPipelineIndex = visibleIndices.front();
    } else {
        bool selectedVisible = false;
        for (int index : visibleIndices) {
            if (index == state.selectedPipelineIndex) {
                selectedVisible = true;
                break;
            }
        }
        if (!selectedVisible) state.selectedPipelineIndex = visibleIndices.front();
    }

    if (ImGui::BeginTable("pipeline_split", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("pipeline_map", ImGuiTableColumnFlags_WidthStretch, 0.44f);
        ImGui::TableSetupColumn("pipeline_detail", ImGuiTableColumnFlags_WidthStretch, 0.56f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (BeginPipelinePane("pipeline_map_pane", ImVec2(0.0f, 0.0f))) {
            DrawCardHeader(u8"Карта блоков");
            for (size_t groupIndex = 0; groupIndex < groupOrder.size(); ++groupIndex) {
                if (groupIndex > 0) ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
                const std::string& group = groupOrder[groupIndex];
                ImGui::TextDisabled("%s", group.c_str());
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                for (int index : visibleIndices) {
                    if (PipelineGroupLabel(state.pipelineSteps[index]) != group) continue;
                    const bool selected = state.selectedPipelineIndex == index;
                    if (DrawPipelineStageRow(state, index, selected)) {
                        state.selectedPipelineIndex = index;
                    }
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                }
            }
        }
        EndPipelinePane();

        ImGui::TableSetColumnIndex(1);
        if (BeginPipelinePane("pipeline_detail_pane", ImVec2(0.0f, 0.0f))) {
            const PipelineStep& step = state.pipelineSteps[state.selectedPipelineIndex];
            const std::string title = PipelineDisplayLabel(step, state.selectedPipelineIndex);
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;

            ImGui::TextColored(accent, "%s", title.c_str());
            if (!step.branch.empty()) {
                ImGui::TextDisabled("%s", step.branch.c_str());
            }
            if (ImGui::BeginTable("pipeline_meta_table", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("pipeline_meta_label", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableSetupColumn("pipeline_meta_value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                DrawPipelineFieldRow(u8"Вход", step.input);
                DrawPipelineFieldRow(u8"Выход", step.output);
                DrawPipelineFieldRow(u8"Ответственный", step.owner);
                DrawPipelineFieldRow(u8"Критерий готовности", step.doneCriteria);
                DrawPipelineFieldRow(u8"Следующий этап", step.nextStageLabel);
                ImGui::EndTable();
            }

            ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
            DrawCardHeader(u8"Что делаем");
            DrawPipelineWrappedText(step.description);

            ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
            DrawCardHeader(u8"Подсказки");
            if (step.hints.empty()) {
                ImGui::TextDisabled(u8"Для этого блока подсказки пока не заданы.");
            } else {
                for (const auto& hint : step.hints) {
                    ImGui::BulletText("%s", hint.c_str());
                }
            }

            ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
            DrawCardHeader(u8"Проверка в движке");
            DrawPipelineWrappedText(step.engineCheck);

            const std::vector<int> incoming = CollectIncomingPipelineIndices(state, step.id);
            ImGui::Dummy(ImVec2(0.0f, CardSpacing()));
            DrawCardHeader(u8"Путь");
            if (!incoming.empty()) {
                ImGui::TextDisabled(u8"Приходит из");
                for (int prevIndex : incoming) {
                    const std::string label = PipelineDisplayLabel(state.pipelineSteps[prevIndex], prevIndex);
                    if (ImGui::Button(label.c_str())) {
                        state.selectedPipelineIndex = prevIndex;
                    }
                    if (prevIndex != incoming.back()) ImGui::SameLine();
                }
            }
            if (incoming.empty()) {
                ImGui::TextDisabled(u8"Стартовая точка пайплайна.");
            }
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            if (!step.nextStageLabel.empty()) {
                ImGui::TextDisabled(u8"По таблице");
                DrawPipelineWrappedText(step.nextStageLabel);
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            }
            if (step.nextIds.empty()) {
                ImGui::TextDisabled(u8"Финальная точка пайплайна.");
            } else {
                ImGui::TextDisabled(u8"Ведет к");
                for (size_t nextIdx = 0; nextIdx < step.nextIds.size(); ++nextIdx) {
                    const int targetIndex = FindPipelineStepIndexById(state, step.nextIds[nextIdx]);
                    if (targetIndex < 0) continue;
                    const std::string label = PipelineDisplayLabel(state.pipelineSteps[targetIndex], targetIndex);
                    const std::string preview = BuildPipelineRoutePreview(state, targetIndex, 3);
                    if (DrawPipelineJumpButton(("pipeline_jump_" + std::to_string(targetIndex)).c_str(),
                                               label,
                                               preview,
                                               nextIdx == 0 && step.nextIds.size() == 1)) {
                        state.selectedPipelineIndex = targetIndex;
                    }
                    if (nextIdx + 1 < step.nextIds.size()) {
                        ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    }
                }
            }

            if (!step.risk.empty() && ImGui::CollapsingHeader(u8"Риски и возвраты")) {
                DrawPipelineWrappedText(step.risk);
            }
            if (!step.legacyNotes.empty() && ImGui::CollapsingHeader(u8"Практика Forge Mirror")) {
                DrawPipelineWrappedText(step.legacyNotes);
            }

            if (state.isAdmin) {
                ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
                if (ImGui::CollapsingHeader(u8"Редактирование")) {
                    SyncPipelineEditBuffers(state, state.selectedPipelineIndex);
                    ImGui::TextDisabled(u8"Быстрое редактирование названия и описания.");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText(u8"##pipeline_title", state.pipelineTitle.data(), state.pipelineTitle.size());
                    ImGui::TextDisabled(u8"Описание");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextMultiline(u8"##pipeline_desc", state.pipelineDescription.data(),
                                              state.pipelineDescription.size(), ImVec2(-1.0f, 150.0f));

                    bool saveRequested = false;
                    bool addRequested = false;
                    bool deleteRequested = false;
                    int moveFrom = -1;
                    int moveTo = -1;

                    if (ImGui::BeginTable("pipeline_edit_actions", 5, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        {
                            const ImGuiStyle& style = ImGui::GetStyle();
                            ImVec4 accentButton = style.Colors[ImGuiCol_CheckMark];
                            accentButton.w = 0.86f;
                            ImVec4 accentHover = BlendColor(accentButton, style.Colors[ImGuiCol_ButtonHovered], 0.35f);
                            ImVec4 accentActive = BlendColor(accentButton, style.Colors[ImGuiCol_ButtonActive], 0.25f);
                            ImGui::PushStyleColor(ImGuiCol_Button, accentButton);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentHover);
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentActive);
                            if (ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                                saveRequested = true;
                            }
                            ImGui::PopStyleColor(3);
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
                            SetStatus(state, u8"Пайплайн сохранен.", 0.45f, 0.9f, 0.45f);
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
                            SetStatus(state, u8"Блок добавлен.", 0.45f, 0.9f, 0.45f);
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
                            SetStatus(state, u8"Блок удален.", 0.45f, 0.9f, 0.45f);
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
                            SetStatus(state, u8"Порядок блоков обновлен.", 0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state, moveResult.errorMessage, 1.0f, 0.45f, 0.45f);
                        }
                    }
                }
            }
        }
        EndPipelinePane();

        ImGui::EndTable();
    }
}
