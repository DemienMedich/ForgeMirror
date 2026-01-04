static void CloseWorkspaceWindows(GuiState& state) {
    state.showSkillCatalog = false;
    state.showPipeline = false;
    state.showView3d = false;
    state.showTasks = false;
    state.showPomodoro = false;
    state.showShortcuts = false;
    state.showAbout = false;
    state.showRules = false;
    state.showUiSettings = false;
    state.showView3dSettings = false;
    state.showLogs = false;
    state.showAdminStats = false;
    state.workspaceTabRequest = static_cast<int>(UiWindowId::Profile);
}

void DrawMainMenuBar(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;

    const bool canArchive = state.selectedIndex >= 0 &&
    state.selectedIndex < static_cast<int>(state.profiles.size()) &&
    !state.profiles[state.selectedIndex].archived;
    const bool canRestore = state.selectedIndex >= 0 &&
    state.selectedIndex < static_cast<int>(state.profiles.size()) &&
    state.profiles[state.selectedIndex].archived;
    const bool canDelete = state.selectedIndex >= 0 &&
    state.selectedIndex < static_cast<int>(state.profiles.size());
    const bool hasActive = state.active.has_value();

    ImVec4 menuBg = style.Colors[ImGuiCol_WindowBg];
    menuBg.w = std::max(menuBg.w, 0.85f);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, menuBg);
    if (ImGui::BeginMainMenuBar()) {
#ifdef APP_VERSION
        ImGui::TextDisabled("ForgeMirror %s", APP_VERSION);
#else
        ImGui::TextDisabled("ForgeMirror");
#endif

        if (ImGui::BeginMenu(u8"Профили")) {
            if (state.active) {
                std::string label = "[" + state.active->id + "] " + state.active->profile.name();
                ImGui::TextDisabled(u8"Активный: %s", label.c_str());
            } else {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Активный: (не выбран)");
            }
            ImGui::Separator();

            const bool profilesWasOpen = state.showProfilesWindow;
            ImGui::MenuItem(u8"Окно профилей", nullptr, &state.showProfilesWindow);
            if (!profilesWasOpen && state.showProfilesWindow) {
                state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
            }
            if (ImGui::MenuItem(u8"Обновить список")) {
                RefreshProfiles(state, storage, catalog);
            }
            ImGui::Separator();

            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Создать")) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            if (!state.isAdmin || !hasActive) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Добавить опыт")) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            if (!state.isAdmin || !hasActive) ImGui::EndDisabled();

            const bool canArchiveAction = canArchive || canRestore;
            const char* archiveLabel = canRestore ? u8"Вернуть" : u8"В архив";
            if (!state.isAdmin || !canArchiveAction) ImGui::BeginDisabled();
            if (ImGui::MenuItem(archiveLabel)) {
                state.confirmPopupRequest = true;
                state.confirmAction = canRestore ? ConfirmAction::Restore : ConfirmAction::Archive;
            }
            if (!state.isAdmin || !canArchiveAction) ImGui::EndDisabled();

            if (!state.isAdmin || !canDelete) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Удалить")) {
                state.confirmPopupRequest = true;
                state.confirmAction = ConfirmAction::Delete;
            }
            if (!state.isAdmin || !canDelete) ImGui::EndDisabled();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"Разделы")) {
            if (ImGui::MenuItem(u8"Профиль", "F1")) {
                CloseWorkspaceWindows(state);
            }
            if (ImGui::MenuItem(u8"Задачи")) {
                OpenWorkspaceTab(state, state.showTasks, UiWindowId::Tasks);
            }
            if (ImGui::MenuItem(u8"Навыки", "F2")) {
                OpenWorkspaceTab(state, state.showSkillCatalog, UiWindowId::SkillCatalog);
            }
            if (ImGui::MenuItem(u8"Пайплайн", "F3")) {
                OpenWorkspaceTab(state, state.showPipeline, UiWindowId::Pipeline);
            }
            if (ImGui::MenuItem(u8"Помодоро")) {
                OpenWorkspaceTab(state, state.showPomodoro, UiWindowId::Pomodoro);
            }
            if (ImGui::MenuItem(u8"Ярлыки")) {
                OpenWorkspaceTab(state, state.showShortcuts, UiWindowId::Shortcuts);
            }
            if (ImGui::MenuItem(u8"3D просмотр")) {
                OpenWorkspaceTab(state, state.showView3d, UiWindowId::View3D);
            }
            if (ImGui::MenuItem(u8"Настройки интерфейса")) {
                OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
            }
            if (state.isAdmin) {
                ImGui::Separator();
                if (ImGui::MenuItem(u8"Статистика", "F5")) {
                    OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
                }
                if (ImGui::MenuItem(u8"Логи", "F6")) {
                    OpenWorkspaceTab(state, state.showLogs, UiWindowId::Logs);
                }
                if (ImGui::MenuItem(u8"Правила", "F4")) {
                    OpenWorkspaceTab(state, state.showRules, UiWindowId::Rules);
                }
                if (ImGui::MenuItem(u8"3D настройки")) {
                    OpenWorkspaceTab(state, state.showView3dSettings, UiWindowId::View3DSettings);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"Окно")) {
            bool fullscreen = state.ui.windowFullscreen;
            if (ImGui::MenuItem(u8"Во весь экран", "F11", &fullscreen)) {
                state.ui.windowFullscreen = fullscreen;
                state.uiDirty = true;
            }
            bool borderless = !state.ui.windowDecorated;
            if (ImGui::MenuItem(u8"Без рамки", "F10", &borderless)) {
                state.ui.windowDecorated = !borderless;
                state.uiDirty = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(u8"О программе")) {
                OpenWorkspaceTab(state, state.showAbout, UiWindowId::About);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"Админ")) {
            ImGui::TextUnformatted(state.isAdmin ? u8"[Режим администратора]" : u8"[Режим просмотра]");
            ImGui::Separator();
            if (ImGui::MenuItem(state.isAdmin ? u8"Выйти из админа" : u8"Войти как админ")) {
                if (state.isAdmin) {
                    state.isAdmin = false;
                    state.showRules = false;
                    state.showView3dSettings = false;
                    state.showLogs = false;
                    state.showAdminStats = false;
                } else {
                    state.adminPopupRequest = true;
                    state.adminPassword.fill('\0');
                }
            }
            if (state.isAdmin) {
                if (ImGui::MenuItem(u8"Сменить пароль администратора")) {
                    state.adminChangePopupRequest = true;
                    state.adminPasswordCurrent.fill('\0');
                    state.adminPasswordNew.fill('\0');
                    state.adminPasswordConfirm.fill('\0');
                }
            }
            ImGui::EndMenu();
        }

        if (state.cloudConfig.enabled && ImGui::BeginMenu(u8"Облако")) {
            auto refreshCloudState = [&]() {
                state.cloudManifest = LoadCloudManifest(state.cloudConfig, state.storageDir);
                state.cloudUpdateAvailable = IsUpdateAvailable(state.cloudManifest, APP_VERSION);
            };

            if (ImGui::MenuItem(u8"Обновить из облака")) {
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

            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::MenuItem(u8"Выгрузить в облако")) {
                CloudSyncResult result = PushCloudSnapshot(state.cloudConfig, state.storageDir, CloudRole::Admin);
                refreshCloudState();
                if (result.ok) {
                    SetStatus(state, result.message, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, result.message, 1.0f, 0.75f, 0.35f);
                }
            }
            if (!state.isAdmin) ImGui::EndDisabled();

            if (!state.cloudManifest.appVersion.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled(u8"Версия в облаке: %s", state.cloudManifest.appVersion.c_str());
                if (state.cloudManifest.dataUpdatedAt > 0) {
                    std::string stamp = FormatTimestamp(state.cloudManifest.dataUpdatedAt, "%Y-%m-%d %H:%M");
                    ImGui::TextDisabled(u8"Данные обновлены: %s", stamp.c_str());
                }
                if (!state.cloudManifest.notes.empty()) {
                    ImGui::TextWrapped(u8"Примечание: %s", state.cloudManifest.notes.c_str());
                }
            }

            if (state.cloudUpdateAvailable) {
                ImGui::Separator();
                ImGui::TextColored(accent, u8"Доступно обновление клиента.");
                if (state.cloudManifest.releaseFile.empty()) {
                    ImGui::TextDisabled(u8"Файл обновления не указан.");
                } else {
                    if (ImGui::MenuItem(u8"Скачать обновление")) {
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

            ImGui::EndMenu();
        }

        const float iconSize = ImGui::GetFrameHeight();
        const float iconGap = style.ItemSpacing.x * 0.75f;
        const int iconCount = 6;
        const float iconsWidth = iconCount * iconSize + (iconCount - 1) * iconGap;
        float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - iconsWidth;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(rightX);
        }

        if (IconButtonGui("top_profiles", UiIcon::Users, u8"Профили", accent, iconSize, true, state.showProfilesWindow)) {
            state.showProfilesWindow = !state.showProfilesWindow;
            if (state.showProfilesWindow) {
                state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
            }
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_refresh", UiIcon::Refresh, u8"Обновить профили", accent, iconSize)) {
            RefreshProfiles(state, storage, catalog);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_chart", UiIcon::Chart, u8"Статистика", accent, iconSize, state.isAdmin, state.showAdminStats)) {
            OpenWorkspaceTab(state, state.showAdminStats, UiWindowId::AdminStats);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_settings", UiIcon::Settings, u8"Настройки интерфейса", accent, iconSize, true, state.showUiSettings)) {
            OpenWorkspaceTab(state, state.showUiSettings, UiWindowId::UiSettings);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_about", UiIcon::Info, u8"О программе", accent, iconSize, true, state.showAbout)) {
            OpenWorkspaceTab(state, state.showAbout, UiWindowId::About);
        }
        ImGui::SameLine(0.0f, iconGap);
        if (IconButtonGui("top_exit", UiIcon::Exit, u8"Выход", accent, iconSize)) {
            state.requestExit = true;
        }

        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor();
}

void DrawProfilesPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);

    if (BeginCard("profiles_list_card")) {
        DrawCardHeader(u8"Профили");
        ImGui::TextDisabled(u8"Главное меню: выберите профиль или создайте новый.");
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        const float actionsWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionsWidth < 360.0f;
        if (stackActions) {
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать профиль", ImVec2(-1.0f, 0.0f))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                RefreshProfiles(state, storage, catalog);
            }
        } else if (ImGui::BeginTable("profile_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            if (!state.isAdmin) ImGui::BeginDisabled();
            if (ImGui::Button(u8"Создать профиль", ImVec2(-1.0f, 0.0f))) {
                state.modalBuffer.fill('\0');
                state.createPopupRequest = true;
            }
            if (!state.isAdmin) ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                RefreshProfiles(state, storage, catalog);
            }
            ImGui::EndTable();
        }
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
        } else if (ImGui::BeginTable("profile_filters", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("filter_search", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("filter_archive", ImGuiTableColumnFlags_WidthStretch, 0.95f);
            ImGui::TableSetupColumn("filter_sort", ImGuiTableColumnFlags_WidthStretch, 0.95f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Поиск");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##profile_search", u8"Поиск профиля",
                state.filters.profile.data(), state.filters.profile.size());
            if (ImGui::SmallButton(u8"Сброс##profile_filter")) {
                state.filters.profile.fill('\0');
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Профили");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"##profile_archive", &state.profileArchiveFilter, archiveLabels, IM_ARRAYSIZE(archiveLabels));
            ImGui::TableSetColumnIndex(3);
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
                std::string nameA = ToLowerUtf8(pa.name);
                std::string nameB = ToLowerUtf8(pb.name);
                if (nameA != nameB) return nameA < nameB;
            }
            return pa.id < pb.id;
        });

        const float minListHeight = ImGui::GetTextLineHeightWithSpacing() * 10.0f;
        const float listHeight = std::max(220.0f, minListHeight);
        if (ImGui::BeginChild("profiles", ImVec2(0.0f, listHeight), false)) {
            ImGui::TextDisabled(u8"Показано: %d из %d",
            static_cast<int>(visibleIndices.size()),
            static_cast<int>(state.profiles.size()));
            if (state.profiles.empty()) {
                ImGui::TextDisabled(u8"Профили не найдены.");
                if (state.isAdmin) {
                    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
                    ImVec4 softBg = accent;
                    softBg.w = 0.18f;
                    ImVec4 softHover = accent;
                    softHover.w = 0.28f;
                    ImVec4 softActive = accent;
                    softActive.w = 0.38f;
                    ImGui::PushStyleColor(ImGuiCol_Button, softBg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, softHover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, softActive);
                    if (ImGui::Button(u8"Создать первый профиль", ImVec2(-1.0f, 0.0f))) {
                        state.modalBuffer.fill('\0');
                        state.createPopupRequest = true;
                    }
                    ImGui::PopStyleColor(3);
                } else {
                    ImGui::TextDisabled(u8"Создание профилей доступно администратору.");
                }
            } else if (visibleIndices.empty()) {
                ImGui::TextDisabled(u8"Нет профилей по фильтру.");
                if (ImGui::SmallButton(u8"Сбросить фильтр")) {
                    state.filters.profile.fill('\0');
                }
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
    }
    EndCard();

}
