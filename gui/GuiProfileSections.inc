static void DrawProfileOverviewSection(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, Profile& profile, int overallLevel, std::int64_t nowSec) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 accentSoft = accent;
    accentSoft.w = 0.75f;
    const bool focusMode = (state.ui.profileViewMode == 2);
    RankSpan span = ComputeRankSpan(overallLevel);
    const int levelsLeft = (span.hasNext && span.nextLevel > span.currentLevel)
        ? (span.nextLevel - overallLevel)
        : 0;
    if (BeginCard("profile_state_card")) {
        DrawCardHeader(u8"Сводка состояния");
        const auto& catScores = profile.category_best_scores();
        int minScore = Profile::kMaxCategoryScore;
        int maxScore = 0;
        int minIdx = 0;
        for (size_t i = 0; i < catScores.size(); ++i) {
            if (catScores[i] <= minScore) {
                minScore = catScores[i];
                minIdx = static_cast<int>(i);
            }
            maxScore = std::max(maxScore, catScores[i]);
        }
        const bool hasImbalance = (maxScore - minScore >= 3);
        const std::int64_t lastTaskTs = profile.last_task_timestamp();
        const int daysSince = lastTaskTs > 0
            ? static_cast<int>((nowSec - lastTaskTs) / (24 * 3600))
            : -1;
        const bool penaltiesEnabled = profile.penalties_enabled();
        const int recoveryLeft = penaltiesEnabled ? profile.recovery_tasks_remaining() : 0;

        std::string stateText;
        ImVec4 stateColor = accentSoft;
        if (lastTaskTs <= 0) {
            stateText = u8"Нет активности.";
            stateColor = LogLevelColor(AppLogLevel::Warning);
        } else if (recoveryLeft > 0) {
            stateText = u8"Прогрев";
            stateColor = LogLevelColor(AppLogLevel::Error);
        } else if (daysSince <= 7) {
            stateText = u8"Рост";
            stateColor = accent;
        } else if (daysSince <= 21) {
            stateText = u8"Плато";
        } else {
            stateText = u8"Спад";
            stateColor = LogLevelColor(AppLogLevel::Warning);
        }
        if (daysSince >= 0) {
            stateText += u8" (" + std::to_string(daysSince) + u8" дн. назад)";
        }

        std::string imbalanceText = u8"Баланс";
        ImVec4 imbalanceColor = accentSoft;
        if (hasImbalance) {
            imbalanceText = std::string(u8"Категория ") + Profile::kCategoryLabels[minIdx] +
                u8" (" + std::to_string(minScore) + u8"/10)";
            imbalanceColor = LogLevelColor(AppLogLevel::Warning);
        }

        std::string focusText;
        if (recoveryLeft > 0) {
            focusText = u8"Прогрев: осталось " + std::to_string(recoveryLeft) + u8" задач";
        } else if (minScore < Profile::kMaxCategoryScore) {
            focusText = std::string(u8"Категория ") + Profile::kCategoryLabels[minIdx] +
                u8" (" + std::to_string(minScore) + u8"/10)";
        } else if (span.hasNext && levelsLeft > 0) {
            focusText = std::string(u8"До ранга ") + span.nextLabel + u8": " +
                std::to_string(levelsLeft) + u8" ур.";
        } else {
            focusText = u8"Поддержание формы";
        }

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, TableCellPadding());
        ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("profile_state_summary", 3, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("state_status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("state_balance", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("state_focus", ImGuiTableColumnFlags_WidthStretch, 1.15f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Состояние");
            ImGui::TextColored(stateColor, "%s", stateText.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(u8"Последняя активность и текущая динамика профиля.");
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Перекос");
            if (hasImbalance) {
                ImVec4 warnBg = LogLevelColor(AppLogLevel::Warning);
                warnBg.w = std::clamp(warnBg.w * 0.16f, 0.06f, 0.16f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(warnBg));
            }
            ImGui::TextColored(imbalanceColor, "%s", imbalanceText.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(u8"Самая слабая категория относительно остальных.");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Фокус");
            ImGui::TextColored(accent, "%s", focusText.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(u8"Ближайшее полезное действие для профиля.");
            }
            ImGui::EndTable();
        }
        }
        ImGui::PopStyleVar();
    }
    EndCard();

    if (BeginCard("profile_balance_card")) {
        DrawCardHeader(u8"Зоны перекоса");
        const auto& catScores = profile.category_best_scores();
        struct CatRow { int idx; int score; };
        std::vector<CatRow> rows;
        rows.reserve(catScores.size());
        for (size_t i = 0; i < catScores.size(); ++i) {
            rows.push_back({static_cast<int>(i), catScores[i]});
        }
        std::sort(rows.begin(), rows.end(), [](const CatRow& a, const CatRow& b) {
            if (a.score != b.score) return a.score < b.score;
            return a.idx < b.idx;
        });
        if (rows.empty()) {
            DrawEmptyStateGui("profile_balance_empty", UiIcon::Chart,
                              u8"Нет данных по категориям",
                              u8"Категории появятся после начисления опыта по навыкам.");
        } else {
            const int showCount = std::min<int>(3, static_cast<int>(rows.size()));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, TableCellPadding());
            ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
            ImVec4 warnRow = warnText;
            warnRow.w = std::clamp(warnRow.w * 0.16f, 0.06f, 0.16f);
            {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("profile_balance_table", 3, ControlTableFlagsGui())) {
                ImGui::TableSetupColumn(u8"Категория", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn(u8"Оценка", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(u8"Прогресс", ImGuiTableColumnFlags_WidthStretch);
                for (int i = 0; i < showCount; ++i) {
                    const auto& row = rows[i];
                    const float ratio = static_cast<float>(row.score) / static_cast<float>(Profile::kMaxCategoryScore);
                    const bool isWeakest = (i == 0);
                    ImGui::TableNextRow();
                    if (isWeakest) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(warnRow));
                    }
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                    if (isWeakest) {
                        ImGui::TextColored(warnText, "%s", Profile::kCategoryLabels[row.idx]);
                    } else {
                        ImGui::TextUnformatted(Profile::kCategoryLabels[row.idx]);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d/10", row.score);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(i);
                    const float barAlpha = isWeakest ? 1.0f : 0.82f;
                    DrawMiniBar("##balance_bar", ratio,
                                ChartPaletteColor(row.idx, static_cast<int>(catScores.size()), barAlpha), 8.0f);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            }
            ImGui::PopStyleVar();
            ImGui::TextDisabled(u8"Слабые зоны: %d из %d", showCount, static_cast<int>(rows.size()));
        }
    }
    EndCard();

    if (focusMode) {
        ImGui::TextDisabled(u8"Фокусный режим: детали скрыты.");
        return;
    }

    if (ImGui::CollapsingHeader(u8"Топ навыков")) {
        auto skills = profile.list_skills();
        if (skills.empty()) {
            ImGui::TextDisabled(u8"Навыков нет.");
        } else {
            const int totalSkills = static_cast<int>(skills.size());
            std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                const int totalA = TotalSkillXpGui(a);
                const int totalB = TotalSkillXpGui(b);
                if (totalA != totalB) return totalA > totalB;
                return a.level > b.level;
            });
            const int shownSkills = std::min(totalSkills, 3);
            if (skills.size() > static_cast<size_t>(shownSkills)) skills.resize(static_cast<size_t>(shownSkills));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, TableCellPadding());
            ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImVec4 topRowBg = style.Colors[ImGuiCol_CheckMark];
            topRowBg.w = std::clamp(topRowBg.w * 0.12f, 0.04f, 0.12f);
            const ImU32 topRowBgU32 = ImGui::GetColorU32(topRowBg);
            {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("top_skills", 4, ControlTableFlagsGui(ImGuiTableFlags_RowBg))) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn(u8"Навык", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(u8"XP", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn(u8"Ур.", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableHeadersRow();
                int rowRank = 0;
                for (const auto& skill : skills) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (rowRank < 3) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, topRowBgU32);
                    }
                    ImGui::TextDisabled("%d", rowRank + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                    ImGui::TextUnformatted(catalog.display_name(skill.name).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", TotalSkillXpGui(skill));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", skill.level);
                    ++rowRank;
                }
                ImGui::EndTable();
            }
            }
            ImGui::PopStyleVar();
            ImGui::TextDisabled(u8"Показано: %d из %d. Подробные рейтинги — в аналитике.", shownSkills, totalSkills);
        }
    }

    if (ImGui::CollapsingHeader(u8"Диагностика состояния")) {
        const std::int64_t lastTask = profile.last_task_timestamp();
        const bool hasLastTask = lastTask > 0;
        std::string lastTaskLabel = hasLastTask ? FormatTimestamp(lastTask, "%d.%m %H:%M") : std::string(u8"нет данных");
        std::string lastTaskFull = hasLastTask ? FormatTimestamp(lastTask, "%Y-%m-%d %H:%M") : std::string();
        std::string elapsedLabel = hasLastTask
        ? FormatDurationShort(std::max<std::int64_t>(0, nowSec - lastTask))
        : std::string(u8"-");
        const bool penaltiesEnabled = profile.penalties_enabled();
        const int recoveryLeft = penaltiesEnabled ? profile.recovery_tasks_remaining() : 0;

        std::string categoryAttention;
        int categoriesBelow = 0;
        const auto& catScores = profile.category_best_scores();
        for (size_t i = 0; i < catScores.size(); ++i) {
            if (catScores[i] >= Profile::kMaxCategoryScore) continue;
            categoriesBelow += 1;
            if (!categoryAttention.empty()) categoryAttention += ", ";
            categoryAttention += Profile::kCategoryLabels[i];
            categoryAttention += "=";
            categoryAttention += std::to_string(catScores[i]);
            categoryAttention += "/10";
        }
        if (categoryAttention.empty()) {
            categoryAttention = u8"все освоены";
        }

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, TableCellPadding());
        ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        auto rowTint = [](AppLogLevel level) {
            ImVec4 tint = LogLevelColor(level);
            tint.w = std::clamp(tint.w * 0.12f, 0.04f, 0.12f);
            return ImGui::GetColorU32(tint);
        };
        {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("profile_status", 4, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(u8"Параметр", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableSetupColumn(u8"Статус", ImGuiTableColumnFlags_WidthFixed, 112.0f);
            ImGui::TableSetupColumn(u8"Детали", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int rowRank = 0;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", ++rowRank);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Активность");
            ImGui::TableSetColumnIndex(2);
            if (hasLastTask) {
                ImGui::TextColored(accentSoft, "%s", lastTaskLabel.c_str());
                if (ImGui::IsItemHovered() && !lastTaskFull.empty()) {
                    ImGui::SetTooltip("%s", lastTaskFull.c_str());
                }
            } else {
                ImGui::TextDisabled("%s", lastTaskLabel.c_str());
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled(u8"%s назад", elapsedLabel.c_str());
            ImGui::TableNextRow();
            if (recoveryLeft > 0) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowTint(AppLogLevel::Error));
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", ++rowRank);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Прогрев");
            ImGui::TableSetColumnIndex(2);
            if (recoveryLeft > 0) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"активен");
                ImGui::TableSetColumnIndex(3);
                ImGui::Text(u8"задач: %d", recoveryLeft);
            } else {
                ImGui::TextColored(accentSoft, u8"нет");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled(u8"-");
            }
            ImGui::TableNextRow();
            if (categoriesBelow > 0) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowTint(AppLogLevel::Warning));
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", ++rowRank);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Категории");
            ImGui::TableSetColumnIndex(2);
            if (categoriesBelow > 0) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"%d ниже", categoriesBelow);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                    ImGui::TextWrapped("%s", categoryAttention.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled(u8"наведите для списка");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                    ImGui::TextWrapped("%s", categoryAttention.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::TextColored(accentSoft, u8"все 10/10");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled(u8"-");
            }
            ImGui::EndTable();
        }
        }
        ImGui::PopStyleVar();
    }

    }

