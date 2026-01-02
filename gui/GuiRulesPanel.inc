void DrawRulesPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Rules, state.storageDir);
    DrawCardHeader(u8"Правила");
    ImGui::TextDisabled(u8"Настройка формулы уровней и наград. Изменения влияют на весь прогресс.");
    GameplayConfig& draft = state.rulesDraft;
    auto rulesMatch = [](const GameplayConfig& a, const GameplayConfig& b) {
        auto closeEnough = [](float lhs, float rhs) { return std::fabs(lhs - rhs) < 0.0001f; };
        if (a.levelBaseXp != b.levelBaseXp) return false;
        if (a.levelLinearXp != b.levelLinearXp) return false;
        if (a.levelQuadraticXp != b.levelQuadraticXp) return false;
        if (a.categoryBaseXp != b.categoryBaseXp) return false;
        if (!closeEnough(a.focusBaseBonus, b.focusBaseBonus)) return false;
        if (!closeEnough(a.focusAdditionalBonus, b.focusAdditionalBonus)) return false;
        if (!closeEnough(a.repeatRewardFactor, b.repeatRewardFactor)) return false;
        if (!closeEnough(a.recoveryRewardFactor, b.recoveryRewardFactor)) return false;
        if (a.recoveryWarmupTasks != b.recoveryWarmupTasks) return false;
        return true;
    };
    DrawCardHeader(u8"Кривая уровней");
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool stackRules = avail < 420.0f;
    if (stackRules) {
        ImGui::TextDisabled(u8"Базовый XP (уровень 1)");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##level_base", &draft.levelBaseXp);
        ImGui::TextDisabled(u8"Линейный прирост за уровень");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##level_linear", &draft.levelLinearXp);
        ImGui::TextDisabled(u8"Квадратичный прирост за уровень");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##level_quad", &draft.levelQuadraticXp);
    } else if (ImGui::BeginTable("level_rules", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("level_label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("level_value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("level_hint", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImVec4 labelBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        labelBg.w = std::clamp(labelBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 labelBgU32 = ImGui::GetColorU32(labelBg);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Базовый XP (уровень 1)");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##level_base", &draft.levelBaseXp);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"база");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Линейный прирост за уровень");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##level_linear", &draft.levelLinearXp);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"линия");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Квадратичный прирост за уровень");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##level_quad", &draft.levelQuadraticXp);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"квадр.");
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Базовый XP категорий");
    if (stackRules) {
        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
            std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[idx] + " XP";
            ImGui::TextDisabled("%s", label.c_str());
            int value = draft.categoryBaseXp[idx];
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt(("##cat_xp_" + std::to_string(idx)).c_str(), &value)) {
                draft.categoryBaseXp[idx] = value;
            }
        }
    } else if (ImGui::BeginTable("category_rules", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("cat_label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("cat_value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("cat_hint", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImVec4 labelBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        labelBg.w = std::clamp(labelBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 labelBgU32 = ImGui::GetColorU32(labelBg);
        for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
            std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[idx] + " XP";
            int value = draft.categoryBaseXp[idx];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
            ImGui::TextDisabled("%s", label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt(("##cat_xp_" + std::to_string(idx)).c_str(), &value)) {
                draft.categoryBaseXp[idx] = value;
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("C%s", Profile::kCategoryLabels[idx]);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Бонусы и штрафы");
    if (stackRules) {
        ImGui::TextDisabled(u8"Базовый фокус-бонус");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat(u8"##focus_base", &draft.focusBaseBonus, 0.05f, 0.5f, "%.2f");
        ImGui::TextDisabled(u8"Доп. фокус-бонус");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat(u8"##focus_extra", &draft.focusAdditionalBonus, 0.05f, 0.5f, "%.2f");
        ImGui::TextDisabled(u8"Коэффициент награды при повторе");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"##repeat_factor", &draft.repeatRewardFactor, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled(u8"Коэффициент награды при прогреве");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"##recovery_factor", &draft.recoveryRewardFactor, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled(u8"Задач прогрева");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##recovery_tasks", &draft.recoveryWarmupTasks);
    } else if (ImGui::BeginTable("bonus_rules", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("bonus_label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("bonus_value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("bonus_hint", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImVec4 labelBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        labelBg.w = std::clamp(labelBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 labelBgU32 = ImGui::GetColorU32(labelBg);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Базовый фокус-бонус");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat(u8"##focus_base", &draft.focusBaseBonus, 0.05f, 0.5f, "%.2f");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"%");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Доп. фокус-бонус");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat(u8"##focus_extra", &draft.focusAdditionalBonus, 0.05f, 0.5f, "%.2f");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"%");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Коэффициент награды при повторе");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"##repeat_factor", &draft.repeatRewardFactor, 0.0f, 1.0f, "%.2f");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"x");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Коэффициент награды при прогреве");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"##recovery_factor", &draft.recoveryRewardFactor, 0.0f, 1.0f, "%.2f");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"x");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, labelBgU32);
        ImGui::TextDisabled(u8"Задач прогрева");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt(u8"##recovery_tasks", &draft.recoveryWarmupTasks);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"шт");
        ImGui::EndTable();
    }
    ImGui::TextDisabled(u8"Изменения применяются в CLI и GUI после сохранения.");
    ImGui::TextDisabled(u8"Рекомендуется пересчитать профили после изменения правил.");
    const bool hasChanges = !rulesMatch(draft, state.rulesConfig);
    if (stackRules) {
        if (!hasChanges) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
            draft = state.rulesConfig;
        }
        if (ImGui::Button(u8"Сохранить и применить", ImVec2(-1.0f, 0.0f))) {
            SaveRulesResult result = SaveGameplayRulesAction(draft, state.storageDir);
            if (result.ok) {
                state.rulesConfig = result.config;
                state.rulesDraft = result.config;
                SetGameplayConfig(result.config);
                ReapplyRulesToProfiles(state, storage, catalog);
                std::string keepId = state.active ? state.active->id : std::string{};
                RefreshProfiles(state, storage, catalog, keepId);
                SetStatus(state, result.message, 0.45f, 0.9f, 0.45f, 1.0f,
                          AppLogLevel::Info, u8"Правила");
            } else {
                SetStatus(state, result.message, 1.0f, 0.45f, 0.45f, 1.0f,
                          AppLogLevel::Error, u8"Правила");
            }
        }
        if (!hasChanges) ImGui::EndDisabled();
    } else if (ImGui::BeginTable("rules_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
        if (!hasChanges) ImGui::BeginDisabled();
        ImGui::TableSetupColumn("rules_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("rules_left", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("rules_right", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("1");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
            draft = state.rulesConfig;
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        if (ImGui::Button(u8"Сохранить и применить", ImVec2(-1.0f, 0.0f))) {
            SaveRulesResult result = SaveGameplayRulesAction(draft, state.storageDir);
            if (result.ok) {
                state.rulesConfig = result.config;
                state.rulesDraft = result.config;
                SetGameplayConfig(result.config);
                ReapplyRulesToProfiles(state, storage, catalog);
                std::string keepId = state.active ? state.active->id : std::string{};
                RefreshProfiles(state, storage, catalog, keepId);
                SetStatus(state, result.message, 0.45f, 0.9f, 0.45f, 1.0f,
                          AppLogLevel::Info, u8"Правила");
            } else {
                SetStatus(state, result.message, 1.0f, 0.45f, 0.45f, 1.0f,
                          AppLogLevel::Error, u8"Правила");
            }
        }
        if (!hasChanges) ImGui::EndDisabled();
        ImGui::EndTable();
    }
    if (!hasChanges) {
        ImGui::TextDisabled(u8"Нет несохранённых изменений.");
    }
}

