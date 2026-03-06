static void RefreshProfileProfessionCache(GuiState& state, IJobStorage& storage) {
    state.profileProfessionCache.clear();
    state.profileLoginCache.clear();
    state.profileBlockedCache.clear();
    state.profileWalletCache.clear();
    std::string restoreId = state.active ? state.active->id : std::string{};
    for (const auto& info : state.profiles) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            state.profileProfessionCache[info.id] = profile->profession_id();
            std::string login = profile->login().empty() ? info.id : profile->login();
            state.profileLoginCache[info.id] = login;
            state.profileBlockedCache[info.id] = profile->is_blocked();
        }
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    state.profileProfessionDirty = false;
}

static std::string ProfessionNameById(const GuiState& state, const std::string& id) {
    if (id.empty()) return {};
    for (const auto& p : state.professions) {
        if (p.id == id) return p.name;
    }
    return {};
}

void DrawProfessionsPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Professions, state.storageDir);
    if (!state.modules.professions) {
        DrawCardHeader(u8"Профессии");
        ImGui::TextDisabled(u8"Модуль профессий отключён.");
        return;
    }
    DrawCardHeader(u8"Профессии (админ)");
    if (!state.isAdmin) {
        ImGui::TextDisabled(u8"Только для администратора.");
        return;
    }
    ImGui::TextDisabled(u8"Хранятся в meta/professions.txt, синхронизируются через облако.");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    if (ImGui::BeginTable("prof_list", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        for (int i = 0; i < static_cast<int>(state.professions.size()); ++i) {
            const auto& p = state.professions[i];
            ImGui::TableNextRow();
            ImGui::PushID(p.id.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", i + 1);
            ImGui::TableSetColumnIndex(1);
            bool selected = state.professionEditIndex == i;
            if (ImGui::Selectable(p.name.c_str(), selected, 0)) {
                state.professionEditIndex = i;
                std::snprintf(state.newProfessionName.data(), state.newProfessionName.size(), "%s", p.name.c_str());
                std::snprintf(state.newProfessionDesc.data(), state.newProfessionDesc.size(), "%s", p.description.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton(u8"Х##del_prof")) {
                state.pendingDeleteProfessionIndex = i;
                state.pendingDeleteProfessionId = state.professions[i].id;
                state.professionDeletePopupRequest = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (state.professionDeletePopupRequest) {
        ImGui::OpenPopup(u8"Удалить профессию?");
        state.professionDeletePopupRequest = false;
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    bool deleteOpen = true;
    if (ImGui::BeginPopupModal(u8"Удалить профессию?", &deleteOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModalShadow();
        std::string profName;
        std::string profId = state.pendingDeleteProfessionId;
        if (state.pendingDeleteProfessionIndex >= 0 &&
            state.pendingDeleteProfessionIndex < static_cast<int>(state.professions.size())) {
            const auto& p = state.professions[state.pendingDeleteProfessionIndex];
            if (profId.empty() | p.id == profId) {
                profName = p.name;
                profId = p.id;
            }
        }
        if (profName.empty() && !profId.empty()) {
            for (const auto& p : state.professions) {
                if (p.id == profId) {
                    profName = p.name;
                    break;
                }
            }
        }
        if (profName.empty()) profName = u8"(не найдена)";
        ImGui::TextWrapped(u8"Вы уверены, что хотите удалить профессию?");
        ImGui::TextDisabled(u8"%s", profName.c_str());
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        const float width = ImGui::GetContentRegionAvail().x;
        const bool stackButtons = width < 260.0f;
        auto doDelete = [&]() {
            std::string removedId = profId;
            if (removedId.empty() && state.pendingDeleteProfessionIndex >= 0 &&
                state.pendingDeleteProfessionIndex < static_cast<int>(state.professions.size())) {
                removedId = state.professions[state.pendingDeleteProfessionIndex].id;
            }
            AppProfessionMutationResult deleteResult = AppDeleteProfessionEntry(
                state.storageDir,
                state.professions,
                storage,
                state.profiles,
                catalog,
                state.active ? state.active->id : std::string{},
                removedId);
            if (deleteResult.ok) {
                if (state.active && state.active->profile.profession_id() == removedId) {
                    state.active->profile.set_profession_id("");
                }
                state.profileProfessionDirty = true;
                state.professionEditIndex = -1;
                state.newProfessionName.fill('\0');
                state.newProfessionDesc.fill('\0');
                std::string status = u8"Профессия удалена.";
                if (deleteResult.affectedProfiles > 0 || deleteResult.affectedSkills > 0) {
                    status = u8"Профессия удалена: профилей очищено " +
                             std::to_string(deleteResult.affectedProfiles) +
                             u8", навыков отвязано " + std::to_string(deleteResult.affectedSkills) + u8".";
                }
                SetStatus(state, status, 0.45f, 0.9f, 0.45f);
            } else {
                SetStatus(state, deleteResult.errorMessage, 1.0f, 0.45f, 0.45f);
            }
            state.pendingDeleteProfessionIndex = -1;
            state.pendingDeleteProfessionId.clear();
            ImGui::CloseCurrentPopup();
            deleteOpen = false;
        };
        auto doCancel = [&]() {
            state.pendingDeleteProfessionIndex = -1;
            state.pendingDeleteProfessionId.clear();
            ImGui::CloseCurrentPopup();
            deleteOpen = false;
        };
        if (stackButtons) {
            if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, 0.0f))) {
                doDelete();
            }
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                doCancel();
            }
        } else if (ImGui::BeginTable("prof_delete_actions", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Button(u8"Удалить", ImVec2(-1.0f, 0.0f))) {
                doDelete();
            }
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button(u8"Отмена", ImVec2(-1.0f, 0.0f))) {
                doCancel();
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawCardHeader(u8"Редактирование");
    if (ImGui::Button(u8"Новая профессия", ImVec2(-1.0f, 0.0f))) {
        state.professionEditIndex = -1;
        state.newProfessionName.fill('\0');
        state.newProfessionDesc.fill('\0');
    }
    if (state.professionEditIndex >= 0 &&
        state.professionEditIndex < static_cast<int>(state.professions.size())) {
        ImGui::TextDisabled(u8"Редактирование: %s", state.professions[state.professionEditIndex].name.c_str());
    } else {
        ImGui::TextDisabled(u8"Создание новой профессии");
    }
    ImGui::TextDisabled(u8"Название");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##prof_name", state.newProfessionName.data(), state.newProfessionName.size());
    ImGui::TextDisabled(u8"Описание");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##prof_desc", state.newProfessionDesc.data(), state.newProfessionDesc.size(), ImVec2(-1.0f, 70.0f));
    auto saveProfession = [&]() {
        std::string name = TrimStringGui(state.newProfessionName.data());
        std::string desc = TrimStringGui(state.newProfessionDesc.data());
        AppProfessionMutationResult saveResult = AppSaveProfessionEntry(
            state.storageDir,
            state.professions,
            state.professionEditIndex,
            name,
            desc);
        if (!saveResult.ok) {
            SetStatus(state, saveResult.errorMessage, 1.0f, 0.45f, 0.45f);
            return;
        }
        state.profileProfessionDirty = true;
        SetStatus(state, u8"Профессия сохранена.", 0.45f, 0.9f, 0.45f);
        state.professionEditIndex = saveResult.professionIndex;
        std::snprintf(state.newProfessionName.data(), state.newProfessionName.size(), "%s", name.c_str());
        std::snprintf(state.newProfessionDesc.data(), state.newProfessionDesc.size(), "%s", desc.c_str());
    };
    if (ImGui::Button(u8"Сохранить профессию", ImVec2(-1.0f, 0.0f))) {
        saveProfession();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    if (state.professionEditIndex >= 0 && state.professionEditIndex < static_cast<int>(state.professions.size())) {
        const auto& prof = state.professions[state.professionEditIndex];
        const std::string& profId = prof.id;

        DrawCardHeader(u8"Профили профессии");
        if (ImGui::SmallButton(u8"Обновить профили")) {
            state.profileProfessionDirty = true;
        }
        if (state.profileProfessionDirty) {
            RefreshProfileProfessionCache(state, storage);
        }
        if (state.profiles.empty()) {
            ImGui::TextDisabled(u8"Профили не найдены.");
        } else if (ImGui::BeginTable("prof_members", 3, ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(u8"Профиль", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn(u8"Статус", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn(u8"Назначить", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            for (const auto& info : state.profiles) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(info.name.c_str());
                ImGui::TableSetColumnIndex(1);
                auto it = state.profileProfessionCache.find(info.id);
                std::string currentProf = (it == state.profileProfessionCache.end()) ? std::string{} : it->second;
                if (info.archived) {
                    ImGui::TextDisabled(u8"Архив");
                } else if (currentProf.empty()) {
                    ImGui::TextDisabled(u8"Без профессии");
                } else if (currentProf == profId) {
                    ImGui::TextDisabled(u8"Текущая");
                } else {
                    std::string otherName = ProfessionNameById(state, currentProf);
                    if (otherName.empty()) otherName = currentProf;
                    ImGui::TextDisabled("%s", otherName.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                bool assigned = (!currentProf.empty() && currentProf == profId);
                bool checked = assigned;
                if (info.archived) ImGui::BeginDisabled();
                ImGui::PushID(info.id.c_str());
                if (ImGui::Checkbox("##assign_prof", &checked)) {
                    std::string newProf = checked ? profId : std::string{};
                    AppProfessionMutationResult assignResult = AppAssignProfessionToProfile(
                        storage,
                        state.active ? state.active->id : std::string{},
                        info.id,
                        newProf);
                    if (assignResult.ok) {
                        state.profileProfessionCache[info.id] = newProf;
                        if (state.active && state.active->id == info.id) {
                            state.active->profile.set_profession_id(newProf);
                        }
                        SetStatus(state, u8"Профессия профиля обновлена.", 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, assignResult.errorMessage, 1.0f, 0.45f, 0.45f);
                    }
                }
                ImGui::PopID();
                if (info.archived) ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        DrawCardHeader(u8"Навыки профессии");
        const auto& catalogSkills = catalog.skills();
        if (catalogSkills.empty()) {
            ImGui::TextDisabled(u8"Навыков пока нет.");
        } else if (ImGui::BeginChild("prof_skill_list", ImVec2(0.0f, 220.0f), true)) {
            for (const auto& id : catalogSkills) {
                std::string name = catalog.display_name(id);
                bool assigned = catalog.has_profession(id, profId);
                ImGui::PushID(id.c_str());
                if (ImGui::Checkbox("##prof_skill", &assigned)) {
                    auto profs = catalog.professions(id);
                    if (assigned) {
                        if (std::find(profs.begin(), profs.end(), profId) == profs.end()) {
                            profs.push_back(profId);
                        }
                    } else {
                        profs.erase(std::remove(profs.begin(), profs.end(), profId), profs.end());
                    }
                    bool ok = catalog.update_skill(id, name, catalog.weight(id),
                                                   catalog.description(id), catalog.category(id), profs);
                    if (ok) {
                        SetStatus(state, u8"Навык обновлён.", 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, u8"Не удалось обновить навык.", 1.0f, 0.45f, 0.45f);
                    }
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    } else {
        ImGui::TextDisabled(u8"Выберите профессию, чтобы назначать профили и навыки.");
    }
}
