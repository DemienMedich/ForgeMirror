void DrawXpModal(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // XP sheet modal
    if (state.xpPopupRequest) {
        ImGui::OpenPopup(u8"Добавление опыта");
        state.xpPopupRequest = false;
    }
    bool xpPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Добавление опыта", &xpPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!state.isAdmin) {
            ImGui::TextUnformatted(u8"Доступно только администратору.");
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
        if (catalogSkills.empty()) {
            ImGui::TextUnformatted(u8"Каталог пуст.");
        } else if (!state.active) {
            ImGui::TextUnformatted(u8"Нет активного профиля.");
        } else {
            state.taskCategoryIndex = NormalizeCategoryIndex(state.taskCategoryIndex);
            state.taskScore = ClampToRange(state.taskScore, 1, Profile::kMaxCategoryScore);
            const GameplayConfig& rules = GetGameplayConfig();

            ImGui::TextUnformatted(u8"Параметры задачи");
            ImGui::Separator();
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
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                u8"Лучший результат: %d/10 (категория освоена)", currentBest);
            } else {
                ImGui::Text(u8"Лучший результат: %d/10", currentBest);
                if (penaltiesEnabled && state.taskScore <= currentBest) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                    u8"Повтор оценки - глобальный XP ограничен 35%% (навыки без штрафа).");
                } else if (!penaltiesEnabled && state.taskScore <= currentBest) {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.95f, 1.0f),
                    u8"Штрафы за повтор включатся после ранга Сеньор.");
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted(u8"Распределите навыки (автоподбор до 100%)");
            const float controlsWidth = ImGui::GetContentRegionAvail().x;
            const bool stackControls = controlsWidth < 520.0f;
            if (stackControls) {
                if (ImGui::Button(u8"Равномерно", ImVec2(-1.0f, 0.0f))) {
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
            } else if (ImGui::BeginTable("xp_controls", 4, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("xp_btn", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("xp_filter", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("xp_reset", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("xp_sort", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Равномерно", ImVec2(-1.0f, 0.0f))) {
                    PrepareXpEntries(state, catalog);
                }
                ImGui::TableNextColumn();
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
            if (ImGui::BeginTable("xp_sheet", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn(u8"Навык");
                ImGui::TableSetupColumn(u8"Доля (%)");
                ImGui::TableSetupColumn(u8"Ачивки");
                ImGui::TableSetupColumn("XP");
                ImGui::TableHeadersRow();
                for (int orderIdx = 0; orderIdx < static_cast<int>(xpOrder.size()); ++orderIdx) {
                    int i = xpOrder[orderIdx];
                    ImGui::TableNextRow();
                    const std::string& skillId = catalogSkills[i];
                    const std::string& displayName = xpNames[i];
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(displayName.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID(i);
                    int percent = state.xpEntries[i].percent;
                    ImGui::PushItemWidth(sliderWidth);
                    if (ImGui::SliderInt("##share", &percent, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                        AdjustSkillShare(state, i, percent);
                        percent = state.xpEntries[i].percent;
                    }
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(2);
                    if (xpBonus[i] > 0.01) {
                        ImGui::Text("+%.1f%%", xpBonus[i]);
                    } else {
                        ImGui::TextUnformatted("-");
                    }
                    ImGui::TableSetColumnIndex(3);
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

            ImGui::Separator();
            if (percentSum == 100) {
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), u8"Назначено: %d%%", percentSum);
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), u8"Назначено: %d%%", percentSum);
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
                const int storedBest = state.active->profile.category_best_score(static_cast<size_t>(readyCategory));
                int percentCheck = 0;
                bool hasContribution = false;
                for (const auto& entry : state.xpEntries) {
                    int clamped = ClampToRange(entry.percent, 0, 100);
                    percentCheck += clamped;
                    if (clamped > 0) hasContribution = true;
                }
                if (percentCheck != 100) {
                    SetStatus(state, u8"Сумма долей должна быть 100%.", 1.0f, 0.45f, 0.45f);
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
                    std::vector<int> xpDistribution(state.xpEntries.size(), 0);
                    int remainder = basePool;
                    int fallbackIndex = -1;
                    for (int i = 0; i < static_cast<int>(state.xpEntries.size()); ++i) {
                        int percent = ClampToRange(state.xpEntries[i].percent, 0, 100);
                        if (percent <= 0) continue;
                        int shareXp = (basePool * percent) / 100;
                        xpDistribution[i] = shareXp;
                        remainder -= shareXp;
                        if (fallbackIndex == -1 || percent > state.xpEntries[fallbackIndex].percent) {
                            fallbackIndex = i;
                        }
                    }
                    if (remainder > 0 && fallbackIndex >= 0) {
                        xpDistribution[fallbackIndex] += remainder;
                    }
                    const char* categoryLabel = ClassificationLabel(readyCategory);
                    std::ostringstream skillsStream;
                    skillsStream.imbue(std::locale::classic());
                    bool firstSkill = true;
                    for (int i = 0; i < static_cast<int>(catalogSkills.size()); ++i) {
                        int shareXp = xpDistribution[i];
                        if (shareXp <= 0) continue;
                        const std::string& skillId = catalogSkills[i];
                        const std::string displayName = catalog.display_name(skillId);
                        double weight = catalog.weight(skillId);
                        state.active->profile.add_skill(skillId, 1, weight);
                        double mult = state.active->profile.skill_bonus_multiplier(skillId, nowSeconds);
                        double bonusPercent = std::max(0.0, (mult - 1.0) * 100.0);
                        int finalSkillXp = static_cast<int>(std::round(shareXp * mult));
                        bool leveled = state.active->profile.grant_xp(skillId, finalSkillXp);
                        if (!firstSkill) skillsStream << " | ";
                        skillsStream << displayName << " +" << finalSkillXp << " XP (" << state.xpEntries[i].percent << "%";
                        if (bonusPercent > 0.01) {
                            skillsStream << ", +" << std::fixed << std::setprecision(1) << bonusPercent << "%";
                            skillsStream << std::defaultfloat;
                        }
                        skillsStream << ")";
                        if (leveled) skillsStream << u8" уровень вверх";
                        firstSkill = false;
                    }
                    if (!firstSkill) {
                        AppendLog(state, state.active->id, skillsStream.str());
                    }
                    const bool penaltiesEnabled = state.active->profile.penalties_enabled();
                    bool repeatPenalty = penaltiesEnabled && readyScore <= storedBest;
                    int effectiveXp = basePool;
                    if (repeatPenalty) {
                        effectiveXp = static_cast<int>(std::round(effectiveXp * rules.repeatRewardFactor));
                    }
                    constexpr std::int64_t kThirtyDays = 30LL * 24 * 3600;
                    if (penaltiesEnabled && state.active->profile.last_task_timestamp() > 0 &&
                    (nowSeconds - state.active->profile.last_task_timestamp()) > kThirtyDays) {
                        state.active->profile.start_penalty_recovery(rules.recoveryWarmupTasks);
                    }
                    bool recoveryPenalty = false;
                    if (!penaltiesEnabled && state.active->profile.penalty_active()) {
                        state.active->profile.start_penalty_recovery(0);
                    }
                    if (penaltiesEnabled && state.active->profile.penalty_active()) {
                        recoveryPenalty = true;
                        effectiveXp = static_cast<int>(std::round(effectiveXp * rules.recoveryRewardFactor));
                        state.active->profile.consume_penalty_task();
                    }
                    state.active->profile.set_last_task_timestamp(nowSeconds);
                    state.active->profile.increment_tasks_completed();
                    if (effectiveXp > 0) {
                        state.active->profile.grant_global_xp(effectiveXp);
                    }
                    if (readyScore > storedBest) {
                        state.active->profile.update_category_best_score(static_cast<size_t>(readyCategory), readyScore);
                        std::ostringstream bestStream;
                        bestStream.imbue(std::locale::classic());
                        bestStream << u8"Категория " << categoryLabel << u8": лучший результат " << readyScore << "/10";
                        AppendLog(state, state.active->id, bestStream.str());
                        if (readyScore == Profile::kMaxCategoryScore) {
                            std::ostringstream mastery;
                            mastery.imbue(std::locale::classic());
                            mastery << u8"Категория " << categoryLabel << u8" освоена!";
                            AppendLog(state, state.active->id, mastery.str());
                        }
                    }
                    constexpr bool kDecayEnabled = false;
                    if (kDecayEnabled) {
                        state.active->profile.reset_category_cooldown(static_cast<size_t>(readyCategory));
                        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                            if (idx == static_cast<size_t>(readyCategory)) continue;
                            state.active->profile.tick_category_cooldown(idx);
                            if (state.active->profile.category_cooldown(idx) < 0) {
                                state.active->profile.update_category_best_score(
                                idx, state.active->profile.category_best_score(idx) - 1);
                                state.active->profile.reset_category_cooldown(idx);
                                std::ostringstream decay;
                                decay.imbue(std::locale::classic());
                                decay << u8"Категория " << Profile::kCategoryLabels[idx] << u8" снижена до "
                                << state.active->profile.category_best_score(idx) << "/10";
                                AppendLog(state, state.active->id, decay.str());
                            }
                        }
                        int buffer = state.active->profile.category_cooldown(0);
                        for (size_t idx = 1; idx < Profile::kCategoryCount; ++idx) {
                            buffer = std::min(buffer, state.active->profile.category_cooldown(idx));
                        }
                        buffer = std::max(0, buffer);
                        state.active->profile.set_inactivity_tasks(buffer);
                    }
                    std::ostringstream summaryStream;
                    summaryStream.imbue(std::locale::classic());
                    summaryStream << u8"Задача [" << categoryLabel << u8"] с оценкой " << readyScore
                    << u8" => +" << effectiveXp << " XP";
                    const int repeatPercent = static_cast<int>(std::round(rules.repeatRewardFactor * 100.0f));
                    const int recoveryPercent = static_cast<int>(std::round(rules.recoveryRewardFactor * 100.0f));
                    if (repeatPenalty) summaryStream << u8" (повтор " << repeatPercent << "%)";
                    if (recoveryPenalty) summaryStream << u8" (прогрев " << recoveryPercent << "%)";
                    const std::string summaryText = summaryStream.str();
                    AppendLog(state, state.active->id, summaryText);
                    storage.save_profile(state.active->profile);
                    PrepareXpEntries(state, catalog);
                    SetStatus(state, summaryText, 0.45f, 0.9f, 0.45f);
                    ImGui::CloseCurrentPopup();
                    xpPopupOpen = false;
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
