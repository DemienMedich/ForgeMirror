void DrawMainMenuPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Main menu window
    if (state.showMainMenu) {
        ImGui::Begin(u8"Главное меню", &state.showMainMenu);
        EnsureWindowVisible();
        DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
        ImGui::TextUnformatted(state.isAdmin ? u8"[Режим администратора]" : u8"[Режим просмотра]");
        if (!state.isAdmin) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        if (ImGui::Button(state.isAdmin ? u8"Выйти из админа" : u8"Войти как админ",
            ImVec2(-1.0f, 0.0f))) {
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
        ImGui::TextUnformatted(u8"Окно");
        {
            const float windowAvail = ImGui::GetContentRegionAvail().x;
            const bool stackWindowButtons = windowAvail < 360.0f;
            bool fullscreen = state.ui.windowFullscreen;
            bool borderless = !state.ui.windowDecorated;
            if (stackWindowButtons) {
                if (ToggleMenuButtonGui(u8"Во весь экран", fullscreen, ImVec2(-1.0f, 0.0f))) {
                    state.ui.windowFullscreen = fullscreen;
                    state.uiDirty = true;
                }
                if (ToggleMenuButtonGui(u8"Без рамки", borderless, ImVec2(-1.0f, 0.0f))) {
                    state.ui.windowDecorated = !borderless;
                    state.uiDirty = true;
                }
            } else if (ImGui::BeginTable("window_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("window_left", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("window_right", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ToggleMenuButtonGui(u8"Во весь экран", fullscreen, ImVec2(-1.0f, 0.0f))) {
                    state.ui.windowFullscreen = fullscreen;
                    state.uiDirty = true;
                }
                ImGui::TableNextColumn();
                if (ToggleMenuButtonGui(u8"Без рамки", borderless, ImVec2(-1.0f, 0.0f))) {
                    state.ui.windowDecorated = !borderless;
                    state.uiDirty = true;
                }
                ImGui::EndTable();
            }
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
        const float actionHeight = std::max(ImGui::GetFrameHeight() * 1.35f, 34.0f);
        const bool stackQuick = ImGui::GetContentRegionAvail().x < 360.0f;
        const bool canArchiveAction = canArchive || canRestore;
        const char* archiveLabel = canRestore ? u8"Вернуть" : u8"В архив";
        if (stackQuick) {
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать", ImVec2(-1.0f, actionHeight))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            if (!state.isAdmin || !hasActive) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, actionHeight))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            if (!state.isAdmin || !hasActive) ImGui::EndDisabled();

            if (!state.isAdmin || !canArchiveAction) ImGui::BeginDisabled();
            if (ImGui::Button(archiveLabel, ImVec2(-1.0f, actionHeight))) {
                state.confirmPopupRequest = true;
                state.confirmAction = canRestore ? ConfirmAction::Restore : ConfirmAction::Archive;
            }
            if (!state.isAdmin || !canArchiveAction) ImGui::EndDisabled();

            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Статистика", ImVec2(-1.0f, actionHeight))) {
                state.showAdminStats = true;
                state.workspaceTabRequest = static_cast<int>(UiWindowId::AdminStats);
            }
            if (!state.isAdmin) ImGui::EndDisabled();
        } else if (ImGui::BeginTable("quick_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("quick_left", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("quick_right", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow(0, actionHeight);
            ImGui::TableNextColumn();
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать", ImVec2(-1.0f, actionHeight))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (!state.isAdmin || !hasActive) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, actionHeight))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            if (!state.isAdmin || !hasActive) ImGui::EndDisabled();

            ImGui::TableNextRow(0, actionHeight);
            ImGui::TableNextColumn();
            if (!state.isAdmin || !canArchiveAction) ImGui::BeginDisabled();
            if (ImGui::Button(archiveLabel, ImVec2(-1.0f, actionHeight))) {
                state.confirmPopupRequest = true;
                state.confirmAction = canRestore ? ConfirmAction::Restore : ConfirmAction::Archive;
            }
            if (!state.isAdmin || !canArchiveAction) ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Статистика", ImVec2(-1.0f, actionHeight))) {
                state.showAdminStats = true;
                state.workspaceTabRequest = static_cast<int>(UiWindowId::AdminStats);
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextUnformatted(u8"Сервис");
        const bool stackService = ImGui::GetContentRegionAvail().x < 420.0f;
        if (stackService) {
            if (ImGui::Button(u8"Обновить", ImVec2(-1.0f, actionHeight))) {
                RefreshProfiles(state, storage, catalog);
            }
            if (!state.isAdmin || !canDelete) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, actionHeight))) {
                state.confirmPopupRequest = true;
                state.confirmAction = ConfirmAction::Delete;
            }
            if (!state.isAdmin || !canDelete) ImGui::EndDisabled();
            if (ImGui::Button(u8"Выход", ImVec2(-1.0f, actionHeight))) {
                state.requestExit = true;
            }
        } else if (ImGui::BeginTable("service_actions", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("service_left", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("service_mid", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("service_right", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow(0, actionHeight);
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Обновить", ImVec2(-1.0f, actionHeight))) {
                RefreshProfiles(state, storage, catalog);
            }
            ImGui::TableNextColumn();
            if (!state.isAdmin || !canDelete) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, actionHeight))) {
                state.confirmPopupRequest = true;
                state.confirmAction = ConfirmAction::Delete;
            }
            if (!state.isAdmin || !canDelete) ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Выход", ImVec2(-1.0f, actionHeight))) {
                state.requestExit = true;
            }
            ImGui::EndTable();
        }

        if (state.cloudConfig.enabled) {
            auto refreshCloudState = [&]() {
                state.cloudManifest = LoadCloudManifest(state.cloudConfig, state.storageDir);
                state.cloudUpdateAvailable = IsUpdateAvailable(state.cloudManifest, APP_VERSION);
            };
            ImGui::Separator();
            ImGui::TextUnformatted(u8"Облако");
            if (ImGui::Button(u8"Обновить из облака", ImVec2(-1.0f, actionHeight))) {
                CloudRole role = state.isAdmin ? CloudRole::Admin : CloudRole::Viewer;
                CloudSyncResult result = PullCloudSnapshot(state.cloudConfig, state.storageDir, role);
                RefreshProfiles(state, storage, catalog);
                refreshCloudState();
                if (result.ok) {
                    SetStatus(state, result.message, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, result.message, 1.0f, 0.75f, 0.35f);
                }
            }
            if (state.isAdmin) {
                if (ImGui::Button(u8"Выгрузить в облако", ImVec2(-1.0f, actionHeight))) {
                    CloudSyncResult result = PushCloudSnapshot(state.cloudConfig, state.storageDir, CloudRole::Admin);
                    refreshCloudState();
                    if (result.ok) {
                        SetStatus(state, result.message, 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, result.message, 1.0f, 0.75f, 0.35f);
                    }
                }
            }
            if (!state.cloudManifest.appVersion.empty()) {
                ImGui::TextDisabled(u8"Версия в облаке: %s", state.cloudManifest.appVersion.c_str());
                if (state.cloudManifest.dataUpdatedAt > 0) {
                    std::string stamp = FormatTimestamp(state.cloudManifest.dataUpdatedAt, "%Y-%m-%d %H:%M");
                    ImGui::TextDisabled(u8"Данные обновлены: %s", stamp.c_str());
                }
                if (!state.cloudManifest.notes.empty()) {
                    ImGui::TextWrapped(u8"Примечание: %s", state.cloudManifest.notes.c_str());
                }
                if (state.cloudUpdateAvailable) {
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    u8"Доступно обновление клиента.");
                    if (state.cloudManifest.releaseFile.empty()) {
                        ImGui::TextDisabled(u8"Файл обновления не указан.");
                    } else {
                        if (ImGui::Button(u8"Скачать обновление", ImVec2(-1.0f, actionHeight))) {
                            std::filesystem::path outPath;
                            CloudSyncResult dl = DownloadCloudRelease(state.cloudConfig, state.storageDir,
                                state.cloudManifest, outPath);
                            if (dl.ok) {
                                std::string msg = dl.message;
                                msg += u8" Файл: ";
                                msg += outPath.string();
                                SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                            } else {
                                SetStatus(state, dl.message, 1.0f, 0.75f, 0.35f);
                            }
                        }
                    }
                }
            }
        }

        ImGui::Separator();
        const float filtersWidth = ImGui::GetContentRegionAvail().x;
        const bool stackFilters = filtersWidth < 520.0f;
        const char* archiveLabels[] = {u8"Все", u8"Активные", u8"Архив"};
        const char* profileSortLabels[] = {u8"ID", u8"Имя"};
        if (stackFilters) {
            ImGui::TextDisabled(u8"Поиск");
            ImGui::InputTextWithHint(u8"##profile_search", u8"Поиск профиля",
                state.filters.profile.data(), state.filters.profile.size());
            if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
                state.filters.profile.fill('\0');
            }
            ImGui::TextDisabled(u8"Профили");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_archive", &state.profileArchiveFilter, archiveLabels, IM_ARRAYSIZE(archiveLabels));
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_sort", &state.profileSort, profileSortLabels, IM_ARRAYSIZE(profileSortLabels));
        } else if (ImGui::BeginTable("profile_filters", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("filter_search", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("filter_archive", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("filter_sort", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Поиск");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##profile_search", u8"Поиск профиля",
                state.filters.profile.data(), state.filters.profile.size());
            if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
                state.filters.profile.fill('\0');
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Профили");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_archive", &state.profileArchiveFilter, archiveLabels, IM_ARRAYSIZE(archiveLabels));
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Сортировка");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_sort", &state.profileSort, profileSortLabels, IM_ARRAYSIZE(profileSortLabels));
            ImGui::EndTable();
        }
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
