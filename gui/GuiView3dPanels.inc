void DrawView3dSettingsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::View3DSettings, state.storageDir);
    DrawCardHeader(u8"Модели");
    ImGui::TextDisabled(u8"Выберите модель и настройте вращение/цвет линий.");
    const auto models = LoadModelChoices(state.storageDir);
    const float filterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilter = filterWidth < 360.0f;
    ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    if (stackFilter) {
        ImGui::TextDisabled(u8"Фильтр моделей");
        ImGui::InputTextWithHint(u8"##model_filter", u8"Фильтр моделей",
                                 state.filters.model.data(), state.filters.model.size());
        if (ImGui::SmallButton(u8"Сброс##model_filter")) {
            state.filters.model.fill('\0');
        }
    } else if (ImGui::BeginTable("model_filter_row", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("model_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("model_filter", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("model_reset", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::TextDisabled(u8"Фильтр моделей");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##model_filter", u8"Фильтр моделей",
                                 state.filters.model.data(), state.filters.model.size());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            state.filters.model.fill('\0');
        }
        ImGui::EndTable();
    }
    int visibleModels = 0;
    for (const auto& model : models) {
        if (!MatchesFilter(model.label, state.filters.model.data())) continue;
        visibleModels += 1;
    }
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        visibleModels, static_cast<int>(models.size()));
    if (models.empty()) {
        ImGui::TextDisabled(u8"Нет моделей в папке data/models.");
    } else if (visibleModels == 0) {
        ImGui::TextDisabled(u8"Нет моделей по фильтру.");
        if (ImGui::SmallButton(u8"Сбросить фильтр")) {
            state.filters.model.fill('\0');
        }
    }
    std::string modelLabel = state.ui.modelPath.empty() ? u8"(не выбран)" : std::filesystem::path(state.ui.modelPath).filename().string();
    ImGui::TextDisabled(u8"Модель");
    if (ImGui::BeginCombo(u8"##model_combo", modelLabel.c_str())) {
        bool noneSelected = state.ui.modelPath.empty();
        if (ImGui::Selectable(u8"(не выбрана)", noneSelected)) {
            state.ui.modelPath.clear();
            std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", "");
            state.viewMeshPath.clear();
        }
        for (const auto& model : models) {
            if (!MatchesFilter(model.label, state.filters.model.data())) continue;
            bool selected = model.relativePath == state.ui.modelPath;
            if (ImGui::Selectable(model.label.c_str(), selected)) {
                state.ui.modelPath = model.relativePath;
                std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", state.ui.modelPath.c_str());
                state.viewMeshPath.clear();
            }
        }
        ImGui::EndCombo();
    }
    const float pathWidth = ImGui::GetContentRegionAvail().x;
    const bool stackPath = pathWidth < 420.0f;
    if (stackPath) {
        ImGui::TextDisabled(u8"Путь к модели");
        if (ImGui::InputText(u8"##model_path", state.modelPathBuffer.data(), state.modelPathBuffer.size())) {
            state.ui.modelPath = state.modelPathBuffer.data();
        }
        if (ImGui::Button(u8"Загрузить", ImVec2(-1.0f, 0.0f))) {
            state.viewMeshPath.clear();
        }
    } else if (ImGui::BeginTable("model_path_row", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("model_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("model_path", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("model_load", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::TextDisabled(u8"Путь к модели");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText(u8"##model_path", state.modelPathBuffer.data(), state.modelPathBuffer.size())) {
            state.ui.modelPath = state.modelPathBuffer.data();
        }
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"Загрузить", ImVec2(-1.0f, 0.0f))) {
            state.viewMeshPath.clear();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Параметры");
    ImGui::Checkbox(u8"Авто?вращение", &state.ui.modelAutoRotate);
    ImGui::SliderFloat(u8"Скорость вращения", &state.ui.modelAutoSpeed, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat(u8"Yaw", &state.ui.modelYaw, -3.14f, 3.14f, "%.2f");
    ImGui::SliderFloat(u8"Pitch", &state.ui.modelPitch, -1.57f, 1.57f, "%.2f");
    ImGui::SliderFloat(u8"Zoom", &state.ui.modelZoom, 0.3f, 3.0f, "%.2f");
    ImGui::ColorEdit4(u8"Цвет линий", &state.ui.modelColor.x);
}

void DrawView3dPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::View3D, state.storageDir);
    DrawCardHeader(u8"3D просмотр");
    ImGui::TextDisabled(u8"ЛКМ — вращение, колесо — масштаб. Автовращение включается в настройках.");
    auto resolved = state.ui.modelPath.empty()
        ? std::optional<std::filesystem::path>()
        : ResolveIconPath(state.ui.modelPath, state.storageDir);
    if (!resolved) {
        if (state.viewMeshPath != "<cube>") {
            state.viewMesh = MakeCubeMesh();
            state.viewMeshPath = "<cube>";
            state.viewMeshError.clear();
        }
    } else if (state.viewMeshPath != resolved->string()) {
        state.viewMeshPath = resolved->string();
        state.viewMeshError.clear();
        state.viewMesh.triangles.clear();
        state.viewMesh.valid = false;
        if (!LoadMeshFromFile(*resolved, state.viewMesh, state.viewMeshError)) {
            state.viewMesh = MakeCubeMesh();
        }
    }
    if (!state.viewMeshError.empty()) {
        ImGui::TextColored(LogLevelColor(AppLogLevel::Error), "%s", state.viewMeshError.c_str());
        ImGui::TextDisabled(u8"Проверьте путь к файлу или формат модели.");
    }

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.y < 180.0f) viewportSize.y = 180.0f;
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##view3d", viewportSize, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        state.ui.modelYaw += delta.x * 0.01f;
        state.ui.modelPitch += delta.y * 0.01f;
        state.ui.modelAutoRotate = false;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        state.ui.modelZoom = std::clamp(state.ui.modelZoom + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.0f);
    }
    if (state.ui.modelAutoRotate) {
        state.ui.modelYaw += state.ui.modelAutoSpeed * ImGui::GetIO().DeltaTime;
    }
    if (state.viewMesh.valid) {
        DrawMeshWireframe(state.viewMesh, state.ui, ImGui::GetWindowDrawList(), viewportPos, viewportSize);
    } else {
        ImU32 hintColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ImGui::GetWindowDrawList()->AddText(viewportPos, hintColor, u8"Загрузите OBJ/FBX модель.");
    }
}

