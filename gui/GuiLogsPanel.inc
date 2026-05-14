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

static bool LaunchLogPath(const std::filesystem::path& path) {
    if (path.empty()) return false;
#if defined(_WIN32)
    std::filesystem::path full(path);
    std::error_code ec;
    if (!full.is_absolute()) {
        full = std::filesystem::absolute(full, ec);
    }
    return ShellExecuteA(nullptr, "open", full.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > (HINSTANCE)32;
#else
    return false;
#endif
}

void DrawLogsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Logs, state.storageDir);
    DrawCardHeader(u8"Логи");
    ImGui::TextDisabled(u8"Фиксируют действия и ошибки. Используйте фильтры для поиска.");
    auto LogIconButton = [](const char* id, UiIcon icon, const char* tooltip,
                            bool enabled = true, bool selected = false) -> bool {
        ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        return IconButtonGui(id, icon, tooltip, accent, ImGui::GetFrameHeight(), enabled, selected);
    };
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
    auto exportTaskAudit = [&]() {
        if (state.taskAudit.empty()) {
            SetStatus(state, u8"Журнал задач пуст.", 1.0f, 0.75f, 0.35f, 1.0f,
                      AppLogLevel::Warning, u8"Логи");
            return;
        }
        std::error_code ec;
        auto reportDir = state.storageDir / "meta" / "reports";
        std::filesystem::create_directories(reportDir, ec);
        if (ec) {
            SetStatus(state, u8"Не удалось создать папку отчетов.", 1.0f, 0.45f, 0.45f, 1.0f,
                      AppLogLevel::Error, u8"Логи");
            return;
        }
        std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
        if (stamp.empty()) stamp = std::to_string(NowSeconds());
        auto outPath = reportDir / ("task-audit-" + stamp + ".txt");
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            SetStatus(state, u8"Не удалось сохранить журнал задач.", 1.0f, 0.45f, 0.45f, 1.0f,
                      AppLogLevel::Error, u8"Логи");
            return;
        }
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        out << "Журнал изменений задач\n";
        for (const auto& row : state.taskAudit) {
            const std::string ts = row.timestamp > 0
                ? FormatTimestamp(row.timestamp, "%Y-%m-%d %H:%M:%S")
                : std::string("-");
            out << ts << " | "
                << SanitizeLogLine(row.actor.empty() ? std::string("system") : row.actor) << " | "
                << SanitizeLogLine(row.taskId) << " | "
                << SanitizeLogLine(row.field) << " | "
                << SanitizeLogLine(row.oldValue) << " -> "
                << SanitizeLogLine(row.newValue) << "\n";
        }
        std::string msg = u8"Экспортирован журнал задач: ";
        msg += outPath.string();
        SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Логи");
    };
    auto setLevelPreset = [&](bool showInfo, bool showWarn, bool showError) {
        state.logShowInfo = showInfo;
        state.logShowWarning = showWarn;
        state.logShowError = showError;
    };
    if (stackFilters) {
        ImGui::TextDisabled(u8"Поиск");
        ImGui::InputTextWithHint(u8"##log_filter", u8"Поиск по тексту/источнику",
                                 state.filters.logs.data(), state.filters.logs.size());
        if (LogIconButton("log_filter_reset_stack", UiIcon::Clear, u8"Сбросить поиск и источник")) {
            state.filters.logs.fill('\0');
            state.filters.logSource.fill('\0');
        }
        ImGui::TextDisabled(u8"Источник");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##log_source", &sourceIndex, logSourceLabels.data(),
                         static_cast<int>(logSourceLabels.size()))) {
            applySourceFilter();
        }
        ImGui::TextDisabled(u8"Уровни");
        if (ImGui::SmallButton(u8"Все")) {
            setLevelPreset(true, true, true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Предупр+Ошибки")) {
            setLevelPreset(false, true, true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Только ошибки")) {
            setLevelPreset(false, false, true);
        }
        if (LogIconButton("log_export_stack", UiIcon::Export, u8"Экспортировать отфильтрованные логи")) {
            exportLogs();
        }
        ImGui::SameLine();
        if (LogIconButton("log_clear_stack", UiIcon::Close, u8"Очистить текущий журнал логов")) {
            state.appLogs.clear();
            SetStatus(state, u8"Логи очищены.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Логи");
        }
    } else {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("log_filters", 6, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("log_idx", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("log_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("log_reset", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
            ImGui::TableSetupColumn("log_source", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("log_export", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
            ImGui::TableSetupColumn("log_clear", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
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
            if (LogIconButton("log_filter_reset", UiIcon::Clear, u8"Сбросить поиск и источник")) {
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
            ImGui::TextDisabled(u8"Уровни");
            if (ImGui::SmallButton(u8"Все##log_levels_all")) {
                setLevelPreset(true, true, true);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Предупр+Ошиб##log_levels_warn")) {
                setLevelPreset(false, true, true);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Ошибки##log_levels_err")) {
                setLevelPreset(false, false, true);
            }
            ImGui::TableNextColumn();
            if (LogIconButton("log_export", UiIcon::Export, u8"Экспортировать отфильтрованные логи")) {
                exportLogs();
            }
            ImGui::TableNextColumn();
            if (LogIconButton("log_clear", UiIcon::Close, u8"Очистить текущий журнал логов")) {
                state.appLogs.clear();
                SetStatus(state, u8"Логи очищены.", 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Логи");
            }
            ImGui::EndTable();
        }
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
        if (DrawEmptyStateGui("logs_filter_empty_top", UiIcon::Search,
                              u8"По фильтрам ничего не найдено",
                              u8"Сбросьте поиск или источник, чтобы вернуть полный журнал.",
                              UiIcon::Refresh, u8"Сбросить фильтры логов")) {
            state.filters.logs.fill('\0');
            state.filters.logSource.fill('\0');
        }
    }

    std::unordered_map<std::string, int> sourceCounts;
    sourceCounts.reserve(logSources.size());
    int topSourceCount = 0;
    std::string topSource;
    if (filteredCount > 0) {
        for (const auto& entry : state.appLogs) {
            if (!MatchesLogFilters(state, entry)) continue;
            if (entry.source.empty()) continue;
            int count = ++sourceCounts[entry.source];
            if (count > topSourceCount) {
                topSourceCount = count;
                topSource = entry.source;
            }
        }
    }
    const bool hasFocusSource = !topSource.empty() && topSourceCount >= 3;

    if (hasFocusSource) {
        ImGui::TextDisabled(u8"Зона внимания");
        ImGui::SameLine();
        ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s (%d)", topSource.c_str(), topSourceCount);
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
    auto focusAccent = [&]() {
        ImVec4 row = LogLevelColor(AppLogLevel::Warning);
        row.w = std::clamp(row.w * 0.12f, 0.04f, 0.12f);
        return ImGui::GetColorU32(row);
    };
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
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
                } else if (hasFocusSource && !entry.source.empty() && entry.source == topSource) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, focusAccent());
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
                } else if (hasFocusSource && !entry.source.empty() && entry.source == topSource) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, focusAccent());
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
                    if (hasFocusSource && entry.source == topSource) {
                        ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
                        warnText.w = 0.95f;
                        ImGui::TextColored(warnText, "%s", entry.source.c_str());
                    } else {
                        ImGui::TextUnformatted(entry.source.c_str());
                    }
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
            DrawEmptyStateGui("logs_empty_all", UiIcon::List,
                              u8"Журнал пока пуст",
                              u8"Записи появятся после действий, ошибок и системных событий приложения.");
        } else {
            if (DrawEmptyStateGui("logs_empty_filtered", UiIcon::Search,
                                  u8"Нет записей по фильтру",
                                  u8"Измените поисковый запрос, источник или уровни логов.",
                                  UiIcon::Refresh, u8"Сбросить фильтры логов")) {
                state.filters.logs.fill('\0');
                state.filters.logSource.fill('\0');
                state.logShowInfo = true;
                state.logShowWarning = true;
                state.logShowError = true;
            }
        }
    }

    ImGui::Spacing();
    if (BeginCard("log_task_audit_card")) {
        DrawCardHeader(u8"Аудит задач");
        ImGui::TextDisabled(u8"Изменения задач из meta/task-audit.log");
        ImGui::SameLine();
        if (LogIconButton("task_audit_export", UiIcon::Export, u8"Экспортировать аудит задач")) {
            exportTaskAudit();
        }
        ImGui::SameLine();
        if (LogIconButton("task_audit_open", UiIcon::FileText, u8"Открыть файл аудита задач")) {
            const auto auditPath = TaskAuditStoragePath(state.storageDir);
            std::error_code ec;
            if (!std::filesystem::exists(auditPath, ec)) {
                SetStatus(state, u8"Файл аудита задач не найден.", 1.0f, 0.75f, 0.35f, 1.0f,
                          AppLogLevel::Warning, u8"Логи");
            } else if (!LaunchLogPath(auditPath)) {
                SetStatus(state, u8"Не удалось открыть файл аудита задач.", 1.0f, 0.45f, 0.45f, 1.0f,
                          AppLogLevel::Error, u8"Логи");
            }
        }

        static std::array<char, 64> taskAuditActorFilter{};
        static std::array<char, 96> taskAuditTaskFilter{};
        static std::array<char, 48> taskAuditFieldFilter{};

        {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("log_task_audit_filters", 4, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("audit_filter_actor", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("audit_filter_task", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("audit_filter_field", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("audit_filter_reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 14.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::InputTextWithHint(u8"##task_audit_actor", u8"Актор", taskAuditActorFilter.data(), taskAuditActorFilter.size());
            ImGui::TableSetColumnIndex(1);
            ImGui::InputTextWithHint(u8"##task_audit_task", u8"Задача/ID", taskAuditTaskFilter.data(), taskAuditTaskFilter.size());
            ImGui::TableSetColumnIndex(2);
            ImGui::InputTextWithHint(u8"##task_audit_field", u8"Поле", taskAuditFieldFilter.data(), taskAuditFieldFilter.size());
            ImGui::TableSetColumnIndex(3);
            if (LogIconButton("task_audit_reset", UiIcon::Clear, u8"Сбросить фильтры аудита задач")) {
                taskAuditActorFilter.fill('\0');
                taskAuditTaskFilter.fill('\0');
                taskAuditFieldFilter.fill('\0');
            }
            ImGui::EndTable();
            }
        }

        auto matchesTaskAudit = [&](const TaskAuditEntry& row) {
            const std::string actor = row.actor.empty() ? std::string("system") : row.actor;
            std::string taskText = row.taskId;
            if (!row.oldValue.empty()) {
                taskText += " ";
                taskText += row.oldValue;
            }
            if (!row.newValue.empty()) {
                taskText += " ";
                taskText += row.newValue;
            }
            if (!MatchesFilter(actor, taskAuditActorFilter.data())) return false;
            if (!MatchesFilter(taskText, taskAuditTaskFilter.data())) return false;
            if (!MatchesFilter(row.field, taskAuditFieldFilter.data())) return false;
            return true;
        };

        if (state.taskAudit.empty()) {
            DrawEmptyStateGui("task_audit_empty_all", UiIcon::List,
                              u8"Аудит задач пуст",
                              u8"Изменения задач появятся здесь после создания, правок и закрытия задач.");
        } else {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("log_task_audit_table", 5,
                                  ControlTableFlagsGui(ImGuiTableFlags_RowBg),
                                  ImVec2(0.0f, 180.0f))) {
            ImGui::TableSetupColumn(u8"Время", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(u8"Актор", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(u8"Поле", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"Изменение", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            int shown = 0;
            constexpr int kPreview = 20;
            for (auto it = state.taskAudit.rbegin(); it != state.taskAudit.rend() && shown < kPreview; ++it) {
                const TaskAuditEntry& row = *it;
                if (!matchesTaskAudit(row)) continue;
                ++shown;
                const std::string ts = row.timestamp > 0
                    ? FormatTimestamp(row.timestamp, "%Y-%m-%d %H:%M:%S")
                    : std::string("-");
                const std::string actor = row.actor.empty() ? std::string("system") : row.actor;
                std::string change = row.oldValue;
                change += " -> ";
                change += row.newValue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(ts.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(actor.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.taskId.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.field.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextWrapped("%s", change.c_str());
            }
            ImGui::EndTable();
            ImGui::TextDisabled(u8"Показано: %d из %d", shown, static_cast<int>(state.taskAudit.size()));
            if (shown == 0) {
                DrawEmptyStateGui("task_audit_empty_filtered", UiIcon::Search,
                                  u8"По фильтрам аудита ничего не найдено",
                                  u8"Очистите фильтры актора, задачи или поля, чтобы вернуть записи.");
            }
            }
        }
    }
    EndCard();

    SyncFiltersToUi(state);
}

