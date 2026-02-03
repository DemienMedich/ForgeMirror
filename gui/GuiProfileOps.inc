namespace {
static void ReportProfileError(GuiState& state, std::string message) {
    state.profileLoadError = std::move(message);
    state.profileLoading = false;
    SetStatus(state, state.profileLoadError, 1.0f, 0.45f, 0.45f, 1.0f, AppLogLevel::Error, u8"Профиль");
}
}

static std::vector<std::string> SplitProfileIdList(const std::string& raw) {
    std::vector<std::string> out;
    if (raw.empty()) return out;
    std::istringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = TrimStringGui(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static std::string JoinProfileIdList(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) {
        if (id.empty()) continue;
        if (!out.empty()) out += ',';
        out += id;
    }
    return out;
}

static void ApplyProfileAccessSettings(GuiState& state) {
    state.unlockedProfiles.clear();
    for (const auto& id : SplitProfileIdList(state.ui.profileUnlocked)) {
        state.unlockedProfiles.insert(id);
    }
    state.recentProfiles = SplitProfileIdList(state.ui.profileRecent);
    constexpr size_t kMaxRecentProfiles = 6;
    if (state.recentProfiles.size() > kMaxRecentProfiles) {
        state.recentProfiles.resize(kMaxRecentProfiles);
    }
}

static bool UpdateProfileAccessSettings(GuiState& state) {
    std::vector<std::string> unlockedList;
    unlockedList.reserve(state.unlockedProfiles.size());
    for (const auto& id : state.unlockedProfiles) {
        if (!id.empty()) unlockedList.push_back(id);
    }
    std::sort(unlockedList.begin(), unlockedList.end());
    const std::string unlockedJoined = JoinProfileIdList(unlockedList);
    const std::string recentJoined = JoinProfileIdList(state.recentProfiles);
    bool changed = false;
    if (state.ui.profileUnlocked != unlockedJoined) {
        state.ui.profileUnlocked = unlockedJoined;
        changed = true;
    }
    if (state.ui.profileRecent != recentJoined) {
        state.ui.profileRecent = recentJoined;
        changed = true;
    }
    return changed;
}

static void SaveProfileAccessSettings(GuiState& state) {
    if (UpdateProfileAccessSettings(state)) {
        SaveUiSettings(state.storageDir, state.ui);
    }
}

static void TrackRecentProfile(GuiState& state, const std::string& id) {
    if (id.empty()) return;
    auto& list = state.recentProfiles;
    auto it = std::find(list.begin(), list.end(), id);
    if (it != list.end()) {
        list.erase(it);
    }
    list.insert(list.begin(), id);
    constexpr size_t kMaxRecentProfiles = 6;
    if (list.size() > kMaxRecentProfiles) {
        list.resize(kMaxRecentProfiles);
    }
    SaveProfileAccessSettings(state);
}

static void PruneProfileAccessState(GuiState& state) {
    std::unordered_set<std::string> validIds;
    validIds.reserve(state.profiles.size());
    for (const auto& info : state.profiles) {
        validIds.insert(info.id);
    }
    bool changed = false;
    for (auto it = state.unlockedProfiles.begin(); it != state.unlockedProfiles.end(); ) {
        if (validIds.find(*it) == validIds.end()) {
            it = state.unlockedProfiles.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    auto& recent = state.recentProfiles;
    auto newEnd = std::remove_if(recent.begin(), recent.end(), [&](const std::string& id) {
        return validIds.find(id) == validIds.end();
    });
    if (newEnd != recent.end()) {
        recent.erase(newEnd, recent.end());
        changed = true;
    }
    if (changed) {
        SaveProfileAccessSettings(state);
    }
}

// Load the currently selected profile from storage and synchronise its skills.
void RefreshActiveProfile(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    state.profileLoading = true;
    state.profileLoadError.clear();
    state.active.reset();
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.profiles.size())) {
        state.profileLoading = false;
        return;
    }
    const auto& info = state.profiles[state.selectedIndex];
    if (!storage.set_active_profile(info.id)) {
        ReportProfileError(state, u8"Не удалось активировать профиль.");
        return;
    }
    if (auto loaded = storage.load_profile()) {
        SyncProfileWithCatalog(*loaded, catalog);
        storage.save_profile(*loaded);
        state.active = GuiProfile{*loaded, info.id};
        state.ui.lastProfileId = info.id;
        TrackRecentProfile(state, info.id);
        state.activityLogs[state.active->id];
        PrepareXpEntries(state, catalog);
        state.profileLoadError.clear();
        state.profileLoading = false;
    } else {
        ReportProfileError(state, std::string(u8"Не удалось загрузить профиль [") + info.id + u8"].");
    }
}

void ReapplyRulesToProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    auto list = storage.list_profiles();
    std::string focusId = state.active ? state.active->id : std::string{};
    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            // Preserve current level/progress but re-evaluate XP with the new rules.
            int level = profile->overall_level();
            int progress = profile->level_progress();
            profile->set_level_and_progress(level, progress);
            storage.save_profile(*profile);
        }
    }
    if (!focusId.empty()) {
        storage.set_active_profile(focusId);
    }
}

void SyncRankSelection(GuiState& state) {
    if (!state.active) {
        state.selectedRankIndex = 0;
        return;
    }
    const auto& opts = RankOptions();
    int bestIdx = 0;
    for (int idx = 0; idx < static_cast<int>(opts.size()); ++idx) {
        if (state.active->profile.overall_level() >= opts[idx].level) {
            bestIdx = idx;
        }
    }
    state.selectedRankIndex = bestIdx;
}

bool SelectProfileById(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& id) {
    if (id.empty()) return false;
    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
        if (state.profiles[i].id == id) {
            state.selectedIndex = i;
            RefreshActiveProfile(state, storage, catalog);
            SyncRankSelection(state);
            PrepareTaskShares(state);
            return true;
        }
    }
    RefreshProfiles(state, storage, catalog, id);
    SyncRankSelection(state);
    return state.active.has_value();
}

// Reload profile list and try to keep the previously selected/active entry.
void RefreshProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& preferredId) {
    std::string currentId;
    if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.profiles.size())) {
        currentId = state.profiles[state.selectedIndex].id;
    }

    state.profiles = LoadProfiles(storage);
    state.profileProfessionDirty = true;
    state.adminStatsDirty = true;
    state.profileLoading = false;
    state.profileLoadError.clear();
    PruneProfileAccessState(state);
    if (state.profiles.empty()) {
        state.selectedIndex = -1;
        state.active.reset();
        state.profileLoadError = u8"Профили не найдены.";
        return;
    }

    std::string targetId = preferredId.empty() ? currentId : preferredId;
    if (!targetId.empty()) {
        for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
            if (state.profiles[i].id == targetId) {
                state.selectedIndex = i;
                RefreshActiveProfile(state, storage, catalog);
                PrepareTaskShares(state);
                return;
            }
        }
    }

    state.selectedIndex = 0;
    RefreshActiveProfile(state, storage, catalog);
    PrepareTaskShares(state);
}
