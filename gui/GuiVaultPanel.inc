static void SyncVaultBuffers(GuiState& state) {
    std::snprintf(state.vaultCurrencyName.data(), state.vaultCurrencyName.size(), "%s",
                  state.vault.currencyName.c_str());
    std::snprintf(state.vaultCurrencyCode.data(), state.vaultCurrencyCode.size(), "%s",
                  state.vault.currencyCode.c_str());
    state.vaultLogLimitDraft = state.vault.logLimit;
    state.vaultPomodoroStartMinutes = state.vault.pomodoroStartMinutes;
    state.vaultPomodoroEndMinutes = state.vault.pomodoroEndMinutes;
    state.vaultPomodoroMinMinutes = state.vault.pomodoroMinMinutes;
    state.vaultPomodoroCoins = state.vault.pomodoroCoinsPerCycle;
    state.vaultPomodoroDaysMask = state.vault.pomodoroDaysMask;
}

static void ApplyVaultSettings(GuiState& state) {
    AppVaultMutationResult result = AppApplyVaultDraft(
        state.storageDir,
        state.vault,
        TrimStringGui(state.vaultCurrencyName.data()),
        TrimStringGui(state.vaultCurrencyCode.data()),
        state.vaultLogLimitDraft,
        state.vaultPomodoroStartMinutes,
        state.vaultPomodoroEndMinutes,
        state.vaultPomodoroMinMinutes,
        state.vaultPomodoroCoins,
        state.vaultPomodoroDaysMask);
    if (result.ok) {
        SyncVaultBuffers(state);
        SetStatus(state, u8"Настройки хранилища сохранены.", 0.45f, 0.9f, 0.45f);
    } else {
        SetStatus(state, result.errorMessage, 1.0f, 0.45f, 0.45f);
    }
}

void DrawVaultAdminPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::VaultAdmin, state.storageDir);
    DrawCardHeader(u8"Хранилище");
    if (!state.isAdmin) {
        ImGui::TextDisabled(u8"Только для администратора.");
        return;
    }
    ImGui::TextDisabled(u8"Хранится в meta/storage.json, синхронизируется через облако.");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::Text(u8"Баланс: %.0f", state.vault.balance);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawCardHeader(u8"Валюта");
    ImGui::TextDisabled(u8"Название и код можно менять без миграций.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##vault_currency_name", u8"Название монеты", state.vaultCurrencyName.data(),
                              state.vaultCurrencyName.size());
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputTextWithHint("##vault_currency_code", u8"Код", state.vaultCurrencyCode.data(),
                              state.vaultCurrencyCode.size());
    if (ImGui::Button(u8"Сохранить название", ImVec2(-1.0f, 0.0f))) {
        ApplyVaultSettings(state);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawCardHeader(u8"Журнал операций");
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderInt(u8"Хранить записей", &state.vaultLogLimitDraft, 10, 50)) {
        // draft only
    }
    if (ImGui::SmallButton(u8"Применить лимит")) {
        ApplyVaultSettings(state);
    }


    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawCardHeader(u8"Начисление за помодоро");
    ImGui::TextDisabled(u8"Монеты начисляются за полный цикл фокуса.");
    int startHour = state.vaultPomodoroStartMinutes / 60;
    int startMin = state.vaultPomodoroStartMinutes % 60;
    int endHour = state.vaultPomodoroEndMinutes / 60;
    int endMin = state.vaultPomodoroEndMinutes % 60;
    if (ImGui::BeginTable("vault_pomodoro_time", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("h", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("m", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled(u8"Старт");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##vault_start_h", &startHour, 0, 23);
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##vault_start_m", &startMin, 0, 59);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled(u8"Конец");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##vault_end_h", &endHour, 0, 23);
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##vault_end_m", &endMin, 0, 59);
        ImGui::EndTable();
    }
    state.vaultPomodoroStartMinutes = startHour * 60 + startMin;
    state.vaultPomodoroEndMinutes = endHour * 60 + endMin;
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt(u8"Мин. минут", &state.vaultPomodoroMinMinutes, 1, 90);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt(u8"Монет за цикл", &state.vaultPomodoroCoins, 0, 5);

    ImGui::TextDisabled(u8"Дни недели");
    const char* dayLabels[] = {u8"Вс", u8"Пн", u8"Вт", u8"Ср", u8"Чт", u8"Пт", u8"Сб"};
    for (int d = 0; d < 7; ++d) {
        bool enabled = (state.vaultPomodoroDaysMask & (1 << d)) != 0;
        if (ImGui::Checkbox(dayLabels[d], &enabled)) {
            if (enabled) state.vaultPomodoroDaysMask |= (1 << d);
            else state.vaultPomodoroDaysMask &= ~(1 << d);
        }
        if (d < 6) ImGui::SameLine();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    if (ImGui::SmallButton(u8"Сохранить правила")) {
        ApplyVaultSettings(state);
    }
    const float listHeight = std::max(160.0f, ImGui::GetTextLineHeightWithSpacing() * 6.0f);
    if (ImGui::BeginChild("vault_log", ImVec2(0.0f, listHeight), true)) {
        if (state.vault.log.empty()) {
            ImGui::TextDisabled(u8"Операций пока нет.");
        } else if (ImGui::BeginTable("vault_log_table", 4,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(u8"Время", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn(u8"Действие", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn(u8"Сумма", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"Примечание", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            for (const auto& entry : state.vault.log) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string ts = FormatTimestamp(entry.timestamp, "%Y-%m-%d %H:%M");
                ImGui::TextUnformatted(ts.empty() ? "-" : ts.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(entry.action.empty() ? "-" : entry.action.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", entry.amount);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(entry.note.empty() ? "" : entry.note.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}
