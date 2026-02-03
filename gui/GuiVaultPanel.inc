static void SyncVaultBuffers(GuiState& state) {
    std::snprintf(state.vaultCurrencyName.data(), state.vaultCurrencyName.size(), "%s",
                  state.vault.currencyName.c_str());
    std::snprintf(state.vaultCurrencyCode.data(), state.vaultCurrencyCode.size(), "%s",
                  state.vault.currencyCode.c_str());
    state.vaultLogLimitDraft = state.vault.logLimit;
}

static void ApplyVaultSettings(GuiState& state) {
    state.vault.currencyName = TrimStringGui(state.vaultCurrencyName.data());
    state.vault.currencyCode = TrimStringGui(state.vaultCurrencyCode.data());
    state.vault.logLimit = std::clamp(state.vaultLogLimitDraft, 10, 50);
    if (SaveStorageVault(state.storageDir, state.vault)) {
        SetStatus(state, u8"Настройки хранилища сохранены.", 0.45f, 0.9f, 0.45f);
    } else {
        SetStatus(state, u8"Не удалось сохранить хранилище.", 1.0f, 0.45f, 0.45f);
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
