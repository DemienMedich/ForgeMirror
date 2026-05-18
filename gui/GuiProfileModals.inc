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
                AppendProfileAudit(state.storageDir, result.id, "create", std::string("login=") + result.login);
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
        } else {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("create_profile_actions", 3, ControlTableFlagsGui())) {
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
                case ConfirmAction::Block: action = u8"блокировку"; break;
                case ConfirmAction::Unblock: action = u8"разблокировку"; break;
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
                } else if (state.confirmAction == ConfirmAction::Block) {
                    actionResult = SetProfileBlockedAction(storage, info.id, true);
                } else if (state.confirmAction == ConfirmAction::Unblock) {
                    actionResult = SetProfileBlockedAction(storage, info.id, false);
                }
                if (actionResult.ok) {
                    SetStatus(state, actionResult.message, 0.45f, 0.9f, 0.45f, 1.0f,
                    AppLogLevel::Info, u8"Профили");
                    std::string newFocus;
                    if (state.confirmAction != ConfirmAction::Delete) {
                        newFocus = info.id;
                    }
                    if (state.confirmAction == ConfirmAction::Block || state.confirmAction == ConfirmAction::Unblock) {
                        AppendProfileAudit(state.storageDir, info.id, state.confirmAction == ConfirmAction::Block ? "block" : "unblock");
                    }
                    RefreshProfiles(state, storage, catalog, newFocus);
                    if (state.confirmAction == ConfirmAction::Block || state.confirmAction == ConfirmAction::Unblock) {
                        state.profileProfessionDirty = true;
                    }
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
            } else {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("confirm_actions", 3, ControlTableFlagsGui())) {
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
            }
        } else {
            if (DrawEmptyStateGui("profile_confirm_missing_empty", UiIcon::Users,
                                  u8"Профиль не выбран",
                                  u8"Выберите активный профиль и повторите действие.",
                                  UiIcon::Close,
                                  u8"Закрыть",
                                  true)) {
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
            if (DrawEmptyStateGui("profile_unlock_missing_empty", UiIcon::Users,
                                  u8"Профиль не выбран",
                                  u8"Выберите профиль, который нужно разблокировать.",
                                  UiIcon::Close,
                                  u8"Закрыть",
                                  true)) {
                ImGui::CloseCurrentPopup();
                unlockOpen = false;
            }
        } else {
            const auto& profile = state.active->profile;
            ImGui::TextDisabled(u8"Профиль: %s", profile.name().c_str());
            bool submitUnlockRequested = ImGui::InputText(u8"Пароль", state.profilePasswordInput.data(),
                                                          state.profilePasswordInput.size(),
                                                          ImGuiInputTextFlags_Password |
                                                              ImGuiInputTextFlags_EnterReturnsTrue);
            DrawKeyboardHintLine(true);
            ImGui::TextDisabled(u8"Доверенное устройство");
            const char* trustLabels[] = {u8"Только в этой сессии", u8"30 дней", u8"90 дней"};
            int trustIndex = 0;
            if (state.profileTrustDays >= 90) trustIndex = 2;
            else if (state.profileTrustDays >= 30) trustIndex = 1;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##profile_trust", &trustIndex, trustLabels, IM_ARRAYSIZE(trustLabels))) {
                state.profileTrustDays = (trustIndex == 2) ? 90 : (trustIndex == 1 ? 30 : 0);
            }
            ImGui::TextDisabled(u8"Хранится локально и не синхронизируется.");
            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 260.0f;
            auto submitUnlock = [&]() {
                std::string input = state.profilePasswordInput.data();
                if (VerifyProfilePassword(profile, input)) {
                    const int trustDays = (state.profileTrustDays >= 90) ? 90 : (state.profileTrustDays >= 30 ? 30 : 0);
                    if (trustDays > 0) {
                        const std::int64_t expiresAt = NowSeconds() + static_cast<std::int64_t>(trustDays) * 86400;
                        state.trustedProfiles[state.active->id] = expiresAt;
                        SaveProfileAccessSettings(state);
                    } else {
                        state.unlockedProfiles.insert(state.active->id);
                    }
                    AppendProfileAudit(state.storageDir, state.active->id, "unlock");
                    SetStatus(state, u8"Доступ к профилю открыт.", 0.45f, 0.9f, 0.45f);
                    std::fill(state.profilePasswordInput.begin(), state.profilePasswordInput.end(), '\0');
                    ImGui::CloseCurrentPopup();
                    unlockOpen = false;
                } else {
                    SetStatus(state, u8"Неверный пароль.", 1.0f, 0.45f, 0.45f);
                }
            };
            if (submitUnlockRequested) {
                submitUnlock();
            }
            if (stackActions) {
                if (ImGui::Button(u8"Войти", ImVec2(-1.0f, 0.0f))) {
                    submitUnlock();
                }
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    unlockOpen = false;
                }
            } else {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("unlock_actions", 2, ControlTableFlagsGui())) {
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
            if (DrawEmptyStateGui("profile_change_password_missing_empty", UiIcon::Users,
                                  u8"Профиль не выбран",
                                  u8"Выберите профиль, для которого нужно изменить пароль.",
                                  UiIcon::Close,
                                  u8"Закрыть",
                                  true)) {
                ImGui::CloseCurrentPopup();
                changeOpen = false;
            }
        } else {
            auto profile = state.active->profile;
            const bool hasPassword = !profile.password_encoded().empty();
            bool submitChangeRequested = false;
            if (hasPassword) {
                submitChangeRequested |= ImGui::InputText(u8"Текущий пароль", state.profilePasswordInput.data(),
                                                          state.profilePasswordInput.size(),
                                                          ImGuiInputTextFlags_Password |
                                                              ImGuiInputTextFlags_EnterReturnsTrue);
            } else {
                ImGui::TextDisabled(u8"Пароль не задан. Установите новый.");
            }
            submitChangeRequested |= ImGui::InputText(u8"Новый пароль", state.profilePasswordNew.data(),
                                                      state.profilePasswordNew.size(),
                                                      ImGuiInputTextFlags_Password |
                                                          ImGuiInputTextFlags_EnterReturnsTrue);
            submitChangeRequested |= ImGui::InputText(u8"Повтор", state.profilePasswordConfirm.data(),
                                                      state.profilePasswordConfirm.size(),
                                                      ImGuiInputTextFlags_Password |
                                                          ImGuiInputTextFlags_EnterReturnsTrue);

            DrawKeyboardHintLine(true);
            ImGui::TextDisabled(u8"Доверенное устройство");
            const char* trustLabels[] = {u8"Только в этой сессии", u8"30 дней", u8"90 дней"};
            int trustIndex = 0;
            if (state.profileTrustDays >= 90) trustIndex = 2;
            else if (state.profileTrustDays >= 30) trustIndex = 1;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##profile_trust", &trustIndex, trustLabels, IM_ARRAYSIZE(trustLabels))) {
                state.profileTrustDays = (trustIndex == 2) ? 90 : (trustIndex == 1 ? 30 : 0);
            }
            ImGui::TextDisabled(u8"Хранится локально и не синхронизируется.");

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
                AppProfileMutationResult mutation = AppChangeProfilePassword(
                    storage, state.active->id, state.active->id, current, next, hasPassword);
                if (!mutation.ok || !mutation.profile) {
                    SetStatus(state,
                              mutation.errorMessage.empty() ? u8"Не удалось сохранить пароль." : mutation.errorMessage,
                              1.0f, 0.45f, 0.45f);
                    return;
                }
                state.active->profile = *mutation.profile;
                state.unlockedProfiles.erase(state.active->id);
                state.trustedProfiles.erase(state.active->id);
                const int trustDays = (state.profileTrustDays >= 90) ? 90 : (state.profileTrustDays >= 30 ? 30 : 0);
                if (trustDays > 0) {
                    const std::int64_t expiresAt = NowSeconds() + static_cast<std::int64_t>(trustDays) * 86400;
                    state.trustedProfiles[state.active->id] = expiresAt;
                } else {
                    state.unlockedProfiles.insert(state.active->id);
                }
                SaveProfileAccessSettings(state);
                AppendProfileAudit(state.storageDir, state.active->id, "password_change");
                SetStatus(state, u8"Пароль обновлён.", 0.45f, 0.9f, 0.45f);
                std::fill(state.profilePasswordInput.begin(), state.profilePasswordInput.end(), '\0');
                std::fill(state.profilePasswordNew.begin(), state.profilePasswordNew.end(), '\0');
                std::fill(state.profilePasswordConfirm.begin(), state.profilePasswordConfirm.end(), '\0');
                ImGui::CloseCurrentPopup();
                changeOpen = false;
            };
            if (submitChangeRequested) {
                applyChange();
            }
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
            } else {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("change_actions", 2, ControlTableFlagsGui())) {
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
        }
        ImGui::EndPopup();
    }
    if (!changeOpen && ImGui::IsPopupOpen(u8"Сменить пароль")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.profileResetPasswordRequest) {
        ImGui::OpenPopup(u8"Сбросить пароль");
        state.profileResetPasswordRequest = false;
        state.profileResetConfirm = false;
    }
    bool resetOpen = true;
    if (ImGui::BeginPopupModal(u8"Сбросить пароль", &resetOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        if (!state.active || state.active->id != state.profileResetTargetId) {
            if (DrawEmptyStateGui("profile_reset_password_missing_empty", UiIcon::Users,
                                  u8"Профиль не выбран",
                                  u8"Выберите профиль, для которого нужно сбросить пароль.",
                                  UiIcon::Close,
                                  u8"Закрыть",
                                  true)) {
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
                AppProfileMutationResult mutation = AppResetProfilePassword(
                    storage, state.active->id, state.active->id, state.profileResetPassword);
                if (!mutation.ok || !mutation.profile) {
                    SetStatus(state,
                              mutation.errorMessage.empty() ? u8"Не удалось сбросить пароль." : mutation.errorMessage,
                              1.0f, 0.45f, 0.45f);
                    return;
                }
                state.active->profile = *mutation.profile;
                state.unlockedProfiles.erase(state.active->id);
                state.trustedProfiles.erase(state.active->id);
                SaveProfileAccessSettings(state);
                AppendProfileAudit(state.storageDir, state.active->id, "password_reset");
                SetStatus(state, u8"Пароль сброшен.", 0.45f, 0.9f, 0.45f);
                ImGui::CloseCurrentPopup();
                resetOpen = false;
            };
            ImGui::Spacing();
            ImGui::Checkbox(u8"Подтверждаю сброс пароля", &state.profileResetConfirm);
            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 260.0f;
            if (stackActions) {
                if (!state.profileResetConfirm) ImGui::BeginDisabled();
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    applyReset();
                }
                if (!state.profileResetConfirm) ImGui::EndDisabled();
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    resetOpen = false;
                }
            } else {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("reset_actions", 2, ControlTableFlagsGui())) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (!state.profileResetConfirm) ImGui::BeginDisabled();
                if (ImGui::Button(u8"Сбросить", ImVec2(-1.0f, 0.0f))) {
                    applyReset();
                }
                if (!state.profileResetConfirm) ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    resetOpen = false;
                }
                ImGui::EndTable();
                }
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
