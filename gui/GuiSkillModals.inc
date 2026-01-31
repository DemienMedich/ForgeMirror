void DrawSkillCatalogModals(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    // Skill Catalog modals
    if (state.addSkillPopupRequest) {
        ImGui::OpenPopup(u8"Добавить навык");
        state.addSkillPopupRequest = false;
    }
    bool addSkillOpen = true;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float minWidth = 360.0f;
    const float maxWidth = std::max(minWidth, std::min(560.0f, display.x - 80.0f));
    const float maxHeight = std::max(240.0f, display.y - 80.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, 0.0f), ImVec2(maxWidth, maxHeight));
    if (ImGui::BeginPopupModal(u8"Добавить навык", &addSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawCardHeader(u8"Параметры навыка");
        ImGui::TextDisabled(u8"Название и описание обязательны.");
        ImGui::TextDisabled(u8"Название");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText(u8"##skill_name", state.newSkillName.data(), state.newSkillName.size());
        ImGui::TextDisabled(u8"Вес (0.5 - 1.6)");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputFloat(u8"##skill_weight", &state.newSkillWeight, 0.05f, 0.2f, "%.2f");
        ImGui::TextDisabled(u8"Профессии (можно несколько)");
        ImGui::TextDisabled(u8"Если не выбрано — навык доступен всем.");
        state.newSkillProfessionFlags.resize(state.professions.size(), 0);
        if (state.professions.empty()) {
            ImGui::TextDisabled(u8"Профессии не заданы.");
            ImGui::TextDisabled(u8"Навык будет общим.");
        } else {
            for (size_t i = 0; i < state.professions.size(); ++i) {
                bool checked = state.newSkillProfessionFlags[i] != 0;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Checkbox("##skill_prof_multi", &checked)) {
                    state.newSkillProfessionFlags[i] = checked ? 1 : 0;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(state.professions[i].name.c_str());
                ImGui::PopID();
            }
        }
        ImGui::TextDisabled(u8"Описание");
        ImGui::InputTextMultiline(u8"##skill_desc", state.newSkillDesc.data(), state.newSkillDesc.size(), ImVec2(-1.0f, 120.0f));
        auto saveSkill = [&]() {
            std::vector<std::string> profIds;
            profIds.reserve(state.professions.size());
            for (size_t i = 0; i < state.professions.size(); ++i) {
                if (i < state.newSkillProfessionFlags.size() && state.newSkillProfessionFlags[i]) {
                    profIds.push_back(state.professions[i].id);
                }
            }
            AddSkillResult result = AddSkillAction(catalog, state.newSkillName.data(),
            static_cast<double>(state.newSkillWeight),
            state.newSkillDesc.data(),
            std::string{},
            profIds);
            AppLogLevel level = LogLevelForResult(result);
            SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
            result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Навыки");
            if (result.ok) {
                PrepareXpEntries(state, catalog);
                std::string keepId = state.active ? state.active->id : std::string{};
                RefreshProfiles(state, storage, catalog, keepId);
                if (!result.id.empty()) {
                    const auto& skills = catalog.skills();
                    for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                        if (skills[i] == result.id) {
                            state.selectedCatalogIndex = i;
                            break;
                        }
                    }
                }
                ImGui::CloseCurrentPopup();
                addSkillOpen = false;
            }
        };
        const float actionWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionWidth < 300.0f;
        if (stackActions) {
            if (ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                saveSkill();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                addSkillOpen = false;
            }
        } else if (ImGui::BeginTable("add_skill_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            if (ImGui::Button(u8"Сохранить", ImVec2(-1.0f, 0.0f))) {
                saveSkill();
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                addSkillOpen = false;
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    if (!addSkillOpen && ImGui::IsPopupOpen(u8"Добавить навык")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.deleteSkillPopupRequest) {
        ImGui::OpenPopup(u8"Удалить навык");
        state.deleteSkillPopupRequest = false;
    }
    bool deleteSkillOpen = true;
    if (ImGui::BeginPopupModal(u8"Удалить навык", &deleteSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!state.pendingSkillDelete.empty()) {
            const std::string displayName = catalog.display_name(state.pendingSkillDelete);
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImGui::TextColored(accent, u8"Удалить навык '%s' из каталога?", displayName.c_str());
            auto deleteSkill = [&]() {
                bool removedFromProfiles = false;
                ActionResult result = RemoveSkillAction(storage, catalog, state.pendingSkillDelete,
                state.active ? state.active->id : std::string{},
                removedFromProfiles);
                AppLogLevel level = LogLevelForResult(result);
                SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Навыки");
                if (result.ok) {
                    PrepareXpEntries(state, catalog);
                    std::string keepId = state.active ? state.active->id : std::string{};
                    RefreshProfiles(state, storage, catalog, keepId);
                    if (state.selectedCatalogIndex >= static_cast<int>(catalog.skills().size())) {
                        state.selectedCatalogIndex = static_cast<int>(catalog.skills().size()) - 1;
                    }
                }
                ImGui::CloseCurrentPopup();
                deleteSkillOpen = false;
                state.pendingSkillDelete.clear();
            };
            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 300.0f;
            if (stackActions) {
                if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, 0.0f))) {
                    deleteSkill();
                }
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                    state.pendingSkillDelete.clear();
                }
            } else if (ImGui::BeginTable("delete_skill_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, 0.0f))) {
                    deleteSkill();
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    deleteSkillOpen = false;
                    state.pendingSkillDelete.clear();
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled(u8"Навык не выбран.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                deleteSkillOpen = false;
            }
        }
        ImGui::EndPopup();
    }
    if (!deleteSkillOpen && ImGui::IsPopupOpen(u8"Удалить навык")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.mergeSkillPopupRequest) {
        ImGui::OpenPopup(u8"Слияние навыков");
        state.mergeSkillPopupRequest = false;
    }
    bool mergeSkillOpen = true;
    if (ImGui::BeginPopupModal(u8"Слияние навыков", &mergeSkillOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!state.pendingMergeFromId.empty() && !state.pendingMergeToId.empty()) {
            const std::string fromName = catalog.display_name(state.pendingMergeFromId);
            const std::string toName = catalog.display_name(state.pendingMergeToId);
            ImGui::Text(u8"Навык '%s' уже существует.", toName.c_str());
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImGui::TextColored(accent, u8"Слить опыт из '%s' в '%s'?", fromName.c_str(), toName.c_str());
            auto mergeSkills = [&]() {
                std::string mergeCategory = catalog.category(state.pendingMergeToId);
                if (mergeCategory.empty()) {
                    mergeCategory = catalog.category(state.pendingMergeFromId);
                }
                ActionResult result = MergeSkillAction(storage, catalog,
                state.pendingMergeFromId,
                state.pendingMergeToId,
                state.pendingMergeName,
                static_cast<double>(state.pendingMergeWeight),
                state.pendingMergeDesc,
                mergeCategory,
                state.pendingMergeProfessions,
                state.active ? state.active->id : std::string{});
                AppLogLevel level = LogLevelForResult(result);
                SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Навыки");
                if (result.ok) {
                    PrepareXpEntries(state, catalog);
                    std::string keepId = state.active ? state.active->id : std::string{};
                    RefreshProfiles(state, storage, catalog, keepId);
                    const auto& skills = catalog.skills();
                    for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
                        if (skills[i] == state.pendingMergeToId) {
                            state.selectedCatalogIndex = i;
                            break;
                        }
                    }
                    state.lastCatalogSelection = -1;
                }
                state.pendingMergeFromId.clear();
                state.pendingMergeToId.clear();
                state.pendingMergeName.clear();
                state.pendingMergeDesc.clear();
                state.pendingMergeProfessions.clear();
                state.pendingMergeWeight = 1.0f;
                ImGui::CloseCurrentPopup();
                mergeSkillOpen = false;
            };
            const float actionWidth = ImGui::GetContentRegionAvail().x;
            const bool stackActions = actionWidth < 300.0f;
            if (stackActions) {
                if (ImGui::Button(u8"Слить", ImVec2(-1.0f, 0.0f))) {
                    mergeSkills();
                }
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    state.pendingMergeFromId.clear();
                    state.pendingMergeToId.clear();
                    state.pendingMergeName.clear();
                    state.pendingMergeDesc.clear();
                    state.pendingMergeProfessions.clear();
                    state.pendingMergeWeight = 1.0f;
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
            } else if (ImGui::BeginTable("merge_skill_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
                const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawAnchorIcon(UiIcon::List);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                if (ImGui::Button(u8"Слить", ImVec2(-1.0f, 0.0f))) {
                    mergeSkills();
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                    state.pendingMergeFromId.clear();
                    state.pendingMergeToId.clear();
                    state.pendingMergeName.clear();
                    state.pendingMergeDesc.clear();
                    state.pendingMergeProfessions.clear();
                    state.pendingMergeWeight = 1.0f;
                    ImGui::CloseCurrentPopup();
                    mergeSkillOpen = false;
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled(u8"Нет данных для слияния.");
            if (ImGui::Button(u8"Закрыть")) {
                ImGui::CloseCurrentPopup();
                mergeSkillOpen = false;
            }
        }
        ImGui::EndPopup();
    }
    if (!mergeSkillOpen && ImGui::IsPopupOpen(u8"Слияние навыков")) {
        ImGui::CloseCurrentPopup();
    }

    if (state.clearSkillsPopupRequest) {
        ImGui::OpenPopup(u8"Очистить навыки");
        state.clearSkillsPopupRequest = false;
    }
    bool clearSkillsOpen = true;
    if (ImGui::BeginPopupModal(u8"Очистить навыки", &clearSkillsOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec4 warn = LogLevelColor(AppLogLevel::Warning);
        warn.w = 1.0f;
        ImGui::TextColored(warn, u8"Удалить все навыки из каталога и профилей?");
        ImGui::TextDisabled(u8"Действие нельзя отменить.");
        auto clearAll = [&]() {
            std::string keepId = state.active ? state.active->id : std::string{};
            ActionResult result = ClearAllSkillsAction(storage, catalog, state.storageDir, keepId);
            AppLogLevel level = LogLevelForResult(result);
            SetStatus(state, result.message, result.ok ? 0.45f : 1.0f, result.ok ? 0.9f : 0.45f,
                      result.ok ? 0.45f : 0.45f, 1.0f, level, u8"Навыки");
            PrepareXpEntries(state, catalog);
            RefreshProfiles(state, storage, catalog, keepId);
            state.selectedCatalogIndex = -1;
            state.lastCatalogSelection = -1;
            ImGui::CloseCurrentPopup();
            clearSkillsOpen = false;
        };
        const float actionWidth = ImGui::GetContentRegionAvail().x;
        const bool stackActions = actionWidth < 300.0f;
        if (stackActions) {
            if (ImGui::Button(u8"Очистить", ImVec2(-1.0f, 0.0f))) {
                clearAll();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                clearSkillsOpen = false;
            }
        } else if (ImGui::BeginTable("clear_skills_actions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("action_primary", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("action_secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImVec4 keyBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
            const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::List);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            if (ImGui::Button(u8"Очистить", ImVec2(-1.0f, 0.0f))) {
                clearAll();
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                clearSkillsOpen = false;
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    if (!clearSkillsOpen && ImGui::IsPopupOpen(u8"Очистить навыки")) {
        ImGui::CloseCurrentPopup();
    }

}
