void DrawMainMenuPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Main menu window
    if (state.showMainMenu) {
        ImGui::Begin(u8"Главное меню", &state.showMainMenu);
        EnsureWindowVisible();
        DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
        ImGui::TextUnformatted(state.isAdmin ? u8"[Режим администратора]" : u8"[Режим просмотра]");
        if (!state.isAdmin) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        if (ImGui::Button(state.isAdmin ? u8"Выйти из админа" : u8"Войти как админ")) {
            if (state.isAdmin) {
                state.isAdmin = false;
                state.showRules = false;
                state.showUiSettings = false;
                state.showView3dSettings = false;
                state.showLogs = false;
                state.showAdminStats = false;
            } else {
                state.adminPopupRequest = true;
                state.adminPassword.fill('\0');
            }
        }
        if (!state.isAdmin) ImGui::PopStyleColor();
        ImGui::Separator();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float buttonWidth = std::max(80.0f, (availWidth - spacing * 2.0f) / 3.0f);
        const ImVec2 buttonSize(buttonWidth, 0.0f);
        ImGui::TextUnformatted(u8"Окно");
        bool fullscreen = state.ui.windowFullscreen;
        if (ToggleMenuButtonGui(u8"Во весь экран", fullscreen, buttonSize)) {
            state.ui.windowFullscreen = fullscreen;
            state.uiDirty = true;
        }
        ImGui::SameLine();
        bool borderless = !state.ui.windowDecorated;
        if (ToggleMenuButtonGui(u8"Без рамки", borderless, buttonSize)) {
            state.ui.windowDecorated = !borderless;
            state.uiDirty = true;
        }
        ImGui::Separator();

        const bool canArchive = state.selectedIndex >= 0 &&
        state.selectedIndex < static_cast<int>(state.profiles.size()) &&
        !state.profiles[state.selectedIndex].archived;
        const bool canRestore = state.selectedIndex >= 0 &&
        state.selectedIndex < static_cast<int>(state.profiles.size()) &&
        state.profiles[state.selectedIndex].archived;
        const bool canDelete = state.selectedIndex >= 0 &&
        state.selectedIndex < static_cast<int>(state.profiles.size());
        const bool hasActive = state.active.has_value();

        ImGui::TextUnformatted(u8"Быстрые действия");
        if (ImGui::BeginTable("quick_actions", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableNextColumn();
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать", ImVec2(-1.0f, 0.0f))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (!state.isAdmin || !hasActive) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, 0.0f))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            if (!state.isAdmin || !hasActive) ImGui::EndDisabled();

            ImGui::TableNextColumn();
            const bool canArchiveAction = canArchive || canRestore;
            const char* archiveLabel = canRestore ? u8"Вернуть" : u8"В архив";
            if (!state.isAdmin || !canArchiveAction) ImGui::BeginDisabled();
            if (ImGui::Button(archiveLabel, ImVec2(-1.0f, 0.0f))) {
                state.confirmPopupRequest = true;
                state.confirmAction = canRestore ? ConfirmAction::Restore : ConfirmAction::Archive;
            }
            if (!state.isAdmin || !canArchiveAction) ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Статистика", ImVec2(-1.0f, 0.0f))) {
                state.showAdminStats = true;
                state.workspaceTabRequest = static_cast<int>(UiWindowId::AdminStats);
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextUnformatted(u8"Сервис");
        if (ImGui::BeginTable("service_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Обновить", ImVec2(-1.0f, 0.0f))) {
                RefreshProfiles(state, storage, catalog);
            }
            ImGui::TableNextColumn();
            if (!state.isAdmin || !canDelete) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, 0.0f))) {
                state.confirmPopupRequest = true;
                state.confirmAction = ConfirmAction::Delete;
            }
            if (!state.isAdmin || !canDelete) ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Выход", ImVec2(-1.0f, 0.0f))) {
                state.requestExit = true;
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::InputText(u8"Поиск профиля", state.filters.profile.data(), state.filters.profile.size());
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
            state.filters.profile.fill('\0');
        }
        ImGui::SameLine();
        const char* archiveLabels[] = {u8"Все", u8"Активные", u8"Архив"};
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo(u8"Профили", &state.profileArchiveFilter, archiveLabels, IM_ARRAYSIZE(archiveLabels));
        ImGui::SameLine();
        const char* profileSortLabels[] = {u8"ID", u8"Имя"};
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo(u8"Сортировка", &state.profileSort, profileSortLabels, IM_ARRAYSIZE(profileSortLabels));
        ImGui::Separator();
        std::vector<int> visibleIndices;
        visibleIndices.reserve(state.profiles.size());
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            const auto& info = state.profiles[i];
            if (state.profileArchiveFilter == 1 && info.archived) continue;
            if (state.profileArchiveFilter == 2 && !info.archived) continue;
            if (!MatchesFilter(info.id + " " + info.name, state.filters.profile.data())) {
                continue;
            }
            visibleIndices.push_back(i);
        }
        const int sortMode = std::clamp(state.profileSort, 0, 1);
        std::sort(visibleIndices.begin(), visibleIndices.end(), [&](int a, int b) {
            const auto& pa = state.profiles[a];
            const auto& pb = state.profiles[b];
            if (sortMode == 1) {
                std::string nameA = ToLowerAscii(pa.name);
                std::string nameB = ToLowerAscii(pb.name);
                if (nameA != nameB) return nameA < nameB;
            }
            return pa.id < pb.id;
        });

        if (ImGui::BeginChild("profiles", ImVec2(0, 0), false)) {
            ImGui::TextDisabled(u8"Показано: %d из %d",
            static_cast<int>(visibleIndices.size()),
            static_cast<int>(state.profiles.size()));
            if (visibleIndices.empty()) {
                ImGui::TextDisabled(u8"Нет профилей по фильтру.");
            }
            for (int i : visibleIndices) {
                const auto& info = state.profiles[i];
                std::string label = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
                if (ImGui::Selectable(label.c_str(), state.selectedIndex == i)) {
                    state.selectedIndex = i;
                    RefreshActiveProfile(state, storage, catalog);
                    SyncRankSelection(state);
                    SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }

}
