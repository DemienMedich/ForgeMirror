void DrawUiSettingsPanel(GuiState& state, ImGuiStyle& style) {
    DrawWindowBackground(state.ui, UiWindowId::UiSettings, state.storageDir);
    ImGui::TextUnformatted(u8"Тема и стиль");
    const char* themes[] = {u8"Тёмная", u8"Светлая", u8"Классика"};
    if (ImGui::Combo(u8"Тема", &state.ui.theme, themes, IM_ARRAYSIZE(themes))) {
        state.uiDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сбросить тему")) {
        ApplyUiTheme(state.ui.theme, style);
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            state.ui.colors[i] = style.Colors[entries[i].col];
        }
        state.ui.customColors = false;
        state.uiDirty = true;
    }
    ImGui::TextUnformatted(u8"Готовые темы");
    if (ImGui::Button(u8"Графит")) {
        ApplyThemePreset(0, state.ui, style);
        state.uiDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сапфир")) {
        ApplyThemePreset(1, state.ui, style);
        state.uiDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Песок")) {
        ApplyThemePreset(2, state.ui, style);
        state.uiDirty = true;
    }
    ImGui::TextUnformatted(u8"Пресеты интерфейса");
    if (ImGui::Button(u8"Презентация")) {
        state.ui.fontScale = 1.25f;
        state.ui.alpha = 1.0f;
        state.ui.windowRounding = 8.0f;
        state.ui.frameRounding = 6.0f;
        state.ui.scrollbarRounding = 8.0f;
        state.ui.grabRounding = 6.0f;
        state.ui.windowPadding = ImVec2(12.0f, 12.0f);
        state.ui.framePadding = ImVec2(8.0f, 6.0f);
        state.ui.itemSpacing = ImVec2(10.0f, 8.0f);
        state.uiDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Компактный")) {
        state.ui.fontScale = 0.9f;
        state.ui.alpha = 0.95f;
        state.ui.windowRounding = 4.0f;
        state.ui.frameRounding = 3.0f;
        state.ui.scrollbarRounding = 4.0f;
        state.ui.grabRounding = 3.0f;
        state.ui.windowPadding = ImVec2(6.0f, 6.0f);
        state.ui.framePadding = ImVec2(5.0f, 3.0f);
        state.ui.itemSpacing = ImVec2(6.0f, 4.0f);
        state.uiDirty = true;
    }
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Пользовательские пресеты");
    ImGui::InputText(u8"Имя пресета", state.uiPresetName.data(), state.uiPresetName.size());
    ImGui::SameLine();
    if (ImGui::Button(u8"Сохранить пресет")) {
        const std::string rawName = TrimStringGui(state.uiPresetName.data());
        const std::string presetName = SanitizePresetName(rawName);
        std::error_code ec;
        std::filesystem::create_directories(UiPresetsDir(state.storageDir), ec);
        auto presetPath = UiPresetPath(state.storageDir, presetName);
        SaveUiSettingsToPath(presetPath, state.ui);
        std::snprintf(state.uiPresetName.data(), state.uiPresetName.size(), "%s", presetName.c_str());
        SetStatus(state, u8"Пресет сохранён.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
    }
    const auto presets = LoadUiPresetNames(state.storageDir);
    if (presets.empty()) {
        ImGui::TextDisabled(u8"Нет сохранённых пресетов.");
        state.uiPresetIndex = 0;
    } else {
        state.uiPresetIndex = std::clamp(state.uiPresetIndex, 0, static_cast<int>(presets.size()) - 1);
        std::vector<const char*> presetLabels;
        presetLabels.reserve(presets.size());
        for (const auto& name : presets) presetLabels.push_back(name.c_str());
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo(u8"Список пресетов", &state.uiPresetIndex, presetLabels.data(),
                     static_cast<int>(presetLabels.size()));
        ImGui::SameLine();
        if (ImGui::Button(u8"Загрузить пресет")) {
            const auto presetPath = UiPresetPath(state.storageDir, presets[state.uiPresetIndex]);
            state.ui = LoadUiSettingsFromPath(presetPath, style);
            ApplyUiTheme(state.ui.theme, style);
            ApplyUiSettings(state.ui, style, ImGui::GetIO());
            state.uiLastTheme = state.ui.theme;
            state.uiDirty = true;
            std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
            ApplyUiFiltersToState(state);
            SetStatus(state, u8"Пресет загружен.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"Удалить пресет")) {
            const auto presetPath = UiPresetPath(state.storageDir, presets[state.uiPresetIndex]);
            std::error_code ec;
            std::filesystem::remove(presetPath, ec);
            SetStatus(state, u8"Пресет удалён.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
        }
    }
    ImGui::SliderFloat(u8"Масштаб шрифта", &state.ui.fontScale, 0.8f, 1.6f, "%.2f");
    ImGui::SliderFloat(u8"Прозрачность", &state.ui.alpha, 0.6f, 1.0f, "%.2f");
    ImGui::SliderFloat(u8"Скругление окон", &state.ui.windowRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление элементов", &state.ui.frameRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление скролла", &state.ui.scrollbarRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление захвата", &state.ui.grabRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Прозрачность фона", &state.ui.backgroundAlpha, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Окно");
    bool borderless = !state.ui.windowDecorated;
    if (ImGui::Checkbox(u8"Без рамки окна", &borderless)) {
        state.ui.windowDecorated = !borderless;
        state.uiDirty = true;
    }
    if (ImGui::Checkbox(u8"Полноэкранный режим (F11)", &state.ui.windowFullscreen)) {
        state.uiDirty = true;
    }
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Отступы");
    ImGui::SliderFloat2(u8"Отступ окна", &state.ui.windowPadding.x, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat2(u8"Отступ элемента", &state.ui.framePadding.x, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat2(u8"Интервал", &state.ui.itemSpacing.x, 0.0f, 20.0f, "%.1f");

    ImGui::Separator();
    if (ImGui::Checkbox(u8"Кастомные цвета", &state.ui.customColors)) {
        if (state.ui.customColors) {
            const auto& entries = UiColorEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                state.ui.colors[i] = style.Colors[entries[i].col];
            }
        }
    }
    if (state.ui.customColors) {
        const auto& entries = UiColorEntries();
        const auto& groups = UiColorGroups();
        for (const auto& group : groups) {
            if (group.indices.empty()) continue;
            if (ImGui::CollapsingHeader(group.label, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int idx : group.indices) {
                    ImGui::ColorEdit4(entries[idx].name, &state.ui.colors[idx].x);
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"Фоны окон");
    ImGui::Checkbox(u8"Замостить фон", &state.ui.backgroundTiled);
    ImGui::SliderFloat(u8"Масштаб плитки", &state.ui.backgroundTileScale, 0.25f, 3.0f, "%.2f");
    const auto backgrounds = LoadUiBackgroundChoices(state.storageDir);
    const auto& windows = UiWindows();
    std::string uniformBackground;
    bool uniform = true;
    if (!windows.empty()) {
        uniformBackground = state.ui.backgrounds[0];
        for (size_t i = 1; i < windows.size(); ++i) {
            if (state.ui.backgrounds[i] != uniformBackground) {
                uniform = false;
                break;
            }
        }
    }
    std::string quickLabel;
    if (!uniform) {
        quickLabel = u8"Разные";
    } else if (uniformBackground.empty()) {
        quickLabel = u8"(нет)";
    } else {
        quickLabel = std::filesystem::path(uniformBackground).filename().string();
    }
    if (ImGui::BeginCombo(u8"Быстрый фон", quickLabel.c_str())) {
        const bool noneSelected = uniform && uniformBackground.empty();
        if (ImGui::Selectable(u8"(нет)", noneSelected)) {
            for (auto& bg : state.ui.backgrounds) {
                bg.clear();
            }
            state.uiDirty = true;
        }
        if (backgrounds.empty()) {
            ImGui::TextDisabled(u8"Фоны не найдены.");
        } else {
            for (const auto& bg : backgrounds) {
                const bool selected = uniform && bg.relativePath == uniformBackground;
                if (ImGui::Selectable(bg.label.c_str(), selected)) {
                    for (auto& entry : state.ui.backgrounds) {
                        entry = bg.relativePath;
                    }
                    state.uiDirty = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"Очистить все")) {
        for (auto& bg : state.ui.backgrounds) {
            bg.clear();
        }
        state.uiDirty = true;
    }
    ImGui::InputTextWithHint(u8"##bg_filter", u8"Фильтр фонов",
                             state.filters.background.data(), state.filters.background.size());
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"Сброс##bg_filter")) {
        state.filters.background.fill('\0');
    }
    int visibleBackgrounds = 0;
    for (const auto& bg : backgrounds) {
        if (!MatchesFilter(bg.label, state.filters.background.data())) continue;
        visibleBackgrounds += 1;
    }
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        visibleBackgrounds, static_cast<int>(backgrounds.size()));
    for (size_t i = 0; i < windows.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        std::string current = state.ui.backgrounds[i];
        std::string label = current.empty() ? u8"(нет)" : std::filesystem::path(current).filename().string();
        if (ImGui::BeginCombo(windows[i].label, label.c_str())) {
            bool noneSelected = current.empty();
            if (ImGui::Selectable(u8"(нет)", noneSelected)) {
                state.ui.backgrounds[i].clear();
                state.uiDirty = true;
            }
            if (visibleBackgrounds == 0) {
                ImGui::TextDisabled(u8"Нет фонов по фильтру.");
            }
            for (const auto& bg : backgrounds) {
                if (!MatchesFilter(bg.label, state.filters.background.data())) continue;
                bool selected = bg.relativePath == current;
                ImGui::PushID(bg.label.c_str());
                if (const auto* tex = GetIconTexture(bg.absolutePath)) {
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, ImVec2(32.0f, 32.0f));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(bg.label.c_str(), selected)) {
                    state.ui.backgrounds[i] = bg.relativePath;
                    state.uiDirty = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button(u8"Сохранить настройки")) {
        SaveUiSettings(state.storageDir, state.ui);
        state.uiDirty = false;
        AppendAppLog(state, AppLogLevel::Info, u8"Интерфейс", u8"Настройки интерфейса сохранены.");
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Сбросить цвета")) {
        ApplyUiTheme(state.ui.theme, style);
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            state.ui.colors[i] = style.Colors[entries[i].col];
        }
        state.ui.customColors = false;
        state.uiDirty = true;
    }
}
