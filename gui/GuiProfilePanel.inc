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

static void DrawProfileSignalCard(const char* id,
                                  UiIcon icon,
                                  const char* title,
                                  const std::string& value,
                                  const std::string& detail,
                                  const ImVec4& accent,
                                  float height = 90.0f) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rounding = std::max(10.0f, style.FrameRounding + 4.0f);
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    if (ImGui::BeginChild("profile_signal_card", ImVec2(0.0f, height), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float contentH = std::max(0.0f, ImGui::GetContentRegionAvail().y);
        const float badgeSize = std::clamp(height - 40.0f, 30.0f, 42.0f);
        const float badgeRadius = badgeSize * 0.5f;
        const ImVec2 badgePos = ImGui::GetCursorScreenPos();
        const ImVec2 badgeCenter(badgePos.x + badgeRadius, badgePos.y + contentH * 0.5f);
        DrawIconBadge(drawList, icon, badgeCenter, badgeRadius, accent, true);

        ImGui::Dummy(ImVec2(badgeSize + 10.0f, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", title);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::TextWrapped("%s", value.c_str());
        ImGui::PopStyleColor();
        if (!detail.empty()) {
            ImGui::TextDisabled("%s", detail.c_str());
        } else {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void DrawProfilePanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Profile, state.storageDir);

    if (BeginCard("profile_select_card")) {
        DrawCardHeader(u8"Выбор профиля");
        const bool hasProfiles = !state.profiles.empty();
        if (!hasProfiles) {
            ImGui::TextDisabled(u8"Профили не найдены.");
        } else {
            std::string currentLabel = u8"не выбран";
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
                    state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
                }
                if (ImGui::Button(u8"Обновить список", ImVec2(-1.0f, 0.0f))) {
                    RefreshProfiles(state, storage, catalog);
                }
            } else if (ImGui::BeginTable("profile_select_actions", 4, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("select_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("select", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("open", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("refresh", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
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
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Профили...", ImVec2(-1.0f, 0.0f))) {
                    state.showProfilesWindow = true;
                    state.workspaceTabRequest = static_cast<int>(UiWindowId::MainMenu);
                }
                ImGui::TableSetColumnIndex(3);
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
        if (state.profileLoading) {
            ImGui::TextDisabled(u8"Загружаем профиль...");
            const float pulse = ImFmod(static_cast<float>(ImGui::GetTime() * 0.35f), 1.0f);
            ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f));
        } else if (!state.profileLoadError.empty()) {
            ImGui::TextColored(LogLevelColor(AppLogLevel::Error), "%s", state.profileLoadError.c_str());
            ImGui::TextDisabled(u8"Обновите список профилей или выберите другой.");
        } else if (state.profiles.empty()) {
            ImGui::TextDisabled(u8"Профили не найдены.");
        } else {
            ImGui::TextDisabled(u8"Выберите профиль, чтобы увидеть детали.");
        }
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
        auto SpiritIconFor = [](ProfileSpirit spirit) {
            return spirit == ProfileSpirit::Evil ? UiIcon::SpiritEvil : UiIcon::SpiritGood;
        };
        auto SpiritColorFor = [&](ProfileSpirit spirit) {
            ImVec4 color = accent;
            if (spirit == ProfileSpirit::Evil) {
                color = LogLevelColor(AppLogLevel::Warning);
            } else if (spirit == ProfileSpirit::None) {
                color = style.Colors[ImGuiCol_TextDisabled];
            }
            color.w = std::max(color.w, 0.90f);
            return color;
        };
        auto DrawSpiritBadge = [&](const char* id, ProfileSpirit spirit) {
            if (spirit == ProfileSpirit::None) {
                ImGui::TextDisabled(u8"Без духа");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ProfileSpiritEffectLabel(spirit));
                return;
            }
            const ImVec4 color = SpiritColorFor(spirit);
            bool drewTexture = false;
            if (auto iconPath = ResolveSpiritIconPath(spirit, state.storageDir)) {
                if (const auto* iconTex = GetIconTexture(*iconPath)) {
                    const float iconSize = ImGui::GetFrameHeight();
                    ImGui::PushID(id);
                    ImGui::Image((ImTextureID)(intptr_t)iconTex->id, ImVec2(iconSize, iconSize));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ProfileSpiritEffectLabel(spirit));
                    ImGui::PopID();
                    drewTexture = true;
                }
            }
            if (!drewTexture) {
                IconButtonGui(id, SpiritIconFor(spirit), ProfileSpiritEffectLabel(spirit), color,
                              ImGui::GetFrameHeight(), true, true);
            }
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", ProfileSpiritLabel(spirit));
        };

        const std::string rankText = DescribeOverallRank(profile);
        const char* rankLabel = RankLabelForLevel(overallLevel);
        std::string professionName = u8"не задана";
        const std::string profId = profile.profession_id();
        if (!profId.empty()) {
            for (const auto& p : state.professions) {
                if (p.id == profId) {
                    professionName = p.name;
                    break;
                }
            }
            if (professionName == u8"не задана") {
                professionName = profId;
            }
        }
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
        const bool walletHasPassword = !profile.password_encoded().empty();
        const bool walletUnlocked = walletHasPassword && IsProfileAccessUnlocked(state, state.active->id);
        std::string walletCurrency = !state.vault.currencyName.empty() ? state.vault.currencyName : state.vault.currencyCode;
        if (walletCurrency.empty()) walletCurrency = u8"Кукоин";
        if (BeginCard("profile_header_card")) {
        if (ImGui::BeginTable("profile_header_summary", 5, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Уровень");
            ImGui::TextColored(accent, "%d", overallLevel);
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Ранг");
            ImGui::TextColored(accent, "%s", rankLabel ? rankLabel : u8"-");
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Прогресс");
            ImGui::TextColored(accent, "%s", progressLabel.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Профессия");
            ImGui::TextColored(accent, "%s", professionName.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled(u8"Дух");
            DrawSpiritBadge("profile_spirit_header", profile.spirit());
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0.0f, SectionSpacing()));
        ImGui::TextDisabled(u8"Режим");
        ImGui::SameLine();
        auto drawModeButton = [&](const char* label, int mode, const char* tooltip) {
            const bool selected = (state.ui.profileViewMode == mode);
            if (selected) {
                ImVec4 base = BlendColor(style.Colors[ImGuiCol_ButtonActive], accent, 0.35f);
                ImVec4 hover = BlendColor(style.Colors[ImGuiCol_ButtonHovered], accent, 0.30f);
                ImVec4 active = BlendColor(base, accent, 0.20f);
                base.w = std::max(base.w, 0.85f);
                hover.w = std::max(hover.w, 0.90f);
                active.w = std::max(active.w, 0.95f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
            }
            if (ImGui::Button(label)) {
                state.ui.profileViewMode = mode;
                state.uiDirty = true;
            }
            if (selected) {
                ImGui::PopStyleColor(3);
            }
            if (tooltip && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        };
        drawModeButton(u8"Обзор", 0, u8"Быстрый обзор состояния.");
        ImGui::SameLine();
        drawModeButton(u8"Аналитика", 1, u8"Детали, фильтры и история.");
        ImGui::SameLine();
        drawModeButton(u8"Фокус", 2, u8"Только ключевые блоки.");
        ImGui::TextDisabled(u8"XP - опыт: начисляется навыкам и повышает уровень.");
        ImGui::Separator();
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
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, TableCellPadding());
        if (ImGui::BeginTable("profile_info", 2, ImGuiTableFlags_SizingStretchProp)) {
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
            ImGui::TextDisabled(u8"Логин");
            ImGui::TableSetColumnIndex(1);
            std::string login = profile.login().empty() ? state.active->id : profile.login();
            ImGui::TextUnformatted(login.c_str());
            if (ImGui::GetContentRegionAvail().x > 120.0f) {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(u8"Копировать##profile_login")) {
                ImGui::SetClipboardText(login.c_str());
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Доступ");
            ImGui::TableSetColumnIndex(1);
            const bool hasPassword = !profile.password_encoded().empty();
            const bool unlocked = hasPassword && IsProfileAccessUnlocked(state, state.active->id);
            const bool sessionUnlock = hasPassword && state.unlockedProfiles.count(state.active->id) > 0;
            std::string accessText;
            if (!hasPassword) {
                accessText = u8"Пароль не задан";
            } else if (!unlocked) {
                accessText = u8"Закрыт";
            } else {
                auto it = state.trustedProfiles.find(state.active->id);
                if (it != state.trustedProfiles.end() && it->second > NowSeconds()) {
                    accessText = u8"Открыт до " + FormatTimestamp(it->second, "%Y-%m-%d");
                } else if (sessionUnlock) {
                    accessText = u8"Открыт (сессия)";
                } else {
                    accessText = u8"Открыт";
                }
            }
            ImVec4 accessColor = unlocked ? accent : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            if (!hasPassword) {
                accessColor = LogLevelColor(AppLogLevel::Warning);
                accessColor.w = std::max(accessColor.w, 0.9f);
            }
            ImGui::TextColored(accessColor, "%s", accessText.c_str());
            const float actionsWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionsWidth < 320.0f;
            auto openUnlock = [&]() {
                state.profileAuthTargetId = state.active->id;
                state.profileAuthTargetName = profile.name();
                std::fill(state.profilePasswordInput.begin(), state.profilePasswordInput.end(), '\0');
                state.profileUnlockPopupRequest = true;
            };
            auto openChange = [&]() {
                state.profileAuthTargetId = state.active->id;
                state.profileAuthTargetName = profile.name();
                std::fill(state.profilePasswordInput.begin(), state.profilePasswordInput.end(), '\0');
                std::fill(state.profilePasswordNew.begin(), state.profilePasswordNew.end(), '\0');
                std::fill(state.profilePasswordConfirm.begin(), state.profilePasswordConfirm.end(), '\0');
                state.profileChangePasswordRequest = true;
            };
            auto openReset = [&]() {
                state.profileResetTargetId = state.active->id;
                state.profileResetTargetName = profile.name();
                state.profileResetPassword = GenerateRandomPassword();
                state.profileResetPasswordRequest = true;
            };
            if (hasPassword && !unlocked) {
                if (!stackActions) ImGui::SameLine();
                if (ImGui::SmallButton(u8"Ввести пароль")) {
                    openUnlock();
                }
            }
            const char* changeLabel = hasPassword ? u8"Сменить пароль" : u8"Задать пароль";
            if (!stackActions) ImGui::SameLine();
            if (ImGui::SmallButton(changeLabel)) {
                openChange();
            }
            if (state.isAdmin) {
                if (!stackActions) ImGui::SameLine();
                if (ImGui::SmallButton(u8"Сбросить пароль")) {
                    openReset();
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Статус");
            ImGui::TableSetColumnIndex(1);
            const bool blocked = profile.is_blocked();
            ImVec4 statusColor = blocked ? LogLevelColor(AppLogLevel::Warning) : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            if (blocked) statusColor.w = std::max(statusColor.w, 0.9f);
            ImGui::TextColored(statusColor, "%s", blocked ? u8"Заблокирован" : u8"Активен");
            if (state.isAdmin) {
                const float statusWidth = ImGui::GetContentRegionAvail().x;
                const bool stackStatusActions = statusWidth < 220.0f;
                if (!stackStatusActions) ImGui::SameLine();
                const char* actionLabel = blocked ? u8"Разблокировать" : u8"Блокировать";
                if (ImGui::SmallButton(actionLabel)) {
                    state.confirmAction = blocked ? ConfirmAction::Unblock : ConfirmAction::Block;
                    state.confirmPopupRequest = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Дух");
            ImGui::TableSetColumnIndex(1);
            const char* spiritLabels[] = {u8"Без духа", u8"Добрый дух (+1% XP)", u8"Злой дух (-1% XP)"};
            int draftSpirit = static_cast<int>(profile.spirit());
            if (state.isAdmin) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##profile_spirit", &draftSpirit, spiritLabels, IM_ARRAYSIZE(spiritLabels))) {
                    draftSpirit = std::clamp(draftSpirit, 0, 2);
                    const ProfileSpirit oldSpirit = profile.spirit();
                    const ProfileSpirit nextSpirit = static_cast<ProfileSpirit>(draftSpirit);
                    if (oldSpirit != nextSpirit) {
                        Profile updatedProfile = profile;
                        updatedProfile.set_spirit(nextSpirit);
                        AppProfileMutationResult mutation = AppSaveProfileSnapshot(
                            storage, state.active->id, state.active->id, updatedProfile);
                        if (mutation.ok && mutation.profile) {
                            state.active->profile = *mutation.profile;
                            AppendProfileAudit(state.storageDir, state.active->id, "spirit",
                                               std::string(ProfileSpiritId(oldSpirit)) + "->" + ProfileSpiritId(nextSpirit));
                            SetStatus(state, u8"Дух профиля обновлён.", 0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state,
                                      mutation.errorMessage.empty() ? u8"Не удалось сохранить дух профиля." : mutation.errorMessage,
                                      1.0f, 0.45f, 0.45f);
                        }
                    }
                }
            } else {
                DrawSpiritBadge("profile_spirit_info", profile.spirit());
                if (profile.spirit() == ProfileSpirit::Evil) {
                    constexpr double kEvilSpiritRemovalCost = 200.0;
                    const bool enoughCoins = profile.wallet_balance() + 0.000001 >= kEvilSpiritRemovalCost;
                    const bool canRemoveSpirit = walletUnlocked && enoughCoins;
                    if (ImGui::GetContentRegionAvail().x > 170.0f) {
                        ImGui::SameLine();
                    }
                    if (!canRemoveSpirit) ImGui::BeginDisabled();
                    if (ImGui::SmallButton(u8"Снять за 200")) {
                        AppProfileMutationResult mutation = AppRemoveEvilSpiritForCoins(
                            storage, state.active->id, state.active->id, state.storageDir, state.vault, kEvilSpiritRemovalCost);
                        if (mutation.ok && mutation.profile) {
                            state.active->profile = *mutation.profile;
                            state.profileWalletCache[state.active->id] = mutation.profile->wallet_balance();
                            AppendProfileAudit(state.storageDir, state.active->id, "spirit_purchase", "evil->none cost=200");
                            SetStatus(state, u8"Злой дух снят. 200 Кукоинов отправлены в хранилище.", 0.45f, 0.9f, 0.45f);
                        } else {
                            SetStatus(state,
                                      mutation.errorMessage.empty() ? u8"Не удалось снять Злого духа." : mutation.errorMessage,
                                      1.0f, mutation.userError ? 0.75f : 0.45f, mutation.userError ? 0.35f : 0.45f);
                        }
                    }
                    if (!canRemoveSpirit) {
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            if (!walletUnlocked) {
                                ImGui::SetTooltip(u8"Нужно разблокировать свой профиль паролем.");
                            } else {
                                ImGui::SetTooltip(u8"Нужно 200 %s. Сейчас: %.0f.", walletCurrency.c_str(), profile.wallet_balance());
                            }
                        }
                        ImGui::EndDisabled();
                    } else if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(u8"Списать 200 %s и убрать дебафф -1%% XP.", walletCurrency.c_str());
                    }
                }
            }
            ImGui::TextDisabled("%s", ProfileSpiritEffectLabel(profile.spirit()));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(u8"Опыт");
            ImGui::TableSetColumnIndex(1);
            const bool xpEnabled = state.isAdmin;
            const bool emphasizeAddXp = xpEnabled && totalXp <= 0;
            if (!xpEnabled) ImGui::BeginDisabled();
            if (emphasizeAddXp) {
                ImVec4 addBg = accent;
                addBg.w = 0.18f;
                ImVec4 addHover = addBg;
                addHover.w = 0.28f;
                ImVec4 addActive = addBg;
                addActive.w = 0.38f;
                ImGui::PushStyleColor(ImGuiCol_Button, addBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, addHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, addActive);
            }
            if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, 0.0f))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            if (emphasizeAddXp) {
                ImGui::PopStyleColor(3);
            }
            if (!xpEnabled) {
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(u8"Доступно только администратору.");
                }
                ImGui::EndDisabled();
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
        const ProfileTaskOverview taskOverview =
            (state.modules.tasks && state.active) ? BuildProfileTaskOverview(state, state.active->id) : ProfileTaskOverview{};
        const RankSpan rankSpan = ComputeRankSpan(overallLevel);
        const auto& summaryCatScores = profile.category_best_scores();
        int weakestCategoryIndex = 0;
        int weakestCategoryScore = Profile::kMaxCategoryScore;
        for (size_t i = 0; i < summaryCatScores.size(); ++i) {
            if (summaryCatScores[i] <= weakestCategoryScore) {
                weakestCategoryScore = summaryCatScores[i];
                weakestCategoryIndex = static_cast<int>(i);
            }
        }
        const int recoveryLeft = profile.penalties_enabled() ? profile.recovery_tasks_remaining() : 0;
        std::string milestoneValue;
        std::string milestoneDetail;
        ImVec4 milestoneAccent = accent;
        if (recoveryLeft > 0) {
            milestoneValue = u8"Прогрев";
            milestoneDetail = u8"Осталось задач: " + std::to_string(recoveryLeft);
            milestoneAccent = LogLevelColor(AppLogLevel::Warning);
        } else if (rankSpan.hasNext && rankSpan.nextLevel > overallLevel) {
            milestoneValue = rankSpan.nextLabel;
            milestoneDetail = u8"Еще " + std::to_string(rankSpan.nextLevel - overallLevel) + u8" ур.";
        } else {
            milestoneValue = u8"Потолок ранга";
            milestoneDetail = u8"Сейчас важнее удерживать форму.";
            milestoneAccent = BlendColor(style.Colors[ImGuiCol_TextDisabled], accent, 0.25f);
        }

        std::string focusValue = u8"Свободное окно";
        std::string focusDetail = u8"Можно брать новую задачу.";
        ImVec4 focusAccent = accent;
        if (taskOverview.primaryTask) {
            focusValue = taskOverview.primaryTask->title.empty()
                ? std::string(u8"Без названия")
                : taskOverview.primaryTask->title;
            const std::string projectName = ResolveTaskProjectName(state, *taskOverview.primaryTask);
            const std::string pipelineName = ResolveTaskPipelineName(state, *taskOverview.primaryTask);
            focusDetail = projectName.empty() ? std::string(u8"Без проекта") : projectName;
            if (!pipelineName.empty()) {
                focusDetail += u8" · " + pipelineName;
            }
            if (taskOverview.primaryTask->deadlineAt > 0) {
                const std::string deadlineText = FormatTimestamp(taskOverview.primaryTask->deadlineAt, "%d.%m %H:%M");
                focusDetail += u8" · " + deadlineText;
                if (taskOverview.primaryTask->deadlineAt < nowSec) {
                    focusAccent = LogLevelColor(AppLogLevel::Warning);
                }
            }
        }

        std::string workloadValue = taskOverview.active > 0
            ? std::to_string(taskOverview.active) + u8" активных"
            : std::string(u8"Нагрузка свободна");
        std::ostringstream workloadDetail;
        workloadDetail.imbue(std::locale::classic());
        workloadDetail << u8"Н:" << taskOverview.created << u8" | В:" << taskOverview.inProgress;
        if (taskOverview.overdue > 0) {
            workloadDetail << u8" | П:" << taskOverview.overdue;
        }
        if (taskOverview.xpPending > 0) {
            workloadDetail << u8" | XP:" << taskOverview.xpPending;
        }
        ImVec4 workloadAccent = taskOverview.overdue > 0
            ? LogLevelColor(AppLogLevel::Warning)
            : accent;

        std::string balanceValue;
        std::string balanceDetail;
        ImVec4 balanceAccent = accent;
        if (weakestCategoryScore < Profile::kMaxCategoryScore) {
            balanceValue = std::string(u8"Категория ") + Profile::kCategoryLabels[weakestCategoryIndex];
            balanceDetail = std::to_string(weakestCategoryScore) + u8"/10, добрать до 10/10";
            if (weakestCategoryScore <= 6) {
                balanceAccent = LogLevelColor(AppLogLevel::Warning);
            }
        } else {
            balanceValue = u8"Баланс закрыт";
            balanceDetail = u8"Все категории уже на 10/10.";
            balanceAccent = BlendColor(style.Colors[ImGuiCol_TextDisabled], accent, 0.25f);
        }
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
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    const float signalAvail = ImGui::GetContentRegionAvail().x;
    const int signalColumns = signalAvail >= 1120.0f ? 4 : (signalAvail >= 620.0f ? 2 : 1);
    struct ProfileSignalCardData {
        const char* id;
        UiIcon icon;
        const char* title;
        std::string value;
        std::string detail;
        ImVec4 accent;
    };
    std::vector<ProfileSignalCardData> signalCards;
    signalCards.push_back({"profile_signal_milestone", UiIcon::Chart, u8"Следующий рубеж", milestoneValue, milestoneDetail, milestoneAccent});
    signalCards.push_back({"profile_signal_focus", UiIcon::Flow, u8"Рабочий фокус", focusValue, focusDetail, focusAccent});
    signalCards.push_back({"profile_signal_load", UiIcon::List, u8"Нагрузка", workloadValue, workloadDetail.str(), workloadAccent});
    signalCards.push_back({"profile_signal_balance", UiIcon::Bell, u8"Слабая зона", balanceValue, balanceDetail, balanceAccent});
    if (ImGui::BeginTable("profile_signal_cards", signalColumns, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < static_cast<int>(signalCards.size()); ++i) {
            if (i % signalColumns == 0) {
                ImGui::TableNextRow();
            }
            ImGui::TableSetColumnIndex(i % signalColumns);
            const auto& card = signalCards[static_cast<size_t>(i)];
            DrawProfileSignalCard(card.id, card.icon, card.title, card.value, card.detail, card.accent);
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::TextDisabled(u8"Быстрые действия");
    const float actionsAvail = ImGui::GetContentRegionAvail().x;
    const bool stackActions = actionsAvail < 640.0f;
    if (stackActions) {
        const bool xpEnabled = state.isAdmin;
        if (!xpEnabled) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, 0.0f))) {
            PrepareXpEntries(state, catalog);
            state.xpPopupRequest = true;
        }
        if (!xpEnabled) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(u8"Доступно только администратору.");
            }
            ImGui::EndDisabled();
        }
        if (state.modules.tasks) {
            const char* taskButtonLabel = taskOverview.primaryTask ? u8"Фокус-задача" : u8"К задачам";
            if (ImGui::Button(taskButtonLabel, ImVec2(-1.0f, 0.0f))) {
                if (taskOverview.primaryTask) {
                    FocusTasksForProfile(state, state.active->id, 0, taskOverview.primaryTask->id);
                } else {
                    FocusTasksForProfile(state, state.active->id);
                }
            }
        }
        if (ImGui::Button(u8"Графики и категории", ImVec2(-1.0f, 0.0f))) {
            state.ui.profileViewMode = 1;
            state.ui.profileTab = 3;
            state.profileSection = 3;
            state.profileTabRequest = 3;
            state.uiDirty = true;
        }
    } else if (ImGui::BeginTable("profile_quick_actions", state.modules.tasks ? 3 : 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool xpEnabled = state.isAdmin;
        if (!xpEnabled) ImGui::BeginDisabled();
        if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, 0.0f))) {
            PrepareXpEntries(state, catalog);
            state.xpPopupRequest = true;
        }
        if (!xpEnabled) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(u8"Доступно только администратору.");
            }
            ImGui::EndDisabled();
        }
        int actionColumn = 1;
        if (state.modules.tasks) {
            ImGui::TableSetColumnIndex(actionColumn++);
            const char* taskButtonLabel = taskOverview.primaryTask ? u8"Фокус-задача" : u8"К задачам";
            if (ImGui::Button(taskButtonLabel, ImVec2(-1.0f, 0.0f))) {
                if (taskOverview.primaryTask) {
                    FocusTasksForProfile(state, state.active->id, 0, taskOverview.primaryTask->id);
                } else {
                    FocusTasksForProfile(state, state.active->id);
                }
            }
        }
        ImGui::TableSetColumnIndex(actionColumn);
        if (ImGui::Button(u8"Графики и категории", ImVec2(-1.0f, 0.0f))) {
            state.ui.profileViewMode = 1;
            state.ui.profileTab = 3;
            state.profileSection = 3;
            state.profileTabRequest = 3;
            state.uiDirty = true;
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(u8"Навыков: %d | Ачивок активных: %d / %d",
    skillCount, activeAchCount, achCount);
    ImGui::TextDisabled(u8"Выполнено задач: %d", profile.tasks_completed());
    if (state.modules.tasks && state.active) {
        ImGui::TextDisabled(u8"Текущие задачи: %d | Н:%d | В:%d",
                            taskOverview.active, taskOverview.created, taskOverview.inProgress);
        if (taskOverview.overdue > 0) {
            ImGui::SameLine();
            ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"П:%d", taskOverview.overdue);
        }
        if (taskOverview.xpPending > 0) {
            ImGui::SameLine();
            ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), u8"XP:%d", taskOverview.xpPending);
        }
        if (taskOverview.primaryTask) {
            std::string focusTitle = taskOverview.primaryTask->title.empty()
                ? std::string(u8"Без названия")
                : taskOverview.primaryTask->title;
            if (taskOverview.primaryTask->deadlineAt > 0) {
                const std::string deadlineText = FormatTimestamp(taskOverview.primaryTask->deadlineAt, "%d.%m %H:%M");
                ImGui::TextDisabled(u8"Фокус: %s | %s", focusTitle.c_str(), deadlineText.c_str());
            } else {
                ImGui::TextDisabled(u8"Фокус: %s | без дедлайна", focusTitle.c_str());
            }
        }
        if (ImGui::GetContentRegionAvail().x > 140.0f) {
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(u8"К задачам")) {
            FocusTasksForProfile(state, state.active->id);
        }
    }
    if (state.isAdmin || walletUnlocked) {
        std::ostringstream walletStream;
        walletStream.imbue(std::locale::classic());
        walletStream << std::fixed << std::setprecision(0) << profile.wallet_balance();
        ImGui::TextDisabled(u8"Кошелек: %s %s", walletStream.str().c_str(), walletCurrency.c_str());
    } else {
        ImGui::TextDisabled(u8"Кошелек: скрыт (нужен пароль)");
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (state.modules.achievements) {
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
                                ImVec4 activeTint = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
                                activeTint.w = 1.0f;
                                ImVec4 inactiveTint = BlendColor(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], activeTint, 0.25f);
                                ImGui::ImageWithBg(ImTextureRef((ImTextureID)(intptr_t)iconTex->id),
                                ImVec2(iconSize, iconSize),
                                ImVec2(0, 0), ImVec2(1, 1),
                                ImVec4(0, 0, 0, 0), active ? activeTint : inactiveTint);
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
        } else {
            ImGui::TextDisabled(u8"Модуль ачивок отключён.");
        }
        }
        EndCard();
        ImGui::Spacing();

        const int viewMode = state.ui.profileViewMode;
        const bool overviewMode = (viewMode == 0);
        const bool focusMode = (viewMode == 2);
        auto moduleTabAllowed = [&](int index) {
            if (index == 1) return state.modules.achievements;
            if (index == 5) return state.modules.tasks;
            return true;
        };
        auto viewTabAllowed = [&](int index) {
            if (focusMode) return index == 0;
            if (overviewMode) return index == 0 || index == 3;
            return true;
        };
        auto tabAllowed = [&](int index) {
            return moduleTabAllowed(index) && viewTabAllowed(index);
        };
        if (!tabAllowed(state.ui.profileTab)) {
            state.ui.profileTab = 0;
            state.profileTabRequest = -1;
        }
        state.profileSection = std::clamp(state.ui.profileTab, 0, 5);
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
            struct ProfileTabInfo {
                const char* label;
                int index;
                bool allowOverview;
            };
            static const ProfileTabInfo tabs[] = {
                {u8"Обзор", 0, true},
                {u8"Ачивки", 1, false},
                {u8"Навыки", 2, false},
                {u8"Диаграмма", 3, true},
                {u8"Действия", 4, false},
                {u8"Задачи", 5, false}
            };
            auto tabFlagsFor = [&](int index) {
                return (state.profileTabRequest == index) ? ImGuiTabItemFlags_SetSelected : 0;
            };
            for (const auto& tabInfo : tabs) {
                if (!moduleTabAllowed(tabInfo.index)) {
                    continue;
                }
                const bool disableTab = !viewTabAllowed(tabInfo.index);
                if (disableTab) ImGui::BeginDisabled();
                if (ImGui::BeginTabItem(tabInfo.label, nullptr, tabFlagsFor(tabInfo.index))) {
                    state.profileSection = tabInfo.index;
                    state.ui.profileTab = tabInfo.index;
                    state.profileTabRequest = -1;
                    ImGui::EndTabItem();
                }
                if (disableTab) ImGui::EndDisabled();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor(3);

        if (state.profileSection == 0) {
            DrawProfileOverviewSection(state, storage, catalog, profile, overallLevel, nowSec);
        }

        if (state.profileSection == 1) {
            if (state.modules.achievements) {
                DrawProfileAchievementsSection(state, catalog, profile, nowSec);
            } else {
                ImGui::TextDisabled(u8"Модуль ачивок отключён.");
            }
        }

        if (state.isAdmin && ImGui::CollapsingHeader(u8"Администрирование")) {
            const auto& opts = RankOptions();
            std::vector<const char*> labels;
            labels.reserve(opts.size());
            for (const auto& o : opts) labels.push_back(o.label);
            ImGui::TextDisabled(u8"Назначить ранг");
            if (state.selectedRankIndex >= static_cast<int>(opts.size())) state.selectedRankIndex = 0;
            auto applyRank = [&]() {
                const int newLevel = opts[state.selectedRankIndex].level;
                AppProfileMutationResult mutation = AppAssignProfileLevel(
                    storage, state.active->id, state.active->id, newLevel, newLevel);
                if (!mutation.ok || !mutation.profile) {
                    SetStatus(state,
                              mutation.errorMessage.empty() ? u8"Не удалось обновить ранг." : mutation.errorMessage,
                              1.0f, 0.45f, 0.45f);
                    return;
                }
                state.active->profile = *mutation.profile;
                SetStatus(state, u8"Ранг обновлён.", 0.45f, 0.9f, 0.45f);
            };
            const float rankWidth = ImGui::GetContentRegionAvail().x;
            const bool stackRank = rankWidth < 360.0f;
            if (stackRank) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(),
                                 static_cast<int>(labels.size()))) {
                    state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
                }
                if (ImGui::Button(u8"Применить ранг", ImVec2(-1.0f, 0.0f))) {
                    applyRank();
                }
            } else if (ImGui::BeginTable("profile_rank_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("rank_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("rank_combo", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("rank_apply", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##rank_combo", &state.selectedRankIndex, labels.data(),
                                 static_cast<int>(labels.size()))) {
                    state.selectedRankIndex = std::clamp(state.selectedRankIndex, 0, static_cast<int>(opts.size()) - 1);
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Применить ранг", ImVec2(-1.0f, 0.0f))) {
                    applyRank();
                }
                ImGui::EndTable();
            }
        }
        if (state.isAdmin && ImGui::CollapsingHeader(u8"Отчет")) {
            auto exportTxt = [&]() {
                std::filesystem::path outPath;
                if (ExportProfileReportTxt(state, catalog, profile, state.active->id, outPath)) {
                    std::string msg = u8"Отчет сохранен: ";
                    msg += outPath.u8string();
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f);
                } else {
                    SetStatus(state, u8"Не удалось сохранить отчет.", 1.0f, 0.45f, 0.45f);
                }
            };
            auto exportCsv = [&]() {
                std::filesystem::path outPath;
                if (ExportProfileReportCsv(state, catalog, profile, state.active->id, outPath)) {
                    std::string msg = u8"Отчет сохранен: ";
                    msg += outPath.u8string();
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
            } else if (ImGui::BeginTable("profile_report_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("report_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("report_txt", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("report_csv", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                if (ImGui::Button(u8"Экспорт TXT", ImVec2(-1.0f, 0.0f))) {
                    exportTxt();
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
                    exportCsv();
                }
                ImGui::EndTable();
            }
            const std::string reportDir = (state.storageDir / "meta" / "reports").u8string();
            ImGui::TextDisabled(u8"Папка отчетов: %s", reportDir.c_str());
            if (ImGui::GetContentRegionAvail().x > 140.0f) {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(u8"Копировать путь")) {
                ImGui::SetClipboardText(reportDir.c_str());
            }
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

        if (state.profileSection == 5) {
            if (state.modules.tasks) {
                DrawProfileTasksSection(state, state.active->id);
            } else {
                ImGui::TextDisabled(u8"Модуль задач отключён.");
            }
        }

        ShowStatus(state);
    }
}
