static void TrackRecentProfile(GuiState& state, const std::string& id);

static void ReportProfileError(GuiState& state, std::string message) {
    state.profileLoadError = std::move(message);
    state.profileLoading = false;
    SetStatus(state, state.profileLoadError, 1.0f, 0.45f, 0.45f, 1.0f, AppLogLevel::Error, u8"Профиль");
}

static void ApplyLoadedProfileResult(GuiState& state, AppContext& app,
                                     const ActiveProfileLoadResult& result,
                                     bool trackRecent = true) {
    state.active.reset();
    if (!result.hasSelection) {
        state.profileLoading = false;
        state.profileLoadError.clear();
        return;
    }
    if (!result.ok || !result.profile) {
        ReportProfileError(state, result.errorMessage.empty() ? std::string(u8"Не удалось загрузить профиль.")
                                                              : result.errorMessage);
        return;
    }
    state.active = GuiProfile{*result.profile, result.profileId};
    state.ui.lastProfileId = result.profileId;
    if (trackRecent) {
        TrackRecentProfile(state, result.profileId);
    }
    state.activityLogs[state.active->id];
    PrepareXpEntries(state, app.catalog);
    state.profileLoadError.clear();
    state.profileLoading = false;
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

static std::unordered_map<std::string, std::int64_t> ParseTrustedProfileList(const std::string& raw) {
    std::unordered_map<std::string, std::int64_t> out;
    if (raw.empty()) return out;
    std::istringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = TrimStringGui(item);
        if (item.empty()) continue;
        const auto sep = item.find(':');
        if (sep == std::string::npos) continue;
        std::string id = TrimStringGui(item.substr(0, sep));
        std::string val = TrimStringGui(item.substr(sep + 1));
        if (id.empty() || val.empty()) continue;
        long long ts = 0;
        try {
            ts = std::stoll(val);
        } catch (...) {
            continue;
        }
        if (ts <= 0) continue;
        auto it = out.find(id);
        if (it == out.end() || it->second < ts) {
            out[id] = static_cast<std::int64_t>(ts);
        }
    }
    return out;
}

