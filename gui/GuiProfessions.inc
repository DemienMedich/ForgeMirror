static std::filesystem::path ProfessionsPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "professions.txt";
}

static std::string ProfTrim(const std::string& s) {
    return TrimStringGui(s.c_str());
}

static std::vector<ProfessionEntry> LoadProfessions(const std::filesystem::path& storageDir) {
    std::vector<ProfessionEntry> out;
    std::ifstream in(ProfessionsPath(storageDir), std::ios::binary);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        std::string part;
        std::istringstream ss(line);
        while (std::getline(ss, part, '|')) {
            parts.push_back(ProfTrim(part));
        }
        if (parts.size() < 2) continue;
        ProfessionEntry e;
        size_t idx = 0;
        e.id = parts[idx++];
        e.name = parts[idx++];
        if (idx < parts.size()) {
            e.description = parts[idx++];
        }
        if (!e.id.empty() && !e.name.empty()) {
            out.push_back(std::move(e));
        }
    }
    return out;
}

static bool SaveProfessions(const std::filesystem::path& storageDir, const std::vector<ProfessionEntry>& list) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    for (const auto& e : list) {
        if (e.id.empty() || e.name.empty()) continue;
        out << e.id << "|" << e.name;
        out << "|" << e.description;
        out << "\n";
    }
    std::string data = out.str();
    data.insert(data.begin(), static_cast<char>(0xEF));
    data.insert(data.begin() + 1, static_cast<char>(0xBB));
    data.insert(data.begin() + 2, static_cast<char>(0xBF));
    std::error_code ec;
    std::filesystem::create_directories(ProfessionsPath(storageDir).parent_path(), ec);
    std::ofstream f(ProfessionsPath(storageDir), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), data.size());
    return f.good();
}

static std::string GenerateProfessionId(const std::vector<ProfessionEntry>& list) {
    const std::int64_t now = NowSeconds();
    int suffix = static_cast<int>(list.size()) + 1;
    std::string base = "pr_" + std::to_string(now);
    auto exists = [&](const std::string& id) {
        return std::any_of(list.begin(), list.end(), [&](const ProfessionEntry& e) { return e.id == id; });
    };
    std::string candidate = base;
    while (exists(candidate)) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

static void RefreshProfileProfessionCache(GuiState& state, IJobStorage& storage) {
    state.profileProfessionCache.clear();
    std::string restoreId = state.active ? state.active->id : std::string{};
    for (const auto& info : state.profiles) {
        if (info.archived) continue;
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            state.profileProfessionCache[info.id] = profile->profession_id();
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

static bool ApplyProfileProfession(GuiState& state, IJobStorage& storage,
                                   const std::string& profileId, const std::string& professionId) {
    std::string restoreId = state.active ? state.active->id : std::string{};
    if (!storage.set_active_profile(profileId)) return false;
    if (auto profile = storage.load_profile()) {
        profile->set_profession_id(professionId);
        if (!storage.save_profile(*profile)) return false;
    } else {
        return false;
    }
    if (!restoreId.empty() && restoreId != profileId) {
        storage.set_active_profile(restoreId);
    }
    if (state.active && state.active->id == profileId) {
        state.active->profile.set_profession_id(professionId);
    }
    state.profileProfessionCache[profileId] = professionId;
    return true;
}

static void ClearProfilesForProfession(GuiState& state, IJobStorage& storage, const std::string& professionId) {
    if (professionId.empty()) return;
    std::string restoreId = state.active ? state.active->id : std::string{};
    for (const auto& info : state.profiles) {
        if (info.archived) continue;
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            if (profile->profession_id() == professionId) {
                profile->set_profession_id("");
                storage.save_profile(*profile);
                state.profileProfessionCache[info.id].clear();
                if (state.active && state.active->id == info.id) {
                    state.active->profile.set_profession_id("");
                }
            }
        }
    }
    if (!restoreId.empty()) {
        storage.set_active_profile(restoreId);
    }
    state.profileProfessionDirty = true;
}

static void ClearSkillsForProfession(SkillCatalog& catalog, const std::string& professionId) {
    if (professionId.empty()) return;
    auto skills = catalog.skills();
    for (const auto& id : skills) {
        if (catalog.has_profession(id, professionId)) {
            const std::string name = catalog.display_name(id);
            const std::string desc = catalog.description(id);
            const std::string category = catalog.category(id);
            const double weight = catalog.weight(id);
            auto profs = catalog.professions(id);
            profs.erase(std::remove(profs.begin(), profs.end(), professionId), profs.end());
            catalog.update_skill(id, name, weight, desc, category, profs);
        }
    }
}

// Professions storage helpers (meta/professions.txt)
#include <fstream>
#include <sstream>
#include <locale>
#include <imgui.h>

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
            if (ImGui::Selectable(p.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            ImGui::SetItemAllowOverlap();
                state.professionEditIndex = i;
                std::snprintf(state.newProfessionName.data(), state.newProfessionName.size(), "%s", p.name.c_str());
                std::snprintf(state.newProfessionDesc.data(), state.newProfessionDesc.size(), "%s", p.description.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton(u8"?##del_prof")) {
                const std::string removedId = state.professions[i].id;
                state.professions.erase(state.professions.begin() + i);
                SaveProfessions(state.storageDir, state.professions);
                ClearProfilesForProfession(state, storage, removedId);
                ClearSkillsForProfession(catalog, removedId);
                state.profileProfessionDirty = true;
                SetStatus(state, u8"Профессия удалена.", 0.45f, 0.9f, 0.45f);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
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
        if (name.empty()) {
            SetStatus(state, u8"Название обязательно.", 1.0f, 0.45f, 0.45f);
            return;
        }
        int editedIndex = state.professionEditIndex;
        if (editedIndex >= 0 && editedIndex < static_cast<int>(state.professions.size())) {
            auto& p = state.professions[editedIndex];
            p.name = name;
            p.description = desc;
        } else {
            ProfessionEntry p;
            p.id = GenerateProfessionId(state.professions);
            p.name = name;
            p.description = desc;
            state.professions.push_back(std::move(p));
            editedIndex = static_cast<int>(state.professions.size()) - 1;
        }
        SaveProfessions(state.storageDir, state.professions);
        state.profileProfessionDirty = true;
        SetStatus(state, u8"Профессия сохранена.", 0.45f, 0.9f, 0.45f);
        state.professionEditIndex = editedIndex;
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
        } else if (ImGui::BeginTable("prof_members", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
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
                    if (ApplyProfileProfession(state, storage, info.id, newProf)) {
                        SetStatus(state, u8"Профессия профиля обновлена.", 0.45f, 0.9f, 0.45f);
                    } else {
                        SetStatus(state, u8"Не удалось обновить профессию профиля.", 1.0f, 0.45f, 0.45f);
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
