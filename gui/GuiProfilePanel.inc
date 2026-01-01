static void DrawKpiCardIcon(const char* id, UiIcon icon, const char* title, const std::string& value,
                            const ImVec4& accent, float height = 72.0f) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rounding = std::max(10.0f, style.FrameRounding + 4.0f);
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    if (ImGui::BeginChild("kpi_card", ImVec2(0.0f, height), ImGuiChildFlags_Border,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float contentH = std::max(0.0f, ImGui::GetContentRegionAvail().y);
        const float badgeSize = std::clamp(height - 26.0f, 34.0f, 46.0f);
        const float badgeRadius = badgeSize * 0.5f;
        const ImVec2 badgePos = ImGui::GetCursorScreenPos();
        const ImVec2 badgeCenter(badgePos.x + badgeRadius, badgePos.y + contentH * 0.5f);
        DrawIconBadge(drawList, icon, badgeCenter, badgeRadius, accent, true);

        ImGui::Dummy(ImVec2(badgeSize + 10.0f, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        float y = ImGui::GetCursorPosY();
        float textBlock = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        float offsetY = std::max(0.0f, (contentH - textBlock) * 0.5f - 1.0f);
        ImGui::SetCursorPosY(y + offsetY);
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", title);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SetWindowFontScale(1.15f);
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

void DrawProfilePanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Profile details window
    ImGui::Begin(u8"Профиль");
    EnsureWindowVisible();
    DrawWindowBackground(state.ui, UiWindowId::Profile, state.storageDir);

    if (BeginCard("profile_select_card")) {
        DrawCardHeader(u8"Выбор профиля");
        const bool hasProfiles = !state.profiles.empty();
        if (!hasProfiles) {
            ImGui::TextDisabled(u8"Профили не найдены.");
        } else {
            std::string currentLabel = u8"(не выбран)";
            if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
                const auto& info = state.profiles[state.selectedIndex];
                currentLabel = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
            }

            const float avail = ImGui::GetContentRegionAvail().x;
            const bool stack = avail < 520.0f;
            if (stack) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##profile_select", currentLabel.c_str())) {
                    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                        const auto& info = state.profiles[i];
                        std::string label = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
                        const bool selected = state.selectedIndex == i;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            state.selectedIndex = i;
                            RefreshActiveProfile(state, storage, catalog);
                            SyncRankSelection(state);
                            SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button(u8"Профили...", ImVec2(-1.0f, 0.0f))) {
                    state.showProfilesWindow = true;
                }
                if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                    RefreshProfiles(state, storage, catalog);
                }
            } else if (ImGui::BeginTable("profile_select_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("select", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("open", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("refresh", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##profile_select", currentLabel.c_str())) {
                    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
                        const auto& info = state.profiles[i];
                        std::string label = "[" + info.id + "] " + info.name + (info.archived ? u8" (в архиве)" : "");
                        const bool selected = state.selectedIndex == i;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            state.selectedIndex = i;
                            RefreshActiveProfile(state, storage, catalog);
                            SyncRankSelection(state);
                            SetStatus(state, "", 0.6f, 0.7f, 1.0f);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Профили...", ImVec2(-1.0f, 0.0f))) {
                    state.showProfilesWindow = true;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                    RefreshProfiles(state, storage, catalog);
                }
                ImGui::EndTable();
            }
        }
    }
    EndCard();
    ImGui::Spacing();

    if (!state.active) {
        ImGui::TextDisabled(u8"Выберите профиль, чтобы увидеть детали.");
    } else {
        Profile& profile = state.active->profile;
        const int overallLevel = profile.overall_level();
        const int totalXp = profile.total_xp();
        const int progressXp = profile.level_progress();
        const int xpToNext = profile.xp_to_next_level();
        const int xpNeededThisLevel = progressXp + xpToNext;
        const float progressRatio = xpNeededThisLevel > 0 ? static_cast<float>(progressXp) / static_cast<float>(xpNeededThisLevel) : 0.0f;
        const std::int64_t nowSec = NowSeconds();
        const std::string progressLabel = std::to_string(progressXp) + " / " + std::to_string(xpNeededThisLevel);
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        accent.w = 1.0f;

        const std::string rankText = DescribeOverallRank(profile);
        const auto& catScores = profile.category_best_scores();
        std::string categoriesLine;
        {
            std::ostringstream catStream;
            for (size_t i = 0; i < catScores.size(); ++i) {
                if (i) catStream << ", ";
                catStream << Profile::kCategoryLabels[i] << "=" << catScores[i] << "/10";
            }
            categoriesLine = catStream.str();
        }
        if (BeginCard("profile_header_card")) {
        const float headerAvail = ImGui::GetContentRegionAvail().x;
        const float spacingX = ImGui::GetStyle().ItemSpacing.x;
        const float minLeftWidth = 240.0f;
        const float maxLeftWidth = 420.0f;
        const float minRightWidth = 240.0f;
        float leftWidth = std::clamp(headerAvail * 0.56f, minLeftWidth, maxLeftWidth);
        if (headerAvail - leftWidth - spacingX < minRightWidth) {
            leftWidth = std::max(0.0f, headerAvail - minRightWidth - spacingX);
        }
        ImGui::BeginChild("profile_header_left", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
        if (ImGui::BeginTable("profile_info", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Имя");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(profile.name().c_str());
            if (ImGui::GetContentRegionAvail().x > 120.0f) {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(u8"Копировать##profile_name")) {
                ImGui::SetClipboardText(profile.name().c_str());
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"ID");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(state.active->id.c_str());
            if (ImGui::GetContentRegionAvail().x > 120.0f) {
                ImGui::SameLine();
            }
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
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("profile_header_right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoScrollbar);
        const float ringAvail = ImGui::GetContentRegionAvail().x;
        const float ringSize = std::min(190.0f, ringAvail);
        if (ringSize >= 140.0f) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 chartSize(ringSize, ringSize);
            const float baseX = ImGui::GetCursorPosX();
            const float centerX = baseX + std::max(0.0f, (ringAvail - ringSize) * 0.5f);
            if (centerX > baseX) {
                ImGui::SetCursorPosX(centerX);
            }
            ImGui::Dummy(chartSize);
            const ImVec2 pMin = ImGui::GetItemRectMin();
            const ImVec2 pMax = ImGui::GetItemRectMax();
            const ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);

            float outerRadius = ringSize * 0.5f - 6.0f;
            float outerThickness = std::max(10.0f, outerRadius * 0.16f);
            float innerRadius = std::max(6.0f, outerRadius - outerThickness - 8.0f);
            float innerThickness = std::max(8.0f, outerThickness * 0.70f);

            if (!catScores.empty()) {
                std::vector<float> segValues;
                segValues.reserve(catScores.size());
                for (size_t i = 0; i < catScores.size(); ++i) {
                    segValues.push_back(static_cast<float>(catScores[i]));
                }
                DrawSegmentedRing(drawList, center, outerRadius, outerThickness,
                                  segValues.data(), static_cast<int>(segValues.size()), 10.0f, 0.12f);
            }

            ImVec4 track = style.Colors[ImGuiCol_TextDisabled];
            track.w = 0.18f;
            ImVec4 fill = accent;
            fill.w = 0.92f;
            DrawProgressRing(drawList, center, innerRadius, innerThickness, progressRatio,
                             ImGui::GetColorU32(track), ImGui::GetColorU32(fill));

            ImVec4 centerBg = style.Colors[ImGuiCol_FrameBg];
            centerBg.w = std::max(centerBg.w, 0.35f);
            float fillRadius = std::max(0.0f, innerRadius - innerThickness * 0.55f);
            drawList->AddCircleFilled(center, fillRadius, ImGui::GetColorU32(centerBg), 32);

            std::string levelText = std::to_string(overallLevel);
            std::ostringstream pctStream;
            pctStream.imbue(std::locale::classic());
            pctStream << std::fixed << std::setprecision(1) << (progressRatio * 100.0f);
            std::string pctText = pctStream.str() + "%";

            ImFont* font = ImGui::GetFont();
            const float fontBase = ImGui::GetFontSize();
            const float bigFont = fontBase * 1.55f;
            const float smallFont = fontBase * 0.95f;
            const ImVec2 levelSize = font->CalcTextSizeA(bigFont, std::numeric_limits<float>::max(), 0.0f, levelText.c_str());
            const ImVec2 pctSize = font->CalcTextSizeA(smallFont, std::numeric_limits<float>::max(), 0.0f, pctText.c_str());
            const ImVec2 levelPos(center.x - levelSize.x * 0.5f, center.y - levelSize.y * 0.65f);
            const ImVec2 pctPos(center.x - pctSize.x * 0.5f, center.y + levelSize.y * 0.05f);
            drawList->AddText(font, bigFont, levelPos, ImGui::GetColorU32(accent), levelText.c_str());
            drawList->AddText(font, smallFont, pctPos, ImGui::GetColorU32(style.Colors[ImGuiCol_TextDisabled]), pctText.c_str());

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            auto centeredTextDisabled = [&](const std::string& text) {
                const float w = ImGui::CalcTextSize(text.c_str()).x;
                ImGui::SetCursorPosX(baseX + std::max(0.0f, (ringAvail - w) * 0.5f));
                ImGui::TextDisabled("%s", text.c_str());
            };
            centeredTextDisabled(std::string(u8"Всего XP: ") + std::to_string(totalXp));
            centeredTextDisabled(std::string(u8"Прогресс: ") + progressLabel);
            ImGui::SetCursorPosX(baseX);
        } else {
            ImGui::SetWindowFontScale(1.25f);
            ImGui::TextColored(accent, u8"Уровень %d", overallLevel);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled(u8"Всего XP: %d", totalXp);
            ImGui::TextDisabled(u8"Прогресс уровня");
            ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f), progressLabel.c_str());
        }
        ImGui::EndChild();
        }
        EndCard();
        ImGui::Spacing();
        if (BeginCard("profile_summary_card")) {
        DrawCardHeader(u8"Сводка профиля");
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
            DrawKpiCardIcon("kpi_level", UiIcon::Chart, u8"Уровень", std::to_string(overallLevel), accent);
            ImGui::TableSetColumnIndex(1);
            DrawKpiCardIcon("kpi_rank", UiIcon::User, u8"Ранг", rankText, accent);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawKpiCardIcon("kpi_total", UiIcon::Chart, u8"Всего XP", std::to_string(totalXp), accent);
            ImGui::TableSetColumnIndex(1);
            DrawKpiCardIcon("kpi_bonus", UiIcon::Bell, u8"Активный бонус", bonusLabel, accent);
            ImGui::EndTable();
        }
        ImGui::TextDisabled(u8"Навыков: %d | Ачивок активных: %d / %d",
        skillCount, activeAchCount, achCount);
        ImGui::TextDisabled(u8"Выполнено задач: %d", profile.tasks_completed());
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
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
        }
        EndCard();
        ImGui::Spacing();

        state.profileSection = std::clamp(state.ui.profileTab, 0, 4);
        ImVec4 tab = BlendColor(style.Colors[ImGuiCol_Tab], accent, 0.20f);
        ImVec4 tabHover = BlendColor(style.Colors[ImGuiCol_TabHovered], accent, 0.25f);
        ImVec4 tabActive = BlendColor(style.Colors[ImGuiCol_TabSelected], accent, 0.30f);
        tab.w = std::max(tab.w, 0.75f);
        tabHover.w = std::max(tabHover.w, 0.85f);
        tabActive.w = std::max(tabActive.w, 0.95f);
        ImGui::PushStyleColor(ImGuiCol_Tab, tab);
        ImGui::PushStyleColor(ImGuiCol_TabHovered, tabHover);
        ImGui::PushStyleColor(ImGuiCol_TabSelected, tabActive);
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
        ImGui::PopStyleColor(3);

        if (state.profileSection == 0) {
            DrawProfileOverviewSection(state, storage, catalog, profile, overallLevel, nowSec);
        }

        if (state.profileSection == 1) {
            DrawProfileAchievementsSection(state, catalog, profile, nowSec);
        }

        if (state.isAdmin && ImGui::CollapsingHeader(u8"Администрирование", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& opts = RankOptions();
            std::vector<const char*> labels;
            labels.reserve(opts.size());
            for (const auto& o : opts) labels.push_back(o.label);
            ImGui::TextDisabled(u8"Назначить ранг");
            if (state.selectedRankIndex >= static_cast<int>(opts.size())) state.selectedRankIndex = 0;
            const float rankWidth = ImGui::GetContentRegionAvail().x;
            const bool stackRank = rankWidth < 360.0f;
            if (stackRank) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(),
                                 static_cast<int>(labels.size()))) {
                    state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
                }
                if (ImGui::Button(u8"Применить ранг", ImVec2(-1.0f, 0.0f))) {
                    const int newLevel = opts[state.selectedRankIndex].level;
                    state.active->profile.set_level_and_progress(newLevel, newLevel);
                    storage.save_profile(state.active->profile);
                    SetStatus(state, u8"Ранг обновлён.", 0.45f, 0.9f, 0.45f);
                }
            } else if (ImGui::BeginTable("profile_rank_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(),
                                 static_cast<int>(labels.size()))) {
                    state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Применить ранг", ImVec2(-1.0f, 0.0f))) {
                    const int newLevel = opts[state.selectedRankIndex].level;
                    state.active->profile.set_level_and_progress(newLevel, newLevel);
                    storage.save_profile(state.active->profile);
                    SetStatus(state, u8"Ранг обновлён.", 0.45f, 0.9f, 0.45f);
                }
                ImGui::EndTable();
            }
        }

        if (state.isAdmin && ImGui::CollapsingHeader(u8"Отчет", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto exportTxt = [&]() {
                std::filesystem::path outPath;
                if (ExportProfileReportTxt(state, catalog, profile, state.active->id, outPath)) {
                    std::string msg = u8"Отчет сохранен: ";
                    msg += outPath.string();
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                }
            };
            auto exportCsv = [&]() {
                std::filesystem::path outPath;
                if (ExportProfileReportCsv(state, catalog, profile, state.active->id, outPath)) {
                    std::string msg = u8"Отчет сохранен: ";
                    msg += outPath.string();
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                }
            };
            const float reportWidth = ImGui::GetContentRegionAvail().x;
            const bool stackReport = reportWidth < 360.0f;
            if (stackReport) {
                if (ImGui::Button(u8"Экспорт TXT", ImVec2(-1.0f, 0.0f))) {
                    exportTxt();
                }
                if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
                    exportCsv();
                }
            } else if (ImGui::BeginTable("profile_report_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Экспорт TXT", ImVec2(-1.0f, 0.0f))) {
                    exportTxt();
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
                    exportCsv();
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled(u8"Отчеты сохраняются в data/meta/reports.");
        }

        if (state.profileSection == 2) {
            DrawProfileSkillsSection(state, catalog, profile, nowSec);
        }

        if (state.profileSection == 3) {
            DrawProfileChartSection(state, catalog, profile);
        }

        if (state.profileSection == 4) {
            DrawProfileActivitySection(state, state.active->id);
        }

        ShowStatus(state);
    }
    ImGui::End();

}

