static void DrawLogKpiCard(const char* id, UiIcon icon, const char* title, const std::string& value,
                           const ImVec4& accent, float height = 70.0f) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rounding = std::max(9.0f, style.FrameRounding + 3.0f);
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    if (ImGui::BeginChild("log_kpi", ImVec2(0.0f, height), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float contentH = std::max(0.0f, ImGui::GetContentRegionAvail().y);
        const float badgeSize = std::clamp(height - 24.0f, 30.0f, 42.0f);
        const float badgeRadius = badgeSize * 0.5f;
        const ImVec2 badgePos = ImGui::GetCursorScreenPos();
        const ImVec2 badgeCenter(badgePos.x + badgeRadius, badgePos.y + contentH * 0.5f);
        DrawIconBadge(drawList, icon, badgeCenter, badgeRadius, accent, true);

        ImGui::Dummy(ImVec2(badgeSize + 10.0f, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        float y = ImGui::GetCursorPosY();
        float textBlock = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        float offsetY = std::max(0.0f, (contentH - textBlock) * 0.5f);
        ImGui::SetCursorPosY(y + offsetY);
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", title);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SetWindowFontScale(1.12f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void DrawLogsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Logs, state.storageDir);
    DrawCardHeader(u8"Логи");
    ImGui::TextDisabled(u8"Фиксируют действия и ошибки. Используйте фильтры для поиска.");
    int infoCount = 0;
    int warnCount = 0;
    int errorCount = 0;
    std::int64_t minTs = 0;
    std::int64_t maxTs = 0;
    std::vector<std::string> logSources;
    logSources.reserve(state.appLogs.size());
    for (const auto& entry : state.appLogs) {
        if (entry.level == AppLogLevel::Warning) {
            warnCount += 1;
        } else if (entry.level == AppLogLevel::Error) {
            errorCount += 1;
        } else {
            infoCount += 1;
        }
        if (entry.timestamp > 0) {
            if (minTs == 0 || entry.timestamp < minTs) minTs = entry.timestamp;
            if (maxTs == 0 || entry.timestamp > maxTs) maxTs = entry.timestamp;
        }
        if (entry.source.empty()) continue;
        if (std::find(logSources.begin(), logSources.end(), entry.source) == logSources.end()) {
            logSources.push_back(entry.source);
        }
    }
    std::sort(logSources.begin(), logSources.end());

    const int totalCount = static_cast<int>(state.appLogs.size());
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 infoColor = LogLevelColor(AppLogLevel::Info);
    ImVec4 warnColor = LogLevelColor(AppLogLevel::Warning);
    ImVec4 errorColor = LogLevelColor(AppLogLevel::Error);

    if (BeginCard("log_summary_card")) {
        DrawCardHeader(u8"Сводка");
        const float summaryWidth = ImGui::GetContentRegionAvail().x;
        const int columns = summaryWidth < 540.0f ? 2 : 4;
        struct LogKpi { const char* id; UiIcon icon; const char* label; int value; ImVec4 accent; };
        const std::array<LogKpi, 4> kpis = {{
            {"total", UiIcon::List, u8"Всего", totalCount, accent},
            {"info", UiIcon::Chart, u8"Инфо", infoCount, infoColor},
            {"warn", UiIcon::Bell, u8"Предупр.", warnCount, warnColor},
            {"error", UiIcon::Exit, u8"Ошибки", errorCount, errorColor}
        }};
        if (ImGui::BeginTable("log_kpi", columns, ImGuiTableFlags_SizingStretchSame)) {
            size_t index = 0;
            const int rows = static_cast<int>((kpis.size() + columns - 1) / columns);
            for (int r = 0; r < rows; ++r) {
                ImGui::TableNextRow();
                for (int c = 0; c < columns; ++c) {
                    ImGui::TableNextColumn();
                    if (index < kpis.size()) {
                        const LogKpi& kpi = kpis[index];
                        DrawLogKpiCard(kpi.id, kpi.icon, kpi.label, std::to_string(kpi.value), kpi.accent);
                        ++index;
                    } else {
                        ImGui::Dummy(ImVec2(0.0f, 0.0f));
                    }
                }
            }
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled(u8"Активность");
        constexpr int kBins = 16;
        std::array<float, kBins> activity{};
        if (totalCount > 0) {
            if (minTs > 0 && maxTs > minTs) {
                const double span = static_cast<double>(maxTs - minTs);
                for (const auto& entry : state.appLogs) {
                    if (entry.timestamp <= 0) continue;
                    double t = static_cast<double>(entry.timestamp - minTs) / span;
                    int bin = static_cast<int>(t * (kBins - 1));
                    bin = std::clamp(bin, 0, kBins - 1);
                    activity[static_cast<size_t>(bin)] += 1.0f;
                }
            } else {
                int idx = 0;
                const int count = totalCount;
                for (const auto& entry : state.appLogs) {
                    (void)entry;
                    int bin = (count > 0) ? static_cast<int>((idx * kBins) / count) : 0;
                    bin = std::clamp(bin, 0, kBins - 1);
                    activity[static_cast<size_t>(bin)] += 1.0f;
                    ++idx;
                }
            }
        }
        float maxActivity = 0.0f;
        for (float v : activity) maxActivity = std::max(maxActivity, v);
        if (maxActivity <= 0.0f) maxActivity = 1.0f;
        DrawSparkline("##log_activity", activity.data(), kBins, maxActivity, accent, 52.0f);
        if (totalCount == 0) {
            ImGui::TextDisabled(u8"Пока нет записей.");
        }
    }
    EndCard();
    ImGui::Spacing();

    DrawCardHeader(u8"Фильтры");
    ImGui::TextDisabled(u8"Источник — модуль, который записал сообщение.");
    const float filterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilters = filterWidth < 720.0f;
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
    } else if (ImGui::BeginTable("log_filters", 6, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("log_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("log_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("log_reset", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("log_source", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("log_export", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("log_clear", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
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
    const bool hasTextFilter = state.filters.logs[0] != '\0';
    const bool hasSourceFilter = state.filters.logSource[0] != '\0';
    if (filteredCount == 0 && (hasTextFilter || hasSourceFilter)) {
        ImGui::TextDisabled(u8"По фильтрам ничего не найдено.");
        if (ImGui::SmallButton(u8"Сбросить фильтры")) {
            state.filters.logs.fill('\0');
            state.filters.logSource.fill('\0');
        }
    }

    ImVec2 tableSize(0.0f, 260.0f);
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    auto rowAccent = [](AppLogLevel level) {
        ImVec4 row = LogLevelColor(level);
        row.w = std::clamp(row.w * 0.14f, 0.04f, 0.12f);
        return ImGui::GetColorU32(row);
    };
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;
    if (state.logCompactView) {
        if (ImGui::BeginTable("app_logs_compact", 3, flags, tableSize)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(u8"Сообщение", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int rowIndex = 0;
            for (const auto& entry : state.appLogs) {
                if (!MatchesLogFilters(state, entry)) continue;
                if (entry.level != AppLogLevel::Info) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowAccent(entry.level));
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", rowIndex + 1);
                ImGui::TableSetColumnIndex(1);
                ImVec4 levelColor = LogLevelColor(entry.level);
                ImGui::TextColored(levelColor, "%s", LogLevelLabel(entry.level));
                ImGui::TableSetColumnIndex(2);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
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
                ++rowIndex;
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
        if (ImGui::BeginTable("app_logs", 5, flags, tableSize)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn(u8"Время", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(u8"Источник", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(u8"Сообщение", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int rowIndex = 0;
            for (const auto& entry : state.appLogs) {
                if (!MatchesLogFilters(state, entry)) continue;
                if (entry.level != AppLogLevel::Info) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowAccent(entry.level));
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", rowIndex + 1);
                ImGui::TableSetColumnIndex(1);
                std::string ts = FormatTimestamp(entry.timestamp, "%Y-%m-%d %H:%M:%S");
                if (ts.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::TextUnformatted(ts.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                ImVec4 levelColor = LogLevelColor(entry.level);
                ImGui::TextColored(levelColor, "%s", LogLevelLabel(entry.level));
                ImGui::TableSetColumnIndex(3);
                if (entry.source.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::TextUnformatted(entry.source.c_str());
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextWrapped("%s", entry.message.c_str());
                ++rowIndex;
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
    if (filteredCount == 0) {
        if (totalCount == 0) {
            ImGui::TextDisabled(u8"Пока нет записей. Они появятся после действий в приложении.");
        } else {
            ImGui::TextDisabled(u8"Нет записей по фильтру.");
        }
    }
    SyncFiltersToUi(state);
}

