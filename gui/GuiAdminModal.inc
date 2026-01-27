void DrawAdminLoginModal(GuiState& state) {
    // Admin login modal
    if (state.adminPopupRequest) {
        ImGui::OpenPopup(u8"Вход администратора");
        state.adminPopupRequest = false;
    }
    bool adminPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Вход администратора", &adminPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawCardHeader(u8"Пароль администратора");
        ImGui::InputText(u8"Пароль", state.adminPassword.data(), state.adminPassword.size(), ImGuiInputTextFlags_Password);
        const std::string layoutLabel = KeyboardLayoutLabel();
        ImGui::TextDisabled(u8"Раскладка: %s", layoutLabel.c_str());
        auto handleLogin = [&]() {
            if (std::string(state.adminPassword.data()) == state.adminPasswordExpected) {
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
        } else if (ImGui::BeginTable("admin_login_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            if (ImGui::Button(u8"Войти", ImVec2(-1.0f, 0.0f))) {
                handleLogin();
            }
            ImGui::TableSetColumnIndex(2);
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

void DrawAdminPasswordModal(GuiState& state) {
    if (state.adminChangePopupRequest) {
        ImGui::OpenPopup(u8"Смена пароля администратора");
        state.adminChangePopupRequest = false;
    }
    bool popupOpen = true;
    if (ImGui::BeginPopupModal(u8"Смена пароля администратора", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawCardHeader(u8"Пароль");
        const char* envLabel = "FORGEMIRROR_ADMIN_PASSWORD";
        const char* env = std::getenv(envLabel);
        const bool envOverride = env && *env;
        if (envOverride) {
            ImGui::TextDisabled(u8"Пароль задан через %s.", envLabel);
            ImGui::TextDisabled(u8"Смена через приложение недоступна.");
            ImGui::Spacing();
        }

        if (envOverride) ImGui::BeginDisabled();
        ImGui::InputText(u8"Текущий пароль", state.adminPasswordCurrent.data(),
                         state.adminPasswordCurrent.size(), ImGuiInputTextFlags_Password);
        ImGui::InputText(u8"Новый пароль", state.adminPasswordNew.data(),
                         state.adminPasswordNew.size(), ImGuiInputTextFlags_Password);
        ImGui::InputText(u8"Повторите пароль", state.adminPasswordConfirm.data(),
                         state.adminPasswordConfirm.size(), ImGuiInputTextFlags_Password);
        if (envOverride) ImGui::EndDisabled();

        auto handleSave = [&]() {
            const std::string current(state.adminPasswordCurrent.data());
            const std::string next(state.adminPasswordNew.data());
            const std::string confirm(state.adminPasswordConfirm.data());
            if (current != state.adminPasswordExpected) {
                SetStatus(state, "Неверный текущий пароль.", 1.0f, 0.45f, 0.45f, 1.0f,
                AppLogLevel::Warning, "Админ");
                return;
            }
            if (next.empty()) {
                SetStatus(state, "Новый пароль не может быть пустым.", 1.0f, 0.6f, 0.4f, 1.0f,
                AppLogLevel::Warning, "Админ");
                return;
            }
            if (next != confirm) {
                SetStatus(state, "Пароли не совпадают.", 1.0f, 0.6f, 0.4f, 1.0f,
                AppLogLevel::Warning, "Админ");
                return;
            }
            if (!SetAdminPassword(state.storageDir, next)) {
                SetStatus(state, "Не удалось сохранить пароль.", 1.0f, 0.45f, 0.45f, 1.0f,
                AppLogLevel::Error, "Админ");
                return;
            }
            state.adminPasswordExpected = next;
            state.adminPasswordCurrent.fill('\0');
            state.adminPasswordNew.fill('\0');
            state.adminPasswordConfirm.fill('\0');
            SetStatus(state, "Пароль администратора обновлён.", 0.45f, 0.9f, 0.45f, 1.0f,
            AppLogLevel::Info, "Админ");
            ImGui::CloseCurrentPopup();
            popupOpen = false;
        };

        const float actionWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionWidth < 320.0f;
        if (stackActions) {
            if (!envOverride && ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                handleSave();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                popupOpen = false;
            }
        } else if (ImGui::BeginTable("admin_password_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            if (!envOverride && ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                handleSave();
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                popupOpen = false;
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    if (!popupOpen && ImGui::IsPopupOpen(u8"Смена пароля администратора")) {
        ImGui::CloseCurrentPopup();
    }
}
