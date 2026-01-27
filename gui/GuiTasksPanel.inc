static bool RollbackTask(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const TaskEntry& task) {
    const std::string previousActive = state.active ? state.active->id : std::string{};
    bool ok = true;
    for (const auto& participant : task.participants) {
        if (participant.profileId.empty()) continue;
        if (!storage.set_active_profile(participant.profileId)) {
            SetStatus(state, u8"Не удалось активировать профиль для отката XP.", 1.0f, 0.45f, 0.45f);
            ok = false;
            break;
        }
        auto profileOpt = storage.load_profile();
        if (!profileOpt) {
            SetStatus(state, u8"Не удалось загрузить профиль для отката XP.", 1.0f, 0.45f, 0.45f);
            ok = false;
            break;
        }
        Profile profile = *profileOpt;
        const int newTotalXp = std::max(0, profile.total_xp() - participant.globalXp);
        const int newTasks = std::max(0, profile.tasks_completed() - 1);
        profile.set_total_xp(newTotalXp);
        profile.set_tasks_completed(newTasks);
        storage.save_profile(profile);
    }
    if (!previousActive.empty()) {
        SelectProfileById(state, storage, catalog, previousActive);
    }
    return ok;
}

void DrawTasksPanel(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    DrawWindowBackground(state.ui, UiWindowId::Tasks, state.storageDir);
    if (!state.modules.tasks) {
        DrawCardHeader(u8"Задачи");
        ImGui::TextDisabled(u8"Модуль задач отключён.");
        return;
    }
    DrawCardHeader(u8"Задачи");
    ImGui::TextDisabled(u8"Все выполненные задачи с распределением вклада.");
    if (state.tasks.empty()) {
        ImGui::TextDisabled(u8"Задач пока нет.");
        if (state.isAdmin) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            accent.w = 1.0f;
            ImVec4 addBg = accent;
            addBg.w = 0.18f;
            ImVec4 addHover = addBg;
            addHover.w = 0.28f;
            ImVec4 addActive = addBg;
            addActive.w = 0.38f;
            ImGui::PushStyleColor(ImGuiCol_Button, addBg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, addHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, addActive);
            if (ImGui::Button(u8"Добавить опыт", ImVec2(-1.0f, 0.0f))) {
                PrepareXpEntries(state, catalog);
                state.xpPopupRequest = true;
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::TextDisabled(u8"Добавление опыта доступно администратору.");
        }
        return;
    }

    auto ComputeFilteredTasks = [&state]() {
        std::vector<const TaskEntry*> ordered;
        ordered.reserve(state.tasks.size());
        for (const auto& task : state.tasks) {
            ordered.push_back(&task);
        }
        std::sort(ordered.begin(), ordered.end(), [](const TaskEntry* a, const TaskEntry* b) {
            return a->createdAt > b->createdAt;
        });
        const std::int64_t nowSec = NowSeconds();
        const std::int64_t minTs = state.tasksFilterDays > 0
            ? nowSec - static_cast<std::int64_t>(state.tasksFilterDays) * 86400
            : 0;
        const std::string filterProfileId = (state.tasksFilterProfile > 0 &&
            state.tasksFilterProfile <= static_cast<int>(state.profiles.size()))
            ? state.profiles[state.tasksFilterProfile - 1].id
            : std::string();
        std::vector<const TaskEntry*> filtered;
        filtered.reserve(ordered.size());
        for (const TaskEntry* task : ordered) {
            const std::string filterText = task->project + " " + task->title + " " + task->description;
            if (!MatchesFilter(filterText, state.filters.tasks.data())) continue;
            if (state.tasksFilterCategory > 0 && task->category != state.tasksFilterCategory - 1) continue;
            if (!filterProfileId.empty() && !FindTaskParticipant(*task, filterProfileId)) continue;
            if (minTs > 0 && task->createdAt < minTs) continue;
            filtered.push_back(task);
        }
        return filtered;
    };

    std::unordered_map<std::string, std::string> nameById;
    nameById.reserve(state.profiles.size());
    for (const auto& info : state.profiles) {
        nameById[info.id] = info.name;
    }

    DrawCardHeader(u8"Фильтр");
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 keyBg = style.Colors[ImGuiCol_FrameBg];
    keyBg.w = std::clamp(keyBg.w * 0.35f, 0.05f, 0.14f);
    const ImU32 keyBgU32 = ImGui::GetColorU32(keyBg);
    const float filtersWidth = ImGui::GetContentRegionAvail().x;
    const bool stackFilters = filtersWidth < 620.0f;
    std::vector<std::string> profileLabels;
    profileLabels.reserve(state.profiles.size() + 1);
    profileLabels.push_back(u8"Все профили");
    for (const auto& info : state.profiles) {
        profileLabels.push_back("[" + info.id + "] " + info.name);
    }
    std::vector<const char*> profileLabelPtrs;
    profileLabelPtrs.reserve(profileLabels.size());
    for (const auto& label : profileLabels) {
        profileLabelPtrs.push_back(label.c_str());
    }
    state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);

    std::vector<std::string> categoryLabels;
    categoryLabels.reserve(Profile::kCategoryCount + 1);
    categoryLabels.push_back(u8"Все категории");
    for (const char* label : Profile::kCategoryLabels) {
        categoryLabels.push_back(label ? label : "");
    }
    std::vector<const char*> categoryLabelPtrs;
    categoryLabelPtrs.reserve(categoryLabels.size());
    for (const auto& label : categoryLabels) {
        categoryLabelPtrs.push_back(label.c_str());
    }
    state.tasksFilterCategory = std::clamp(state.tasksFilterCategory, 0,
                                           static_cast<int>(categoryLabelPtrs.size()) - 1);

    const int dayOptions[] = {0, 7, 30, 90, 365};
    const char* dayLabels[] = {u8"Все время", u8"7 дней", u8"30 дней", u8"90 дней", u8"365 дней"};
    int dayIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(dayOptions); ++i) {
        if (dayOptions[i] == state.tasksFilterDays) {
            dayIndex = i;
            break;
        }
    }

    if (stackFilters) {
        ImGui::TextDisabled(u8"Поиск");
        ImGui::InputTextWithHint(u8"##tasks_filter", u8"Поиск по проекту, задаче или описанию",
                                 state.filters.tasks.data(), state.filters.tasks.size());
        ImGui::TextDisabled(u8"Профили");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_profile", &state.tasksFilterProfile,
                         profileLabelPtrs.data(), static_cast<int>(profileLabelPtrs.size()))) {
            state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);
        }
        ImGui::TextDisabled(u8"Категория");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_category", &state.tasksFilterCategory,
                         categoryLabelPtrs.data(), static_cast<int>(categoryLabelPtrs.size()))) {
            state.tasksFilterCategory = std::clamp(state.tasksFilterCategory, 0, static_cast<int>(categoryLabelPtrs.size()) - 1);
        }
        ImGui::TextDisabled(u8"Период");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_days", &dayIndex, dayLabels, IM_ARRAYSIZE(dayLabels))) {
            state.tasksFilterDays = dayOptions[dayIndex];
        }
        ImGui::TextDisabled(u8"Экспорт");
        if (ImGui::Button(u8"Экспорт CSV", ImVec2(-1.0f, 0.0f))) {
            std::filesystem::path outPath;
            int exported = 0;
            std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
            if (!ExportTasksCsv(state, filtered, nameById, outPath, exported)) {
                SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
            } else {
                std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                msg += outPath.string();
                msg += ")";
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
            }
        }
        if (ImGui::Button(u8"Экспорт TXT", ImVec2(-1.0f, 0.0f))) {
            std::filesystem::path outPath;
            int exported = 0;
            std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
            if (!ExportTasksTxt(state, filtered, nameById, outPath, exported)) {
                SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
            } else {
                std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                msg += outPath.string();
                msg += ")";
                SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
            }
        }
    } else if (ImGui::BeginTable("tasks_filters", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("filter_search", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("filter_profile", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("filter_category", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawAnchorIcon(UiIcon::List);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
        ImGui::TextDisabled(u8"Поиск");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(u8"##tasks_filter", u8"Поиск по проекту, задаче или описанию",
                                 state.filters.tasks.data(), state.filters.tasks.size());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled(u8"Профили");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_profile", &state.tasksFilterProfile,
                         profileLabelPtrs.data(), static_cast<int>(profileLabelPtrs.size()))) {
            state.tasksFilterProfile = std::clamp(state.tasksFilterProfile, 0, static_cast<int>(profileLabelPtrs.size()) - 1);
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled(u8"Категория");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"##tasks_category", &state.tasksFilterCategory,
                         categoryLabelPtrs.data(), static_cast<int>(categoryLabelPtrs.size()))) {
            state.tasksFilterCategory = std::clamp(state.tasksFilterCategory, 0, static_cast<int>(categoryLabelPtrs.size()) - 1);
        }
        ImGui::EndTable();
        if (ImGui::BeginTable("tasks_filters_extra", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("filter_idx", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("filter_period", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("filter_actions", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawAnchorIcon(UiIcon::Chart);
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextDisabled(u8"Период");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(u8"##tasks_days", &dayIndex, dayLabels, IM_ARRAYSIZE(dayLabels))) {
                state.tasksFilterDays = dayOptions[dayIndex];
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(u8"Экспорт");
            if (ImGui::Button(u8"CSV", ImVec2(-1.0f, 0.0f))) {
                std::filesystem::path outPath;
                int exported = 0;
                std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
                if (!ExportTasksCsv(state, filtered, nameById, outPath, exported)) {
                    SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
                } else {
                    std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                    msg += outPath.string();
                    msg += ")";
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
                }
            }
            if (ImGui::Button(u8"TXT", ImVec2(-1.0f, 0.0f))) {
                std::filesystem::path outPath;
                int exported = 0;
                std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();
                if (!ExportTasksTxt(state, filtered, nameById, outPath, exported)) {
                    SetStatus(state, u8"Нет задач для экспорта.", 1.0f, 0.75f, 0.35f);
                } else {
                    std::string msg = u8"Экспортировано задач: " + std::to_string(exported) + u8" (";
                    msg += outPath.string();
                    msg += ")";
                    SetStatus(state, msg, 0.45f, 0.9f, 0.45f, 1.0f, AppLogLevel::Info, u8"Задачи");
                }
            }
            ImGui::EndTable();
        }
    }

    std::vector<const TaskEntry*> filtered = ComputeFilteredTasks();

    std::array<int, Profile::kCategoryCount> categoryCounts{};
    const int filteredCount = static_cast<int>(filtered.size());
    int topIdx = 0;
    int topCount = 0;
    int minIdx = -1;
    int minCount = 0;
    int activeCategories = 0;
    int scoreSum = 0;
    int poolSum = 0;
    std::unordered_set<std::string> uniqueProfiles;
    std::string lastStamp;
    if (!filtered.empty()) {
        lastStamp = FormatTimestamp(filtered.front()->createdAt, "%Y-%m-%d %H:%M");
        uniqueProfiles.reserve(state.profiles.size());
        for (const TaskEntry* task : filtered) {
            int idx = std::clamp(task->category, 0, static_cast<int>(Profile::kCategoryCount - 1));
            categoryCounts[idx] += 1;
            if (categoryCounts[idx] >= topCount) {
                topCount = categoryCounts[idx];
                topIdx = idx;
            }
            scoreSum += task->score;
            poolSum += task->basePool;
            for (const auto& participant : task->participants) {
                uniqueProfiles.insert(participant.profileId);
            }
        }
        minIdx = topIdx;
        minCount = topCount;
        for (size_t i = 0; i < categoryCounts.size(); ++i) {
            if (categoryCounts[i] <= 0) {
                continue;
            }
            activeCategories += 1;
            if (categoryCounts[i] < minCount) {
                minCount = categoryCounts[i];
                minIdx = static_cast<int>(i);
            }
        }
    }
    const bool hasAttention = (activeCategories >= 2) && (topCount - minCount >= 2) && (minIdx >= 0);

    if (BeginCard("tasks_summary_card")) {
        DrawCardHeader(u8"Сводка");
        if (filtered.empty()) {
            ImGui::TextDisabled(u8"Нет задач по текущим фильтрам.");
        } else {
            std::ostringstream focusStream;
            focusStream.imbue(std::locale::classic());
            float pct = filteredCount > 0 ? (static_cast<float>(topCount) / static_cast<float>(filteredCount)) * 100.0f : 0.0f;
            focusStream << std::fixed << std::setprecision(0) << pct;
            std::string focusText = std::string(ClassificationLabel(topIdx)) +
                " (" + std::to_string(topCount) + ", " + focusStream.str() + "%)";
            const float avgScore = filteredCount > 0
                ? static_cast<float>(scoreSum) / static_cast<float>(filteredCount)
                : 0.0f;
            const float avgPool = filteredCount > 0
                ? static_cast<float>(poolSum) / static_cast<float>(filteredCount)
                : 0.0f;
            if (ImGui::BeginTable("tasks_summary_table", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("sum_total", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("sum_last", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("sum_focus", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("sum_score", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("sum_people", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Всего");
                ImGui::Text("%d", filteredCount);
                ImGui::TableSetColumnIndex(1);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Последняя");
                ImGui::Text("%s", lastStamp.empty() ? "-" : lastStamp.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Фокус");
                ImGui::Text("%s", focusText.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Оценка ср.");
                ImGui::Text("%.1f", avgScore);
                ImGui::TableSetColumnIndex(4);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
                ImGui::TextDisabled(u8"Участники");
                ImGui::Text("%d", static_cast<int>(uniqueProfiles.size()));
                ImGui::EndTable();
            }
            if (hasAttention) {
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled(u8"Зона внимания");
                ImGui::SameLine();
                ImGui::TextColored(LogLevelColor(AppLogLevel::Warning), "%s (%d)",
                                   ClassificationLabel(minIdx), minCount);
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled(u8"Средний пул XP: %.0f", avgPool);

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextDisabled(u8"Активность за 14 дней");
            constexpr int kDays = 14;
            std::array<float, kDays> dayCounts{};
            const std::int64_t nowSec = NowSeconds();
            const std::int64_t today = nowSec / 86400;
            const std::int64_t startDay = today - (kDays - 1);
            int maxCount = 0;
            for (const TaskEntry* task : filtered) {
                if (!task) continue;
                const std::int64_t day = task->createdAt / 86400;
                const int idx = static_cast<int>(day - startDay);
                if (idx < 0 || idx >= kDays) continue;
                dayCounts[idx] += 1.0f;
                maxCount = std::max(maxCount, static_cast<int>(dayCounts[idx]));
            }
            if (maxCount == 0) {
                ImGui::TextDisabled(u8"Нет активности за период.");
            } else {
                ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
                accent.w = 1.0f;
                DrawSparkline("tasks_activity", dayCounts.data(), kDays,
                              static_cast<float>(maxCount), accent, 36.0f);
            }
        }
    }
    EndCard();
    ImGui::Spacing();

    int visibleCount = 0;
    if (ImGui::BeginTable("tasks_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"Дата", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(u8"Проект", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(u8"Задача", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(u8"Участники", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(u8"Категория", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(u8"Оценка", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (const TaskEntry* task : filtered) {
            ++visibleCount;
            ImGui::TableNextRow();
            if (hasAttention && task->category == minIdx) {
                ImVec4 rowHighlight = LogLevelColor(AppLogLevel::Warning);
                rowHighlight.w = std::clamp(rowHighlight.w * 0.16f, 0.08f, 0.22f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(rowHighlight));
            }
            ImGui::TableSetColumnIndex(0);
            const bool isExpanded = (state.tasksExpandedId == task->id);
            const std::string rowId = std::string("##task_row_") + task->id;
            if (ImGui::Selectable(rowId.c_str(), isExpanded, ImGuiSelectableFlags_SpanAllColumns)) {
                state.tasksExpandedId = isExpanded ? std::string() : task->id;
            }
            ImGui::SameLine();
            const std::string stamp = FormatTimestamp(task->createdAt, "%d.%m.%Y %H:%M");
            ImGui::TextDisabled("%s", stamp.empty() ? "-" : stamp.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, keyBgU32);
            ImGui::TextUnformatted(task->project.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(task->title.c_str());
            if (!task->description.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(task->description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(3);
            const std::string participants = BuildParticipantLabel(*task, nameById);
            ImGui::TextUnformatted(participants.c_str());
            ImGui::TableSetColumnIndex(4);
            if (hasAttention && task->category == minIdx) {
                ImVec4 warnText = LogLevelColor(AppLogLevel::Warning);
                warnText.w = 0.95f;
                ImGui::TextColored(warnText, "%s", ClassificationLabel(task->category));
            } else {
                ImGui::TextUnformatted(ClassificationLabel(task->category));
            }
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d/10", task->score);
        }
        ImGui::EndTable();
    }
    const int totalTasks = static_cast<int>(state.tasks.size());
    ImGui::TextDisabled(u8"Показано: %d из %d (всего: %d)", visibleCount, filteredCount, totalTasks);
    if (filteredCount == 0) {
        ImGui::TextDisabled(u8"Нет задач по фильтру.");
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"Сбросить фильтр")) {
            state.filters.tasks.fill('\0');
            state.tasksFilterProfile = 0;
            state.tasksFilterCategory = 0;
            state.tasksFilterDays = 0;
        }
    }

    const TaskEntry* expanded = nullptr;
    if (!state.tasksExpandedId.empty()) {
        for (const TaskEntry* task : filtered) {
            if (task && task->id == state.tasksExpandedId) {
                expanded = task;
                break;
            }
        }
        if (!expanded) {
            state.tasksExpandedId.clear();
        }
    }
    if (expanded) {
        if (BeginCard("tasks_detail_card")) {
            DrawCardHeader(u8"Детали задачи");
            ImGui::TextDisabled(u8"Клик по строке задачи раскрывает детали.");
            std::string title = TaskDisplayTitle(*expanded);
            if (!title.empty()) {
                ImGui::TextUnformatted(title.c_str());
            }
            ImGui::TextDisabled(u8"Категория: %s", ClassificationLabel(expanded->category));
            ImGui::TextDisabled(u8"Оценка: %d/10", expanded->score);
            ImGui::TextDisabled(u8"Базовый XP: %d", expanded->baseXp);
            ImGui::TextDisabled(u8"Базовый пул: %d", expanded->basePool);
            if (!expanded->description.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled(u8"Описание");
                ImGui::TextWrapped("%s", expanded->description.c_str());
            }

            int totalGlobalXp = 0;
            int totalSkillXp = 0;
            for (const auto& participant : expanded->participants) {
                totalGlobalXp += participant.globalXp;
                totalSkillXp += participant.skillXp;
            }
            ImGui::Spacing();
            ImGui::TextDisabled(u8"Итоговый XP: %d", totalGlobalXp);
            ImGui::TextDisabled(u8"Навыки (суммарно): %d XP", totalSkillXp);

            ImGui::Spacing();
            ImGui::TextDisabled(u8"Участники");
            if (expanded->participants.empty()) {
                ImGui::TextDisabled(u8"Нет участников.");
            } else if (ImGui::BeginTable("tasks_detail_participants", 4,
                                         ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(u8"Профиль", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn(u8"Вклад", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(u8"XP", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn(u8"Навыки", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const auto& participant : expanded->participants) {
                    auto it = nameById.find(participant.profileId);
                    const std::string name = (it == nameById.end()) ? participant.profileId : it->second;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d%%", participant.percent);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", participant.globalXp);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", participant.skillXp);
                }
                ImGui::EndTable();
            }
            if (state.isAdmin && !expanded->id.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled(u8"Действия");
                if (ImGui::Button(u8"Удалить задачу и откатить XP", ImVec2(-1.0f, 0.0f))) {
                    state.pendingTaskDeleteId = expanded->id;
                    state.taskDeletePopupRequest = true;
                }
            }
        }
        EndCard();
    }

    if (state.taskDeletePopupRequest) {
        ImGui::OpenPopup(u8"Удалить задачу?");
        state.taskDeletePopupRequest = false;
    }
    bool deleteOpen = true;
    if (ImGui::BeginPopupModal(u8"Удалить задачу?", &deleteOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        TaskEntry* target = nullptr;
        for (auto& task : state.tasks) {
            if (task.id == state.pendingTaskDeleteId) {
                target = &task;
                break;
            }
        }
        if (!target) {
            ImGui::TextDisabled(u8"Задача не найдена.");
        } else {
            std::string title = TaskDisplayTitle(*target);
            if (title.empty()) title = target->id;
            ImGui::TextWrapped(u8"Удалить задачу \"%s\" и откатить начисленный XP для участников?", title.c_str());
            ImGui::Spacing();
            if (ImGui::Button(u8"Удалить", ImVec2(140.0f, 0.0f))) {
                TaskEntry copy = *target;
                bool rollbackOk = RollbackTask(state, storage, catalog, copy);
                if (rollbackOk) {
                    state.tasks.erase(std::remove_if(state.tasks.begin(), state.tasks.end(),
                                                     [&](const TaskEntry& t) { return t.id == copy.id; }),
                                      state.tasks.end());
                    SaveTasks(state.storageDir, state.tasks);
                    if (state.tasksExpandedId == copy.id) {
                        state.tasksExpandedId.clear();
                    }
                    SetStatus(state, u8"Задача удалена, XP откатан.", 0.45f, 0.9f, 0.45f);
                }
                state.pendingTaskDeleteId.clear();
                ImGui::CloseCurrentPopup();
                deleteOpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Отмена", ImVec2(140.0f, 0.0f))) {
                state.pendingTaskDeleteId.clear();
                ImGui::CloseCurrentPopup();
                deleteOpen = false;
            }
        }
        ImGui::EndPopup();
    }
    if (!deleteOpen && ImGui::IsPopupOpen(u8"Удалить задачу?")) {
        ImGui::CloseCurrentPopup();
    }
}
