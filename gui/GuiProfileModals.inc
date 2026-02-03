void DrawCreateProfileModal(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Create profile modal
    if (state.createPopupRequest) {
        ImGui::OpenPopup(u8"Создать профиль");
        state.createPopupRequest = false;
    }
    bool createPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Создать профиль", &createPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        DrawCardHeader(u8"Данные профиля");
        ImGui::InputText(u8"Имя", state.modalBuffer.data(), state.modalBuffer.size());
        auto createProfile = [&]() {
            CreateProfileResult result = CreateProfileAction(storage, catalog, state.modalBuffer.data());
            AppLogLevel level = LogLevelForResult(result);
            SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
            result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Профили");
            if (result.ok) {
                RefreshProfiles(state, storage, catalog, result.id);
                if (!result.login.empty() && !result.password.empty()) {
                    state.profileCredentialsLogin = result.login;
                    state.profileCredentialsPassword = result.password;
                    state.profileCredentialsPopupRequest = true;
                }
                ImGui::CloseCurrentPopup();
                createPopupOpen = false;
            }
        };
        const float actionWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionWidth < 280.0f;
        if (stackActions) {
            if (ImGui::Button(u8"Создать", ImVec2(-1.0f, 0.0f))) {
                createProfile();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                createPopupOpen = false;
            }
        } else if (ImGui::BeginTable("create_profile_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
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
            if (ImGui::Button(u8"Создать", ImVec2(-1.0f, 0.0f))) {
                createProfile();
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                createPopupOpen = false;
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    if (!createPopupOpen && ImGui::IsPopupOpen(u8"Создать профиль")) {
        ImGui::CloseCurrentPopup();
    }

}


void DrawConfirmModal(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Confirm modal
    if (state.confirmPopupRequest) {
        ImGui::OpenPopup(u8"Подтвердите действие");
        state.confirmPopupRequest = false;
    }
    bool confirmPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Подтвердите действие", &confirmPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
            const auto& info = state.profiles[state.selectedIndex];
            std::string action;
            switch (state.confirmAction) {
                case ConfirmAction::Archive: action = u8"архивирование"; break;
                case ConfirmAction::Restore: action = u8"восстановление"; break;
                case ConfirmAction::Delete: action = u8"удаление"; break;
                default: action = ""; break;
            }
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImGui::TextColored(accent, u8"Подтвердить %s профиля [%s] %s?",
                               action.c_str(), info.id.c_str(), info.name.c_str());
            auto applyAction = [&]() {
                ActionResult actionResult;
                if (state.confirmAction == ConfirmAction::Archive) {
                    actionResult = SetProfileArchivedAction(storage, info.id, true);
                } else if (state.confirmAction == ConfirmAction::Restore) {
                    actionResult = SetProfileArchivedAction(storage, info.id, false);
                } else if (state.confirmAction == ConfirmAction::Delete) {
                    actionResult = DeleteProfileAction(storage, info.id);
                }
                if (actionResult.ok) {
                    SetStatus(state, actionResult.message, 0.45f, 0.9f, 0.45f, 1.0f,
                    AppLogLevel::Info, u8"Профили");
                    std::string newFocus;
                    if (state.confirmAction != ConfirmAction::Delete) {
                        newFocus = info.id;
                    }
                    RefreshProfiles(state, storage, catalog, newFocus);
                } else {
                    SetStatus(state, actionResult.message, 1.0f, 0.45f, 0.45f, 1.0f,
                    AppLogLevel::Error, u8"Профили");
                }
                ImGui::CloseCurrentPopup();
                confirmPopupOpen = false;
            };
            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 280.0f;
            if (stackActions) {
                if (ImGui::Button(u8"Да", ImVec2(-1.0f, 0.0f))) {
                    applyAction();
                }
                if (ImGui::Button(u8"Нет", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
            } else if (ImGui::BeginTable("confirm_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
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
                if (ImGui::Button(u8"Да", ImVec2(-1.0f, 0.0f))) {
                    applyAction();
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Нет", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    confirmPopupOpen = false;
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled(u8"Профиль не выбран.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                confirmPopupOpen = false;
            }
        }
        ImGui::EndPopup();
    }
    if (!confirmPopupOpen && ImGui::IsPopupOpen(u8"Подтвердите действие")) {
        ImGui::CloseCurrentPopup();
        state.confirmAction = ConfirmAction::None;
    }

}


static bool VerifyProfilePassword(const Profile& profile, const std::string& input) {
    if (profile.password_encoded().empty()) return false;
    const std::string decoded = DecodePassword(profile.password_encoded());
    return !decoded.empty() && decoded == input;
}

void DrawProfilePasswordModals(GuiState& state, IJobStorage& storage) {
    if (state.profileUnlockPopupRequest) {
        ImGui::OpenPopup(u8"Доступ к профилю");
        state.profileUnlockPopupRequest = false;
    }
    bool unlockOpen = true;
    if (ImGui::BeginPopupModal(u8"Доступ к профилю", &unlockOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        if (!state.active || state.active->id != state.profileAuthTargetId) {
            ImGui::TextDisabled(u8"Профиль не выбран.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                unlockOpen = false;
            }
        } else {
            const auto& profile = state.active->profile;
            ImGui::TextDisabled(u8"Профиль: %s", profile.name().c_str());
            ImGui::InputText(u8"Пароль", state.profilePasswordInput.data(), state.profilePasswordInput.size(), ImGuiInputTextFlags_Password);
            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 260.0f;
            auto submitUnlock = [&]() {
                std::string input = state.profilePasswordInput.data();
                if (VerifyProfilePassword(profile, input)) {
                    state.unlockedProfiles.insert(state.active->id);
                    SetStatus(state, u8"Доступ к профилю открыт.", 0.45f, 0.9f, 0.45f);
                    std::fill(state.profilePasswordInput.begin(), state.profilePasswordInput.end(), '\0');
                    ImGui::CloseCurrentPopup();
                    unlockOpen = false;
                } else {
                    SetStatus(state, u8"Неверный пароль.", 1.0f, 0.45f, 0.45f);
                }
            };
            if (stackActions) {
                if (ImGui::Button(u8"Войти", ImVec2(-1.0f, 0.0f))) {
                    submitUnlock();
                }
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    unlockOpen = false;
                }
            } else if (ImGui::BeginTable("unlock_actions", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Button(u8"Войти", ImVec2(-1.0f, 0.0f))) {
                    submitUnlock();
                }
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    unlockOpen = false;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndPopup();
    }
    if (!unlockOpen && ImGui::IsPopupOpen(u8"Доступ к профилю")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.profileChangePasswordRequest) {
        ImGui::OpenPopup(u8"Сменить пароль");
        state.profileChangePasswordRequest = false;
    }
    bool changeOpen = true;
    if (ImGui::BeginPopupModal(u8"Сменить пароль", &changeOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        if (!state.active || state.active->id != state.profileAuthTargetId) {
            ImGui::TextDisabled(u8"Профиль не выбран.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                changeOpen = false;
            }
        } else {
            auto profile = state.active->profile;
            const bool hasPassword = !profile.password_encoded().empty();
            if (hasPassword) {
                ImGui::InputText(u8"Текущий пароль", state.profilePasswordInput.data(), state.profilePasswordInput.size(), ImGuiInputTextFlags_Password);
            } else {
                ImGui::TextDisabled(u8"Пароль не задан. Установите новый.");
            }
            ImGui::InputText(u8"Новый пароль", state.profilePasswordNew.data(), state.profilePasswordNew.size(), ImGuiInputTextFlags_Password);
            ImGui::InputText(u8"Повтор", state.profilePasswordConfirm.data(), state.profilePasswordConfirm.size(), ImGuiInputTextFlags_Password);

            auto applyChange = [&]() {
                std::string current = state.profilePasswordInput.data();
                std::string next = state.profilePasswordNew.data();
                std::string confirm = state.profilePasswordConfirm.data();
                if (next.empty()) {
                    SetStatus(state, u8"Новый пароль не может быть пустым.", 1.0f, 0.45f, 0.45f);
                    return;
                }
                if (next != confirm) {
                    SetStatus(state, u8"Пароли не совпадают.", 1.0f, 0.45f, 0.45f);
                    return;
                }
                if (hasPassword && !VerifyProfilePassword(profile, current)) {
                    SetStatus(state, u8"Неверный текущий пароль.", 1.0f, 0.45f, 0.45f);
                    return;
                }
                profile.set_password_encoded(EncodePassword(next));
                storage.set_active_profile(state.active->id);
                if (storage.save_profile(profile)) {
                    state.active->profile = profile;
                    state.unlockedProfiles.insert(state.active->id);
                    SetStatus(state, u8"Пароль обновлён.", 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить пароль.", 1.0f, 0.45f, 0.45f);
                    return;
                }
                std::fill(state.profilePasswordInput.begin(), state.profilePasswordInput.end(), '\0');
                std::fill(state.profilePasswordNew.begin(), state.profilePasswordNew.end(), '\0');
                std::fill(state.profilePasswordConfirm.begin(), state.profilePasswordConfirm.end(), '\0');
                ImGui::CloseCurrentPopup();
                changeOpen = false;
            };

            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 260.0f;
            if (stackActions) {
                if (ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                    applyChange();
                }
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    changeOpen = false;
                }
            } else if (ImGui::BeginTable("change_actions", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                    applyChange();
                }
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    changeOpen = false;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndPopup();
    }
    if (!changeOpen && ImGui::IsPopupOpen(u8"Сменить пароль")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.profileResetPasswordRequest) {
        ImGui::OpenPopup(u8"Сбросить пароль");
        state.profileResetPasswordRequest = false;
    }
    bool resetOpen = true;
    if (ImGui::BeginPopupModal(u8"Сбросить пароль", &resetOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        if (!state.active || state.active->id != state.profileResetTargetId) {
            ImGui::TextDisabled(u8"Профиль не выбран.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                resetOpen = false;
            }
        } else {
            const auto& profile = state.active->profile;
            ImGui::TextDisabled(u8"Профиль: %s", profile.name().c_str());
            ImGui::TextDisabled(u8"Логин:");
            ImGui::TextUnformatted(profile.login().empty() ? state.active->id.c_str() : profile.login().c_str());
            if (ImGui::SmallButton(u8"Копировать логин")) {
                const std::string login = profile.login().empty() ? state.active->id : profile.login();
                ImGui::SetClipboardText(login.c_str());
            }
            ImGui::TextDisabled(u8"Новый пароль:");
            ImGui::TextUnformatted(state.profileResetPassword.c_str());
            if (ImGui::SmallButton(u8"Копировать пароль")) {
                ImGui::SetClipboardText(state.profileResetPassword.c_str());
            }

            auto applyReset = [&]() {
                auto updated = state.active->profile;
                updated.set_password_encoded(EncodePassword(state.profileResetPassword));
                storage.set_active_profile(state.active->id);
                if (storage.save_profile(updated)) {
                    state.active->profile = updated;
                    state.unlockedProfiles.erase(state.active->id);
                    SetStatus(state, u8"Пароль сброшен.", 0.45f, 0.9f, 0.45f);
                    ImGui::CloseCurrentPopup();
                    resetOpen = false;
                } else {
                    SetStatus(state, u8"Не удалось сбросить пароль.", 1.0f, 0.45f, 0.45f);
                }
            };

            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 260.0f;
            if (stackActions) {
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    applyReset();
                }
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    resetOpen = false;
                }
            } else if (ImGui::BeginTable("reset_actions", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    applyReset();
                }
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    resetOpen = false;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndPopup();
    }
    if (!resetOpen && ImGui::IsPopupOpen(u8"Сбросить пароль")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.profileCredentialsPopupRequest) {
        ImGui::OpenPopup(u8"Данные доступа профиля");
        state.profileCredentialsPopupRequest = false;
    }
    bool credsOpen = true;
    if (ImGui::BeginPopupModal(u8"Данные доступа профиля", &credsOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        ImGui::TextDisabled(u8"Логин:");
        ImGui::TextUnformatted(state.profileCredentialsLogin.c_str());
        if (ImGui::SmallButton(u8"Копировать логин")) {
            ImGui::SetClipboardText(state.profileCredentialsLogin.c_str());
        }
        ImGui::TextDisabled(u8"Пароль:");
        ImGui::TextUnformatted(state.profileCredentialsPassword.c_str());
        if (ImGui::SmallButton(u8"Копировать пароль")) {
            ImGui::SetClipboardText(state.profileCredentialsPassword.c_str());
        }
        if (ImGui::Button(u8"Закрыть", ImVec2(-1.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            credsOpen = false;
        }
        ImGui::EndPopup();
    }
    if (!credsOpen && ImGui::IsPopupOpen(u8"Данные доступа профиля")) {
        ImGui::CloseCurrentPopup();
    }
}
