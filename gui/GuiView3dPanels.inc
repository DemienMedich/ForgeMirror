void DrawView3dSettingsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::View3DSettings, state.storageDir);
    const auto models = LoadModelChoices(state.storageDir);
    ImGui::InputTextWithHint(u8"##model_filter", u8"Фильтр моделей",
                             state.filters.model.data(), state.filters.model.size());
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"Сброс##model_filter")) {
        state.filters.model.fill('\0');
    }
    int visibleModels = 0;
    for (const auto& model : models) {
        if (!MatchesFilter(model.label, state.filters.model.data())) continue;
        visibleModels += 1;
    }
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        visibleModels, static_cast<int>(models.size()));
    std::string modelLabel = state.ui.modelPath.empty() ? u8"(не выбран)" : std::filesystem::path(state.ui.modelPath).filename().string();
    if (ImGui::BeginCombo(u8"Модель", modelLabel.c_str())) {
        bool noneSelected = state.ui.modelPath.empty();
        if (ImGui::Selectable(u8"(не выбрана)", noneSelected)) {
            state.ui.modelPath.clear();
            std::snprintf(state.modelPathBuffer.data(), state.modelPathBuffer.size(), "%s", "");
            state.viewMeshPath.clear();
        }
        if (visibleModels == 0) {
            ImGui::TextDisabled(u8"Нет моделей по фильтру.");
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
    if (ImGui::InputText(u8"Путь к модели", state.modelPathBuffer.data(), state.modelPathBuffer.size())) {
        state.ui.modelPath = state.modelPathBuffer.data();
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Загрузить")) {
        state.viewMeshPath.clear();
    }
    ImGui::Checkbox(u8"Авто?вращение", &state.ui.modelAutoRotate);
    ImGui::SliderFloat(u8"Скорость вращения", &state.ui.modelAutoSpeed, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat(u8"Yaw", &state.ui.modelYaw, -3.14f, 3.14f, "%.2f");
    ImGui::SliderFloat(u8"Pitch", &state.ui.modelPitch, -1.57f, 1.57f, "%.2f");
    ImGui::SliderFloat(u8"Zoom", &state.ui.modelZoom, 0.3f, 3.0f, "%.2f");
    ImGui::ColorEdit4(u8"Цвет линий", &state.ui.modelColor.x);
}

void DrawView3dPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::View3D, state.storageDir);
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
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.viewMeshError.c_str());
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
        ImGui::GetWindowDrawList()->AddText(viewportPos, ImGui::GetColorU32(ImVec4(0.8f, 0.7f, 0.4f, 1.0f)),
                                            u8"Загрузите OBJ/FBX модель.");
    }
}