static std::string JoinTrustedProfileList(const std::unordered_map<std::string, std::int64_t>& trusted) {
    if (trusted.empty()) return {};
    std::vector<std::pair<std::string, std::int64_t>> items;
    items.reserve(trusted.size());
    for (const auto& entry : trusted) {
        if (!entry.first.empty() && entry.second > 0) {
            items.emplace_back(entry.first, entry.second);
        }
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::ostringstream out;
    out.imbue(std::locale::classic());
    bool first = true;
    for (const auto& item : items) {
        if (!first) out << ',';
        first = false;
        out << item.first << ':' << item.second;
    }
    return out.str();
}

static void SaveProfileAccessSettings(GuiState& state);

static void ApplyProfileAccessSettings(GuiState& state) {
    state.unlockedProfiles.clear();
    state.trustedProfiles = ParseTrustedProfileList(state.ui.profileTrusted);
    const std::int64_t now = NowSeconds();
    for (auto it = state.trustedProfiles.begin(); it != state.trustedProfiles.end(); ) {
        if (it->first.empty() || it->second <= now) {
            it = state.trustedProfiles.erase(it);
        } else {
            ++it;
        }
    }
    bool migrated = false;
    if (!state.ui.profileUnlocked.empty()) {
        const auto legacy = SplitProfileIdList(state.ui.profileUnlocked);
        if (!legacy.empty()) {
            const std::int64_t expiresAt = now + 30LL * 86400;
            for (const auto& id : legacy) {
                if (id.empty()) continue;
                if (state.trustedProfiles.find(id) == state.trustedProfiles.end()) {
                    state.trustedProfiles[id] = expiresAt;
                    migrated = true;
                }
            }
        }
        state.ui.profileUnlocked.clear();
        migrated = true;
    }
    state.recentProfiles = SplitProfileIdList(state.ui.profileRecent);
    constexpr size_t kMaxRecentProfiles = 6;
    if (state.recentProfiles.size() > kMaxRecentProfiles) {
        state.recentProfiles.resize(kMaxRecentProfiles);
    }
    if (migrated) {
        SaveProfileAccessSettings(state);
    }
}

static bool UpdateProfileAccessSettings(GuiState& state) {
    const std::string trustedJoined = JoinTrustedProfileList(state.trustedProfiles);
    const std::string recentJoined = JoinProfileIdList(state.recentProfiles);
    bool changed = false;
    if (state.ui.profileTrusted != trustedJoined) {
        state.ui.profileTrusted = trustedJoined;
        changed = true;
    }
    if (!state.ui.profileUnlocked.empty()) {
        state.ui.profileUnlocked.clear();
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
    const std::int64_t now = NowSeconds();
    bool changed = false;
    for (auto it = state.unlockedProfiles.begin(); it != state.unlockedProfiles.end(); ) {
        if (validIds.find(*it) == validIds.end()) {
            it = state.unlockedProfiles.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    for (auto it = state.trustedProfiles.begin(); it != state.trustedProfiles.end(); ) {
        if (validIds.find(it->first) == validIds.end() || it->second <= now) {
            it = state.trustedProfiles.erase(it);
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
void RefreshActiveProfile(GuiState& state, AppContext& app) {
    state.profileLoading = true;
    state.profileLoadError.clear();
    ApplyLoadedProfileResult(state, app, LoadActiveProfile(app, state.profiles, state.selectedIndex));
}

void RefreshActiveProfile(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    AppContext app{state.storageDir, storage, catalog};
    RefreshActiveProfile(state, app);
}


void ReapplyRulesToProfiles(GuiState& state, AppContext& app) {
    auto& storage = app.storage;
    auto& catalog = app.catalog;
    auto list = storage.list_profiles();
    std::string focusId = state.active ? state.active->id : std::string{};
    for (const auto& info : list) {
        if (!storage.set_active_profile(info.id)) continue;
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
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

void ReapplyRulesToProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog) {
    AppContext app{state.storageDir, storage, catalog};
    ReapplyRulesToProfiles(state, app);
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

bool SelectProfileById(GuiState& state, AppContext& app, const std::string& id) {
    if (id.empty()) return false;
    for (int i = 0; i < static_cast<int>(state.profiles.size()); ++i) {
        if (state.profiles[static_cast<size_t>(i)].id == id) {
            state.selectedIndex = i;
            state.profileLoading = true;
            ApplyLoadedProfileResult(state, app, LoadActiveProfile(app, state.profiles, i));
            SyncRankSelection(state);
            PrepareTaskShares(state);
            return state.active.has_value();
        }
    }
    RefreshProfiles(state, app, id);
    SyncRankSelection(state);
    return state.active.has_value();
}

bool SelectProfileById(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& id) {
    AppContext app{state.storageDir, storage, catalog};
    return SelectProfileById(state, app, id);
}


// Reload profile list and try to keep the previously selected/active entry.
void RefreshProfiles(GuiState& state, AppContext& app, const std::string& preferredId) {
    state.profileLoading = true;
    state.profileLoadError.clear();

    ProfilesReloadResult result = ReloadProfiles(app, state.profiles, state.selectedIndex, preferredId);
    state.profiles = std::move(result.profiles);
    state.profileProfessionDirty = true;
    state.adminStatsDirty = true;
    PruneProfileAccessState(state);
    if (state.profiles.empty()) {
        state.selectedIndex = -1;
        state.active.reset();
        state.profileLoading = false;
        state.profileLoadError = result.errorMessage.empty() ? std::string(u8"Профили не найдены.")
                                                             : result.errorMessage;
        return;
    }

    state.selectedIndex = result.selectedIndex;
    ApplyLoadedProfileResult(state, app, result.active);
    PrepareTaskShares(state);
}

void RefreshProfiles(GuiState& state, IJobStorage& storage, SkillCatalog& catalog, const std::string& preferredId) {
    AppContext app{state.storageDir, storage, catalog};
    RefreshProfiles(state, app, preferredId);
}

