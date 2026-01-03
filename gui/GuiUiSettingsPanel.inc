void DrawUiSettingsPanel(GuiState& state, ImGuiStyle& style) {
    DrawWindowBackground(state.ui, UiWindowId::UiSettings, state.storageDir);
    DrawCardHeader(u8"Настройки интерфейса");
    ImGui::TextDisabled(u8"Соберите тему, выберите фон и сохраните пресет.");
    if (!state.isAdmin) {
        ImGui::Spacing();
        ImGui::TextDisabled(u8"Доступны только базовые параметры окна.");
        DrawCardHeader(u8"Окно");
        bool borderless = !state.ui.windowDecorated;
        if (ImGui::Checkbox(u8"Без рамки окна", &borderless)) {
            state.ui.windowDecorated = !borderless;
            state.uiDirty = true;
        }
        if (ImGui::Checkbox(u8"Полноэкранный режим (F11)", &state.ui.windowFullscreen)) {
            state.uiDirty = true;
        }
        ImGui::Spacing();
        DrawCardHeader(u8"Действия");
        const bool hasPendingChanges = state.uiDirty;
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        ImVec4 accentSoft = accent;
        accentSoft.w = 0.75f;
        if (!hasPendingChanges) ImGui::BeginDisabled();
        if (hasPendingChanges) {
            ImVec4 saveBg = accent;
            saveBg.w = 0.18f;
            ImVec4 saveHover = accent;
            saveHover.w = 0.28f;
            ImVec4 saveActive = accent;
            saveActive.w = 0.38f;
            ImGui::PushStyleColor(ImGuiCol_Button, saveBg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, saveHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, saveActive);
        }
        if (ImGui::Button(u8"Сохранить настройки")) {
            SaveUiSettings(state.storageDir, state.ui);
            state.uiDirty = false;
            AppendAppLog(state, AppLogLevel::Info, u8"Интерфейс", u8"Настройки интерфейса сохранены.");
        }
        if (hasPendingChanges) {
            ImGui::PopStyleColor(3);
        }
        if (!hasPendingChanges) ImGui::EndDisabled();
        if (hasPendingChanges) {
            ImGui::TextColored(accentSoft, u8"Есть несохранённые изменения.");
        } else {
            ImGui::TextDisabled(u8"Нет несохранённых изменений.");
        }
        return;
    }
    DrawCardHeader(u8"Тема и стиль");
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    const char* themes[] = {u8"Тёмная", u8"Светлая", u8"Классика"};
    const float headerWidth = ImGui::GetContentRegionAvail().x;
    const bool stackTheme = headerWidth < 520.0f;
    if (stackTheme) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"Тема", &state.ui.theme, themes, IM_ARRAYSIZE(themes))) {
            state.uiDirty = true;
        }
        if (ImGui::Button(u8"Сбросить тему", ImVec2(-1.0f, 0.0f))) {
            ApplyUiTheme(state.ui.theme, style);
            const auto& entries = UiColorEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                state.ui.colors[i] = style.Colors[entries[i].col];
            }
            state.ui.customColors = false;
            state.uiDirty = true;
        }
    } else if (ImGui::BeginTable("theme_row", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("theme_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("theme_combo", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("theme_reset", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"Тема", &state.ui.theme, themes, IM_ARRAYSIZE(themes))) {
            state.uiDirty = true;
        }
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Сбросить тему", ImVec2(-1.0f, 0.0f))) {
            ApplyUiTheme(state.ui.theme, style);
            const auto& entries = UiColorEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                state.ui.colors[i] = style.Colors[entries[i].col];
            }
            state.ui.customColors = false;
            state.uiDirty = true;
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Готовые темы");
    const bool stackThemePresets = headerWidth < 520.0f;
    if (stackThemePresets) {
        if (ImGui::Button(u8"Графит", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(0, state.ui, style);
            state.uiDirty = true;
        }
        if (ImGui::Button(u8"Техно-стекло", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(3, state.ui, style);
            state.uiDirty = true;
        }
        if (ImGui::Button(u8"Сапфир", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(1, state.ui, style);
            state.uiDirty = true;
        }
        if (ImGui::Button(u8"Песок", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(2, state.ui, style);
            state.uiDirty = true;
        }
    } else if (ImGui::BeginTable("theme_presets", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Графит", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(0, state.ui, style);
            state.uiDirty = true;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Техно-стекло", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(3, state.ui, style);
            state.uiDirty = true;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Сапфир", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(1, state.ui, style);
            state.uiDirty = true;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Песок", ImVec2(-1.0f, 0.0f))) {
            ApplyThemePreset(2, state.ui, style);
            state.uiDirty = true;
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Пресеты интерфейса");
    ImGui::TextDisabled(u8"Пресеты сохраняют стиль, масштаб и фон.");
    const bool stackUiPresets = headerWidth < 520.0f;
    auto applyGlassUiPreset = [&]() {
        ApplyThemePreset(3, state.ui, style);
        state.ui.fontScale = 1.05f;
        state.ui.alpha = 0.96f;
        state.ui.windowRounding = 14.0f;
        state.ui.frameRounding = 10.0f;
        state.ui.scrollbarRounding = 12.0f;
        state.ui.grabRounding = 10.0f;
        state.ui.windowPadding = ImVec2(14.0f, 12.0f);
        state.ui.framePadding = ImVec2(10.0f, 7.0f);
        state.ui.itemSpacing = ImVec2(10.0f, 8.0f);
        state.ui.backgroundAlpha = 0.20f;
        state.ui.backgroundTiled = false;
        for (auto& bg : state.ui.backgrounds) {
            bg.clear();
        }
        state.ui.windowDecorated = false;
        state.uiDirty = true;
    };
    if (stackUiPresets) {
        if (ImGui::Button(u8"Техно-стекло", ImVec2(-1.0f, 0.0f))) {
            applyGlassUiPreset();
        }
        if (ImGui::Button(u8"Минимализм", ImVec2(-1.0f, 0.0f))) {
            state.ui.fontScale = 1.0f;
            state.ui.alpha = 0.98f;
            state.ui.windowRounding = 10.0f;
            state.ui.frameRounding = 8.0f;
            state.ui.scrollbarRounding = 10.0f;
            state.ui.grabRounding = 8.0f;
            state.ui.windowPadding = ImVec2(12.0f, 10.0f);
            state.ui.framePadding = ImVec2(8.0f, 6.0f);
            state.ui.itemSpacing = ImVec2(10.0f, 8.0f);
            state.ui.backgroundAlpha = 0.18f;
            state.ui.backgroundTiled = false;
            state.ui.windowDecorated = false;
            state.uiDirty = true;
        }
        if (ImGui::Button(u8"Презентация", ImVec2(-1.0f, 0.0f))) {
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
        if (ImGui::Button(u8"Компактный", ImVec2(-1.0f, 0.0f))) {
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
    } else if (ImGui::BeginTable("ui_presets", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Техно-стекло", ImVec2(-1.0f, 0.0f))) {
            applyGlassUiPreset();
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Минимализм", ImVec2(-1.0f, 0.0f))) {
            state.ui.fontScale = 1.0f;
            state.ui.alpha = 0.98f;
            state.ui.windowRounding = 10.0f;
            state.ui.frameRounding = 8.0f;
            state.ui.scrollbarRounding = 10.0f;
            state.ui.grabRounding = 8.0f;
            state.ui.windowPadding = ImVec2(12.0f, 10.0f);
            state.ui.framePadding = ImVec2(8.0f, 6.0f);
            state.ui.itemSpacing = ImVec2(10.0f, 8.0f);
            state.ui.backgroundAlpha = 0.18f;
            state.ui.backgroundTiled = false;
            state.ui.windowDecorated = false;
            state.uiDirty = true;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Презентация", ImVec2(-1.0f, 0.0f))) {
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
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Компактный", ImVec2(-1.0f, 0.0f))) {
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
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Пользовательские пресеты");
    if (stackTheme) {
        ImGui::InputText(u8"Имя пресета", state.uiPresetName.data(), state.uiPresetName.size());
        if (ImGui::Button(u8"Сохранить пресет", ImVec2(-1.0f, 0.0f))) {
            const std::string rawName = TrimStringGui(state.uiPresetName.data());
            const std::string presetName = SanitizePresetName(rawName);
            std::error_code ec;
            std::filesystem::create_directories(UiPresetsDir(state.storageDir), ec);
            auto presetPath = UiPresetPath(state.storageDir, presetName);
            SaveUiSettingsToPath(presetPath, state.ui);
            std::snprintf(state.uiPresetName.data(), state.uiPresetName.size(), "%s", presetName.c_str());
            SetStatus(state, u8"Пресет сохранён.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
        }
    } else if (ImGui::BeginTable("ui_preset_save", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("preset_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("preset_name", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("preset_save", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText(u8"Имя пресета", state.uiPresetName.data(), state.uiPresetName.size());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Сохранить пресет", ImVec2(-1.0f, 0.0f))) {
            const std::string rawName = TrimStringGui(state.uiPresetName.data());
            const std::string presetName = SanitizePresetName(rawName);
            std::error_code ec;
            std::filesystem::create_directories(UiPresetsDir(state.storageDir), ec);
            auto presetPath = UiPresetPath(state.storageDir, presetName);
            SaveUiSettingsToPath(presetPath, state.ui);
            std::snprintf(state.uiPresetName.data(), state.uiPresetName.size(), "%s", presetName.c_str());
            SetStatus(state, u8"Пресет сохранён.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
        }
        ImGui::EndTable();
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
        if (stackTheme) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"Список пресетов", &state.uiPresetIndex, presetLabels.data(),
                         static_cast<int>(presetLabels.size()));
            if (ImGui::Button(u8"Загрузить пресет", ImVec2(-1.0f, 0.0f))) {
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
            if (ImGui::Button(u8"Удалить пресет", ImVec2(-1.0f, 0.0f))) {
                const auto presetPath = UiPresetPath(state.storageDir, presets[state.uiPresetIndex]);
                std::error_code ec;
                std::filesystem::remove(presetPath, ec);
                SetStatus(state, u8"Пресет удалён.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
            }
        } else if (ImGui::BeginTable("ui_preset_list", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("preset_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("preset_list", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("preset_load", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("preset_delete", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"Список пресетов", &state.uiPresetIndex, presetLabels.data(),
                         static_cast<int>(presetLabels.size()));
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Загрузить пресет", ImVec2(-1.0f, 0.0f))) {
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
            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button(u8"Удалить пресет", ImVec2(-1.0f, 0.0f))) {
                const auto presetPath = UiPresetPath(state.storageDir, presets[state.uiPresetIndex]);
                std::error_code ec;
                std::filesystem::remove(presetPath, ec);
                SetStatus(state, u8"Пресет удалён.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Интерфейс");
            }
            ImGui::EndTable();
        }
    }
    ImGui::SliderFloat(u8"Масштаб шрифта", &state.ui.fontScale, 0.8f, 1.6f, "%.2f");
    ImGui::SliderFloat(u8"Прозрачность", &state.ui.alpha, 0.6f, 1.0f, "%.2f");
    ImGui::SliderFloat(u8"Скругление окон", &state.ui.windowRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление элементов", &state.ui.frameRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление скролла", &state.ui.scrollbarRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Скругление захвата", &state.ui.grabRounding, 0.0f, 16.0f, "%.1f");
    ImGui::SliderFloat(u8"Прозрачность фона", &state.ui.backgroundAlpha, 0.0f, 1.0f, "%.2f");
    ImGui::Spacing();
    DrawCardHeader(u8"Окно");
    bool borderless = !state.ui.windowDecorated;
    if (ImGui::Checkbox(u8"Без рамки окна", &borderless)) {
        state.ui.windowDecorated = !borderless;
        state.uiDirty = true;
    }
    if (ImGui::Checkbox(u8"Полноэкранный режим (F11)", &state.ui.windowFullscreen)) {
        state.uiDirty = true;
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Отступы");
    ImGui::SliderFloat2(u8"Отступ окна", &state.ui.windowPadding.x, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat2(u8"Отступ элемента", &state.ui.framePadding.x, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat2(u8"Интервал", &state.ui.itemSpacing.x, 0.0f, 20.0f, "%.1f");

    ImGui::Spacing();
    DrawCardHeader(u8"Цвета");
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
            if (ImGui::CollapsingHeader(group.label)) {
                for (int idx : group.indices) {
                    ImGui::ColorEdit4(entries[idx].name, &state.ui.colors[idx].x);
                }
            }
        }
    }

    ImGui::Spacing();
    DrawCardHeader(u8"Фоны окон");
    ImGui::TextDisabled(u8"Фон применяется ко всем окнам, если выбран общий.");
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
    const float bgWidth = ImGui::GetContentRegionAvail().x;
    const bool stackBackground = bgWidth < 520.0f;
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
    if (stackBackground) {
        if (ImGui::Button(u8"Очистить все", ImVec2(-1.0f, 0.0f))) {
            for (auto& bg : state.ui.backgrounds) {
                bg.clear();
            }
            state.uiDirty = true;
        }
        ImGui::InputTextWithHint(u8"##bg_filter", u8"Фильтр фонов",
                                 state.filters.background.data(), state.filters.background.size());
        if (ImGui::SmallButton(u8"Сброс##bg_filter")) {
            state.filters.background.fill('\0');
        }
    } else if (ImGui::BeginTable("bg_controls", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("bg_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("bg_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("bg_reset", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("bg_clear", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##bg_filter", u8"Фильтр фонов",
                                 state.filters.background.data(), state.filters.background.size());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            state.filters.background.fill('\0');
        }
        ImGui::TableSetColumnIndex(3);
        if (ImGui::Button(u8"Очистить все", ImVec2(-1.0f, 0.0f))) {
            for (auto& bg : state.ui.backgrounds) {
                bg.clear();
            }
            state.uiDirty = true;
        }
        ImGui::EndTable();
    }
    int visibleBackgrounds = 0;
    for (const auto& bg : backgrounds) {
        if (!MatchesFilter(bg.label, state.filters.background.data())) continue;
        visibleBackgrounds += 1;
    }
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        visibleBackgrounds, static_cast<int>(backgrounds.size()));
    if (backgrounds.empty()) {
        ImGui::TextDisabled(u8"Фоны не найдены. Положите изображения в data/backgrounds.");
    } else if (visibleBackgrounds == 0) {
        ImGui::TextDisabled(u8"Нет фонов по фильтру.");
        if (ImGui::SmallButton(u8"Сбросить фильтр")) {
            state.filters.background.fill('\0');
        }
    }
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

    ImGui::Spacing();
    DrawCardHeader(u8"Действия");
    const bool hasPendingChanges = state.uiDirty;
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 accentSoft = accent;
    accentSoft.w = 0.75f;
    if (!hasPendingChanges) ImGui::BeginDisabled();
    if (hasPendingChanges) {
        ImVec4 saveBg = accent;
        saveBg.w = 0.18f;
        ImVec4 saveHover = accent;
        saveHover.w = 0.28f;
        ImVec4 saveActive = accent;
        saveActive.w = 0.38f;
        ImGui::PushStyleColor(ImGuiCol_Button, saveBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, saveHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, saveActive);
    }
    if (ImGui::Button(u8"Сохранить настройки")) {
        SaveUiSettings(state.storageDir, state.ui);
        state.uiDirty = false;
        AppendAppLog(state, AppLogLevel::Info, u8"Интерфейс", u8"Настройки интерфейса сохранены.");
    }
    if (hasPendingChanges) {
        ImGui::PopStyleColor(3);
    }
    if (!hasPendingChanges) ImGui::EndDisabled();
    if (stackTheme) {
        if (ImGui::Button(u8"Сбросить цвета", ImVec2(-1.0f, 0.0f))) {
            ApplyUiTheme(state.ui.theme, style);
            const auto& entries = UiColorEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                state.ui.colors[i] = style.Colors[entries[i].col];
            }
            state.ui.customColors = false;
            state.uiDirty = true;
        }
    } else {
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
    if (hasPendingChanges) {
        ImGui::TextColored(accentSoft, u8"Есть несохранённые изменения.");
    } else {
        ImGui::TextDisabled(u8"Нет несохранённых изменений.");
    }
}
