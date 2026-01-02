void DrawCreateProfileModal(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Create profile modal
    if (state.createPopupRequest) {
        ImGui::OpenPopup(u8"Создать профиль");
        state.createPopupRequest = false;
    }
    bool createPopupOpen = true;
    if (ImGui::BeginPopupModal(u8"Создать профиль", &createPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawCardHeader(u8"Данные профиля");
        ImGui::InputText(u8"Имя", state.modalBuffer.data(), state.modalBuffer.size());
        auto createProfile = [&]() {
            CreateProfileResult result = CreateProfileAction(storage, catalog, state.modalBuffer.data());
            AppLogLevel level = LogLevelForResult(result);
            SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
            result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Профили");
            if (result.ok) {
                RefreshProfiles(state, storage, catalog, result.id);
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
            ImGui::TextDisabled("1");
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
                ImGui::TextDisabled("1");
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
