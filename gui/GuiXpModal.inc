void DrawXpModal(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // XP sheet modal
    if (state.xpPopupRequest) {
        PrepareTaskShares(state);
        ImGui::OpenPopup(u8"Добавление опыта");
        state.xpPopupRequest = false;
    }
    bool xpPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Добавление опыта", &xpPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
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
        std::vector<char> skillAllowed(catalogSkills.size(), 1);
        bool restrictSkills = false;
        int allowedSkillCount = static_cast<int>(catalogSkills.size());
        std::string profId;
        if (state.active) {
            profId = state.active->profile.profession_id();
        }
        const bool professionChanged = profId != state.xpProfessionId;
        if (professionChanged) {
            state.xpProfessionId = profId;
        }
        if (!profId.empty()) {
            restrictSkills = true;
            allowedSkillCount = 0;
            for (size_t i = 0; i < catalogSkills.size(); ++i) {
                const auto profs = catalog.professions(catalogSkills[i]);
                const bool allowed = profs.empty() || std::find(profs.begin(), profs.end(), profId) != profs.end();
                skillAllowed[i] = allowed ? 1 : 0;
                if (allowed) {
                    allowedSkillCount += 1;
                }
            }
        }
        auto SetEvenSkillRatingsForAllowed = [&]() {
            for (size_t i = 0; i < state.xpEntries.size(); ++i) {
                state.xpEntries[i].rating = (i < skillAllowed.size() && skillAllowed[i]) ? 1 : 0;
            }
            RecalculateXpPercentages(state.xpEntries);
        };
        if (state.xpEntries.size() != catalogSkills.size()) {
            SetEvenSkillRatingsForAllowed();
        } else if (professionChanged) {
            SetEvenSkillRatingsForAllowed();
        }
        if (state.taskShares.size() != state.profiles.size()) {
            PrepareTaskShares(state);
        }
        TaskEntry* sourceTask = nullptr;
        if (!state.xpSourceTaskId.empty()) {
            auto it = std::find_if(state.tasks.begin(), state.tasks.end(),
                                   [&](const TaskEntry& task) { return task.id == state.xpSourceTaskId; });
            if (it != state.tasks.end()) {
                sourceTask = &(*it);
            }
        }
        const bool fromTask = (sourceTask != nullptr);
        const int taskPenaltyPercent = fromTask
            ? std::clamp(sourceTask->deadlinePenaltyPercent, 0, 100)
            : std::clamp(state.xpTaskPenaltyPercent, 0, 100);
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
            if (fromTask) {
                ImGui::TextDisabled(u8"XP будет записан в существующую задачу.");
                ImGui::TextDisabled(u8"Проект");
                ImGui::TextUnformatted(state.taskProject.data());
                ImGui::TextDisabled(u8"Задача");
                ImGui::TextUnformatted(state.taskTitle.data());
                ImGui::TextDisabled(u8"Краткое описание");
                ImGui::TextWrapped("%s", state.taskDescription.data());
                if (taskPenaltyPercent > 0) {
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning),
                                       u8"Штраф дедлайна: -%d%% к итоговому XP-пулу.", taskPenaltyPercent);
                }
            } else {
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
            }

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

            if (ImGui::BeginTable("task_members", 3, ImGuiTableFlags_RowBg |
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
            ImGui::TextDisabled(u8"Оцените навыки 0-5, доли считаются автоматически.");
            if (restrictSkills) {
                ImGui::TextDisabled(u8"Показаны навыки вашей профессии и общие.");
            }
            const float controlsWidth = ImGui::GetContentRegionAvail().x;
            const bool stackControls = controlsWidth < 520.0f;
            if (stackControls) {
                if (ImGui::Button(u8"Равномерно##skills_stack", ImVec2(-1.0f, 0.0f))) {
                    SetEvenSkillRatingsForAllowed();
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
                    SetEvenSkillRatingsForAllowed();
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
            const float ratingWidth = ImGui::CalcTextSize("5").x + ImGui::GetStyle().FramePadding.x * 6.0f;
            if (restrictSkills) {
                for (size_t i = 0; i < state.xpEntries.size(); ++i) {
                    if (i < skillAllowed.size() && !skillAllowed[i]) {
                        state.xpEntries[i].rating = 0;
                        state.xpEntries[i].percent = 0;
                    }
                }
            }
            const float percentWidth = ImGui::CalcTextSize("100%").x + ImGui::GetStyle().FramePadding.x * 4.0f;
            RecalculateXpPercentages(state.xpEntries);
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
                xpPreview[i] = ApplyProfileSpiritXpModifier(
                    state.active->profile.spirit(),
                    static_cast<int>(std::round(previewXp * mult)));
                if (restrictSkills && i < static_cast<int>(skillAllowed.size()) && !skillAllowed[i]) {
                    continue;
                }
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
            const int totalSkillCount = restrictSkills ? allowedSkillCount : static_cast<int>(catalogSkills.size());
            ImGui::TextDisabled(u8"Показано: %d из %d",
            visibleXpCount, totalSkillCount);
            const ImGuiStyle& style = ImGui::GetStyle();
            ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImVec4 topRowBg = style.Colors[ImGuiCol_CheckMark];
            topRowBg.w = std::clamp(topRowBg.w * 0.12f, 0.04f, 0.12f);
            const ImU32 topRowBgU32 = ImGui::GetColorU32(topRowBg);
            const float ratingColumnWidth = ratingWidth + 24.0f;
            const float shareColumnWidth = percentWidth + 24.0f;
            if (ImGui::BeginTable("xp_sheet", 6, ImGuiTableFlags_RowBg |
                                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn(u8"Навык", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn(u8"Оценка", ImGuiTableColumnFlags_WidthFixed, ratingColumnWidth);
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
                    const std::string& displayName = xpNames[i];
                    ImGui::TableSetColumnIndex(0);
                    DrawAnchorIcon(UiIcon::List);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                    ImGui::TextUnformatted(displayName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(i);
                    int rating = state.xpEntries[i].rating;
                    ImGui::PushItemWidth(ratingWidth);
                    if (ImGui::SliderInt("##rating", &rating, 0, 5, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                        AdjustSkillRating(state, i, rating);
                        rating = state.xpEntries[i].rating;
                    }
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(3);
                    if (state.xpEntries[i].percent > 0) {
                        ImGui::Text("%d%%", state.xpEntries[i].percent);
                    } else {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (xpBonus[i] > 0.01) {
                        ImGui::Text("+%.1f%%", xpBonus[i]);
                    } else {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(5);
                    const int previewXp = (baseXp * state.xpEntries[i].percent) / 100;
                    if (xpPreview[i] != previewXp) {
                        ImGui::Text("%d -> %d", previewXp, xpPreview[i]);
                    } else {
                        ImGui::Text("%d", previewXp);
                    }
                    if (state.active->profile.spirit() != ProfileSpirit::None && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", ProfileSpiritEffectLabel(state.active->profile.spirit()));
                    }
                }
                ImGui::EndTable();
            }
            if (visibleXpCount == 0) {
                ImGui::TextDisabled(u8"Нет навыков по фильтру.");
            }

            int percentSum = 0;
            int ratingSum = 0;
            int maxSharePercent = 0;
            for (const auto& entry : state.xpEntries) {
                percentSum += entry.percent;
                ratingSum += std::max(0, entry.rating);
                if (entry.percent > maxSharePercent) {
                    maxSharePercent = entry.percent;
                }
            }

            if (ratingSum <= 0) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"Выберите навыки и оценки.");
            } else if (percentSum == 100) {
                ImGui::TextColored(accent, u8"Назначено: %d%%", percentSum);
            } else {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"Назначено: %d%%", percentSum);
            }
            const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * (static_cast<float>(maxSharePercent) / 100.0f);
            const int previewPoolRaw = static_cast<int>(std::round(baseXp * scoreMultiplier * focusBonus));
            const int previewPool = static_cast<int>(std::round(
                static_cast<double>(previewPoolRaw) * static_cast<double>(100 - taskPenaltyPercent) / 100.0));
            ImGui::Text(u8"Фокус-бонус: %.2f (макс. доля %d%%)", focusBonus, maxSharePercent);
            if (taskPenaltyPercent > 0) {
                ImGui::TextDisabled(u8"Прогноз XP по навыкам: %d -> %d после дедлайна (-%d%%)",
                                    previewPoolRaw, previewPool, taskPenaltyPercent);
            } else {
                ImGui::TextDisabled(u8"Прогноз XP по навыкам до штрафов: %d", previewPool);
            }
            if (state.active->profile.spirit() != ProfileSpirit::None) {
                ImVec4 spiritColor = state.active->profile.spirit() == ProfileSpirit::Evil
                    ? LogLevelColor(AppLogLevel::Warning)
                    : accent;
                spiritColor.w = std::max(spiritColor.w, 0.90f);
                ImGui::TextColored(spiritColor,
                                   u8"%s: %s",
                                   ProfileSpiritLabel(state.active->profile.spirit()),
                                   ProfileSpiritEffectLabel(state.active->profile.spirit()));
            }
            ImGui::TextDisabled(u8"Дух применяется персонально к каждому участнику при сохранении XP.");

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
            const int spiritEffectivePreview = ApplyProfileSpiritXpModifier(state.active->profile.spirit(), effectivePreview);
            if (spiritEffectivePreview != effectivePreview) {
                ImGui::TextDisabled(u8"%s: %d -> %d%s", globalHint.c_str(), effectivePreview, spiritEffectivePreview,
                                    previewPenalty ? u8" (после штрафов и духа)" : u8" (после духа)");
            } else {
                ImGui::TextDisabled(u8"%s: %d%s", globalHint.c_str(), effectivePreview,
                previewPenalty ? u8" (после штрафов)" : "");
            }

            if (ImGui::Button(u8"Применить", ImVec2(stackControls ? -1.0f : 0.0f, 0.0f))) {
                const int readyCategory = NormalizeCategoryIndex(state.taskCategoryIndex);
                const int readyScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
                const std::string project = TrimStringGui(state.taskProject.data());
                const std::string title = TrimStringGui(state.taskTitle.data());
                const std::string description = TrimStringGui(state.taskDescription.data());
                const int memberSum = SumSelectedProfileShares(state.taskShares);
                const int memberCount = CountSelectedProfileShares(state.taskShares);
                int percentCheck = 0;
                int ratingCheck = 0;
                for (const auto& entry : state.xpEntries) {
                    int clamped = ClampToRange(entry.percent, 0, 100);
                    percentCheck += clamped;
                    ratingCheck += std::max(0, entry.rating);
                }
                const bool hasContribution = ratingCheck > 0;
                const std::vector<std::string> selectedSkillIds = [&]() {
                    std::vector<std::string> ids;
                    ids.reserve(state.xpEntries.size());
                    for (size_t i = 0; i < state.xpEntries.size() && i < catalogSkills.size(); ++i) {
                        if (state.xpEntries[i].rating <= 0) continue;
                        ids.push_back(catalogSkills[i]);
                    }
                    return ids;
                }();
                if (project.empty() || title.empty() || description.empty()) {
                    SetStatus(state, u8"Заполните проект, задачу и описание.", 1.0f, 0.45f, 0.45f);
                } else if (memberCount <= 0) {
                    SetStatus(state, u8"Выберите хотя бы один профиль.", 1.0f, 0.45f, 0.45f);
                } else if (memberSum != 100) {
                    SetStatus(state, u8"Сумма вкладов должна быть 100%.", 1.0f, 0.45f, 0.45f);
                } else if (!hasContribution) {
                    SetStatus(state, u8"Нужно выбрать хотя бы один навык.", 1.0f, 0.45f, 0.45f);
                } else if (percentCheck != 100) {
                    SetStatus(state, u8"Сумма долей навыков должна быть 100%.", 1.0f, 0.45f, 0.45f);
                } else {
                    float maxShare = 0.0f;
                    if (!state.xpEntries.empty()) {
                        const auto it = std::max_element(
                        state.xpEntries.begin(), state.xpEntries.end(),
                        [](const XpEntry& a, const XpEntry& b) { return a.percent < b.percent; });
                        maxShare = static_cast<float>(it->percent) / 100.0f;
                    }
                    const float focusBonus = rules.focusBaseBonus + rules.focusAdditionalBonus * maxShare;
                    int basePoolRaw = static_cast<int>(std::round(
                    rules.categoryBaseXp[readyCategory] *
                    std::pow(std::max(0.1f, static_cast<float>(readyScore) / 10.0f), 1.35f) * focusBonus));
                    if (basePoolRaw < 0) basePoolRaw = 0;
                    int basePool = static_cast<int>(std::round(
                        static_cast<double>(basePoolRaw) * static_cast<double>(100 - taskPenaltyPercent) / 100.0));
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
                    for (const auto& projectEntry : state.projects) {
                        if (projectEntry.name == project) {
                            task.projectId = projectEntry.id;
                            break;
                        }
                    }
                    task.title = title;
                    task.description = description;
                    task.deadlineAt = 0;
                    task.status = kTaskStatusDone;
                    task.priority = kTaskPriorityMedium;
                    task.category = readyCategory;
                    task.deadlinePenaltyPercent = taskPenaltyPercent;
                    task.score = readyScore;
                    task.baseXp = rules.categoryBaseXp[readyCategory];
                    task.basePool = basePool;
                    task.createdAt = nowSeconds;
                    task.skillIds = selectedSkillIds;

                    std::vector<TaskParticipant> participants;
                    std::vector<std::string> assignees;
                    std::string activeId = state.active ? state.active->id : std::string{};
                    constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;

                    for (int i = 0; i < static_cast<int>(state.taskShares.size()); ++i) {
                        const auto& share = state.taskShares[i];
                        if (!share.selected || share.archived) continue;
                        assignees.push_back(share.id);
                        if (!storage.set_active_profile(share.id)) continue;
                        auto profileOpt = storage.load_profile();
                        if (!profileOpt) continue;
                        Profile& profile = *profileOpt;
                        const std::string rollbackSnapshot = SerializeProfileTaskRollbackSnapshot(profile);

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
                            const int skillXpBeforeSpirit = static_cast<int>(std::round(shareXp * mult));
                            int finalSkillXp = ApplyProfileSpiritXpModifier(profile.spirit(), skillXpBeforeSpirit);
                            skillXpTotal += finalSkillXp;
                            bool leveled = profile.grant_xp(skillId, finalSkillXp);
                            if (!firstSkill) skillsStream << " | ";
                            skillsStream << displayName << " +" << finalSkillXp << " XP (" << state.xpEntries[s].percent << "%";
                            if (bonusPercent > 0.01) {
                                skillsStream << ", +" << std::fixed << std::setprecision(1) << bonusPercent << "%";
                                skillsStream << std::defaultfloat;
                            }
                            const int spiritPercent = ProfileSpiritXpModifierPercent(profile.spirit());
                            if (spiritPercent != 0) {
                                skillsStream << u8", дух " << (spiritPercent > 0 ? "+" : "") << spiritPercent << "%";
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
                        const int spiritPercent = ProfileSpiritXpModifierPercent(profile.spirit());
                        const int beforeSpiritXp = effectiveXp;
                        effectiveXp = ApplyProfileSpiritXpModifier(profile.spirit(), effectiveXp);
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
                        if (taskPenaltyPercent > 0) {
                            summaryStream << u8" (дедлайн -" << taskPenaltyPercent << "%)";
                        }
                        const int repeatPercent = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
                        const int recoveryPercent = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
                        if (repeatPenalty) summaryStream << u8" (повтор " << repeatPercent << "%)";
                        if (recoveryPenalty) summaryStream << u8" (прогрев " << recoveryPercent << "%)";
                        if (spiritPercent != 0) {
                            summaryStream << u8" (" << ProfileSpiritLabel(profile.spirit()) << " "
                                          << (spiritPercent > 0 ? "+" : "") << spiritPercent << "%";
                            if (beforeSpiritXp != effectiveXp) {
                                summaryStream << ": " << beforeSpiritXp << " -> " << effectiveXp;
                            }
                            summaryStream << ")";
                        }
                        AppendLog(state, share.id, summaryStream.str());

                        AppProfileMutationResult mutation = AppSaveProfileSnapshot(storage, activeId, share.id, profile);
                        if (!mutation.ok) {
                            SetStatus(state,
                                      mutation.errorMessage.empty() ? u8"Не удалось сохранить профиль после начисления XP." : mutation.errorMessage,
                                      1.0f, 0.45f, 0.45f);
                            continue;
                        }
                        TaskParticipant participant;
                        participant.profileId = share.id;
                        participant.percent = share.percent;
                        participant.globalXp = effectiveXp;
                        participant.skillXp = skillXpTotal;
                        participant.rollbackSnapshot = rollbackSnapshot;
                        participants.push_back(std::move(participant));
                    }

                    if (!participants.empty()) {
                        bool saveOk = false;
                        if (fromTask) {
                            AppMutationResult mutation = AppFinalizeTaskXp(
                                state.storageDir,
                                state.tasks,
                                sourceTask->id,
                                readyCategory,
                                readyScore,
                                rules.categoryBaseXp[readyCategory],
                                basePool,
                                assignees,
                                selectedSkillIds,
                                participants,
                                state.isAdmin ? "admin" : (state.active ? state.active->id : "system"),
                                &state.taskAudit);
                            saveOk = mutation.ok;
                            if (!mutation.ok) {
                                SetStatus(state, mutation.errorMessage, 1.0f, 0.45f, 0.45f);
                            }
                        } else {
                            task.participants = std::move(participants);
                            task.assignees = std::move(assignees);
                            state.tasks.push_back(std::move(task));
                            saveOk = SaveTasks(state.storageDir, state.tasks);
                        }
                        if (!activeId.empty()) {
                            storage.set_active_profile(activeId);
                            RefreshActiveProfile(state, storage, catalog);
                            SyncRankSelection(state);
                        }
                        PrepareTaskShares(state);
                        SetEvenSkillRatingsForAllowed();
                        state.taskProject.fill('\0');
                        state.taskTitle.fill('\0');
                        state.taskDescription.fill('\0');
                        state.xpSourceTaskId.clear();
                        state.xpTaskPenaltyPercent = 0;
                        if (saveOk) {
                            SetStatus(state,
                                      fromTask ? u8"XP записан в существующую задачу." : u8"Задача сохранена и опыт начислен.",
                                      0.45f, 0.9f, 0.45f);
                            ImGui::CloseCurrentPopup();
                            xpPopupOpen = false;
                        } else {
                            SetStatus(state,
                                      fromTask
                                          ? u8"Опыт начислен, но не удалось обновить задачу."
                                          : u8"Опыт начислен, но не удалось записать файл задач.",
                                      1.0f, 0.75f, 0.35f,
                                      1.0f, AppLogLevel::Warning, u8"Задачи");
                        }
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
