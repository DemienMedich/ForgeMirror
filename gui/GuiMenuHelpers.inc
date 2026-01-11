static bool ToggleMenuButtonGui(const char* label, bool& value, ImVec2 size) {
    bool toggled = false;
    if (value) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        ImVec4 base = BlendColor(style.Colors[ImGuiCol_ButtonActive], accent, 0.35f);
        ImVec4 hover = BlendColor(style.Colors[ImGuiCol_ButtonHovered], accent, 0.30f);
        ImVec4 active = BlendColor(base, accent, 0.20f);
        base.w = std::max(base.w, 0.85f);
        hover.w = std::max(hover.w, 0.90f);
        active.w = std::max(active.w, 0.95f);
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    }
    if (ImGui::Button(label, size)) {
        value = !value;
        toggled = true;
    }
    if (value) {
        ImGui::PopStyleColor(3);
    }
    return toggled;
}


static void OpenWorkspaceTab(GuiState& state, bool& flag, UiWindowId windowId) {
    flag = true;
    state.workspaceTabRequest = static_cast<int>(windowId);
}

