void DrawProfilePanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Profile details window
    ImGui::Begin(u8"Профиль");
    EnsureWindowVisible();
    DrawWindowBackground(state.ui, UiWindowId::Profile, state.storageDir);
    if (!state.active) {
        ImGui::TextUnformatted(u8"Выберите профиль, чтобы увидеть детали.");
    } else {
        Profile& profile = state.active->profile;
        const int overallLevel = profile.overall_level();
        const int totalXp = profile.total_xp();
        const int progressXp = profile.level_progress();
        const int xpToNext = profile.xp_to_next_level();
        const int xpNeededThisLevel = progressXp + xpToNext;
        const float progressRatio = xpNeededThisLevel > 0 ? static_cast<float>(progressXp) / static_cast<float>(xpNeededThisLevel) : 0.0f;
        const std::int64_t nowSec = NowSeconds();
        const std::string levelText = u8"Уровень " + std::to_string(overallLevel);
        const std::string progressLabel = std::to_string(progressXp) + " / " + std::to_string(xpNeededThisLevel);

        const float levelScale = 1.8f;
        ImVec2 headerStartScreen = ImGui::GetCursorScreenPos();
        ImVec2 levelSize = ImGui::CalcTextSize(levelText.c_str());
        float scaledWidth = levelSize.x * levelScale;
        ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
        ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        ImVec2 windowPos = ImGui::GetWindowPos();
        float rightEdge = windowPos.x + contentMax.x;
        float leftEdge = windowPos.x + contentMin.x;
        float levelX = rightEdge - scaledWidth - ImGui::GetStyle().ItemInnerSpacing.x;
        if (levelX < leftEdge) levelX = leftEdge;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * levelScale,
        ImVec2(levelX, headerStartScreen.y),
        ImGui::GetColorU32(ImGuiCol_Text), levelText.c_str());

        const std::string rankText = DescribeOverallRank(state.active->profile);
        std::string categoriesLine;
        {
            const auto& catScores = state.active->profile.category_best_scores();
            std::ostringstream catStream;
            for (size_t i = 0; i < catScores.size(); ++i) {
                if (i) catStream << ", ";
                catStream << Profile::kCategoryLabels[i] << "=" << catScores[i] << "/10";
            }
            categoriesLine = catStream.str();
        }
        if (ImGui::BeginTable("profile_info", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Имя");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(profile.name().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Копировать##profile_name")) {
                ImGui::SetClipboardText(profile.name().c_str());
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"ID");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(state.active->id.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Копировать##profile_id")) {
                ImGui::SetClipboardText(state.active->id.c_str());
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Ранг");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(rankText.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Категории");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", categoriesLine.c_str());
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::TextUnformatted(u8"Сводка профиля");
        ImVec4 accent(0.9f, 0.85f, 0.3f, 1.0f);
        const int skillCount = static_cast<int>(profile.list_skills().size());
        const int achCount = static_cast<int>(profile.achievements().size());
        int activeAchCount = 0;
        double activeBonus = 0.0;
        for (const auto& ach : profile.achievements()) {
            if (ach.is_active(nowSec)) {
                activeAchCount += 1;
                activeBonus += ach.bonusPercent;
            }
        }
        std::ostringstream bonusStream;
        bonusStream.imbue(std::locale::classic());
        bonusStream << std::fixed << std::setprecision(1) << activeBonus;
        std::string bonusLabel = activeBonus > 0.0
        ? ("+" + bonusStream.str() + "%")
        : std::string("0%");
        if (ImGui::BeginTable("profile_kpi", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawKpiCard("kpi_level", u8"Уровень", std::to_string(overallLevel), accent);
            ImGui::TableSetColumnIndex(1);
            DrawKpiCard("kpi_rank", u8"Ранг", rankText, accent);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawKpiCard("kpi_total", u8"Всего XP", std::to_string(totalXp), accent);
            ImGui::TableSetColumnIndex(1);
            DrawKpiCard("kpi_bonus", u8"Активный бонус", bonusLabel, accent);
            ImGui::EndTable();
        }
        ImGui::TextDisabled(u8"Навыков: %d • Ачивок активных: %d / %d",
        skillCount, activeAchCount, achCount);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled(u8"Прогресс уровня");
        ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f), progressLabel.c_str());
        if (!profile.achievements().empty()) {
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
            std::vector<const Achievement*> recent;
            recent.reserve(profile.achievements().size());
            for (const auto& ach : profile.achievements()) {
                recent.push_back(&ach);
            }
            std::sort(recent.begin(), recent.end(), [](const Achievement* a, const Achievement* b) {
                if (a->awardedAt != b->awardedAt) return a->awardedAt > b->awardedAt;
                return a->title < b->title;
            });
            const int showCount = std::min<int>(5, static_cast<int>(recent.size()));
            if (showCount > 0) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextDisabled(u8"Последние ачивки");
                const float iconSize = 56.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float totalWidth = showCount * iconSize + (showCount - 1) * spacing;
                const float avail = ImGui::GetContentRegionAvail().x;
                if (avail > totalWidth) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - totalWidth) * 0.5f);
                }
                for (int i = 0; i < showCount; ++i) {
                    const Achievement& a = *recent[i];
                    const bool active = a.is_active(nowSec);
                    const std::string achSkillName = catalog.display_name(a.skill);
                    const std::string expires = formatDate(a.expiresAt);
                    const auto iconPath = ResolveIconPath(a.icon, state.storageDir);
                    ImGui::PushID(i);
                    bool hovered = false;
                    if (iconPath) {
                        if (const auto* iconTex = GetIconTexture(*iconPath)) {
                            const ImVec4 tint = active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                            : ImVec4(0.55f, 0.55f, 0.55f, 0.5f);
                            ImGui::ImageWithBg(ImTextureRef((ImTextureID)(intptr_t)iconTex->id),
                            ImVec2(iconSize, iconSize),
                            ImVec2(0, 0), ImVec2(1, 1),
                            ImVec4(0, 0, 0, 0), tint);
                            hovered = ImGui::IsItemHovered();
                        } else {
                            ImGui::BeginDisabled();
                            ImGui::Button("?", ImVec2(iconSize, iconSize));
                            ImGui::EndDisabled();
                            hovered = ImGui::IsItemHovered();
                        }
                    } else {
                        ImGui::BeginDisabled();
                        ImGui::Button("?", ImVec2(iconSize, iconSize));
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
                    if (i + 1 < showCount) {
                        ImGui::SameLine();
                    }
                }
            }
        }

        state.profileSection = std::clamp(state.ui.profileTab, 0, 4);
        if (ImGui::BeginTabBar("profile_tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
            auto tabFlagsFor = [&](int index) {
                return (state.profileTabRequest == index) ? ImGuiTabItemFlags_SetSelected : 0;
            };
            if (ImGui::BeginTabItem(u8"Обзор", nullptr, tabFlagsFor(0))) {
                state.profileSection = 0;
                state.ui.profileTab = 0;
                state.profileTabRequest = -1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"Ачивки", nullptr, tabFlagsFor(1))) {
                state.profileSection = 1;
                state.ui.profileTab = 1;
                state.profileTabRequest = -1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"Навыки", nullptr, tabFlagsFor(2))) {
                state.profileSection = 2;
                state.ui.profileTab = 2;
                state.profileTabRequest = -1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"Диаграмма", nullptr, tabFlagsFor(3))) {
                state.profileSection = 3;
                state.ui.profileTab = 3;
                state.profileTabRequest = -1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"Действия", nullptr, tabFlagsFor(4))) {
                state.profileSection = 4;
                state.ui.profileTab = 4;
                state.profileTabRequest = -1;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (state.profileSection == 0) {
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
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo(u8"Рейтинг", &state.topSkillMode, topModes, IM_ARRAYSIZE(topModes));
                auto skills = profile.list_skills();
                if (skills.empty()) {
                    ImGui::TextUnformatted(u8"Навыков нет.");
                } else {
                    const int totalSkills = static_cast<int>(skills.size());
                    const int shownSkills = std::min(totalSkills, 5);
                    ImGui::SameLine();
                    ImGui::TextDisabled(u8"Показано: %d", shownSkills);
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
                bool hasRecovery = profile.recovery_tasks_remaining() > 0;
                if (ImGui::CollapsingHeader(u8"Индикаторы", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const int signals = (hasRecovery ? 1 : 0) + (categoriesBelow > 0 ? 1 : 0) + (isStale ? 1 : 0);
                    ImGui::TextDisabled(u8"Активных сигналов: %d", signals);
                    if (signals == 0) {
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), u8"Все в норме.");
                    } else if (ImGui::BeginTable("profile_flags", 2, ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_BordersInnerV)) {
                        ImGui::TableSetupColumn(u8"Сигнал");
                        ImGui::TableSetupColumn(u8"Статус");
                        ImGui::TableHeadersRow();
                        if (hasRecovery) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), u8"Прогрев");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text(u8"осталось: %d", profile.recovery_tasks_remaining());
                        }
                        if (categoriesBelow > 0) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), u8"Категории < 10/10");
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
                            ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f), u8"Нет активности");
                            ImGui::TableSetColumnIndex(1);
                            if (staleDays > 0) {
                                ImGui::Text(u8"> %d дн.", staleDays);
                            } else {
                                ImGui::TextUnformatted(u8"> 30 дн.");
                            }
                        }
                        ImGui::EndTable();
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
                const int recoveryLeft = profile.recovery_tasks_remaining();

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

                if (ImGui::BeginTable("profile_status", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn(u8"Параметр");
                    ImGui::TableSetupColumn(u8"Статус");
                    ImGui::TableSetupColumn(u8"Детали");
                    ImGui::TableHeadersRow();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Активность");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", lastTaskLabel.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled(u8"%s назад", elapsedLabel.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Прогрев");
                    ImGui::TableSetColumnIndex(1);
                    if (recoveryLeft > 0) {
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), u8"активен");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text(u8"задач: %d", recoveryLeft);
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), u8"нет");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(u8"-");
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(u8"Категории");
                    ImGui::TableSetColumnIndex(1);
                    if (categoriesBelow > 0) {
                        ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), u8"ниже 10/10");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text(u8"шт: %d", categoriesBelow);
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextWrapped("%s", categoryAttention.c_str());
                            ImGui::EndTooltip();
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), u8"все 10/10");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(u8"-");
                    }
                    ImGui::EndTable();
                }
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

        if (state.profileSection == 1) if (ImGui::CollapsingHeader(u8"Ачивки", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (profile.achievements().empty()) {
                ImGui::TextUnformatted(u8"Ачивок нет.");
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
                ImGui::InputTextWithHint(u8"##ach_filter", u8"Фильтр ачивок",
                state.filters.achievement.data(), state.filters.achievement.size());
                ImGui::SameLine();
                ImGui::Checkbox(u8"Показывать истекшие", &state.showExpiredAchievements);
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Сброс##ach_filter")) {
                    state.filters.achievement.fill('\0');
                    state.showExpiredAchievements = true;
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
                    ImGui::TextUnformatted(u8"Скоро истекают");
                    const size_t limit = std::min<size_t>(3, expiringSoon.size());
                    for (size_t i = 0; i < limit; ++i) {
                        ImGui::BulletText("%s — %s",
                        expiringSoon[i].first.c_str(),
                        FormatDurationShort(expiringSoon[i].second).c_str());
                    }
                    if (expiringSoon.size() > limit) {
                        ImGui::TextDisabled(u8"И ещё: %d", static_cast<int>(expiringSoon.size() - limit));
                    }
                    ImGui::Separator();
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
                    ImGui::TextUnformatted(u8"Нет ачивок по фильтру.");
                }
                ImGui::TextDisabled(u8"Показано: %d | активных: %d | истекших: %d",
                visibleAch, activeCount, expiredCount);
            }
        }

        if (state.isAdmin && ImGui::CollapsingHeader(u8"Администрирование", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& opts = RankOptions();
            std::vector<const char*> labels;
            labels.reserve(opts.size());
            for (const auto& o : opts) labels.push_back(o.label);
            ImGui::TextUnformatted(u8"Назначить ранг");
            if (state.selectedRankIndex >= static_cast<int>(opts.size())) state.selectedRankIndex = 0;
            if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(), static_cast<int>(labels.size()))) {
                state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Применить ранг")) {
                const int newLevel = opts[state.selectedRankIndex].level;
                state.active->profile.set_level_and_progress(newLevel, newLevel);
                storage.save_profile(state.active->profile);
                SetStatus(state, u8"Ранг обновлён.", 0.45f, 0.9f, 0.45f);
            }
        }

        if (state.isAdmin && ImGui::CollapsingHeader(u8"Отчет", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button(u8"Экспорт TXT")) {
                std::filesystem::path outPath;
                if (ExportProfileReportTxt(state, catalog, profile, state.active->id, outPath)) {
                    std::string msg = u8"Отчет сохранен: ";
                    msg += outPath.string();
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Экспорт CSV")) {
                std::filesystem::path outPath;
                if (ExportProfileReportCsv(state, catalog, profile, state.active->id, outPath)) {
                    std::string msg = u8"Отчет сохранен: ";
                    msg += outPath.string();
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                }
            }
            ImGui::TextDisabled(u8"Отчеты сохраняются в data/meta/reports.");
        }

        if (state.profileSection == 2) if (ImGui::CollapsingHeader(u8"Навыки", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText(u8"Поиск навыка в профиле", state.filters.profileSkill.data(),
            state.filters.profileSkill.size());
            const char* sortLabels[] = {u8"По имени", u8"По уровню", u8"По XP", u8"По весу"};
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo(u8"Сортировка", &state.profileSkillSort, sortLabels, IM_ARRAYSIZE(sortLabels));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            const char* weightCategoryLabels[] = {
                u8"Все",
                u8"A (>=1.30)",
                u8"B (1.10-1.29)",
                u8"C (0.90-1.09)",
                u8"D (0.70-0.89)",
                u8"E (<0.70)"
            };
            ImGui::Combo(u8"Категория (вес)", &state.profileSkillCategoryFilter,
            weightCategoryLabels, IM_ARRAYSIZE(weightCategoryLabels));
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Сброс##skill_filters")) {
                state.filters.profileSkill.fill('\0');
                state.profileSkillCategoryFilter = 0;
                state.profileSkillWeightRange[0] = 0.0f;
                state.profileSkillWeightRange[1] = 2.0f;
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
                const std::string nameA = catalog.display_name(a.name);
                const std::string nameB = catalog.display_name(b.name);
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
            ImGui::TextDisabled(u8"Показано: %d из %d", visibleCount, static_cast<int>(skills.size()));
            if (visibleCount == 0) {
                ImGui::TextDisabled(u8"Нет навыков по фильтру.");
            }
        }

        if (state.profileSection == 3) if (ImGui::CollapsingHeader(u8"Диаграмма навыков", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto skills = state.active->profile.list_skills();
            std::sort(skills.begin(), skills.end(), [&](const Skill& a, const Skill& b) {
                return catalog.display_name(a.name) < catalog.display_name(b.name);
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

        if (state.profileSection == 4) if (ImGui::CollapsingHeader(u8"Последние действия", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginChild("profile_activity", ImVec2(0, 140), true)) {
                const auto logIt = state.activityLogs.find(state.active->id);
                const bool hasLog = logIt != state.activityLogs.end() && !logIt->second.empty();
                ImGui::InputTextWithHint(u8"##activity_filter", u8"Фильтр действий",
                state.filters.activity.data(), state.filters.activity.size());
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"Сброс##activity_filter")) {
                    state.filters.activity.fill('\0');
                }
                int activityShown = 0;
                if (hasLog) {
                    for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                        if (!MatchesFilter(*it, state.filters.activity.data())) continue;
                        activityShown += 1;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled(u8"Показано: %d", activityShown);
                ImGui::SameLine();
                if (ImGui::Button(u8"Экспорт")) {
                    if (hasLog) {
                        std::filesystem::path outPath;
                        if (ExportProfileActivity(state, state.active->id, logIt->second,
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
                }
                ImGui::Separator();
                if (hasLog) {
                    for (auto it = logIt->second.rbegin(); it != logIt->second.rend(); ++it) {
                        if (!MatchesFilter(*it, state.filters.activity.data())) continue;
                        ImGui::BulletText("%s", it->c_str());
                    }
                    if (activityShown == 0) {
                        ImGui::TextUnformatted(u8"Нет действий по фильтру.");
                    }
                } else {
                    ImGui::TextUnformatted(u8"Пока нет действий.");
                }
            }
            ImGui::EndChild();
        }

        ShowStatus(state);
    }
    ImGui::End();

}

