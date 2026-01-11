std::optional<std::filesystem::path> FindAssetUpwards(const std::filesystem::path& relative) {
    std::error_code ec;
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        auto candidate = current / relative;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        auto parent = current.parent_path();
        if (parent == current) break;
        current = std::move(parent);
    }
    return std::nullopt;
}

// Pull profile list from storage and sort it for deterministic GUI display.
std::vector<IJobStorage::ProfileInfo> LoadProfiles(IJobStorage& storage) {
    auto list = storage.list_profiles();
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return list;
}

void InitStorageContext(const std::filesystem::path& storageDir, SkillCatalog& catalog,
                        GameplayConfig& gameplayConfig, std::unique_ptr<IJobStorage>& storage) {
    gameplayConfig = LoadGameplayConfig(storageDir);
    SetGameplayConfig(gameplayConfig);
    storage.reset(CreateFileStorage(storageDir));
}
