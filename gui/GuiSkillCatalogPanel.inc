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

        DrawCardHeader(u8"Каталог навыков");
        const float listWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = listWidth < 320.0f;
        const bool canDeleteSkill = state.selectedCatalogIndex >= 0 &&
            state.selectedCatalogIndex < static_cast<int>(catalogSkills.size());
        if (stackActions) {
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Добавить навык", ImVec2(-1.0f, 0.0f))) {
                state.addSkillPopupRequest = true;
                std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
                std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
                state.newSkillWeight = 1.0f;
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            if (!state.isAdmin || !canDeleteSkill) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить навык", ImVec2(-1.0f, 0.0f))) {
                state.deleteSkillPopupRequest = true;
                state.pendingSkillDelete = catalogSkills[state.selectedCatalogIndex];
            }
            if (!state.isAdmin || !canDeleteSkill) ImGui::EndDisabled();
        } else if (ImGui::BeginTable("catalog_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("action_left", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("action_right", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Добавить навык", ImVec2(-1.0f, 0.0f))) {
                state.addSkillPopupRequest = true;
                std::fill(state.newSkillName.begin(), state.newSkillName.end(), 0);
                std::fill(state.newSkillDesc.begin(), state.newSkillDesc.end(), 0);
                state.newSkillWeight = 1.0f;
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::TableNextColumn();
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
        } else if (ImGui::BeginTable("catalog_filters", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("filter_left", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("filter_right", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Поиск");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##catalog_search", u8"Поиск навыка",
                                     state.filters.skill.data(), state.filters.skill.size());
            if (ImGui::SmallButton(u8"Сброс##catalog_filter")) {
                state.filters.skill.fill('\0');
                state.catalogWeightCategoryFilter = 0;
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_sort", &state.catalogSortMode, sortLabels, IM_ARRAYSIZE(sortLabels));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Группировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_group", &state.catalogGroupMode,
                         groupLabels, IM_ARRAYSIZE(groupLabels));
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Категория веса");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##catalog_weight", &state.catalogWeightCategoryFilter,
                         weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
            ImGui::EndTable();
        }
        ImGui::TextDisabled(u8"Всего: %d", static_cast<int>(catalogSkills.size()));
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
        ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("catalog_table", 2, tableFlags)) {
            ImGui::TableSetupColumn(u8"Навык");
            ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            int lastGroup = -1;
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
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
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
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f", catalog.weight(skillId));
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
            ImGui::TextWrapped("%s", desc.empty() ? u8"Описание отсутствует." : desc.c_str());
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
                              result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Каталог");
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
                                  level, u8"Каталог");
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
                            ImGui::TextUnformatted(u8"Иконки не найдены.");
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
            ImGui::TextDisabled(u8"Выберите навык, чтобы увидеть описание.");
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

