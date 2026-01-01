static void DrawProfileOverviewSection(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, Profile& profile, int overallLevel, std::int64_t nowSec) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 accentSoft = accent;
    accentSoft.w = 0.75f;
    {
        RankSpan span = ComputeRankSpan(overallLevel);
        if (span.hasNext && span.nextLevel > span.currentLevel) {
            const int levelsLeft = span.nextLevel - overallLevel;
            std::string nextRankText = std::string(u8"След. ранг: ") + span.nextLabel;
            std::string rankProgressText = u8"До следующего ранга: " + std::to_string(levelsLeft) + u8" ур.";
            const float rankProgress = static_cast<float>(overallLevel - span.currentLevel) /
            static_cast<float>(span.nextLevel - span.currentLevel);
            ImGui::TextUnformatted(nextRankText.c_str());
            ImGui::ProgressBar(std::clamp(rankProgress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), rankProgressText.c_str());
        } else {
            ImGui::TextUnformatted(u8"Ранг: максимум");
        }
    }

    if (ImGui::CollapsingHeader(u8"Топ навыков", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* topModes[] = {u8"По XP", u8"По уровню", u8"По весу"};
        const float topWidth = ImGui::GetContentRegionAvail().x;
        const bool stackTop = topWidth < 420.0f;
        if (stackTop) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"Рейтинг", &state.topSkillMode, topModes, IM_ARRAYSIZE(topModes));
        } else if (ImGui::BeginTable("top_skill_filters", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(u8"Рейтинг", &state.topSkillMode, topModes, IM_ARRAYSIZE(topModes));
            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::EndTable();
        }
        auto skills = profile.list_skills();
        if (skills.empty()) {
            ImGui::TextDisabled(u8"Навыков нет.");
        } else {
            const int totalSkills = static_cast<int>(skills.size());
            const int shownSkills = std::min(totalSkills, 5);
            if (stackTop) {
                ImGui::TextDisabled(u8"Показано: %d", shownSkills);
            } else if (ImGui::BeginTable("top_skill_count", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
                ImGui::TableNextColumn();
                ImGui::TextDisabled(u8"Показано: %d", shownSkills);
                ImGui::EndTable();
            }
            const int mode = std::clamp(state.topSkillMode, 0, 2);
            std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                if (mode == 1) {
                    if (a.level != b.level) return a.level > b.level;
                } else if (mode == 2) {
                    if (a.weight != b.weight) return a.weight > b.weight;
                } else {
                    int totalA = TotalSkillXpGui(a);
                    int totalB = TotalSkillXpGui(b);
                    if (totalA != totalB) return totalA > totalB;
                }
                return a.level > b.level;
            });
            if (skills.size() > 5) skills.resize(5);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
            if (ImGui::BeginTable("top_skills", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(u8"Навык");
                ImGui::TableSetupColumn(u8"Уровень", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(u8"Всего XP", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn(u8"Вес", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();
                for (const auto& skill : skills) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(catalog.display_name(skill.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", skill.level);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", TotalSkillXpGui(skill));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.2f", skill.weight);
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }
    }

    {
        constexpr std::int64_t kStaleThreshold = 30LL * 24 * 3600;
        const bool isStale = profile.last_task_timestamp() > 0 &&
        (nowSec - profile.last_task_timestamp()) > kStaleThreshold;
        int categoriesBelow = 0;
        const auto& catScores = profile.category_best_scores();
        for (size_t i = 0; i < catScores.size(); ++i) {
            if (catScores[i] < Profile::kMaxCategoryScore) {
                categoriesBelow++;
            }
        }
        const bool penaltiesEnabled = profile.penalties_enabled();
        bool hasRecovery = penaltiesEnabled && profile.recovery_tasks_remaining() > 0;
        if (ImGui::CollapsingHeader(u8"Индикаторы", ImGuiTreeNodeFlags_DefaultOpen)) {
            const int signals = (hasRecovery ? 1 : 0) + (categoriesBelow > 0 ? 1 : 0) + (isStale ? 1 : 0);
            ImGui::TextDisabled(u8"Активных сигналов: %d", signals);
            if (signals == 0) {
                ImGui::TextColored(accentSoft, u8"Все в норме.");
            } else {
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
                if (ImGui::BeginTable("profile_flags", 2, ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn(u8"Сигнал");
                ImGui::TableSetupColumn(u8"Статус");
                ImGui::TableHeadersRow();
                if (hasRecovery) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"Прогрев");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(u8"осталось: %d", profile.recovery_tasks_remaining());
                }
                if (categoriesBelow > 0) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Категории < 10/10");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(u8"шт: %d", categoriesBelow);
                }
                if (isStale) {
                    const std::int64_t lastTaskTs = profile.last_task_timestamp();
                    const int staleDays = lastTaskTs > 0
                    ? static_cast<int>((nowSec - lastTaskTs) / (24 * 3600))
                    : 0;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"Нет активности");
                    ImGui::TableSetColumnIndex(1);
                    if (staleDays > 0) {
                        ImGui::Text(u8"> %d дн.", staleDays);
                    } else {
                        ImGui::TextUnformatted(u8"> 30 дн.");
                    }
                }
                ImGui::EndTable();
                }
                ImGui::PopStyleVar();
            }
        }
    }

    if (ImGui::CollapsingHeader(u8"Состояние", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::int64_t lastTask = profile.last_task_timestamp();
        std::string lastTaskLabel = lastTask > 0
        ? FormatTimestamp(lastTask, "%Y-%m-%d %H:%M")
        : std::string(u8"нет данных");
        std::string elapsedLabel = lastTask > 0
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

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
        if (ImGui::BeginTable("profile_status", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn(u8"Параметр");
            ImGui::TableSetupColumn(u8"Статус");
            ImGui::TableSetupColumn(u8"Детали");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Активность");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(accentSoft, "%s", lastTaskLabel.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"%s назад", elapsedLabel.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Прогрев");
            ImGui::TableSetColumnIndex(1);
            if (recoveryLeft > 0) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Error), u8"активен");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text(u8"задач: %d", recoveryLeft);
            } else {
                ImGui::TextColored(accentSoft, u8"нет");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled(u8"-");
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Категории");
            ImGui::TableSetColumnIndex(1);
            if (categoriesBelow > 0) {
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"ниже 10/10");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text(u8"шт: %d", categoriesBelow);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextWrapped("%s", categoryAttention.c_str());
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::TextColored(accentSoft, u8"все 10/10");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled(u8"-");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    if (ImGui::CollapsingHeader(u8"Категории", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& catScores = profile.category_best_scores();
        for (size_t i = 0; i < catScores.size(); ++i) {
            const int score = catScores[i];
            const float ratio = static_cast<float>(score) / static_cast<float>(Profile::kMaxCategoryScore);
            std::string label = std::string(u8"Категория ") + Profile::kCategoryLabels[i];
            ImGui::TextUnformatted(label.c_str());
            std::string barText = std::to_string(score) + "/10";
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), barText.c_str());
        }
    }
    }

static void DrawProfileAchievementsSection(GuiState& state, SkillCatalog& catalog, Profile& profile, std::int64_t nowSec) {
    if (ImGui::CollapsingHeader(u8"Ачивки", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (profile.achievements().empty()) {
        ImGui::TextDisabled(u8"Ачивок нет.");
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
            if (ImGui::SmallButton(u8"Сброс##ach_filter")) {
                state.filters.achievement.fill('\0');
                state.showExpiredAchievements = true;
            }
        } else if (ImGui::BeginTable("ach_filters", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("ach_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("ach_toggle", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("ach_reset", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##ach_filter", u8"Фильтр ачивок",
            state.filters.achievement.data(), state.filters.achievement.size());
            ImGui::TableNextColumn();
            ImGui::Checkbox(u8"Показывать истекшие", &state.showExpiredAchievements);
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
                state.filters.achievement.fill('\0');
                state.showExpiredAchievements = true;
            }
            ImGui::EndTable();
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
        ImGui::TextDisabled(u8"Показано: %d | активных: %d | истекших: %d",
        visibleAch, activeCount, expiredCount);

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
            ImGui::Spacing();
        }

        constexpr int kIconsPerRow = 6;
        const float iconCell = 110.0f;
        int iconIndex = 0;
        if (visibleAch > 0) {
            ImGuiTableFlags gridFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
            if (ImGui::BeginTable("ach_grid", kIconsPerRow, gridFlags)) {
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
                    if (iconIndex % kIconsPerRow == 0) {
                        ImGui::TableNextRow();
                    }
                    ImGui::TableSetColumnIndex(iconIndex % kIconsPerRow);
                    ImGui::PushID(iconIndex);
                    bool hovered = false;
                    if (iconPath) {
                        if (const auto* iconTex = GetIconTexture(*iconPath)) {
                            const ImVec4 tint = active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                            : ImVec4(0.55f, 0.55f, 0.55f, 0.5f);
                            ImGui::ImageWithBg(ImTextureRef((ImTextureID)(intptr_t)iconTex->id),
                            ImVec2(iconCell, iconCell),
                            ImVec2(0, 0), ImVec2(1, 1),
                            ImVec4(0, 0, 0, 0), tint);
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
            ImGui::TextDisabled(u8"Нет ачивок по фильтру.");
        }
        ImGui::TextDisabled(u8"Показано: %d | активных: %d | истекших: %d",
        visibleAch, activeCount, expiredCount);
    }
    }
}

static void DrawProfileSkillsSection(GuiState& state, SkillCatalog& catalog, Profile& profile, std::int64_t nowSec) {
    if (ImGui::CollapsingHeader(u8"Навыки", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawCardHeader(u8"Фильтры");
    const float skillFilterWidth = ImGui::GetContentRegionAvail().x;
    const bool stackSkillFilters = skillFilterWidth < 620.0f;
    ImGui::InputText(u8"Поиск навыка в профиле", state.filters.profileSkill.data(),
    state.filters.profileSkill.size());
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
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Сортировка", &state.profileSkillSort, sortLabels, IM_ARRAYSIZE(sortLabels));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Категория (вес)", &state.profileSkillCategoryFilter,
        weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
        if (ImGui::SmallButton(u8"Сброс##skill_filters")) {
            state.filters.profileSkill.fill('\0');
            state.profileSkillCategoryFilter = 0;
            state.profileSkillWeightRange[0] = 0.0f;
            state.profileSkillWeightRange[1] = 2.0f;
        }
    } else if (ImGui::BeginTable("profile_skill_filters", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("profile_sort", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("profile_category", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("profile_reset", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Сортировка", &state.profileSkillSort, sortLabels, IM_ARRAYSIZE(sortLabels));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(u8"Категория (вес)", &state.profileSkillCategoryFilter,
        weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
        ImGui::TableNextColumn();
        if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
            state.filters.profileSkill.fill('\0');
            state.profileSkillCategoryFilter = 0;
            state.profileSkillWeightRange[0] = 0.0f;
            state.profileSkillWeightRange[1] = 2.0f;
        }
        ImGui::EndTable();
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
    ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
    if (ImGui::BeginTable("skill_table", 5, tableFlags)) {
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
            ++visibleCount;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
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
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", skill.level);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d / %d", skill.xp, skill.xpToNext);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", TotalSkillXpGui(skill));
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f", skill.weight);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, static_cast<int>(skills.size()));
    if (visibleCount == 0) {
        ImGui::TextDisabled(u8"Нет навыков по фильтру.");
    }
    }
}

static void DrawProfileChartSection(GuiState& state, SkillCatalog& catalog, Profile& profile) {
    if (ImGui::CollapsingHeader(u8"Диаграмма навыков", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto skills = state.active->profile.list_skills();
    std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
        return ToLowerUtf8(catalog.display_name(a.name)) < ToLowerUtf8(catalog.display_name(b.name));
    });
    ImVec2 radarSize(ImGui::GetContentRegionAvail().x, 320.0f);
    if (radarSize.x < 200.0f) radarSize.x = 200.0f;
    ImVec2 radarPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##skill_radar_canvas", radarSize);
    if (ImGui::IsItemVisible()) {
        for (auto& skill : skills) {
            skill.name = catalog.display_name(skill.name);
        }
        DrawSkillRadarChart(skills, radarPos, ImGui::GetItemRectSize(), ImGui::GetWindowDrawList());
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }
}

static void DrawProfileActivitySection(GuiState& state, const std::string& profileId) {
    if (ImGui::CollapsingHeader(u8"Последние действия", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginChild("profile_activity", ImVec2(0, 140), true)) {
        DrawCardHeader(u8"Фильтры");
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
            if (ImGui::SmallButton(u8"Сброс##activity_filter")) {
                state.filters.activity.fill('\0');
            }
            ImGui::TextDisabled(u8"Показано: %d", activityShown);
            if (ImGui::Button(u8"Экспорт", ImVec2(-1.0f, 0.0f))) {
                exportActivity();
            }
        } else if (ImGui::BeginTable("activity_filters", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("activity_filter", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("activity_reset", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("activity_count", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("activity_export", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(u8"##activity_filter", u8"Фильтр действий",
            state.filters.activity.data(), state.filters.activity.size());
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Сброс", ImVec2(-1.0f, 0.0f))) {
                state.filters.activity.fill('\0');
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Показано: %d", activityShown);
            ImGui::TableNextColumn();
            if (ImGui::Button(u8"Экспорт", ImVec2(-1.0f, 0.0f))) {
                exportActivity();
            }
            ImGui::EndTable();
        }
        DrawCardHeader(u8"История");
        if (hasLog) {
            for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                if (!MatchesFilter(*it, state.filters.activity.data())) continue;
                ImGui::BulletText("%s", it->c_str());
            }
            if (activityShown == 0) {
                ImGui::TextDisabled(u8"Нет действий по фильтру.");
            }
        } else {
            ImGui::TextDisabled(u8"Пока нет действий.");
        }
    }
    ImGui::EndChild();
    }
}
