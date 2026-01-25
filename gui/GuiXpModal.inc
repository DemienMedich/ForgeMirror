void DrawXpModal(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // XP sheet modal
    if (state.xpPopupRequest) {
        PrepareTaskShares(state);
        ImGui::OpenPopup(u8"Добавление опыта");
        state.xpPopupRequest = false;
    }
    bool xpPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Добавление опыта", &xpPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!state.isAdmin) {
            ImGui::TextDisabled(u8"Доступно только администратору.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                xpPopupOpen = false;
            }
            ImGui::EndPopup();
            if (!xpPopupOpen && ImGui::IsPopupOpen(u8"Добавление опыта")) {
                ImGui::CloseCurrentPopup();
            }
            goto skip_xp_body;
        }
        const auto& catalogSkills = catalog.skills();
        if (state.xpEntries.size() != catalogSkills.size()) {
            PrepareXpEntries(state, catalog);
        }
        if (state.taskShares.size() != state.profiles.size()) {
            PrepareTaskShares(state);
        }
            if (catalogSkills.empty()) {
                ImGui::TextDisabled(u8"Навыков пока нет.");
        } else if (!state.active) {
            ImGui::TextDisabled(u8"Нет активного профиля.");
        } else {
            state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
            state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
            const GameplayConfig& rules = GetGameplayConfig();
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImVec4 accentSoft = accent;
            accentSoft.w = 0.75f;

            DrawCardHeader(u8"Параметры задачи");
            const float headerWidth = ImGui::GetContentRegionAvail().x;
            const bool stackHeader = headerWidth < 440.0f;
            if (stackHeader) {
                ImGui::TextDisabled(u8"Категория");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo(u8"##task_category", &state.taskCategoryIndex,
                Profile::kCategoryLabels.data(), Profile::kCategoryCount)) {
                    state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                }
            } else if (ImGui::BeginTable("xp_task_header", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("header_left", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("header_right", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled(u8"Категория");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo(u8"##task_category", &state.taskCategoryIndex,
                Profile::kCategoryLabels.data(), Profile::kCategoryCount)) {
                    state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled(u8"Базовый XP");
                ImGui::Text("%d", rules.categoryBaseXp[state.taskCategoryIndex]);
                ImGui::EndTable();
            }
            const int currentCategory = state.taskCategoryIndex;
            const int baseXp = rules.categoryBaseXp[currentCategory];
            const float scoreRatio = static_cast<float>(state.taskScore) / static_cast<float>(Profile::kMaxCategoryScore);
            const float scoreMultiplier = std::pow(std::max(0.1f, scoreRatio), 1.35f);
            const int currentBest = state.active->profile.category_best_score(static_cast<size_t>(currentCategory));
            const bool penaltiesEnabled = state.active->profile.penalties_enabled();
            ImGui::TextDisabled(u8"Категория задаёт базовый XP; оценка - нелинейный множитель.");
            if (ImGui::SliderInt(u8"Оценка", &state.taskScore, 1, Profile::kMaxCategoryScore)) {
                state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
            }
            ImGui::Text(u8"Множитель за оценку: %.2f", scoreMultiplier);
            if (currentBest >= Profile::kMaxCategoryScore) {
                ImGui::TextColored(accent,
                u8"Лучший результат: %d/10 (категория освоена)", currentBest);
            } else {
                ImGui::Text(u8"Лучший результат: %d/10", currentBest);
                if (penaltiesEnabled && state.taskScore <= currentBest) {
                    ImGui::TextColored(accentSoft,
                                       u8"Повтор оценки - глобальный XP ограничен 35%% (навыки без штрафа).");
                } else if (!penaltiesEnabled && state.taskScore <= currentBest) {
                    ImGui::TextColored(accentSoft, u8"Штрафы за повтор включатся после ранга Сеньор.");
                }
            }

            ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
            DrawCardHeader(u8"Описание задачи");
            ImGui::TextDisabled(u8"Все поля обязательны для сохранения.");
            ImGui::TextDisabled(u8"Проект");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText(u8"##task_project", state.taskProject.data(), state.taskProject.size());
            ImGui::TextDisabled(u8"Задача");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText(u8"##task_title", state.taskTitle.data(), state.taskTitle.size());
            ImGui::TextDisabled(u8"Краткое описание");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextMultiline(u8"##task_desc", state.taskDescription.data(),
                                      state.taskDescription.size(), ImVec2(-1.0f, 80.0f));

            ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
            DrawCardHeader(u8"Участники и вклад");
            ImGui::TextDisabled(u8"Сумма вкладов должна быть 100%%.");
            const float memberWidth = ImGui::GetContentRegionAvail().x;
            const bool stackMembers = memberWidth < 520.0f;
            if (stackMembers) {
                if (ImGui::Button(u8"Равномерно##members_stack", ImVec2(-1.0f, 0.0f))) {
                    NormalizeProfileShares(state.taskShares);
                }
                if (ImGui::Button(u8"Только активный", ImVec2(-1.0f, 0.0f))) {
                    for (auto& share : state.taskShares) {
                        share.selected = state.active && share.id == state.active->id && !share.archived;
                        share.percent = share.selected ? 100 : 0;
                    }
                }
            } else if (ImGui::BeginTable("task_member_controls", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("member_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("member_even", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("member_active", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                if (ImGui::Button(u8"Равномерно##members_table", ImVec2(-1.0f, 0.0f))) {
                    NormalizeProfileShares(state.taskShares);
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Только активный", ImVec2(-1.0f, 0.0f))) {
                    for (auto& share : state.taskShares) {
                        share.selected = state.active && share.id == state.active->id && !share.archived;
                        share.percent = share.selected ? 100 : 0;
                    }
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginTable("task_members", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                 ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(u8"Профиль", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn(u8"Вклад (%)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn(u8"Статус", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(state.taskShares.size()); ++i) {
                    auto& share = state.taskShares[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool selected = share.selected;
                    if (share.archived) ImGui::BeginDisabled();
                    if (ImGui::Checkbox(("##share_select_" + share.id).c_str(), &selected)) {
                        share.selected = selected;
                        if (!selected) {
                            share.percent = 0;
                            BalanceProfileShares(state.taskShares, i);
                        } else {
                            if (share.percent <= 0) {
                                int selectedCount = std::max(1, CountSelectedProfileShares(state.taskShares));
                                share.percent = std::max(1, 100 / selectedCount);
                            }
                            BalanceProfileShares(state.taskShares, i);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(share.name.c_str());
                    if (share.archived) {
                        ImGui::SameLine();
                        ImGui::TextDisabled(u8"(архив)");
                        ImGui::EndDisabled();
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (!share.selected || share.archived) ImGui::BeginDisabled();
                    ImGui::PushID(i);
                    int percent = share.percent;
                    if (ImGui::SliderInt("##member_share", &percent, 0, 100, "%d%%",
                                         ImGuiSliderFlags_AlwaysClamp)) {
                        AdjustProfileShare(state.taskShares, i, percent);
                        percent = share.percent;
                    }
                    ImGui::PopID();
                    if (!share.selected || share.archived) ImGui::EndDisabled();
                    ImGui::TableSetColumnIndex(2);
                    if (!share.selected) {
                        ImGui::TextDisabled(u8"Не выбран");
                    } else if (share.archived) {
                        ImGui::TextDisabled(u8"Архив");
                    } else {
                        ImGui::TextDisabled(u8"%d%%", share.percent);
                    }
                }
                ImGui::EndTable();
            }

            const int memberSum = SumSelectedProfileShares(state.taskShares);
            if (memberSum == 100) {
                ImGui::TextColored(accent, u8"Распределено: %d%%", memberSum);
            } else {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"Распределено: %d%%", memberSum);
            }

            ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
            DrawCardHeader(u8"Распределение навыков");
            ImGui::TextDisabled(u8"Автоподбор до 100%.");
            const float controlsWidth = ImGui::GetContentRegionAvail().x;
            const bool stackControls = controlsWidth < 520.0f;
            if (stackControls) {
                if (ImGui::Button(u8"Равномерно##skills_stack", ImVec2(-1.0f, 0.0f))) {
                    PrepareXpEntries(state, catalog);
                }
                ImGui::TextDisabled(u8"Фильтр навыков");
                ImGui::InputTextWithHint(u8"##xp_filter", u8"Фильтр навыков",
                state.filters.xpSkill.data(), state.filters.xpSkill.size());
                if (ImGui::SmallButton(u8"Сброс##xp_filter")) {
                    state.filters.xpSkill.fill('\0');
                }
                const char* xpSortLabels[] = {u8"По имени", u8"По доле", u8"По бонусу", u8"По XP"};
                ImGui::TextDisabled(u8"Сортировка");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo(u8"##xp_sort", &state.xpSortMode, xpSortLabels, IM_ARRAYSIZE(xpSortLabels));
            } else if (ImGui::BeginTable("xp_controls", 5, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("xp_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("xp_btn", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("xp_filter", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("xp_reset", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("xp_sort", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Равномерно##skills_table", ImVec2(-1.0f, 0.0f))) {
                    PrepareXpEntries(state, catalog);
                }
                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Фильтр");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(u8"##xp_filter", u8"Фильтр навыков",
                state.filters.xpSkill.data(), state.filters.xpSkill.size());
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
                    state.filters.xpSkill.fill('\0');
                }
                ImGui::TableNextColumn();
                const char* xpSortLabels[] = {u8"По имени", u8"По доле", u8"По бонусу", u8"По XP"};
                ImGui::TextDisabled(u8"Сортировка");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo(u8"##xp_sort", &state.xpSortMode, xpSortLabels, IM_ARRAYSIZE(xpSortLabels));
                ImGui::EndTable();
            }
            const float sliderWidth = ImGui::CalcTextSize("000").x + ImGui::GetStyle().FramePadding.x * 6.0f;
            int percentSum = 0;
            int maxSharePercent = 0;
            for (const auto& entry : state.xpEntries) {
                percentSum += entry.percent;
                if (entry.percent > maxSharePercent) {
                    maxSharePercent = entry.percent;
                }
            }
            const auto now = std::chrono::system_clock::now();
            const std::int64_t nowSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::vector<std::string> xpNames(catalogSkills.size());
            std::vector<double> xpBonus(catalogSkills.size());
            std::vector<int> xpPreview(catalogSkills.size());
            std::vector<int> xpOrder;
            xpOrder.reserve(catalogSkills.size());
            for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                const std::string& skillId = catalogSkills[i];
                xpNames[i] = catalog.display_name(skillId);
                double mult = state.active->profile.skill_bonus_multiplier(skillId, nowSeconds);
                xpBonus[i] = std::max(0.0, (mult - 1.0) * 100.0);
                const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                xpPreview[i] = static_cast<int>(std::round(previewXp * mult));
                if (!MatchesFilter(xpNames[i], state.filters.xpSkill.data())) {
                    continue;
                }
                xpOrder.push_back(i);
            }
            const int xpSortMode = std::clamp(state.xpSortMode, 0, 3);
            std::sort(xpOrder.begin(), xpOrder.end(), [&](int a, int b) {
                if (xpSortMode == 1) {
                    if (state.xpEntries[a].percent != state.xpEntries[b].percent) {
                        return state.xpEntries[a].percent > state.xpEntries[b].percent;
                    }
                } else if (xpSortMode == 2) {
                    if (xpBonus[a] != xpBonus[b]) return xpBonus[a] > xpBonus[b];
                } else if (xpSortMode == 3) {
                    if (xpPreview[a] != xpPreview[b]) return xpPreview[a] > xpPreview[b];
                } else {
                    if (xpNames[a] != xpNames[b]) return xpNames[a] < xpNames[b];
                }
                return a < b;
            });
            const int visibleXpCount = static_cast<int>(xpOrder.size());
            ImGui::TextDisabled(u8"Показано: %d из %d",
            visibleXpCount, static_cast<int>(catalogSkills.size()));
            const ImGuiStyle& style = ImGui::GetStyle();
            ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImVec4 topRowBg = style.Colors[ImGuiCol_CheckMark];
            topRowBg.w = std::clamp(topRowBg.w * 0.12f, 0.04f, 0.12f);
            const ImU32 topRowBgU32 = ImGui::GetColorU32(topRowBg);
            const float shareColumnWidth = sliderWidth + 28.0f;
            if (ImGui::BeginTable("xp_sheet", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn(u8"Навык", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn(u8"Доля (%)", ImGuiTableColumnFlags_WidthFixed, shareColumnWidth);
                ImGui::TableSetupColumn(u8"Ачивки", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("XP", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (int orderIdx = 0; orderIdx < static_cast<int>(xpOrder.size()); ++orderIdx) {
                    int i = xpOrder[orderIdx];
                    if (orderIdx < 3) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
                    }
                    ImGui::TableNextRow();
                    const std::string& skillId = catalogSkills[i];
                    const std::string& displayName = xpNames[i];
                    ImGui::TableSetColumnIndex(0);
                    DrawAnchorIcon(UiIcon::List);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                    ImGui::TextUnformatted(displayName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(i);
                    int percent = state.xpEntries[i].percent;
                    ImGui::PushItemWidth(sliderWidth);
                    if (ImGui::SliderInt("##share", &percent, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                        AdjustSkillShare(state, i, percent);
                        percent = state.xpEntries[i].percent;
                    }
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(3);
                    if (xpBonus[i] > 0.01) {
                        ImGui::Text("+%.1f%%", xpBonus[i]);
                    } else {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(4);
                    const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                    if (xpPreview[i] != previewXp) {
                        ImGui::Text("%d -> %d", previewXp, xpPreview[i]);
                    } else {
                        ImGui::Text("%d", previewXp);
                    }
                }
                ImGui::EndTable();
            }
            if (visibleXpCount == 0) {
                ImGui::TextDisabled(u8"Нет навыков по фильтру.");
            }

            if (percentSum == 100) {
                ImGui::TextColored(accent, u8"Назначено: %d%%", percentSum);
            } else {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"Назначено: %d%%", percentSum);
            }
            const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * (static_cast<float>(maxSharePercent) / 100.0f);
            const int previewPool = static_cast<int>(std::round(baseXp * scoreMultiplier * focusBonus));
            ImGui::Text(u8"Фокус-бонус: %.2f (макс. доля %d%%)", focusBonus, maxSharePercent);
            ImGui::TextDisabled(u8"Прогноз XP по навыкам до штрафов: %d", previewPool);

            const bool repeatPenaltyPreview = penaltiesEnabled && state.taskScore <= currentBest;
            const int repeatPercentPreview = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
            const int recoveryPercentPreview = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
            std::string globalHint = u8"Глобальный XP";
            bool previewPenalty = false;
            if (repeatPenaltyPreview) {
                globalHint += u8" (повтор " + std::to_string(repeatPercentPreview) + "%)";
                previewPenalty = true;
            }
            if (penaltiesEnabled && state.active->profile.penalty_active()) {
                globalHint += u8" (прогрев " + std::to_string(recoveryPercentPreview) + "%)";
                previewPenalty = true;
            }
            int effectivePreview = previewPool;
            if (repeatPenaltyPreview) {
                effectivePreview = static_cast<int>(std::round(effectivePreview * rules.repeatRewardFactor));
            }
            if (penaltiesEnabled && state.active->profile.penalty_active()) {
                effectivePreview = static_cast<int>(std::round(effectivePreview * rules.recoveryRewardFactor));
            }
            ImGui::TextDisabled(u8"%s: %d%s", globalHint.c_str(), effectivePreview,
            previewPenalty ? u8" (после штрафов)" : "");

            if (ImGui::Button(u8"Применить", ImVec2(stackControls ? -1.0f : 0.0f, 0.0f))) {
                const int readyCategory = NormalizeCategoryIndex(state.taskCategoryIndex);
                const int readyScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                const std::string project = TrimStringGui(state.taskProject.data());
                const std::string title = TrimStringGui(state.taskTitle.data());
                const std::string description = TrimStringGui(state.taskDescription.data());
                const int memberSum = SumSelectedProfileShares(state.taskShares);
                const int memberCount = CountSelectedProfileShares(state.taskShares);
                int percentCheck = 0;
                bool hasContribution = false;
                for (const auto& entry : state.xpEntries) {
                    int clamped = ClampToRange(entry.percent, 0, 100);
                    percentCheck += clamped;
                    if (clamped > 0) hasContribution = true;
                }
                if (project.empty() || title.empty() || description.empty()) {
                    SetStatus(state, u8"Заполните проект, задачу и описание.", 1.0f, 0.45f, 0.45f);
                } else if (memberCount <= 0) {
                    SetStatus(state, u8"Выберите хотя бы один профиль.", 1.0f, 0.45f, 0.45f);
                } else if (memberSum != 100) {
                    SetStatus(state, u8"Сумма вкладов должна быть 100%.", 1.0f, 0.45f, 0.45f);
                } else if (percentCheck != 100) {
                    SetStatus(state, u8"Сумма долей навыков должна быть 100%.", 1.0f, 0.45f, 0.45f);
                } else if (!hasContribution) {
                    SetStatus(state, u8"Нужно выбрать хотя бы один навык.", 1.0f, 0.45f, 0.45f);
                } else {
                    float maxShare = 0.0f;
                    if (!state.xpEntries.empty()) {
                        const auto it = std::max_element(
                        state.xpEntries.begin(), state.xpEntries.end(),
                        [](const XpEntry& a, const XpEntry& b) { return a.percent < b.percent; });
                        maxShare = static_cast<float>(it->percent) / 100.0f;
                    }
                    const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * maxShare;
                    int basePool = static_cast<int>(std::round(
                    rules.categoryBaseXp[readyCategory] *
                    std::pow(std::max(0.1f, static_cast<float>(readyScore) / 10.0f), 1.35f) * focusBonus));
                    if (basePool < 0) basePool = 0;
                    const char* categoryLabel = ClassificationLabel(readyCategory);

                    std::vector<int> participantPools(state.taskShares.size(), 0);
                    int remainder = basePool;
                    int fallbackIndex = -1;
                    for (int i = 0; i < static_cast<int>(state.taskShares.size()); ++i) {
                        auto& share = state.taskShares[i];
                        if (!share.selected || share.archived) continue;
                        int percent = ClampToRange(share.percent, 0, 100);
                        if (percent <= 0) continue;
                        int sharePool = (basePool * percent) / 100;
                        participantPools[i] = sharePool;
                        remainder -= sharePool;
                        if (fallbackIndex == -1 || percent > state.taskShares[fallbackIndex].percent) {
                            fallbackIndex = i;
                        }
                    }
                    if (remainder > 0 && fallbackIndex >= 0) {
                        participantPools[fallbackIndex] += remainder;
                    }

                    TaskEntry task;
                    task.id = GenerateTaskId(nowSeconds, state.tasks.size() + 1);
                    task.project = project;
                    task.title = title;
                    task.description = description;
                    task.category = readyCategory;
                    task.score = readyScore;
                    task.baseXp = rules.categoryBaseXp[readyCategory];
                    task.basePool = basePool;
                    task.createdAt = nowSeconds;

                    std::vector<TaskParticipant> participants;
                    std::string activeId = state.active ? state.active->id : std::string{};
                    constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;

                    for (int i = 0; i < static_cast<int>(state.taskShares.size()); ++i) {
                        const auto& share = state.taskShares[i];
                        if (!share.selected || share.archived) continue;
                        if (!storage.set_active_profile(share.id)) continue;
                        auto profileOpt = storage.load_profile();
                        if (!profileOpt) continue;
                        Profile& profile = *profileOpt;

                        const int storedBest = profile.category_best_score(static_cast<size_t>(readyCategory));
                        const bool penaltiesEnabled = profile.penalties_enabled();
                        int participantPool = participantPools[i];
                        if (participantPool <= 0) continue;

                        std::vector<int> xpDistribution(state.xpEntries.size(), 0);
                        int remainderXp = participantPool;
                        int fallbackSkill = -1;
                        for (int s = 0; s < static_cast<int>(state.xpEntries.size()); ++s) {
                            int percent = ClampToRange(state.xpEntries[s].percent, 0, 100);
                            if (percent <= 0) continue;
                            int shareXp = (participantPool * percent) / 100;
                            xpDistribution[s] = shareXp;
                            remainderXp -= shareXp;
                            if (fallbackSkill == -1 || percent > state.xpEntries[fallbackSkill].percent) {
                                fallbackSkill = s;
                            }
                        }
                        if (remainderXp > 0 && fallbackSkill >= 0) {
                            xpDistribution[fallbackSkill] += remainderXp;
                        }

                        std::ostringstream skillsStream;
                        skillsStream.imbue(std::locale::classic());
                        bool firstSkill = true;
                        int skillXpTotal = 0;
                        for (int s = 0; s < static_cast<int>(catalogSkills.size()); ++s) {
                            int shareXp = xpDistribution[s];
                            if (shareXp <= 0) continue;
                            const std::string& skillId = catalogSkills[s];
                            const std::string displayName = catalog.display_name(skillId);
                            double weight = catalog.weight(skillId);
                            profile.add_skill(skillId, 1, weight);
                            double mult = profile.skill_bonus_multiplier(skillId, nowSeconds);
                            double bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
                            int finalSkillXp = static_cast<int>(std::round(shareXp * mult));
                            skillXpTotal += finalSkillXp;
                            bool leveled = profile.grant_xp(skillId, finalSkillXp);
                            if (!firstSkill) skillsStream << " | ";
                            skillsStream << displayName << " +" << finalSkillXp << " XP (" << state.xpEntries[s].percent << "%";
                            if (bonusPercent > 0.01) {
                                skillsStream << ", +" << std::fixed << std::setprecision(1) << bonusPercent << "%";
                                skillsStream << std::defaultfloat;
                            }
                            skillsStream << ")";
                            if (leveled) skillsStream << u8" уровень вверх";
                            firstSkill = false;
                        }
                        if (!firstSkill) {
                            AppendLog(state, share.id, skillsStream.str());
                        }

                        bool repeatPenalty = penaltiesEnabled && readyScore <= storedBest;
                        int effectiveXp = participantPool;
                        if (repeatPenalty) {
                            effectiveXp = static_cast<int>(std::round(effectiveXp * rules.repeatRewardFactor));
                        }
                        if (penaltiesEnabled && profile.last_task_timestamp() > 0 &&
                            (nowSeconds - profile.last_task_timestamp()) > kThirtyDays) {
                            profile.start_penalty_recovery(rules.recoveryWarmupTasks);
                        }
                        bool recoveryPenalty = false;
                        if (!penaltiesEnabled && profile.penalty_active()) {
                            profile.start_penalty_recovery(0);
                        }
                        if (penaltiesEnabled && profile.penalty_active()) {
                            recoveryPenalty = true;
                            effectiveXp = static_cast<int>(std::round(effectiveXp * rules.recoveryRewardFactor));
                            profile.consume_penalty_task();
                        }
                        profile.set_last_task_timestamp(nowSeconds);
                        profile.increment_tasks_completed();
                        if (effectiveXp > 0) {
                            profile.grant_global_xp(effectiveXp);
                        }
                        if (readyScore > storedBest) {
                            profile.update_category_best_score(static_cast<size_t>(readyCategory), readyScore);
                            std::ostringstream bestStream;
                            bestStream.imbue(std::locale::classic());
                            bestStream << u8"Категория " << categoryLabel << u8": лучший результат " << readyScore << "/10";
                            AppendLog(state, share.id, bestStream.str());
                            if (readyScore == Profile::kMaxCategoryScore) {
                                std::ostringstream mastery;
                                mastery.imbue(std::locale::classic());
                                mastery << u8"Категория " << categoryLabel << u8" освоена!";
                                AppendLog(state, share.id, mastery.str());
                            }
                        }

                        constexpr bool kDecayEnabled = true;
                        if (kDecayEnabled) {
                            profile.reset_category_cooldown(static_cast<size_t>(readyCategory));
                            for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                                if (idx == static_cast<size_t>(readyCategory)) continue;
                                profile.tick_category_cooldown(idx);
                                if (profile.category_cooldown(idx) < 0) {
                                    profile.update_category_best_score(
                                    idx, profile.category_best_score(idx) - 1);
                                    profile.reset_category_cooldown(idx);
                                    std::ostringstream decay;
                                    decay.imbue(std::locale::classic());
                                    decay << u8"Категория " << Profile::kCategoryLabels[idx] << u8" снижена до "
                                    << profile.category_best_score(idx) << "/10";
                                    AppendLog(state, share.id, decay.str());
                                }
                            }
                            int buffer = profile.category_cooldown(0);
                            for (size_t idx = 1; idx < Profile::kCategoryCount; ++idx) {
                                buffer = std::min(buffer, profile.category_cooldown(idx));
                            }
                            buffer = std::max(0, buffer);
                            profile.set_inactivity_tasks(buffer);
                        }

                        std::ostringstream summaryStream;
                        summaryStream.imbue(std::locale::classic());
                        summaryStream << u8"Задача [" << categoryLabel << u8"] " << project << " / " << title
                                      << u8" => +" << effectiveXp << " XP";
                        const int repeatPercent = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
                        const int recoveryPercent = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
                        if (repeatPenalty) summaryStream << u8" (повтор " << repeatPercent << "%)";
                        if (recoveryPenalty) summaryStream << u8" (прогрев " << recoveryPercent << "%)";
                        AppendLog(state, share.id, summaryStream.str());

                        storage.save_profile(profile);
                        participants.push_back(TaskParticipant{share.id, share.percent, effectiveXp, skillXpTotal});
                    }

                    if (!participants.empty()) {
                        task.participants = std::move(participants);
                        state.tasks.push_back(std::move(task));
                        const bool saveOk = SaveTasks(state.storageDir, state.tasks);
                        if (!activeId.empty()) {
                            storage.set_active_profile(activeId);
                            RefreshActiveProfile(state, storage, catalog);
                            SyncRankSelection(state);
                        }
                        PrepareTaskShares(state);
                        PrepareXpEntries(state, catalog);
                        state.taskProject.fill('\0');
                        state.taskTitle.fill('\0');
                        state.taskDescription.fill('\0');
                        if (saveOk) {
                            SetStatus(state, u8"Задача сохранена и опыт начислен.", 0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state, u8"Опыт начислен, но не удалось записать файл задач.", 1.0f, 0.75f, 0.35f,
                                      1.0f, AppLogLevel::Warning, u8"Задачи");
                        }
                        ImGui::CloseCurrentPopup();
                        xpPopupOpen = false;
                    } else {
                        SetStatus(state, u8"Не удалось начислить опыт выбранным профилям.", 1.0f, 0.45f, 0.45f);
                    }
                }
            }
            if (!stackControls) {
                ImGui::SameLine();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(stackControls ? -1.0f : 0.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                xpPopupOpen = false;
            }
        }
        ImGui::EndPopup();
    }
    skip_xp_body:
    if (!xpPopupOpen && ImGui::IsPopupOpen(u8"Добавление опыта")) {
        ImGui::CloseCurrentPopup();
    }

}
