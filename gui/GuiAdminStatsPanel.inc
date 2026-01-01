void DrawAdminStatsPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::AdminStats, state.storageDir);
    const std::int64_t nowSec = NowSeconds();
    bool refresh = false;
    const float headerWidth = ImGui::GetContentRegionAvail().x;
    const bool stackHeader = headerWidth < 620.0f;
    std::string updated = FormatTimestamp(state.adminStats.lastUpdated, "%Y-%m-%d %H:%M:%S");
    if (stackHeader) {
        if (ImGui::Button(u8"Обновить", ImVec2(-1.0f, 0.0f))) {
            refresh = true;
        }
        ImGui::Checkbox(u8"Авто", &state.adminStatsAutoRefresh);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt(u8"сек##admin_refresh", &state.adminStatsRefreshSeconds, 5, 120);
        ImGui::TextDisabled(u8"Обновлено: %s", updated.empty() ? u8"-" : updated.c_str());
    } else if (ImGui::BeginTable("admin_refresh", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("admin_refresh_btn", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("admin_refresh_auto", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("admin_refresh_secs", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("admin_refresh_stamp", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Обновить", ImVec2(-1.0f, 0.0f))) {
            refresh = true;
        }
        ImGui::TableNextColumn();
        ImGui::Checkbox(u8"Авто", &state.adminStatsAutoRefresh);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt(u8"сек##admin_refresh", &state.adminStatsRefreshSeconds, 5, 120);
        ImGui::TableNextColumn();
        ImGui::TextDisabled(u8"Обновлено: %s", updated.empty() ? u8"-" : updated.c_str());
        ImGui::EndTable();
    }
    ImGui::Spacing();
    DrawCardHeader(u8"Фильтры");
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
        if (ImGui::SmallButton(u8"Сброс##admin_stats_filter")) {
            resetFilters();
        }
        if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
            exportStats();
        }
    } else if (ImGui::BeginTable("admin_filters", 5, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("admin_filter_search", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("admin_filter_archive", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("admin_filter_rank", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("admin_filter_reset", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("admin_filter_export", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
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
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            resetFilters();
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
            exportStats();
        }
        ImGui::EndTable();
    }
    SyncFiltersToUi(state);

    const int refreshSeconds = std::clamp(state.adminStatsRefreshSeconds, 5, 120);
    if (refresh || state.adminStatsDirty || state.adminStats.lastUpdated == 0 ||
        (state.adminStatsAutoRefresh && nowSec - state.adminStats.lastUpdated > refreshSeconds)) {
        RefreshAdminStats(state, storage);
    }

    const auto& stats = state.adminStats;
    const auto filtered = FilterAdminProfiles(state);
    ImGui::TextDisabled(u8"Показано: %d из %d",
                        static_cast<int>(filtered.size()),
                        static_cast<int>(stats.profiles.size()));
    if (filtered.empty()) {
        ImGui::TextDisabled(u8"Нет данных для отображения.");
        SyncFiltersToUi(state);
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
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
        auto kpi = [&](const char* id, const char* title, const std::string& value, const ImVec4& accent) {
            ImGui::TableNextColumn();
            DrawKpiCard(id, title, value, accent);
        };
        kpi("kpi_total_profiles", u8"Профилей всего", std::to_string(stats.totalProfiles), accentStrong);
        kpi("kpi_active_profiles", u8"Активных", std::to_string(stats.activeProfiles), accent);
        kpi("kpi_archived_profiles", u8"В архиве", std::to_string(stats.archivedProfiles), accentSoft);

        std::ostringstream avgStream;
        avgStream.imbue(std::locale::classic());
        avgStream << std::fixed << std::setprecision(1) << stats.avgLevel;
        kpi("kpi_avg_level", u8"Средний уровень", avgStream.str(), accent);
        kpi("kpi_max_level", u8"Макс. уровень", std::to_string(stats.maxLevel), accentStrong);
        kpi("kpi_total_xp", u8"Всего XP", std::to_string(stats.totalXp), accent);

        std::ostringstream avgXpStream;
        avgXpStream.imbue(std::locale::classic());
        avgXpStream << std::fixed << std::setprecision(0) << stats.avgXp;
        kpi("kpi_avg_xp", u8"Средний XP", avgXpStream.str(), accentSoft);
        kpi("kpi_ach_total", u8"Ачивок всего", std::to_string(stats.achievementsTotal), accentSoft);
        kpi("kpi_ach_active", u8"Ачивок активных", std::to_string(stats.achievementsActive), accent);
        kpi("kpi_recovery", u8"Профилей на прогреве", std::to_string(stats.profilesWithRecovery),
            accentSoft);
        kpi("kpi_no_activity", u8"Без активности", std::to_string(stats.profilesNoActivity),
            accentSoft);
        kpi("kpi_no_ach", u8"Без ачивок", std::to_string(stats.profilesNoAchievements),
            accentSoft);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    auto drawTopTable = [&](const char* id, const char* title, const std::vector<AdminStatsProfile>& items) {
        DrawCardHeader(title);
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable(id, 4, flags, ImVec2(0.0f, topTableHeight))) {
            ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"XP", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (const auto& item : items) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string rowId = std::string("##row_") + item.id;
                if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    SelectProfileById(state, storage, catalog, item.id);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(item.id.c_str());
                ImGui::TableSetColumnIndex(1);
                std::string name = item.name + (item.archived ? u8" (архив)" : "");
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", item.level);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", item.totalXp);
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
    if (ImGui::BeginTable("top_achievements", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                             ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, topTableHeight))) {
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Активные", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(u8"Всего", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (const auto& item : byAchievements) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowId = std::string("##ach_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", item.achievementsActive);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", item.achievementsTotal);
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
        for (size_t i = 0; i < Profile::kCategoryCount; ++i) {
            if (stats.categoryCounts[i] > 0) {
                avgs[i] = static_cast<float>(static_cast<double>(stats.categoryTotals[i]) /
                                             static_cast<double>(stats.categoryCounts[i]));
            } else {
                avgs[i] = 0.0f;
            }
        }
        DrawSparkline("##avg_categories_spark", avgs.data(),
                      static_cast<int>(avgs.size()), 10.0f, accent, 56.0f);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::BeginTable("avg_categories", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(u8"Категория", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(u8"Среднее", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(u8"График", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < Profile::kCategoryCount; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[i];
                ImGui::TextUnformatted(label.c_str());
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
        if (ImGui::BeginTable("rank_dist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
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
    if (ImGui::BeginTable("inactive_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                               ImVec2(0.0f, topTableHeight))) {
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
            ImGui::TextDisabled(u8"Нет профилей старше порога.");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("-");
        }
        for (const auto& item : byInactivity) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowId = std::string("##inactive_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(2);
            const bool hasLastTask = item.lastTaskTimestamp > 0;
            std::string lastText = hasLastTask
                ? FormatTimestamp(item.lastTaskTimestamp, "%Y-%m-%d %H:%M")
                : std::string(u8"нет данных");
            if (hasLastTask) {
                ImGui::TextUnformatted(lastText.c_str());
            } else {
                ImGui::TextDisabled("%s", lastText.c_str());
            }
            ImGui::TableSetColumnIndex(3);
            if (item.lastTaskTimestamp > 0) {
                ImGui::TextUnformatted(FormatDurationShort(nowSec - item.lastTaskTimestamp).c_str());
            } else {
                ImGui::TextDisabled(u8"-");
            }
            ImGui::TableSetColumnIndex(4);
            if (item.recoveryTasksRemaining > 0) {
                ImGui::Text("%d", item.recoveryTasksRemaining);
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    DrawCardHeader(u8"Профили на прогреве");
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
    if (ImGui::BeginTable("recovery_table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                            ImVec2(0.0f, topTableHeight))) {
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(u8"Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"Осталось", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        if (byRecovery.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(u8"Нет активных профилей.");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("-");
        }
        for (const auto& item : byRecovery) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rowId = std::string("##recovery_") + item.id;
            if (ImGui::Selectable(rowId.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectProfileById(state, storage, catalog, item.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item.id.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string name = item.name + (item.archived ? u8" (архив)" : "");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", item.recoveryTasksRemaining);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

