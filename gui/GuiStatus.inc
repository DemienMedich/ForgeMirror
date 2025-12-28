void AppendAppLog(GuiState& state, AppLogLevel level, const std::string& source, const std::string& message) {
    if (message.empty()) return;
    AppLogEntry entry;
    entry.timestamp = NowSeconds();
    entry.level = level;
    entry.source = source;
    entry.message = message;
    state.appLogs.push_back(std::move(entry));
    constexpr size_t kMaxLogs = 200;
    if (state.appLogs.size() > kMaxLogs) {
        state.appLogs.pop_front();
    }
}

// Helper to show feedback banner with color-coded message.
void SetStatus(GuiState& state, const std::string& msg, float r, float g, float b, float a = 1.0f,
               std::optional<AppLogLevel> logLevel = std::nullopt, const char* source = nullptr) {
    state.statusMessage = msg;
    state.statusColor[0] = r;
    state.statusColor[1] = g;
    state.statusColor[2] = b;
    state.statusColor[3] = a;
    if (logLevel.has_value()) {
        const char* src = source ? source : "GUI";
        AppendAppLog(state, *logLevel, src, msg);
    }
}

AppLogLevel LogLevelForResult(const ActionResult& result) {
    if (result.ok) return AppLogLevel::Info;
    if (result.userError) return AppLogLevel::Warning;
    return AppLogLevel::Error;
}


void ShowStatus(const GuiState& state) {
    if (state.statusMessage.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(state.statusColor[0], state.statusColor[1], state.statusColor[2], state.statusColor[3]));
    ImGui::TextWrapped("%s", state.statusMessage.c_str());
    ImGui::PopStyleColor();
}

void DrawKpiCard(const char* id, const char* title, const std::string& value, const ImVec4& accent, float height = 56.0f) {
    ImGui::PushID(id);
    ImGui::BeginChild("kpi_card", ImVec2(0.0f, height), true);
    ImGui::TextDisabled("%s", title);
    ImGui::TextColored(accent, "%s", value.c_str());
    ImGui::EndChild();
    ImGui::PopID();
}
