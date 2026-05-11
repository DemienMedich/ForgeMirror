static void DrawAdminKpiCardIcon(const char* id, UiIcon icon, const char* title, const std::string& value,
                                 const ImVec4& accent, float height = 72.0f) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rounding = std::max(9.0f, style.FrameRounding + 3.0f);
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    if (ImGui::BeginChild("admin_kpi", ImVec2(0.0f, height), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float contentH = std::max(0.0f, ImGui::GetContentRegionAvail().y);
        const float badgeSize = std::clamp(height - 26.0f, 32.0f, 44.0f);
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

void DrawAdminStatsPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::AdminStats, state.storageDir);
    DrawCardHeader(u8"Статистика");
    ImGui::TextDisabled(u8"Сводка по профилям. Клик по строке открывает профиль.");
    auto StatsIconButton = [](const char* id, UiIcon icon, const char* tooltip,
                              bool enabled = true, bool selected = false) -> bool {
        ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        return IconButtonGui(id, icon, tooltip, accent, ImGui::GetFrameHeight(), enabled, selected);
    };
    const std::int64_t nowSec = NowSeconds();
    bool refresh = false;
    const float headerWidth = ImGui::GetContentRegionAvail().x;
    const bool stackHeader = headerWidth < 620.0f;
    std::string updated = FormatTimestamp(state.adminStats.lastUpdated, "%Y-%m-%d %H:%M:%S");
    if (stackHeader) {
        if (StatsIconButton("admin_refresh_stack", UiIcon::Refresh, u8"Обновить статистику")) {
            refresh = true;
        }
        ImGui::Checkbox(u8"Авто", &state.adminStatsAutoRefresh);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt(u8"сек##admin_refresh", &state.adminStatsRefreshSeconds, 5, 120);
        ImGui::TextDisabled(u8"Обновлено: %s", updated.empty() ? u8"-" : updated.c_str());
    } else if (ImGui::BeginTable("admin_refresh", 5, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("admin_refresh_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("admin_refresh_btn", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
        ImGui::TableSetupColumn("admin_refresh_auto", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("admin_refresh_secs", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("admin_refresh_stamp", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableNextColumn();
        if (StatsIconButton("admin_refresh", UiIcon::Refresh, u8"Обновить статистику")) {
            refresh = true;
        }
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Авто", &state.adminStatsAutoRefresh);
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt(u8"сек##admin_refresh", &state.adminStatsRefreshSeconds, 5, 120);
        ImGui::TableNextColumn();
        ImGui::TextDisabled(u8"Обновлено: %s", updated.empty() ? u8"-" : updated.c_str());
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Фильтры");
    ImGui::TextDisabled(u8"Архив — профили, скрытые из основного списка.");
    const float filtersWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilters = filtersWidth < 740.0f;
    const auto& rankOptions = RankOptions();
    std::vector<const char*> rankLabels;
    rankLabels.reserve(rankOptions.size() + 1);
    rankLabels.push_back(u8"Все ранги");
    for (const auto& opt : rankOptions) rankLabels.push_back(opt.label);
    auto resetFilters = [&]() {
        state.filters.adminStats.fill('\0');
        state.adminStatsIncludeArchived = true;
        state.adminStatsRankFilter = 0;
    };
    auto exportStats = [&]() {
        std::filesystem::path outPath;
        if (ExportAdminStatsCsv(state, outPath)) {
            std::string msg = u8"Экспортировано: ";
            msg += outPath.string();
            SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Статистика");
        } else {
            SetStatus(state, u8"Нет данных для экспорта.", 1.0f, 0.75f, 0.35f, 1.0f,
                      AppLogLevel::Warning, u8"Статистика");
        }
    };
    if (stackFilters) {
        ImGui::TextDisabled(u8"Поиск");
        ImGui::InputTextWithHint(u8"##admin_stats_filter", u8"Фильтр по ID/имени",
                                 state.filters.adminStats.data(), state.filters.adminStats.size());
        ImGui::Checkbox(u8"Архив", &state.adminStatsIncludeArchived);
        ImGui::TextDisabled(u8"Ранг");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"##admin_stats_rank", &state.adminStatsRankFilter,
                     rankLabels.data(), static_cast<int>(rankLabels.size()));
        if (StatsIconButton("admin_stats_reset_stack", UiIcon::Refresh, u8"Сбросить фильтры статистики")) {
            resetFilters();
        }
        ImGui::SameLine();
        if (StatsIconButton("admin_stats_export_stack", UiIcon::Launch, u8"Экспортировать статистику в CSV")) {
            exportStats();
        }
    } else if (ImGui::BeginTable("admin_filters", 6, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("admin_filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("admin_filter_search", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("admin_filter_archive", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("admin_filter_rank", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("admin_filter_reset", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
        ImGui::TableSetupColumn("admin_filter_export", ImGuiTableColumnFlags_WidthFixed,
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
        ImGui::InputTextWithHint(u8"##admin_stats_filter", u8"Фильтр по ID/имени",
                                 state.filters.adminStats.data(), state.filters.adminStats.size());
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Архив", &state.adminStatsIncludeArchived);
        ImGui::TableNextColumn();
        ImGui::TextDisabled(u8"Ранг");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"##admin_stats_rank", &state.adminStatsRankFilter,
                     rankLabels.data(), static_cast<int>(rankLabels.size()));
        ImGui::TableNextColumn();
        if (StatsIconButton("admin_stats_reset", UiIcon::Refresh, u8"Сбросить фильтры статистики")) {
            resetFilters();
        }
        ImGui::TableNextColumn();
        if (StatsIconButton("admin_stats_export", UiIcon::Launch, u8"Экспортировать статистику в CSV")) {
            exportStats();
        }
        ImGui::EndTable();
    }
    SyncFiltersToUi(state);

    const int refreshSeconds = std::clamp(state.adminStatsRefreshSeconds, 5, 120);
    if (refresh | state.adminStatsDirty | state.adminStats.lastUpdated == 0 |
        (state.adminStatsAutoRefresh && nowSec - state.adminStats.lastUpdated > refreshSeconds)) {
        RefreshAdminStats(state, storage);
    }

    const auto& stats = state.adminStats;
    const auto filtered = FilterAdminProfiles(state);
    const bool hasFilters = state.filters.adminStats[0] != '\0' |
                            !state.adminStatsIncludeArchived |
                            state.adminStatsRankFilter != 0;
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        static_cast<int>(filtered.size()),
                        static_cast<int>(stats.profiles.size()));
    if (filtered.empty()) {
        if (stats.profiles.empty()) {
            DrawEmptyStateGui("admin_stats_empty_all", UiIcon::User,
                              u8"Нет профилей для статистики",
                              u8"Создайте профиль или импортируйте данные, чтобы увидеть сводку.");
        } else {
            DrawEmptyStateGui("admin_stats_empty_filtered", UiIcon::Search,
                              u8"Нет данных по фильтрам",
                              u8"Сбросьте поиск, ранг или режим архива, чтобы вернуть список.");
        }
        if (hasFilters && ImGui::SmallButton(u8"Сбросить фильтры")) {
            resetFilters();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Обновить")) {
            RefreshAdminStats(state, storage);
        }
        SyncFiltersToUi(state);
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
    const ImGuiStyle& tableStyle = ImGui::GetStyle();
    ImVec4 keyBg = tableStyle.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    ImVec4 topRowBg = tableStyle.Colors[ImGuiCol_CheckMark];
    topRowBg.w = std::clamp(topRowBg.w * 0.12f, 0.04f, 0.12f);
    const ImU32 topRowBgU32 = ImGui::GetColorU32(topRowBg);
    const float topTableHeight = 180.0f;
    DrawCardHeader(u8"Сводка");
    if (ImGui::BeginTable("admin_kpi", 3, ImGuiTableFlags_SizingStretchProp)) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        ImVec4 accentStrong = BlendColor(accent, style.Colors[ImGuiCol_Text], 0.20f);
        accentStrong.w = 1.0f;
        ImVec4 accentSoft = BlendColor(accent, style.Colors[ImGuiCol_TextDisabled], 0.35f);
        accentSoft.w = 0.9f;
        auto kpi = [&](const char* id, UiIcon icon, const char* title, const std::string& value, const ImVec4& accent) {
            ImGui::TableNextColumn();
            DrawAdminKpiCardIcon(id, icon, title, value, accent);
        };
        kpi("kpi_total_profiles", UiIcon::Users, u8"Профилей всего", std::to_string(stats.totalProfiles), accentStrong);
        kpi("kpi_active_profiles", UiIcon::User, u8"Активных", std::to_string(stats.activeProfiles), accent);
        kpi("kpi_archived_profiles", UiIcon::LockClosed, u8"В архиве", std::to_string(stats.archivedProfiles), accentSoft);

        std::ostringstream avgStream;
        avgStream.imbue(std::locale::classic());
        avgStream << std::fixed << std::setprecision(1) << stats.avgLevel;
        kpi("kpi_avg_level", UiIcon::Chart, u8"Средний уровень", avgStream.str(), accent);
        kpi("kpi_max_level", UiIcon::Chart, u8"Макс. уровень", std::to_string(stats.maxLevel), accentStrong);
        kpi("kpi_total_xp", UiIcon::Chart, u8"Всего XP", std::to_string(stats.totalXp), accent);

        std::ostringstream avgXpStream;
        avgXpStream.imbue(std::locale::classic());
        avgXpStream << std::fixed << std::setprecision(0) << stats.avgXp;
        kpi("kpi_avg_xp", UiIcon::Chart, u8"Средний XP", avgXpStream.str(), accentSoft);
        kpi("kpi_ach_total", UiIcon::Bell, u8"Ачивок всего", std::to_string(stats.achievementsTotal), accentSoft);
        kpi("kpi_ach_active", UiIcon::Bell, u8"Ачивок активных", std::to_string(stats.achievementsActive), accent);
        kpi("kpi_recovery", UiIcon::Refresh, u8"Профилей на прогреве", std::to_string(stats.profilesWithRecovery),
            accentSoft);
        kpi("kpi_no_activity", UiIcon::Search, u8"Без активности", std::to_string(stats.profilesNoActivity),
            accentSoft);
        kpi("kpi_no_ach", UiIcon::List, u8"Без ачивок", std::to_string(stats.profilesNoAchievements),
            accentSoft);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    auto drawTopTable = [&](const char* id, const char* title, const std::vector<AdminStatsProfile>& items) {
        DrawCardHeader(title);
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable(id, 5, flags, ImVec2(0.0f, topTableHeight))) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"XP", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            int rowRank = 0;
            for (const auto& item : items) {
                ImGui::TableNextRow();
                if (rowRank < 3) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
                }
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", rowRank + 1);
                ImGui::TableSetColumnIndex(1);
                const std::string rowId = std::string("##row_") + item.id;
                if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    SelectProfileById(state, storage, catalog, item.id);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(item.id.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                std::string name = item.name + (item.archived ? u8" (архив)" : "");
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", item.level);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", item.totalXp);
                ++rowRank;
            }
            ImGui::EndTable();
        }
    };

    std::vector<AdminStatsProfile> byLevel = filtered;
    std::sort(byLevel.begin(), byLevel.end(), [](const auto& a, const auto& b) {
        if (a.level != b.level) return a.level > b.level;
        return a.totalXp > b.totalXp;
    });
    if (byLevel.size() > 6) byLevel.resize(6);

    std::vector<AdminStatsProfile> byXp = filtered;
    std::sort(byXp.begin(), byXp.end(), [](const auto& a, const auto& b) {
        if (a.totalXp != b.totalXp) return a.totalXp > b.totalXp;
        return a.level > b.level;
    });
    if (byXp.size() > 6) byXp.resize(6);

    drawTopTable("top_level", u8"Топ по уровню", byLevel);
    drawTopTable("top_xp", u8"Топ по XP", byXp);

    std::vector<AdminStatsProfile> byAchievements = filtered;
    std::sort(byAchievements.begin(), byAchievements.end(), [](const auto& a, const auto& b) {
        if (a.achievementsActive != b.achievementsActive) return a.achievementsActive > b.achievementsActive;
        if (a.achievementsTotal != b.achievementsTotal) return a.achievementsTotal > b.achievementsTotal;
        return a.level > b.level;
    });
    if (byAchievements.size() > 6) byAchievements.resize(6);
    DrawCardHeader(u8"Топ по ачивкам");
    if (ImGui::BeginTable("top_achievements", 5, ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                             ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, topTableHeight))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Активные", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(u8"Всего", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        int rowRank = 0;
        for (const auto& item : byAchievements) {
            ImGui::TableNextRow();
            if (rowRank < 3) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", rowRank + 1);
            ImGui::TableSetColumnIndex(1);
            const std::string rowId = std::string("##ach_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", item.achievementsActive);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", item.achievementsTotal);
            ++rowRank;
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    DrawCardHeader(u8"Средние категории");
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        std::array<float, Profile::kCategoryCount> avgs{};
        int minIdx = -1;
        float minAvg = 0.0f;
        float maxAvg = 0.0f;
        int activeCategories = 0;
        for (size_t i = 0; i < Profile::kCategoryCount; ++i) {
            if (stats.categoryCounts[i] > 0) {
                avgs[i] = static_cast<float>(static_cast<double>(stats.categoryTotals[i]) /
                                             static_cast<double>(stats.categoryCounts[i]));
                activeCategories += 1;
                if (minIdx == -1 | avgs[i] < minAvg) {
                    minIdx = static_cast<int>(i);
                    minAvg = avgs[i];
                }
                maxAvg = std::max(maxAvg, avgs[i]);
            } else {
                avgs[i] = 0.0f;
            }
        }
        const bool hasAttention = (activeCategories >= 2) && (maxAvg - minAvg >= 1.0f) && (minIdx >= 0);
        DrawSparkline("##avg_categories_spark", avgs.data(),
                      static_cast<int>(avgs.size()), 10.0f, accent, 56.0f);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::BeginTable("avg_categories", 3, ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(u8"Категория", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(u8"Среднее", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"График", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < Profile::kCategoryCount; ++i) {
                ImGui::TableNextRow();
                if (hasAttention && static_cast<int>(i) == minIdx) {
                    ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                    rowHighlight.w = std::clamp(rowHighlight.w * 0.16f, 0.08f, 0.22f);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
                }
                ImGui::TableSetColumnIndex(0);
                std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[i];
                if (hasAttention && static_cast<int>(i) == minIdx) {
                    ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
                    warnText.w = 0.95f;
                    ImGui::TextColored(warnText, "%s", label.c_str());
                } else {
                    ImGui::TextUnformatted(label.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                if (stats.categoryCounts[i] > 0) {
                    ImGui::Text("%.1f/10", avgs[i]);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::PushID(static_cast<int>(i));
                DrawMiniBar("##avg_cat_bar", avgs[i] / 10.0f,
                            ChartPaletteColor(static_cast<int>(i), static_cast<int>(Profile::kCategoryCount), 0.92f),
                            8.0f);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (hasAttention && minIdx >= 0) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"Зона внимания");
            ImGui::SameLine();
            ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s (%.1f/10)",
                               Profile::kCategoryLabels[minIdx], minAvg);
        }
    }

    ImGui::Spacing();
    DrawCardHeader(u8"Распределение по рангам");
    const auto& opts = RankOptions();
    std::vector<int> rankCounts(opts.size(), 0);
    for (const auto& item : filtered) {
        int idx = RankIndexForLevel(item.level);
        if (idx >= 0 && idx < static_cast<int>(rankCounts.size())) {
            rankCounts[idx] += 1;
        }
    }
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        const int total = std::max(1, static_cast<int>(filtered.size()));
        if (ImGui::BeginTable("rank_dist", 3, ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(u8"Ранг", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"Профилей", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"Доля", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < opts.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(opts[i].label);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", rankCounts[i]);
                ImGui::TableSetColumnIndex(2);
                const float share = static_cast<float>(rankCounts[i]) / static_cast<float>(total);
                ImGui::PushID(static_cast<int>(i));
                DrawMiniBar("##rank_share", share, ChartPaletteColor(static_cast<int>(i), static_cast<int>(opts.size()), 0.92f), 8.0f);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    DrawCardHeader(u8"Неактивные профили");
    int thresholdDays = std::clamp(state.inactivityThresholdDays, 1, 365);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt(u8"Порог (дней)", &thresholdDays, 1, 365);
    state.inactivityThresholdDays = thresholdDays;
    SyncFiltersToUi(state);
    const std::int64_t thresholdSec = static_cast<std::int64_t>(thresholdDays) * 24 * 3600;
    std::vector<AdminStatsProfile> byInactivity;
    byInactivity.reserve(filtered.size());
    for (const auto& item : filtered) {
        if (item.lastTaskTimestamp == 0) {
            byInactivity.push_back(item);
        } else if ((nowSec - item.lastTaskTimestamp) >= thresholdSec) {
            byInactivity.push_back(item);
        }
    }
    std::sort(byInactivity.begin(), byInactivity.end(), [](const auto& a, const auto& b) {
        const std::int64_t ta = a.lastTaskTimestamp;
        const std::int64_t tb = b.lastTaskTimestamp;
        if (ta == tb) return a.totalXp > b.totalXp;
        if (ta == 0) return true;
        if (tb == 0) return false;
        return ta < tb;
    });
    const int inactiveTotal = static_cast<int>(byInactivity.size());
    if (byInactivity.size() > 6) byInactivity.resize(6);
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        static_cast<int>(byInactivity.size()), inactiveTotal);
    if (ImGui::BeginTable("inactive_table", 6, ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                               ImVec2(0.0f, topTableHeight))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Последняя активность", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn(u8"Прошло", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Прогрев", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        if (byInactivity.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Нет профилей старше порога.");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(5);
            ImGui::TextDisabled("-");
        }
        int rowRank = 0;
        for (const auto& item : byInactivity) {
            ImGui::TableNextRow();
            if (rowRank < 3) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", rowRank + 1);
            ImGui::TableSetColumnIndex(1);
            const std::string rowId = std::string("##inactive_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(3);
            const bool hasLastTask = item.lastTaskTimestamp > 0;
            std::string lastText = hasLastTask
                ? FormatTimestamp(item.lastTaskTimestamp, "%Y-%m-%d %H:%M")
                : std::string(u8"нет данных");
            if (hasLastTask) {
                ImGui::TextUnformatted(lastText.c_str());
            } else {
                ImGui::TextDisabled("%s", lastText.c_str());
            }
            ImGui::TableSetColumnIndex(4);
            if (item.lastTaskTimestamp > 0) {
                ImGui::TextUnformatted(FormatDurationShort(nowSec - item.lastTaskTimestamp).c_str());
            } else {
                ImGui::TextDisabled(u8"-");
            }
            ImGui::TableSetColumnIndex(5);
            if (item.recoveryTasksRemaining > 0) {
                ImGui::Text("%d", item.recoveryTasksRemaining);
            } else {
                ImGui::TextDisabled("-");
            }
            ++rowRank;
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    DrawCardHeader(u8"Профили на прогреве");
    ImGui::TextDisabled(u8"Прогрев: временный штраф после длительного простоя.");
    std::vector<AdminStatsProfile> byRecovery;
    byRecovery.reserve(filtered.size());
    for (const auto& item : filtered) {
        if (item.recoveryTasksRemaining > 0) {
            byRecovery.push_back(item);
        }
    }
    std::sort(byRecovery.begin(), byRecovery.end(), [](const auto& a, const auto& b) {
        if (a.recoveryTasksRemaining != b.recoveryTasksRemaining) {
            return a.recoveryTasksRemaining > b.recoveryTasksRemaining;
        }
        return a.totalXp > b.totalXp;
    });
    const int recoveryTotal = static_cast<int>(byRecovery.size());
    if (byRecovery.size() > 6) byRecovery.resize(6);
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        static_cast<int>(byRecovery.size()), recoveryTotal);
    if (ImGui::BeginTable("recovery_table", 4, ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                            ImVec2(0.0f, topTableHeight))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Осталось", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        if (byRecovery.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Нет активных профилей.");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("-");
        }
        int rowRank = 0;
        for (const auto& item : byRecovery) {
            ImGui::TableNextRow();
            if (rowRank < 3) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", rowRank + 1);
            ImGui::TableSetColumnIndex(1);
            const std::string rowId = std::string("##recovery_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", item.recoveryTasksRemaining);
            ++rowRank;
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

