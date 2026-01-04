static void DrawCatalogKpiCard(const char* id, UiIcon icon, const char* title, const std::string& value,
                               const ImVec4& accent, float height = 70.0f) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rounding = std::max(9.0f, style.FrameRounding + 3.0f);
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    if (ImGui::BeginChild("catalog_kpi", ImVec2(0.0f, height), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float contentH = std::max(0.0f, ImGui::GetContentRegionAvail().y);
        const float badgeSize = std::clamp(height - 24.0f, 30.0f, 42.0f);
        const float badgeRadius = badgeSize * 0.5f;
        const ImVec2 badgePos = ImGui::GetCursorScreenPos();
        const ImVec2 badgeCenter(badgePos.x + badgeRadius, badgePos.y + contentH * 0.5f);
        DrawIconBadge(drawList, icon, badgeCenter, badgeRadius, accent, true);

        ImGui::Dummy(ImVec2(badgeSize + 10.0f, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        float y = ImGui::GetCursorPosY();
        float textBlock = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        float offsetY = std::max(0.0f, (contentH - textBlock) * 0.5f);
        ImGui::SetCursorPosY(y + offsetY);
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", title);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SetWindowFontScale(1.12f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void DrawSkillCatalogPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::SkillCatalog, state.storageDir);
    const auto& catalogSkills = catalog.skills();
    if (state.selectedCatalogIndex >= static_cast<int>(catalogSkills.size())) {
        state.selectedCatalogIndex = -1;
    }
    if (state.selectedCatalogIndex != state.lastCatalogSelection && state.selectedCatalogIndex >= 0 &&
        state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
        const std::string& selectedId = catalogSkills[state.selectedCatalogIndex];
        const std::string selectedName = catalog.display_name(selectedId);
        const std::string selectedDesc = catalog.description(selectedId);
        state.editedSkillWeight = static_cast<float>(catalog.weight(selectedId));
        state.lastCatalogSelection = state.selectedCatalogIndex;
        state.selectedAchievementIndex = -1;
        state.achTitle.fill('\0');
        state.achIcon.fill('\0');
        state.achBonus = 0.0f;
        state.achDurationDays = 0;
        std::snprintf(state.editSkillName.data(), state.editSkillName.size(), "%s", selectedName.c_str());
        std::snprintf(state.editSkillDesc.data(), state.editSkillDesc.size(), "%s", selectedDesc.c_str());
    }

    const float panelHeight = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("catalog_split", 2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("catalog_list_panel", ImVec2(0.0f, panelHeight), false);

        DrawCardHeader(u8"Навыки");
        ImGui::TextDisabled(u8"Выберите навык слева - детали и редактирование справа.");
        ImGui::TextDisabled(u8"Вес влияет на долю XP при распределении.");
        const float listWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = listWidth < 320.0f;
        const ImGuiStyle& listStyle = ImGui::GetStyle();
        ImVec4 actionKeyBg = listStyle.Colors[ImGuiCol_FrameBg];
        actionKeyBg.w = std::clamp(actionKeyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 actionKeyBgU32 = ImGui::GetColorU32(actionKeyBg);
        const bool canDeleteSkill = state.selectedCatalogIndex >= 0 &&
            state.selectedCatalogIndex < static_cast<int>(catalogSkills.size());
        const bool emphasizeAddSkill = state.isAdmin && catalogSkills.empty();
        ImVec4 addSkillBg = listStyle.Colors[ImGuiCol_CheckMark];
        addSkillBg.w = 0.18f;
        ImVec4 addSkillHover = addSkillBg;
        addSkillHover.w = 0.28f;
        ImVec4 addSkillActive = addSkillBg;
        addSkillActive.w = 0.38f;
        if (stackActions) {
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (emphasizeAddSkill) {
                ImGui::PushStyleColor(ImGuiCol_Button, addSkillBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, addSkillHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, addSkillActive);
            }
            if (ImGui::Button(u8"Добавить навык", ImVec2(-1.0f, 0.0f))) {
                state.addSkillPopupRequest = true;
                std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
                std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
                state.newSkillWeight = 1.0f;
            }
            if (emphasizeAddSkill) {
                ImGui::PopStyleColor(3);
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            if (!state.isAdmin || !canDeleteSkill) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить навык", ImVec2(-1.0f, 0.0f))) {
                state.deleteSkillPopupRequest = true;
                state.pendingSkillDelete = catalogSkills[state.selectedCatalogIndex];
            }
            if (!state.isAdmin || !canDeleteSkill) ImGui::EndDisabled();
        } else if (ImGui::BeginTable("catalog_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_left", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("action_right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, actionKeyBgU32);
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (emphasizeAddSkill) {
                ImGui::PushStyleColor(ImGuiCol_Button, addSkillBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, addSkillHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, addSkillActive);
            }
            if (ImGui::Button(u8"Добавить навык", ImVec2(-1.0f, 0.0f))) {
                state.addSkillPopupRequest = true;
                std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
                std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
                state.newSkillWeight = 1.0f;
            }
            if (emphasizeAddSkill) {
                ImGui::PopStyleColor(3);
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);
            if (!state.isAdmin || !canDeleteSkill) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить навык", ImVec2(-1.0f, 0.0f))) {
                state.deleteSkillPopupRequest = true;
                state.pendingSkillDelete = catalogSkills[state.selectedCatalogIndex];
            }
            if (!state.isAdmin || !canDeleteSkill) ImGui::EndDisabled();
            ImGui::EndTable();
        }

        ImGui::Separator();
        const char* sortLabels[] = {u8"По названию", u8"По весу (убыв.)"};
        const char* groupLabels[] = {u8"Без группировки", u8"По категории веса"};
        const char* weightCategoryLabels[] = {
            u8"Все",
            u8"A (>=1.30)",
            u8"B (1.10-1.29)",
            u8"C (0.90-1.09)",
            u8"D (0.70-0.89)",
            u8"E (<0.70)"
        };
        const bool stackFilters = listWidth < 360.0f;
        if (stackFilters) {
            ImGui::TextDisabled(u8"Поиск");
            ImGui::InputTextWithHint(u8"##catalog_search", u8"Поиск навыка",
                                     state.filters.skill.data(), state.filters.skill.size());
            if (ImGui::SmallButton(u8"Сброс##catalog_filter")) {
                state.filters.skill.fill('\0');
                state.catalogWeightCategoryFilter = 0;
            }
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_sort", &state.catalogSortMode, sortLabels, IM_ARRAYSIZE(sortLabels));
            ImGui::TextDisabled(u8"Группировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_group", &state.catalogGroupMode,
                         groupLabels, IM_ARRAYSIZE(groupLabels));
            ImGui::TextDisabled(u8"Категория веса");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_weight", &state.catalogWeightCategoryFilter,
                         weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
        } else if (ImGui::BeginTable("catalog_filters", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("filter_left", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("filter_right", ImGuiTableColumnFlags_WidthStretch, 0.95f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, actionKeyBgU32);
            ImGui::TextDisabled(u8"Поиск");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##catalog_search", u8"Поиск навыка",
                                     state.filters.skill.data(), state.filters.skill.size());
            if (ImGui::SmallButton(u8"Сброс##catalog_filter")) {
                state.filters.skill.fill('\0');
                state.catalogWeightCategoryFilter = 0;
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_sort", &state.catalogSortMode, sortLabels, IM_ARRAYSIZE(sortLabels));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, actionKeyBgU32);
            ImGui::TextDisabled(u8"Группировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_group", &state.catalogGroupMode,
                         groupLabels, IM_ARRAYSIZE(groupLabels));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Категория веса");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_weight", &state.catalogWeightCategoryFilter,
                         weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
            ImGui::EndTable();
        }
        ImGui::TextDisabled(u8"Всего: %d", static_cast<int>(catalogSkills.size()));
        if (catalogSkills.empty()) {
            ImGui::TextDisabled(u8"Навыков пока нет. Добавьте первый навык.");
        }
        ImGui::Separator();

        int visibleCount = 0;
        std::vector<int> viewIndices;
        viewIndices.reserve(catalogSkills.size());
        for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
            viewIndices.push_back(i);
        }
        const int sortMode = std::clamp(state.catalogSortMode, 0, 1);
        const int groupMode = std::clamp(state.catalogGroupMode, 0, 1);
        std::sort(viewIndices.begin(), viewIndices.end(), [&](int a, int b) {
            const std::string& idA = catalogSkills[a];
            const std::string& idB = catalogSkills[b];
            const double wA = catalog.weight(idA);
            const double wB = catalog.weight(idB);
            if (groupMode == 1) {
                const int groupA = WeightCategoryIndex(wA);
                const int groupB = WeightCategoryIndex(wB);
                if (groupA != groupB) return groupA < groupB;
            }
            if (sortMode == 1) {
                if (wA != wB) return wA > wB;
            }
            const std::string nameA = ToLowerUtf8(catalog.display_name(idA));
            const std::string nameB = ToLowerUtf8(catalog.display_name(idB));
            if (nameA != nameB) return nameA < nameB;
            return idA < idB;
        });

        std::array<int, 6> weightCounts{};
        int matchingCount = 0;
        double minWeight = 0.0;
        double maxWeight = 0.0;
        double sumWeight = 0.0;
        std::vector<float> matchingWeights;
        matchingWeights.reserve(viewIndices.size());
        for (int viewIndex : viewIndices) {
            const std::string& skillId = catalogSkills[viewIndex];
            const std::string displayName = catalog.display_name(skillId);
            if (!MatchesFilter(displayName, state.filters.skill.data())) {
                continue;
            }
            const double weight = catalog.weight(skillId);
            const int weightCategory = WeightCategoryIndex(weight);
            if (state.catalogWeightCategoryFilter > 0 &&
                weightCategory != state.catalogWeightCategoryFilter) {
                continue;
            }
            matchingCount += 1;
            sumWeight += weight;
            if (matchingCount == 1) {
                minWeight = weight;
                maxWeight = weight;
            } else {
                minWeight = std::min(minWeight, weight);
                maxWeight = std::max(maxWeight, weight);
            }
            matchingWeights.push_back(static_cast<float>(weight));
            if (weightCategory >= 1 && weightCategory <= 5) {
                weightCounts[static_cast<size_t>(weightCategory)] += 1;
            }
        }
        if (BeginCard("catalog_summary_card")) {
            DrawCardHeader(u8"Сводка");
            const float summaryWidth = ImGui::GetContentRegionAvail().x;
            const int columns = summaryWidth < 540.0f ? 2 : 4;
            const int totalCount = static_cast<int>(catalogSkills.size());
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            int minCategory = -1;
            int minCategoryCount = 0;
            int maxCategoryCount = 0;
            int activeCategories = 0;
            for (int cat = 1; cat <= 5; ++cat) {
                const int count = weightCounts[static_cast<size_t>(cat)];
                if (count <= 0) continue;
                activeCategories += 1;
                if (minCategory == -1 || count < minCategoryCount) {
                    minCategory = cat;
                    minCategoryCount = count;
                }
                maxCategoryCount = std::max(maxCategoryCount, count);
            }
            const bool hasAttention = (activeCategories >= 2) && (maxCategoryCount - minCategoryCount >= 2);
            std::ostringstream avgStream;
            avgStream.imbue(std::locale::classic());
            if (matchingCount > 0) {
                avgStream << std::fixed << std::setprecision(2) << (sumWeight / static_cast<double>(matchingCount));
            } else {
                avgStream << "0.00";
            }
            std::ostringstream rangeStream;
            rangeStream.imbue(std::locale::classic());
            if (matchingCount > 0) {
                rangeStream << std::fixed << std::setprecision(2) << minWeight << "-" << maxWeight;
            } else {
                rangeStream << "-";
            }
            if (ImGui::BeginTable("catalog_kpi", columns, ImGuiTableFlags_SizingStretchSame)) {
                struct KpiItem {
                    const char* id;
                    UiIcon icon;
                    const char* label;
                    std::string value;
                    ImVec4 color;
                };
                std::array<KpiItem, 4> kpis = {{
                    {"total", UiIcon::List, u8"Всего", std::to_string(totalCount), accent},
                    {"shown", UiIcon::Search, u8"Показано", std::to_string(matchingCount), accent},
                    {"avg", UiIcon::Chart, u8"Средний вес", avgStream.str(), accent},
                    {"range", UiIcon::Flow, u8"Диапазон", rangeStream.str(), accent}
                }};
                size_t index = 0;
                const int rows = static_cast<int>((kpis.size() + columns - 1) / columns);
                for (int r = 0; r < rows; ++r) {
                    ImGui::TableNextRow();
                    for (int c = 0; c < columns; ++c) {
                        ImGui::TableNextColumn();
                        if (index < kpis.size()) {
                            const auto& kpi = kpis[index];
                            DrawCatalogKpiCard(kpi.id, kpi.icon, kpi.label, kpi.value, kpi.color);
                            ++index;
                        } else {
                            ImGui::Dummy(ImVec2(0.0f, 0.0f));
                        }
                    }
                }
                ImGui::EndTable();
            }
            if (matchingCount == 0) {
                ImGui::TextDisabled(u8"Нет навыков по фильтру.");
            } else {
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled(u8"Вес (выборка)");
                constexpr int kSamples = 24;
                std::array<float, kSamples> series{};
                if (matchingWeights.size() == 1) {
                    series.fill(matchingWeights.front());
                } else if (!matchingWeights.empty()) {
                    const size_t last = matchingWeights.size() - 1;
                    for (int i = 0; i < kSamples; ++i) {
                        size_t idx = static_cast<size_t>(std::round((static_cast<double>(i) / (kSamples - 1)) * last));
                        idx = std::min(idx, last);
                        series[static_cast<size_t>(i)] = matchingWeights[idx];
                    }
                }
                float maxSeries = 0.0f;
                for (float v : series) maxSeries = std::max(maxSeries, v);
                if (maxSeries <= 0.0f) maxSeries = 1.0f;
                DrawSparkline("##catalog_weight_series", series.data(), kSamples, maxSeries, accent, 54.0f);
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                const char* catLabels[] = {"", "A", "B", "C", "D", "E"};
                if (ImGui::BeginTable("catalog_weights", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("cat", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("bar", ImGuiTableColumnFlags_WidthStretch);
                for (int cat = 1; cat <= 5; ++cat) {
                    ImGui::TableNextRow();
                    if (hasAttention && cat == minCategory) {
                        ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                        rowHighlight.w = std::clamp(rowHighlight.w * 0.16f, 0.08f, 0.22f);
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
                    }
                    ImGui::TableNextColumn();
                    if (hasAttention && cat == minCategory) {
                        ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
                        warnText.w = 0.95f;
                        ImGui::TextColored(warnText, u8"Кат. %s", catLabels[cat]);
                    } else {
                        ImGui::TextDisabled(u8"Кат. %s", catLabels[cat]);
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", weightCounts[static_cast<size_t>(cat)]);
                    ImGui::TableNextColumn();
                    const float share = static_cast<float>(weightCounts[static_cast<size_t>(cat)]) /
                        static_cast<float>(std::max(1, matchingCount));
                    ImGui::PushID(cat);
                    DrawMiniBar("##w_bar", share, ChartPaletteColor(cat - 1, 5, 0.92f), 8.0f);
                    ImGui::PopID();
                }
                ImGui::EndTable();
                }
                if (hasAttention && minCategory > 0) {
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    ImGui::TextDisabled(u8"Зона внимания");
                    ImGui::SameLine();
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Кат. %s (%d)",
                                       catLabels[minCategory], minCategoryCount);
                }
            }
        }
        EndCard();
        ImGui::Separator();

        ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        ImVec4 topRowBg = style.Colors[ImGuiCol_CheckMark];
        topRowBg.w = std::clamp(topRowBg.w * 0.12f, 0.04f, 0.12f);
        const ImU32 topRowBgU32 = ImGui::GetColorU32(topRowBg);
        const char* catShort[] = {"", "A", "B", "C", "D", "E"};
        if (ImGui::BeginTable("catalog_table", 4, tableFlags)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(u8"Навык");
            ImGui::TableSetupColumn(u8"Кат.", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            int lastGroup = -1;
            int rowRank = 0;
            for (int viewIndex : viewIndices) {
                const std::string& skillId = catalogSkills[viewIndex];
                const std::string displayName = catalog.display_name(skillId);
                if (!MatchesFilter(displayName, state.filters.skill.data())) {
                    continue;
                }
                const int weightCategory = WeightCategoryIndex(catalog.weight(skillId));
                if (state.catalogWeightCategoryFilter > 0 &&
                    weightCategory != state.catalogWeightCategoryFilter) {
                    continue;
                }
                if (groupMode == 1 && weightCategory != lastGroup) {
                    ImGui::TableNextRow();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::GetColorU32(ImGuiCol_Header));
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"%s", WeightCategoryLabel(weightCategory));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted("");
                    lastGroup = weightCategory;
                }
                ++visibleCount;
                if (sortMode == 1 && rowRank < 3) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", rowRank + 1);
                ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, actionKeyBgU32);
                bool selected = state.selectedCatalogIndex == viewIndex;
                if (ImGui::Selectable(displayName.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    state.selectedCatalogIndex = viewIndex;
                }
                if (ImGui::IsItemHovered()) {
                    const std::string desc = catalog.description(skillId);
                    if (!desc.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                        ImGui::TextUnformatted(desc.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
                ImGui::TableSetColumnIndex(2);
                const int clampedCat = std::clamp(weightCategory, 0, 5);
                ImVec4 catColor = ChartPaletteColor(std::max(0, clampedCat - 1), 5, 0.85f);
                if (clampedCat > 0) {
                    ImGui::TextColored(catColor, "%s", catShort[clampedCat]);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", catalog.weight(skillId));
                ++rowRank;
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, static_cast<int>(catalogSkills.size()));
        if (visibleCount == 0) {
            ImGui::TextDisabled(u8"Нет навыков по фильтру.");
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("catalog_editor_panel", ImVec2(0.0f, panelHeight), false);
        DrawCardHeader(u8"Детали навыка");
        const std::int64_t nowSecGlobal = NowSeconds();
        if (state.selectedCatalogIndex >= 0 && state.selectedCatalogIndex < static_cast<int>(catalogSkills.size())) {
            const std::string& skillId = catalogSkills[state.selectedCatalogIndex];
            const std::string displayName = catalog.display_name(skillId);
            const std::string desc = catalog.description(skillId);
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImGui::TextColored(accent, "%s", displayName.c_str());
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
            if (desc.empty()) {
                ImGui::TextDisabled(u8"Описание отсутствует.");
            } else {
                ImGui::TextWrapped("%s", desc.c_str());
            }
            ImGui::PopTextWrapPos();
            if (state.isAdmin) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                DrawCardHeader(u8"Вес навыка");
                ImGui::SliderFloat("##edit_weight", &state.editedSkillWeight, 0.5f, 1.6f, "%.2f");
                if (ImGui::Button(u8"Сохранить вес")) {
                    double newW = static_cast<double>(state.editedSkillWeight);
                    ActionResult result = UpdateSkillWeightAction(catalog, skillId, newW, displayName, desc);
                    AppLogLevel level = LogLevelForResult(result);
                    SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Навыки");
                    if (result.ok) {
                        PrepareXpEntries(state, catalog);
                        RefreshProfiles(state, storage, catalog, state.active ? state.active->id : std::string{});
                    }
                }
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                DrawCardHeader(u8"Редактирование навыка");
                ImGui::InputText(u8"Название", state.editSkillName.data(), state.editSkillName.size());
                ImGui::InputTextMultiline(u8"Описание", state.editSkillDesc.data(), state.editSkillDesc.size(), ImVec2(-1.0f, 80.0f));
                if (ImGui::Button(u8"Сохранить навык")) {
                    const std::string newName = TrimStringGui(state.editSkillName.data());
                    const std::string newDesc = TrimStringGui(state.editSkillDesc.data());
                    auto existingId = newName.empty() ? std::optional<std::string>() : catalog.id_for_name(newName);
                    if (existingId && *existingId != skillId) {
                        state.mergeSkillPopupRequest = true;
                        state.pendingMergeFromId = skillId;
                        state.pendingMergeToId = *existingId;
                        state.pendingMergeName = newName;
                        state.pendingMergeDesc = newDesc;
                        state.pendingMergeWeight = state.editedSkillWeight;
                    } else {
                        ActionResult result = UpdateSkillDetailsAction(catalog, skillId, newName,
                                                                      static_cast<double>(state.editedSkillWeight),
                                                                      newDesc);
                        AppLogLevel level = LogLevelForResult(result);
                        SetStatus(state, result.message, result.ok ? 0.45f : 1.0f,
                                  result.ok ? 0.9f : 0.45f, result.ok ? 0.45f : 0.45f, 1.0f,
                                  level, u8"Навыки");
                        if (result.ok) {
                            PrepareXpEntries(state, catalog);
                            RefreshProfiles(state, storage, catalog, state.active ? state.active->id : std::string{});
                            state.lastCatalogSelection = -1;
                        }
                    }
                }
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                DrawCardHeader(u8"Ачивки навыка");
                if (state.active) {
                    auto& ach = state.active->profile.achievements();
                    ImGui::InputTextWithHint(u8"##skill_ach_filter", u8"Фильтр ачивок",
                                             state.filters.skillAch.data(), state.filters.skillAch.size());
                    ImGui::SameLine();
                    const char* statusLabels[] = {u8"Все", u8"Активные", u8"Истекшие"};
                    ImGui::SetNextItemWidth(140.0f);
                    ImGui::Combo(u8"Статус##skill_ach", &state.skillAchStatusFilter,
                                 statusLabels, IM_ARRAYSIZE(statusLabels));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(u8"Сброс##skill_ach")) {
                        state.filters.skillAch.fill('\0');
                        state.skillAchStatusFilter = 0;
                    }
                    std::vector<int> filtered;
                    filtered.reserve(ach.size());
                    int totalForSkill = 0;
                    for (size_t idx = 0; idx < ach.size(); ++idx) {
                        if (ach[idx].skill != skillId) continue;
                        totalForSkill += 1;
                        const bool active = ach[idx].is_active(nowSecGlobal);
                        if (state.skillAchStatusFilter == 1 && !active) continue;
                        if (state.skillAchStatusFilter == 2 && active) continue;
                        if (!MatchesFilter(ach[idx].title, state.filters.skillAch.data())) continue;
                        filtered.push_back(static_cast<int>(idx));
                    }
                    ImGui::TextDisabled(u8"Показано: %d из %d",
                                        static_cast<int>(filtered.size()), totalForSkill);
                    if (ImGui::BeginChild("ach_list", ImVec2(0, 140), true)) {
                        for (int fi = 0; fi < static_cast<int>(filtered.size()); ++fi) {
                            int idx = filtered[fi];
                            const auto& a = ach[idx];
                            const bool expired = !a.is_active(nowSecGlobal);
                            bool selected = state.selectedAchievementIndex == idx;
                            std::string row = a.title + " (" + std::to_string(a.bonusPercent) + "%)";
                            if (expired) row += u8" [истекла]";
                            ImGui::PushID(idx);
                            if (ImGui::Selectable(row.c_str(), selected)) {
                                state.selectedAchievementIndex = idx;
                                std::snprintf(state.achTitle.data(), state.achTitle.size(), "%s", a.title.c_str());
                                std::snprintf(state.achIcon.data(), state.achIcon.size(), "%s", a.icon.c_str());
                                state.achBonus = static_cast<float>(a.bonusPercent);
                                if (a.expiresAt > 0 && a.awardedAt > 0) {
                                    auto dur = a.expiresAt - a.awardedAt;
                                    state.achDurationDays = static_cast<int>(dur / (24 * 3600));
                                } else {
                                    state.achDurationDays = 0;
                                }
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    if (totalForSkill == 0) {
                        ImGui::TextDisabled(u8"Для навыка нет ачивок.");
                    } else if (filtered.empty()) {
                        ImGui::TextDisabled(u8"Нет ачивок по фильтру.");
                    }
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    ImGui::InputText(u8"Название ачивки", state.achTitle.data(), state.achTitle.size());
                    ImGui::InputText(u8"Иконка (путь)", state.achIcon.data(), state.achIcon.size());
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Список иконок")) {
                        ImGui::OpenPopup(u8"Выбор иконки");
                    }
                    if (ImGui::BeginPopup(u8"Выбор иконки")) {
                        const auto icons = LoadAchievementIconChoices(state.storageDir);
                        if (icons.empty()) {
                            ImGui::TextDisabled(u8"Иконки не найдены.");
                        } else {
                            ImGui::InputTextWithHint(u8"##icon_filter", u8"Фильтр иконок",
                                                     state.filters.iconPicker.data(), state.filters.iconPicker.size());
                            ImGui::SameLine();
                            if (ImGui::SmallButton(u8"Сброс##icon_filter")) {
                                state.filters.iconPicker.fill('\0');
                            }
                            int visibleIcons = 0;
                            for (const auto& choice : icons) {
                                if (!MatchesFilter(choice.label, state.filters.iconPicker.data())) {
                                    continue;
                                }
                                visibleIcons += 1;
                            }
                            ImGui::TextDisabled(u8"Показано: %d из %d",
                                                visibleIcons, static_cast<int>(icons.size()));
                            if (ImGui::BeginChild("icon_picker", ImVec2(360.0f, 240.0f), true)) {
                                for (size_t i = 0; i < icons.size(); ++i) {
                                    const auto& choice = icons[i];
                                    if (!MatchesFilter(choice.label, state.filters.iconPicker.data())) {
                                        continue;
                                    }
                                    ImGui::PushID(static_cast<int>(i));
                                    bool selected = choice.relativePath == std::string(state.achIcon.data());
                                    if (const auto* tex = GetIconTexture(choice.absolutePath)) {
                                        ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)tex->id), ImVec2(32.0f, 32.0f));
                                    } else {
                                        ImGui::Button("?", ImVec2(32.0f, 32.0f));
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Selectable(choice.label.c_str(), selected)) {
                                        std::snprintf(state.achIcon.data(), state.achIcon.size(), "%s", choice.relativePath.c_str());
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndChild();
                            if (visibleIcons == 0) {
                                ImGui::TextDisabled(u8"Нет иконок по фильтру.");
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::InputFloat(u8"Бонус к XP (%)", &state.achBonus, 0.5f, 2.0f, "%.1f");
                    ImGui::InputInt(u8"Срок (дней, 0 = без срока)", &state.achDurationDays);
                    if (state.achDurationDays < 0) state.achDurationDays = 0;
                    if (ImGui::Button(u8"Выдать ачивку")) {
                        ActionResult result = GrantAchievementAction(state.active->profile, storage,
                                                                    state.achTitle.data(), skillId,
                                                                    static_cast<double>(state.achBonus),
                                                                    state.achIcon.data(), nowSecGlobal,
                                                                    state.achDurationDays);
                        AppLogLevel level = LogLevelForResult(result);
                        SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                                  result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Ачивки");
                        if (result.ok) {
                            RefreshProfiles(state, storage, catalog, state.active->id);
                            state.selectedAchievementIndex = -1;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Сохранить изменения")) {
                        ActionResult result = UpdateAchievementAction(state.active->profile, storage,
                                                                     state.selectedAchievementIndex,
                                                                     state.achTitle.data(),
                                                                     static_cast<double>(state.achBonus),
                                                                     state.achIcon.data(), nowSecGlobal,
                                                                     state.achDurationDays);
                        AppLogLevel level = LogLevelForResult(result);
                        SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                                  result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Ачивки");
                        if (result.ok) {
                            RefreshProfiles(state, storage, catalog, state.active->id);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Удалить ачивку")) {
                        ActionResult result = DeleteAchievementAction(state.active->profile, storage,
                                                                     state.selectedAchievementIndex);
                        AppLogLevel level = LogLevelForResult(result);
                        SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                                  result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Ачивки");
                        if (result.ok) {
                            state.selectedAchievementIndex = -1;
                            RefreshProfiles(state, storage, catalog, state.active->id);
                        }
                    }
                } else {
                    ImGui::TextDisabled(u8"Ачивки отображаются при выборе профиля.");
                }
            }
        } else {
            ImGui::TextDisabled(u8"Выберите навык, чтобы увидеть детали.");
            if (state.isAdmin) {
                if (ImGui::Button(u8"Добавить навык", ImVec2(-1.0f, 0.0f))) {
                    state.addSkillPopupRequest = true;
                    std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
                    std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
                    state.newSkillWeight = 1.0f;
                }
            } else {
                ImGui::TextDisabled(u8"Добавление навыков доступно администратору.");
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

