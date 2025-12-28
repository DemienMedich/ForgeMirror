void DrawRulesPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Rules, state.storageDir);
    GameplayConfig& draft = state.rulesDraft;
    ImGui::TextUnformatted(u8"Кривая уровней");
    ImGui::InputInt(u8"Базовый XP (уровень 1)", &draft.levelBaseXp);
    ImGui::InputInt(u8"Линейный прирост за уровень", &draft.levelLinearXp);
    ImGui::InputInt(u8"Квадратичный прирост за уровень", &draft.levelQuadraticXp);
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Базовый XP категорий");
    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
        std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[idx] + " XP";
        int value = draft.categoryBaseXp[idx];
        if (ImGui::InputInt(label.c_str(), &value)) {
            draft.categoryBaseXp[idx] = value;
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Бонусы и штрафы");
    ImGui::InputFloat(u8"Базовый фокус-бонус", &draft.focusBaseBonus, 0.05f, 0.5f, "%.2f");
    ImGui::InputFloat(u8"Доп. фокус-бонус", &draft.focusAdditionalBonus, 0.05f, 0.5f, "%.2f");
    ImGui::SliderFloat(u8"Коэффициент награды при повторе", &draft.repeatRewardFactor, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(u8"Коэффициент награды при прогреве", &draft.recoveryRewardFactor, 0.0f, 1.0f, "%.2f");
    ImGui::InputInt(u8"Задач прогрева", &draft.recoveryWarmupTasks);
    ImGui::TextDisabled(u8"Изменения применяются в CLI и GUI после сохранения.");
    if (ImGui::Button(u8"Сбросить")) {
        draft = state.rulesConfig;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сохранить и применить")) {
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
}

