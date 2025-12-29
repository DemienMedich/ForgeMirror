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
        const std::string progressLabel = std::to_string(progressXp) + " / " + std::to_string(xpNeededThisLevel);
        ImVec4 accent(0.92f, 0.74f, 0.45f, 1.0f);

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
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextColored(accent, u8"Уровень %d", overallLevel);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled(u8"Всего XP: %d", totalXp);
        ImGui::TextDisabled(u8"Прогресс уровня");
        ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f), progressLabel.c_str());
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::TextUnformatted(u8"Сводка профиля");
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
            ImGui::TextUnformatted(u8"Назначить ранг");
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