static void DrawProfileAchievementsSection(GuiState& state, SkillCatalog& catalog, Profile& profile, std::int64_t nowSec) {
    if (ImGui::CollapsingHeader(u8"Ачивки")) {
    ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    if (profile.achievements().empty()) {
        DrawEmptyStateGui("profile_achievements_empty", UiIcon::Bell,
                          u8"Ачивок нет",
                          u8"Активные достижения появятся здесь после выдачи ачивок профилю.");
    } else {
        auto formatDate = [](std::int64_t ts) -> std::string {
            if (ts <= 0) return std::string(u8"без срока");
            std::time_t t = static_cast<std::time_t>(ts);
            std::tm tm{};
            #if defined(_WIN32)
            localtime_s(&tm, &t);
            #else
            localtime_r(&t, &tm);
            #endif
            char buf[32];
            if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm)) {
                return std::string(buf);
            }
            return std::string(u8"срок указан");
        };
        int activeCount = 0;
        int expiredCount = 0;
        for (const auto& a : profile.achievements()) {
            if (a.is_active(nowSec)) {
                activeCount += 1;
            } else {
                expiredCount += 1;
            }
        }
        DrawCardHeader(u8"Фильтры");
        const float achFilterWidth = ImGui::GetContentRegionAvail().x;
        const bool stackAchFilters = achFilterWidth < 520.0f;
        if (stackAchFilters) {
            ImGui::InputTextWithHint(u8"##ach_filter", u8"Фильтр ачивок",
            state.filters.achievement.data(), state.filters.achievement.size());
            ImGui::Checkbox(u8"Показывать истекшие", &state.showExpiredAchievements);
            if (IconButtonGui("profile_ach_reset_stack", UiIcon::Clear,
                              u8"Сбросить фильтр ачивок", accent, ImGui::GetFrameHeight())) {
                state.filters.achievement.fill('\0');
                state.showExpiredAchievements = true;
            }
        } else {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("ach_filters", 4, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("ach_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("ach_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("ach_toggle", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("ach_reset", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
            ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##ach_filter", u8"Фильтр ачивок",
            state.filters.achievement.data(), state.filters.achievement.size());
            ImGui::TableNextColumn();
            ImGui::Checkbox(u8"Показывать истекшие", &state.showExpiredAchievements);
            ImGui::TableNextColumn();
            if (IconButtonGui("profile_ach_reset", UiIcon::Clear,
                              u8"Сбросить фильтр ачивок", accent, ImGui::GetFrameHeight())) {
                state.filters.achievement.fill('\0');
                state.showExpiredAchievements = true;
            }
            ImGui::EndTable();
        }
        }
        int visibleAch = 0;
        for (const auto& a : profile.achievements()) {
            const bool active = a.is_active(nowSec);
            if (!state.showExpiredAchievements && !active) {
                continue;
            }
            const std::string achSkillName = catalog.display_name(a.skill);
            const std::string filterText = a.title + " " + achSkillName;
            if (!MatchesFilter(filterText, state.filters.achievement.data())) {
                continue;
            }
            visibleAch += 1;
        }
        std::vector<std::pair<std::string, std::int64_t>> expiringSoon;
        constexpr std::int64_t kSoonWindow = 7LL * 24 * 3600;
        for (const auto& a : profile.achievements()) {
            if (a.expiresAt == 0) continue;
            if (!a.is_active(nowSec)) continue;
            const std::int64_t remaining = a.expiresAt - nowSec;
            if (remaining <= 0 || remaining > kSoonWindow) continue;
            std::string label = a.title;
            const std::string achSkillName = catalog.display_name(a.skill);
            if (!achSkillName.empty()) {
                label += " (" + achSkillName + ")";
            }
            expiringSoon.emplace_back(std::move(label), remaining);
        }
        if (!expiringSoon.empty()) {
            std::sort(expiringSoon.begin(), expiringSoon.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
            DrawCardHeader(u8"Скоро истекают");
            const size_t limit = std::min<size_t>(3, expiringSoon.size());
            for (size_t i = 0; i < limit; ++i) {
                ImGui::BulletText("%s - %s",
                expiringSoon[i].first.c_str(),
                FormatDurationShort(expiringSoon[i].second).c_str());
            }
            if (expiringSoon.size() > limit) {
                ImGui::TextDisabled(u8"И ещё: %d", static_cast<int>(expiringSoon.size() - limit));
            }
            ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
        }

        const float iconCell = std::clamp(ImGui::GetFrameHeight() * 2.15f, 58.0f, 76.0f);
        const int iconsPerRow = std::clamp(
            static_cast<int>(ImGui::GetContentRegionAvail().x / (iconCell + ImGui::GetStyle().ItemSpacing.x)),
            3, 8);
        int iconIndex = 0;
        if (visibleAch > 0) {
            ImGuiTableFlags gridFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
            if (ImGui::BeginTable("ach_grid", iconsPerRow, gridFlags)) {
                for (const auto& a : profile.achievements()) {
                    bool active = a.is_active(nowSec);
                    if (!state.showExpiredAchievements && !active) {
                        continue;
                    }
                    const std::string achSkillName = catalog.display_name(a.skill);
                    const std::string filterText = a.title + " " + achSkillName;
                    if (!MatchesFilter(filterText, state.filters.achievement.data())) {
                        continue;
                    }
                    const std::string expires = formatDate(a.expiresAt);
                    const auto iconPath = ResolveIconPath(a.icon, state.storageDir);
                    if (iconIndex % iconsPerRow == 0) {
                        ImGui::TableNextRow();
                    }
                    ImGui::TableSetColumnIndex(iconIndex % iconsPerRow);
                    ImGui::PushID(iconIndex);
                    bool hovered = false;
                    if (iconPath) {
                        if (const auto* iconTex = GetIconTexture(*iconPath)) {
                            ImVec4 activeTint = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                            activeTint.w = 1.0f;
                            ImVec4 inactiveTint = BlendColor(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], activeTint, 0.25f);
                            ImGui::ImageWithBg(ImTextureRef((ImTextureID)(intptr_t)iconTex->id),
                            ImVec2(iconCell, iconCell),
                            ImVec2(0, 0), ImVec2(1, 1),
                            ImVec4(0, 0, 0, 0), active ? activeTint : inactiveTint);
                            hovered = ImGui::IsItemHovered();
                        } else {
                            ImGui::BeginDisabled();
                            ImGui::Button("?", ImVec2(iconCell, iconCell));
                            ImGui::EndDisabled();
                            hovered = ImGui::IsItemHovered();
                        }
                    } else {
                        ImGui::BeginDisabled();
                        ImGui::Button("?", ImVec2(iconCell, iconCell));
                        ImGui::EndDisabled();
                        hovered = ImGui::IsItemHovered();
                    }
                    if (hovered) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(a.title.c_str());
                        ImGui::Text(u8"%s, %+0.1f%% XP", achSkillName.c_str(), a.bonusPercent);
                        if (a.expiresAt == 0) {
                            ImGui::TextUnformatted(u8"Срок: без срока");
                        } else {
                            ImGui::Text(u8"Срок до: %s", expires.c_str());
                            if (active) {
                                const std::string remaining = FormatDurationShort(a.expiresAt - nowSec);
                                ImGui::Text(u8"Осталось: %s", remaining.c_str());
                            } else {
                                ImGui::TextUnformatted(u8"Срок: истекла");
                            }
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                    ++iconIndex;
                }
                ImGui::EndTable();
            }
        }
        if (visibleAch == 0) {
            DrawEmptyStateGui("profile_achievements_filter_empty", UiIcon::Search,
                              u8"Нет ачивок по фильтру",
                              u8"Сбросьте фильтр активности или поиск, чтобы вернуть список ачивок.");
        }
        ImGui::TextDisabled(u8"Показано: %d | активных: %d | истекших: %d",
        visibleAch, activeCount, expiredCount);
    }
    }
}

static void DrawProfileSkillsSection(GuiState& state, SkillCatalog& catalog, Profile& profile, std::int64_t nowSec) {
    if (ImGui::CollapsingHeader(u8"Навыки")) {
    ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    DrawCardHeader(u8"Фильтры");
    const float skillFilterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackSkillFilters = skillFilterWidth < 620.0f;
    const char* sortLabels[] = {u8"По имени", u8"По уровню", u8"По XP", u8"По весу"};
    const char* weightCategoryLabels[] = {
        u8"Все",
        u8"A (>=1.30)",
        u8"B (1.10-1.29)",
        u8"C (0.90-1.09)",
        u8"D (0.70-0.89)",
        u8"E (<0.70)"
    };
    if (stackSkillFilters) {
        ImGui::InputTextWithHint(u8"##profile_skill_search", u8"Поиск навыка",
        state.filters.profileSkill.data(), state.filters.profileSkill.size());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Сортировка", &state.profileSkillSort, sortLabels, IM_ARRAYSIZE(sortLabels));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Категория (вес)", &state.profileSkillCategoryFilter,
        weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
        if (IconButtonGui("profile_skill_filters_reset_stack", UiIcon::Clear,
                          u8"Сбросить фильтры навыков", accent, ImGui::GetFrameHeight())) {
            state.filters.profileSkill.fill('\0');
            state.profileSkillCategoryFilter = 0;
            state.profileSkillWeightRange[0] = 0.0f;
            state.profileSkillWeightRange[1] = 2.0f;
        }
    } else {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("profile_skill_filters", 5, ControlTableFlagsGui())) {
        ImGui::TableSetupColumn("profile_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("profile_search", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("profile_sort", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("profile_category", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("profile_reset", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
        ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
        const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##profile_skill_search", u8"Поиск навыка",
        state.filters.profileSkill.data(), state.filters.profileSkill.size());
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Сортировка", &state.profileSkillSort, sortLabels, IM_ARRAYSIZE(sortLabels));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Категория (вес)", &state.profileSkillCategoryFilter,
        weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
        ImGui::TableNextColumn();
        if (IconButtonGui("profile_skill_filters_reset", UiIcon::Clear,
                          u8"Сбросить фильтры навыков", accent, ImGui::GetFrameHeight())) {
            state.filters.profileSkill.fill('\0');
            state.profileSkillCategoryFilter = 0;
            state.profileSkillWeightRange[0] = 0.0f;
            state.profileSkillWeightRange[1] = 2.0f;
        }
        ImGui::EndTable();
        }
    }
    float range[2] = {state.profileSkillWeightRange[0], state.profileSkillWeightRange[1]};
    if (ImGui::SliderFloat2(u8"Диапазон веса", range, 0.0f, 2.0f, "%.2f")) {
        if (range[0] > range[1]) std::swap(range[0], range[1]);
        state.profileSkillWeightRange[0] = range[0];
        state.profileSkillWeightRange[1] = range[1];
    }
    auto skills = state.active->profile.list_skills();
    const int sortMode = std::clamp(state.profileSkillSort, 0, 3);
    std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
        const std::string nameA = ToLowerUtf8(catalog.display_name(a.name));
        const std::string nameB = ToLowerUtf8(catalog.display_name(b.name));
        if (sortMode == 1) {
            if (a.level != b.level) return a.level > b.level;
        } else if (sortMode == 2) {
            int totalA = TotalSkillXpGui(a);
            int totalB = TotalSkillXpGui(b);
            if (totalA != totalB) return totalA > totalB;
        } else if (sortMode == 3) {
            if (a.weight != b.weight) return a.weight > b.weight;
        }
        return nameA < nameB;
    });
    int visibleCount = 0;
    int rowRank = 0;
    ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, TableCellPadding());
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.06f, 0.16f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    const float importantRowHeight = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
    const char* weightShort[] = {"", "A", "B", "C", "D", "E"};
    if (ImGui::BeginTable("skill_table", 6, tableFlags)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn(u8"Навык");
        ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("XP", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn(u8"Всего XP", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (const auto& skill : skills) {
            const std::string displayName = catalog.display_name(skill.name);
            if (!MatchesFilter(displayName, state.filters.profileSkill.data())) {
                continue;
            }
            const float w = static_cast<float>(skill.weight);
            if (w < state.profileSkillWeightRange[0] || w > state.profileSkillWeightRange[1]) {
                continue;
            }
            if (state.profileSkillCategoryFilter > 0) {
                const int categoryIdx = WeightCategoryIndex(skill.weight);
                if (categoryIdx != state.profileSkillCategoryFilter) continue;
            }
            const bool importantRow = (sortMode != 0 && rowRank < 3);
            ++visibleCount;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, importantRow ? importantRowHeight : 0.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", rowRank + 1);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextUnformatted(displayName.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                const std::string desc = catalog.description(skill.name);
                ImGui::TextUnformatted(displayName.c_str());
                if (!desc.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", desc.c_str());
                }
                const int totalSkillXp = TotalSkillXpGui(skill);
                double bonusPercent = std::max(0.0, (profile.skill_bonus_multiplier(skill.name, nowSec) - 1.0) * 100.0);
                ImGui::Separator();
                ImGui::Text(u8"ID: %s", skill.name.c_str());
                ImGui::Text(u8"Уровень: %d", skill.level);
                ImGui::Text(u8"XP: %d / %d", skill.xp, skill.xpToNext);
                ImGui::Text(u8"Всего XP: %d", totalSkillXp);
                ImGui::Text(u8"Вес: %.2f", skill.weight);
                ImGui::Text(u8"Категория веса: %s", WeightCategoryLabel(WeightCategoryIndex(skill.weight)));
                if (bonusPercent > 0.01) {
                    ImGui::Text(u8"Бонус: +%.1f%%", bonusPercent);
                }
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", skill.level);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d / %d", skill.xp, skill.xpToNext);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", TotalSkillXpGui(skill));
            ImGui::TableSetColumnIndex(5);
            const int weightCat = std::clamp(WeightCategoryIndex(skill.weight), 0, 5);
            ImGui::Text("%s %.2f", weightShort[weightCat], skill.weight);
            ++rowRank;
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, static_cast<int>(skills.size()));
    if (visibleCount == 0) {
        DrawEmptyStateGui("profile_skills_filter_empty", UiIcon::Search,
                          u8"Нет навыков по фильтру",
                          u8"Сбросьте поиск, категорию или сортировку, чтобы вернуть список навыков.");
    }
    }
}

