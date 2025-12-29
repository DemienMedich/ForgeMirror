void DrawAdminLoginModal(GuiState& state) {
    // Admin login modal
    if (state.adminPopupRequest) {
        ImGui::OpenPopup(u8"Вход администратора");
        state.adminPopupRequest = false;
    }
    bool adminPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Вход администратора", &adminPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(u8"Пароль", state.adminPassword.data(), state.adminPassword.size(), ImGuiInputTextFlags_Password);
        const std::string layoutLabel = KeyboardLayoutLabel();
        ImGui::TextDisabled(u8"Раскладка: %s", layoutLabel.c_str());
        auto handleLogin = [&]() {
            if (std::string(state.adminPassword.data()) == kAdminPassword) {
                state.isAdmin = true;
                SetStatus(state, "Администратор: доступ открыт.", 0.45f, 0.9f, 0.45f, 1.0f,
                AppLogLevel::Info, "Админ");
                ImGui::CloseCurrentPopup();
                adminPopupOpen = false;
            } else {
                SetStatus(state, "Неверный пароль администратора.", 1.0f, 0.45f, 0.45f, 1.0f,
                AppLogLevel::Warning, "Админ");
            }
        };
        const float actionWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionWidth < 280.0f;
        if (stackActions) {
            if (ImGui::Button(u8"Войти", ImVec2(-1.0f, 0.0f))) {
                handleLogin();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                adminPopupOpen = false;
            }
        } else if (ImGui::BeginTable("admin_login_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Войти", ImVec2(-1.0f, 0.0f))) {
                handleLogin();
            }
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                adminPopupOpen = false;
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    if (!adminPopupOpen && ImGui::IsPopupOpen(u8"Вход администратора")) {
        ImGui::CloseCurrentPopup();
    }

}
