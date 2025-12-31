void DrawLogsPanel(GuiState& state) {
    DrawCardHeader(u8"Фильтры");
    const float filterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilters = filterWidth < 720.0f;
    std::vector<std::string> logSources;
    logSources.reserve(state.appLogs.size());
    for (const auto& entry : state.appLogs) {
        if (entry.source.empty()) continue;
        if (std::find(logSources.begin(), logSources.end(), entry.source) == logSources.end()) {
            logSources.push_back(entry.source);
        }
    }
    std::sort(logSources.begin(), logSources.end());
    std::vector<const char*> logSourceLabels;
    logSourceLabels.reserve(logSources.size() + 1);
    logSourceLabels.push_back(u8"Все источники");
    for (const auto& src : logSources) logSourceLabels.push_back(src.c_str());
    int sourceIndex = 0;
    if (state.filters.logSource[0] != '\0') {
        for (size_t i = 0; i < logSources.size(); ++i) {
            if (logSources[i] == state.filters.logSource.data()) {
                sourceIndex = static_cast<int>(i + 1);
                break;
            }
        }
    }
    auto applySourceFilter = [&]() {
        if (sourceIndex <= 0) {
            state.filters.logSource.fill('\0');
        } else {
            std::snprintf(state.filters.logSource.data(), state.filters.logSource.size(), "%s",
                          logSources[static_cast<size_t>(sourceIndex - 1)].c_str());
        }
    };
    auto exportLogs = [&]() {
        int filteredCount = CountFilteredLogs(state);
        if (filteredCount == 0) {
            SetStatus(state, u8"Нет записей для экспорта.", 1.0f, 0.75f, 0.35f, 1.0f,
                      AppLogLevel::Warning, u8"Логи");
            return;
        }
        std::filesystem::path outPath;
        int exportedCount = 0;
        if (ExportAppLogs(state, outPath, exportedCount)) {
            std::string msg = u8"Экспортировано записей: ";
            msg += std::to_string(exportedCount);
            msg += u8". Файл: ";
            msg += outPath.string();
            SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Логи");
        } else {
            SetStatus(state, u8"Не удалось сохранить лог-файл.", 1.0f, 0.45f, 0.45f, 1.0f,
                      AppLogLevel::Error, u8"Логи");
        }
    };
    if (stackFilters) {
        ImGui::TextDisabled(u8"Поиск");
        ImGui::InputTextWithHint(u8"##log_filter", u8"Поиск по тексту/источнику",
                                 state.filters.logs.data(), state.filters.logs.size());
        if (ImGui::SmallButton(u8"Сброс##log_filter")) {
            state.filters.logs.fill('\0');
            state.filters.logSource.fill('\0');
        }
        ImGui::TextDisabled(u8"Источник");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##log_source", &sourceIndex, logSourceLabels.data(),
                         static_cast<int>(logSourceLabels.size()))) {
            applySourceFilter();
        }
        if (ImGui::Button(u8"Экспорт", ImVec2(-1.0f, 0.0f))) {
            exportLogs();
        }
        if (ImGui::Button(u8"Очистить", ImVec2(-1.0f, 0.0f))) {
            state.appLogs.clear();
        }
    } else if (ImGui::BeginTable("log_filters", 5, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("log_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("log_reset", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("log_source", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("log_export", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("log_clear", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled(u8"Поиск");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##log_filter", u8"Поиск по тексту/источнику",
                                 state.filters.logs.data(), state.filters.logs.size());
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            state.filters.logs.fill('\0');
            state.filters.logSource.fill('\0');
        }
        ImGui::TableNextColumn();
        ImGui::TextDisabled(u8"Источник");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##log_source", &sourceIndex, logSourceLabels.data(),
                         static_cast<int>(logSourceLabels.size()))) {
            applySourceFilter();
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Экспорт", ImVec2(-1.0f, 0.0f))) {
            exportLogs();
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Очистить", ImVec2(-1.0f, 0.0f))) {
            state.appLogs.clear();
        }
        ImGui::EndTable();
    }

    const float toggleWidth = ImGui::GetContentRegionAvail().x;
    const bool stackToggles = toggleWidth < 520.0f;
    if (stackToggles) {
        ImGui::Checkbox(u8"Инфо", &state.logShowInfo);
        ImGui::Checkbox(u8"Предупреждения", &state.logShowWarning);
        ImGui::Checkbox(u8"Ошибки", &state.logShowError);
        ImGui::Checkbox(u8"Автопрокрутка", &state.logAutoScroll);
        ImGui::Checkbox(u8"Компактно", &state.logCompactView);
    } else if (ImGui::BeginTable("log_toggles", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Инфо", &state.logShowInfo);
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Предупреждения", &state.logShowWarning);
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Ошибки", &state.logShowError);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Автопрокрутка", &state.logAutoScroll);
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Компактно", &state.logCompactView);
        ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::EndTable();
    }
    int filteredCount = CountFilteredLogs(state);
    ImGui::TextDisabled(u8"Показано: %d из %d", filteredCount, static_cast<int>(state.appLogs.size()));

    ImVec2 tableSize(0.0f, 260.0f);
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;
    if (state.logCompactView) {
        if (ImGui::BeginTable("app_logs_compact", 2, flags, tableSize)) {
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(u8"Сообщение", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& entry : state.appLogs) {
                if (!MatchesLogFilters(state, entry)) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImVec4 levelColor = LogLevelColor(entry.level);
                ImGui::TextColored(levelColor, "%s", LogLevelLabel(entry.level));
                ImGui::TableSetColumnIndex(1);
                std::string ts = FormatTimestamp(entry.timestamp, "%H:%M:%S");
                std::string line;
                if (!ts.empty()) {
                    line += ts;
                    line += " ";
                }
                if (!entry.source.empty()) {
                    line += entry.source;
                    line += ": ";
                }
                line += entry.message;
                ImGui::TextWrapped("%s", line.c_str());
            }
            if (state.logAutoScroll) {
                float maxScroll = ImGui::GetScrollMaxY();
                if (ImGui::GetScrollY() >= maxScroll - 1.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndTable();
        }
    } else {
        if (ImGui::BeginTable("app_logs", 4, flags, tableSize)) {
            ImGui::TableSetupColumn(u8"Время", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(u8"Источник", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn(u8"Сообщение", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& entry : state.appLogs) {
                if (!MatchesLogFilters(state, entry)) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string ts = FormatTimestamp(entry.timestamp, "%Y-%m-%d %H:%M:%S");
                ImGui::TextUnformatted(ts.empty() ? "-" : ts.c_str());
                ImGui::TableSetColumnIndex(1);
                ImVec4 levelColor = LogLevelColor(entry.level);
                ImGui::TextColored(levelColor, "%s", LogLevelLabel(entry.level));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(entry.source.empty() ? "-" : entry.source.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", entry.message.c_str());
            }
            if (state.logAutoScroll) {
                float maxScroll = ImGui::GetScrollMaxY();
                if (ImGui::GetScrollY() >= maxScroll - 1.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndTable();
        }
    }
    SyncFiltersToUi(state);
}