static void DrawProfileChartSection(GuiState& state, SkillCatalog& catalog, Profile& profile) {
    DrawCardHeader(u8"Аналитика профиля");
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;

        const auto& catScores = profile.category_best_scores();
        int weakestIdx = -1;
        int minScore = Profile::kMaxCategoryScore;
        for (size_t i = 0; i < catScores.size(); ++i) {
            if (catScores[i] <= minScore) {
                minScore = catScores[i];
                weakestIdx = static_cast<int>(i);
            }
        }
        if (BeginCard("profile_chart_categories")) {
            DrawCardHeader(u8"Категории");
            if (catScores.empty()) {
                DrawEmptyStateGui("profile_chart_categories_empty", UiIcon::Chart,
                                  u8"Нет данных по категориям",
                                  u8"Аналитика категорий появится после начисления опыта.");
            } else {
                ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
                ImVec4 warnRow = warnText;
                warnRow.w = std::clamp(warnRow.w * 0.14f, 0.05f, 0.14f);
                {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("cat_bars", 3, ControlTableFlagsGui())) {
                    ImGui::TableSetupColumn("cat_label", ImGuiTableColumnFlags_WidthFixed, 74.0f);
                    ImGui::TableSetupColumn("cat_value", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                    ImGui::TableSetupColumn("cat_bar", ImGuiTableColumnFlags_WidthStretch);
                    for (int i = 0; i < static_cast<int>(catScores.size()); ++i) {
                        const bool isWeakest = (i == weakestIdx);
                        ImGui::TableNextRow();
                        if (isWeakest) {
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(warnRow));
                        }
                        ImGui::TableNextColumn();
                        if (isWeakest) {
                            ImGui::TextColored(warnText, "%s", Profile::kCategoryLabels[i]);
                        } else {
                            ImGui::TextDisabled("%s", Profile::kCategoryLabels[i]);
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%d/10", catScores[i]);
                        ImGui::TableNextColumn();
                        ImGui::PushID(i);
                        const float barAlpha = isWeakest ? 1.0f : 0.82f;
                        DrawMiniBar("##cat_bar", static_cast<float>(catScores[i]) / 10.0f,
                                    ChartPaletteColor(i, static_cast<int>(catScores.size()), barAlpha), 8.0f);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }
        }
        EndCard();
        ImGui::Dummy(ImVec2(0.0f, CardSpacing()));

        auto skills = profile.list_skills();
        if (BeginCard("profile_chart_top_skills")) {
            DrawCardHeader(u8"Топ навыков");
            if (skills.empty()) {
                DrawEmptyStateGui("profile_chart_top_skills_empty", UiIcon::Chart,
                                  u8"У профиля нет навыков",
                                  u8"Начислите опыт по навыкам, чтобы сформировать топ.");
            } else {
                struct SkillStatRow {
                    Skill skill;
                    int totalXp = 0;
                };
                std::vector<SkillStatRow> rows;
                rows.reserve(skills.size());
                for (const auto& skill : skills) {
                    SkillStatRow row;
                    row.skill = skill;
                    row.totalXp = TotalSkillXpGui(skill);
                    rows.push_back(row);
                }
                std::sort(rows.begin(), rows.end(), [&](const SkillStatRow& a, const SkillStatRow& b) {
                    if (a.totalXp != b.totalXp) return a.totalXp > b.totalXp;
                    return ToLowerUtf8(catalog.display_name(a.skill.name)) < ToLowerUtf8(catalog.display_name(b.skill.name));
                });
                const int showCount = std::min<int>(6, static_cast<int>(rows.size()));
                const int maxXp = std::max(1, rows.front().totalXp);
                {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("top_skill_bars", 3, ControlTableFlagsGui())) {
                    ImGui::TableSetupColumn("skill_name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                    ImGui::TableSetupColumn("skill_xp", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                    ImGui::TableSetupColumn("skill_bar", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                    for (int i = 0; i < showCount; ++i) {
                        auto& row = rows[i];
                        const std::string displayName = catalog.display_name(row.skill.name);
                        const float ratio = static_cast<float>(row.totalXp) / static_cast<float>(maxXp);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(displayName.c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(displayName.c_str());
                            ImGui::Separator();
                            ImGui::Text(u8"Уровень: %d", row.skill.level);
                            ImGui::Text(u8"XP: %d / %d", row.skill.xp, row.skill.xpToNext);
                            ImGui::Text(u8"Всего XP: %d", row.totalXp);
                            ImGui::Text(u8"Вес: %.2f", row.skill.weight);
                            ImGui::EndTooltip();
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", row.totalXp);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(u8"Всего XP: %d", row.totalXp);
                        }
                        ImGui::TableNextColumn();
                        ImGui::PushID(i);
                        DrawMiniBar("##skill_bar", ratio,
                                    ChartPaletteColor(i, showCount, 0.92f), 8.0f);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                }
                ImGui::TextDisabled(u8"%d/%d", showCount, static_cast<int>(rows.size()));
            }
        }
        EndCard();
        ImGui::Dummy(ImVec2(0.0f, CardSpacing()));

        if (BeginCard("profile_chart_radar")) {
            DrawCardHeader(u8"Радар навыков");
            if (skills.size() < 3) {
                DrawEmptyStateGui("profile_chart_radar_empty", UiIcon::Chart,
                                  u8"Недостаточно навыков",
                                  u8"Для радар-диаграммы нужно минимум 3 навыка с опытом.");
            } else {
                static int radarCount = 8;
                const int maxRadar = std::min<int>(16, static_cast<int>(skills.size()));
                radarCount = std::clamp(radarCount, 3, maxRadar);
                const float radarControlWidth = ImGui::GetContentRegionAvail().x;
                const bool compactRadarControls = radarControlWidth < 520.0f;
                if (compactRadarControls) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::SliderInt(u8"Оси", &radarCount, 3, maxRadar);
                } else {
                    CompactTableScopeGui compactTable;
                    if (ImGui::BeginTable("radar_controls", 3, ControlTableFlagsGui())) {
                    ImGui::TableSetupColumn("radar_label", ImGuiTableColumnFlags_WidthFixed, 54.0f);
                    ImGui::TableSetupColumn("radar_slider", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                    ImGui::TableSetupColumn("radar_count", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Оси");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::SliderInt("##radar_axes", &radarCount, 3, maxRadar);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled(u8"%d/%d", radarCount, static_cast<int>(skills.size()));
                    ImGui::EndTable();
                    }
                }

                std::vector<Skill> top;
                top.reserve(static_cast<size_t>(radarCount));
                std::vector<std::pair<int, Skill>> scored;
                scored.reserve(skills.size());
                for (const auto& skill : skills) {
                    scored.push_back({TotalSkillXpGui(skill), skill});
                }
                std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
                    if (a.first != b.first) return a.first > b.first;
                    return a.second.name < b.second.name;
                });
                for (int i = 0; i < radarCount && i < static_cast<int>(scored.size()); ++i) {
                    top.push_back(scored[i].second);
                }
                std::sort(top.begin(), top.end(), [&](const Skill& a, const Skill& b) {
                    return ToLowerUtf8(catalog.display_name(a.name)) < ToLowerUtf8(catalog.display_name(b.name));
                });
                for (auto& skill : top) {
                    skill.name = catalog.display_name(skill.name);
                }

                ImVec2 radarSize(ImGui::GetContentRegionAvail().x, 232.0f);
                if (radarSize.x < 200.0f) radarSize.x = 200.0f;
                ImVec2 radarPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##skill_radar_canvas", radarSize);
                if (ImGui::IsItemVisible()) {
                    DrawSkillRadarChart(top, radarPos, ImGui::GetItemRectSize(), ImGui::GetWindowDrawList());
                }
            }
        }
        EndCard();
        ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
    }
}

static void DrawProfileActivitySection(GuiState& state, const std::string& profileId) {
    if (ImGui::CollapsingHeader(u8"Последние действия")) {
    if (ImGui::BeginChild("profile_activity", ImVec2(0, 128), true)) {
        ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;
        const auto logIt = state.activityLogs.find(profileId);
        const bool hasLog = logIt != state.activityLogs.end() && !logIt->second.empty();
        int activityShown = 0;
        if (hasLog) {
            for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                if (!MatchesFilter(*it, state.filters.activity.data())) continue;
                activityShown += 1;
            }
        }
        auto exportActivity = [&]() {
            if (hasLog) {
                std::filesystem::path outPath;
                if (ExportProfileActivity(state, profileId, logIt->second,
                state.filters.activity.data(), outPath)) {
                    std::string msg = u8"История сохранена: ";
                    msg += outPath.string();
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить историю.", 1.0f, 0.45f, 0.45f);
                }
            } else {
                SetStatus(state, u8"Нет действий для экспорта.", 1.0f, 0.75f, 0.35f);
            }
        };
        const float activityWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActivity = activityWidth < 540.0f;
        if (stackActivity) {
            ImGui::InputTextWithHint(u8"##activity_filter", u8"Фильтр действий",
            state.filters.activity.data(), state.filters.activity.size());
            if (IconButtonGui("profile_activity_reset_stack", UiIcon::Clear,
                              u8"Сбросить фильтр действий", accent, ImGui::GetFrameHeight())) {
                state.filters.activity.fill('\0');
            }
            ImGui::SameLine();
            if (IconButtonGui("profile_activity_export_stack", UiIcon::Export,
                              u8"Экспортировать историю действий", accent, ImGui::GetFrameHeight())) {
                exportActivity();
            }
            ImGui::TextDisabled(u8"Показано: %d", activityShown);
        } else {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("activity_filters", 5, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("activity_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("activity_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("activity_reset", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
            ImGui::TableSetupColumn("activity_count", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("activity_export", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f);
            ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##activity_filter", u8"Фильтр действий",
            state.filters.activity.data(), state.filters.activity.size());
            ImGui::TableNextColumn();
            if (IconButtonGui("profile_activity_reset", UiIcon::Clear,
                              u8"Сбросить фильтр действий", accent, ImGui::GetFrameHeight())) {
                state.filters.activity.fill('\0');
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Показано: %d", activityShown);
            ImGui::TableNextColumn();
            if (IconButtonGui("profile_activity_export", UiIcon::Export,
                              u8"Экспортировать историю действий", accent, ImGui::GetFrameHeight())) {
                exportActivity();
            }
            ImGui::EndTable();
            }
        }
        if (hasLog) {
            int rendered = 0;
            constexpr int kMaxActivityRows = 6;
            for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                if (!MatchesFilter(*it, state.filters.activity.data())) continue;
                if (rendered >= kMaxActivityRows) break;
                ImGui::TextWrapped("%s", it->c_str());
                rendered += 1;
            }
            if (activityShown == 0) {
                DrawEmptyStateGui("profile_activity_filter_empty", UiIcon::Search,
                                  u8"Нет действий по фильтру",
                                  u8"Очистите поиск, чтобы вернуть историю действий профиля.");
            } else if (activityShown > rendered) {
                ImGui::TextDisabled(u8"+%d в истории", activityShown - rendered);
            }
        } else {
            DrawEmptyStateGui("profile_activity_empty", UiIcon::List,
                              u8"Пока нет действий",
                              u8"История начнет заполняться после изменений профиля, задач и опыта.");
        }
    }
    ImGui::EndChild();
    }
}

static void DrawProfileTasksSection(GuiState& state, const std::string& profileId) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    const std::int64_t nowSec = NowSeconds();
    const auto taskStatusShort = [](int status) -> const char* {
        switch (NormalizeTaskStatus(status)) {
        case kTaskStatusInProgress:
            return u8"Раб.";
        case kTaskStatusDone:
            return u8"Гот.";
        default:
            return u8"Нов.";
        }
    };
    const auto compactDeadline = [](std::int64_t timestamp) {
        return FormatTimestamp(timestamp, "%d.%m %H:%M");
    };
    const auto fullDeadline = [](std::int64_t timestamp) {
        return FormatTimestamp(timestamp, "%d.%m.%Y %H:%M");
    };

    if (ImGui::CollapsingHeader(u8"Текущие задачи", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ProfileTaskOverview taskOverview = BuildProfileTaskOverview(state, profileId);
        std::vector<const TaskEntry*> activeItems;
        activeItems.reserve(state.tasks.size());
        for (const auto& task : state.tasks) {
            if (!TaskHasAssignee(task, profileId)) continue;
            const int status = NormalizeTaskStatus(task.status);
            if (status == kTaskStatusDone) continue;
            activeItems.push_back(&task);
        }

        std::sort(activeItems.begin(), activeItems.end(), [nowSec](const TaskEntry* a, const TaskEntry* b) {
            return ShouldRankProfileTaskEarlier(*a, *b, nowSec);
        });

        const int totalActiveCount = static_cast<int>(activeItems.size());
        {
        CompactTableScopeGui compactTable;
        if (ImGui::BeginTable("profile_active_tasks_summary", 4, ControlTableFlagsGui())) {
            ImGui::TableSetupColumn("profile_active_total", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("profile_active_overdue", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("profile_active_xp", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("profile_active_focus", ImGuiTableColumnFlags_WidthStretch, 1.15f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Актив");
            ImGui::Text("%d", totalActiveCount);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(u8"Активные задачи профиля.");
            }
            ImGui::TextDisabled(u8"Н:%d | В:%d", taskOverview.created, taskOverview.inProgress);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(u8"Новые: %d\nВ работе: %d", taskOverview.created, taskOverview.inProgress);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(u8"Проср.");
            std::string overdueCountLabel = std::to_string(taskOverview.overdue);
            if (taskOverview.overdue > 0) {
                DrawStatusPillGui("profile_tasks_overdue_count", overdueCountLabel.c_str(),
                                  LogLevelColor(AppLogLevel::Warning),
                                  u8"Просроченные активные задачи.", true);
            } else {
                DrawStatusPillGui("profile_tasks_overdue_count", overdueCountLabel.c_str(),
                                  style.Colors[ImGuiCol_TextDisabled],
                                  u8"Просрочек нет.", false);
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"XP");
            std::string xpCountLabel = std::to_string(taskOverview.xpPending);
            if (taskOverview.xpPending > 0) {
                DrawStatusPillGui("profile_tasks_xp_count", xpCountLabel.c_str(),
                                  LogLevelColor(AppLogLevel::Warning),
                                  u8"Закрытые задачи, ожидающие начисления XP.", true);
            } else {
                DrawStatusPillGui("profile_tasks_xp_count", xpCountLabel.c_str(),
                                  style.Colors[ImGuiCol_TextDisabled],
                                  u8"Нет задач с незавершенным XP handoff.", false);
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled(u8"Фокус");
            if (taskOverview.primaryTask) {
                std::string focusTitle = taskOverview.primaryTask->title.empty()
                    ? std::string(u8"Без названия")
                    : taskOverview.primaryTask->title;
                ImGui::TextUnformatted(focusTitle.c_str());
                if (ImGui::IsItemHovered() && !taskOverview.primaryTask->description.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                    ImGui::TextUnformatted(taskOverview.primaryTask->description.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                if (taskOverview.primaryTask->deadlineAt > 0) {
                    const std::string focusDeadline = compactDeadline(taskOverview.primaryTask->deadlineAt);
                    const std::string focusDeadlineFull = fullDeadline(taskOverview.primaryTask->deadlineAt);
                    const bool focusOverdue = taskOverview.primaryTask->deadlineAt < nowSec;
                    if (focusOverdue) {
                        ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", focusDeadline.c_str());
                    } else {
                        ImGui::TextDisabled("%s", focusDeadline.c_str());
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(u8"Дедлайн: %s", focusDeadlineFull.c_str());
                    }
                } else {
                    ImGui::TextDisabled(u8"без дедлайна");
                }
            } else {
                ImGui::TextDisabled(u8"Нет активных задач");
            }
            ImGui::EndTable();
        }
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (state.modules.tasks) {
            const float actionSize = ImGui::GetFrameHeight();
            ImGui::TextDisabled(u8"Открыть в задачах");
            ImGui::SameLine();
            if (IconButtonGui("profile_tasks_all", UiIcon::Task,
                              u8"Все задачи профиля", accent, actionSize)) {
                FocusTasksForProfile(state, profileId);
            }
            ImGui::SameLine();
            if (IconButtonGui("profile_tasks_active", UiIcon::Play,
                              u8"Активные задачи профиля", accent, actionSize,
                              taskOverview.active > 0)) {
                FocusTasksForProfile(state, profileId, 7);
            }
            ImGui::SameLine();
            const ImVec4 overdueAccent = taskOverview.overdue > 0
                ? LogLevelColor(AppLogLevel::Warning)
                : accent;
            if (IconButtonGui("profile_tasks_overdue", UiIcon::Calendar,
                              u8"Просроченные задачи профиля", overdueAccent, actionSize,
                              taskOverview.overdue > 0)) {
                FocusTasksForProfile(state, profileId, 3);
            }
            ImGui::SameLine();
            const ImVec4 xpAccent = taskOverview.xpPending > 0
                ? LogLevelColor(AppLogLevel::Warning)
                : accent;
            if (IconButtonGui("profile_tasks_xp", UiIcon::Award,
                              u8"Задачи профиля, ожидающие XP", xpAccent, actionSize,
                              taskOverview.xpPending > 0)) {
                FocusTasksForProfile(state, profileId, 6);
            }
        }

        if (activeItems.empty()) {
            if (DrawEmptyStateGui("profile_active_tasks_empty", UiIcon::Task,
                                  u8"Нет активных задач",
                                  u8"У профиля нет открытой нагрузки. Откройте задачи, чтобы назначить новую.",
                                  UiIcon::Plus, state.isAdmin ? u8"Открыть постановку задачи" : nullptr,
                                  state.isAdmin)) {
                FocusTasksForProfile(state, profileId, 0);
            }
        } else {
            const int shownCount = std::min(6, totalActiveCount);
            if (shownCount < totalActiveCount) {
                activeItems.resize(static_cast<size_t>(shownCount));
            }
            {
                CompactTableScopeGui compactTable;
                if (ImGui::BeginTable("profile_active_tasks_table", 5,
                                      ControlTableFlagsGui(ImGuiTableFlags_RowBg))) {
                ImGui::TableSetupColumn(u8"Ст.", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn(u8"Дедл.", ImGuiTableColumnFlags_WidthFixed, 86.0f);
                ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 0.85f);
                ImGui::TableSetupColumn(u8"Этап", ImGuiTableColumnFlags_WidthStretch, 0.95f);
                ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.55f);
                ImGui::TableHeadersRow();
                for (const TaskEntry* task : activeItems) {
                    const int status = NormalizeTaskStatus(task->status);
                    const bool overdue = task->deadlineAt > 0 && task->deadlineAt < nowSec;
                    const bool isFocusTask = taskOverview.primaryTask && taskOverview.primaryTask->id == task->id;
                    ImGui::TableNextRow();
                    if (isFocusTask) {
                        ApplyTableRowStateTintGui(false, true);
                    } else if (overdue) {
                        ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                        rowHighlight.w = std::clamp(rowHighlight.w * 0.14f, 0.05f, 0.16f);
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
                    }

                    ImGui::TableSetColumnIndex(0);
                    ImVec4 statusAccent = style.Colors[ImGuiCol_CheckMark];
                    if (status == kTaskStatusInProgress) {
                        statusAccent = LogLevelColor(AppLogLevel::Info);
                    } else if (overdue) {
                        statusAccent = LogLevelColor(AppLogLevel::Warning);
                    }
                    DrawStatusPillGui("profile_active_task_status", taskStatusShort(status),
                                      statusAccent, TaskStatusLabel(status),
                                      status == kTaskStatusInProgress || overdue);

                    ImGui::TableSetColumnIndex(1);
                    if (task->deadlineAt > 0) {
                        const std::string deadlineText = compactDeadline(task->deadlineAt);
                        const std::string deadlineFull = fullDeadline(task->deadlineAt);
                        if (overdue) {
                            ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s", deadlineText.c_str());
                        } else {
                            ImGui::TextUnformatted(deadlineText.c_str());
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", deadlineFull.c_str());
                        }
                    } else {
                        ImGui::TextDisabled("-");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(u8"Дедлайн не задан.");
                        }
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                    const std::string projectName = ResolveTaskProjectName(state, *task);
                    DrawClippedTextWithTooltip(projectName, u8"Без проекта", projectName.empty());

                    ImGui::TableSetColumnIndex(3);
                    ImU32 pipelineAttentionBg = 0;
                    if (TryGetTaskPipelineAttentionCellBg(state, *task, pipelineAttentionBg)) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, pipelineAttentionBg);
                    }
                    const std::string pipelineName = ResolveTaskPipelineName(state, *task);
                    const int pipelineIndex = ResolveTaskPipelineIndex(state, *task);
                    if (pipelineIndex >= 0 && pipelineIndex < static_cast<int>(state.pipelineSteps.size())) {
                        const PipelineStep& step = state.pipelineSteps[static_cast<size_t>(pipelineIndex)];
                        const std::string stageCode = TaskPipelineStepCode(step, pipelineIndex);
                        const std::string stageTitle = step.title.empty() ? pipelineName : step.title;
                        ImGui::Text("%s", stageCode.c_str());
                        if (ImGui::IsItemHovered() && !stageTitle.empty()) {
                            ImGui::SetTooltip("%s", stageTitle.c_str());
                        }
                    } else {
                        DrawClippedTextWithTooltip(pipelineName, u8"Без этапа", pipelineName.empty());
                    }
                    DrawTaskPipelineProgressHint(state, *task);

                    ImGui::TableSetColumnIndex(4);
                    std::string title = task->title.empty() ? std::string(u8"Без названия") : task->title;
                    bool titleHovered = false;
                    if (state.modules.tasks) {
                        std::string label = title + "##profile_active_task_" + task->id;
                        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth)) {
                            FocusTasksForProfile(state, profileId, 0, task->id);
                        }
                        titleHovered = ImGui::IsItemHovered();
                    } else {
                        ImGui::TextUnformatted(title.c_str());
                        titleHovered = ImGui::IsItemHovered();
                    }
                    ImGui::TextDisabled("%s", ClassificationLabel(task->category));
                    if (task->deadlinePenaltyPercent > 0) {
                        ImGui::SameLine();
                        ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"-%d%%", task->deadlinePenaltyPercent);
                    }
                    if (!task->description.empty() && titleHovered) {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                        ImGui::TextUnformatted(task->description.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
                    ImGui::EndTable();
                }
            }
            ImGui::TextDisabled(u8"Показано: %d из %d", static_cast<int>(activeItems.size()), totalActiveCount);
        }
    }

    if (ImGui::CollapsingHeader(u8"Выполненные задачи")) {
        std::vector<const TaskEntry*> items;
        items.reserve(state.tasks.size());
        int pendingXpTasks = 0;
        for (const auto& task : state.tasks) {
            const bool hasParticipant = FindTaskParticipant(task, profileId) != nullptr;
            const bool awaitingXp = TaskNeedsXpAward(task) && TaskHasAssignee(task, profileId);
            if (awaitingXp) {
                pendingXpTasks += 1;
            }
            if (hasParticipant || awaitingXp) {
                items.push_back(&task);
            }
        }
        if (items.empty()) {
            if (DrawEmptyStateGui("profile_done_tasks_empty", UiIcon::Award,
                                  u8"История задач пуста",
                                  u8"Когда задачи будут закрыты или будут ждать XP, они появятся здесь.",
                                  UiIcon::Search, state.modules.tasks ? u8"Открыть журнал задач" : nullptr,
                                  state.modules.tasks)) {
                FocusTasksForProfile(state, profileId, 0);
            }
            return;
        }
        std::sort(items.begin(), items.end(), [](const TaskEntry* a, const TaskEntry* b) {
            return a->createdAt > b->createdAt;
        });
        if (items.size() > 5) {
            items.resize(5);
        }
        ImGui::TextDisabled(u8"Последние 5");
        if (pendingXpTasks > 0) {
            ImGui::SameLine();
            std::string pendingXpLabel = "XP:" + std::to_string(pendingXpTasks);
            DrawStatusPillGui("profile_done_tasks_pending_xp", pendingXpLabel.c_str(),
                              LogLevelColor(AppLogLevel::Warning),
                              u8"Задачи, закрытые без начисления XP.", true);
            if (state.modules.tasks) {
                ImGui::SameLine();
                if (IconButtonGui("profile_done_tasks_xp", UiIcon::Award,
                                  u8"Открыть задачи, ожидающие XP", LogLevelColor(AppLogLevel::Warning),
                                  ImGui::GetFrameHeight())) {
                    FocusTasksForProfile(state, profileId, 6);
                }
            }
        }
        {
            CompactTableScopeGui compactTable;
            if (ImGui::BeginTable("profile_tasks", 5, ControlTableFlagsGui(ImGuiTableFlags_RowBg))) {
            ImGui::TableSetupColumn(u8"Дата", ImGuiTableColumnFlags_WidthFixed, 86.0f);
            ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.35f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(u8"XP", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableHeadersRow();
            for (const TaskEntry* task : items) {
                const TaskParticipant* participant = FindTaskParticipant(*task, profileId);
                const bool awaitingXp = TaskNeedsXpAward(*task);
                if (!participant && !awaitingXp) continue;
                ImGui::TableNextRow();
                ApplyTableRowStateTintGui(false, awaitingXp);
                ImGui::TableSetColumnIndex(0);
                const std::string stamp = compactDeadline(task->createdAt);
                const std::string stampFull = fullDeadline(task->createdAt);
                ImGui::TextDisabled("%s", stamp.empty() ? "-" : stamp.c_str());
                if (ImGui::IsItemHovered() && !stampFull.empty()) {
                    ImGui::SetTooltip("%s", stampFull.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                const std::string projectName = ResolveTaskProjectName(state, *task);
                DrawClippedTextWithTooltip(projectName, u8"Без проекта", projectName.empty());
                ImGui::TableSetColumnIndex(2);
                DrawClippedTextWithTooltip(task->title.empty() ? std::string(u8"Без названия") : task->title);
                if (!task->description.empty() && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                    ImGui::TextUnformatted(task->description.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(3);
                if (participant) {
                    ImGui::Text("%d%%", participant->percent);
                } else {
                    DrawStatusPillGui("profile_done_task_waiting", u8"Ожид.",
                                      LogLevelColor(AppLogLevel::Warning),
                                      u8"Ожидает распределения вклада и начисления XP.", true);
                }
                ImGui::TableSetColumnIndex(4);
                if (awaitingXp) {
                    DrawTaskXpPendingBadge(*task);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(u8"XP будет назначен после завершения handoff через модуль опыта.");
                    }
                } else if (participant) {
                    ImGui::Text("%d", participant->globalXp);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(u8"Навыки: %d XP", participant->skillXp);
                    }
                }
            }
                ImGui::EndTable();
            }
        }
        ImGui::TextDisabled(u8"Показано: %d", static_cast<int>(items.size()));
    }
}
