static bool ToggleMenuButtonGui(const char* label, bool& value, ImVec2 size) {
    bool toggled = false;
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.36f, 0.55f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.44f, 0.68f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.34f, 0.52f, 1.0f));
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

