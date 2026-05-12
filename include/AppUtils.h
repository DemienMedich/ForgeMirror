#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

class IJobStorage;
class Profile;
class SkillCatalog;

struct ModuleToggles {
    bool tasks = true;
    bool pipeline = true;
    bool achievements = true;
    bool shortcuts = true;
    bool pomodoro = true;
    bool cloud = true;
    bool view3d = false;
    bool professions = true;
};


struct StorageLogEntry {
    std::int64_t timestamp = 0;
    double amount = 0.0;
    std::string action;
    std::string note;
};

struct StorageVaultData {
    std::string currencyName;
    std::string currencyCode;
    double balance = 0.0;
    std::int64_t updatedAt = 0;
    std::int64_t revision = 0;
    std::string contentHash;
    int logLimit = 10;
    std::vector<StorageLogEntry> log;
    int pomodoroStartMinutes = 540;
    int pomodoroEndMinutes = 1080;
    int pomodoroMinMinutes = 20;
    int pomodoroCoinsPerCycle = 1;
    int pomodoroDaysMask = 0;
};


// Convert overall level to human-readable rank (Intern, Junior, etc.).
std::string DescribeOverallRank(const Profile& profile);

// Ensure default admin profile exists in storage.
void EnsureAdminProfile(IJobStorage& storage, SkillCatalog& catalog);

std::filesystem::path ResolveStorageDirectory();

// Project data folder (repo root / data).
std::filesystem::path ProjectSeedDataDir();
// True if project data folder has profiles/skills/etc.
bool HasProjectSeedData();

// Align profile skill names/weights with the catalog definitions.
void SyncProfileWithCatalog(Profile& profile, SkillCatalog& catalog);

// Load admin password from environment or storage config.
std::string LoadAdminPassword(const std::filesystem::path& storageDir);
bool SetAdminPassword(const std::filesystem::path& storageDir, const std::string& password);
bool LoadAdminStayLoggedIn(const std::filesystem::path& storageDir);
bool SetAdminStayLoggedIn(const std::filesystem::path& storageDir, bool enabled);
std::string EncodePassword(const std::string& password);
std::string DecodePassword(const std::string& value);
std::string GenerateRandomPassword(size_t length = 10);
std::string SerializeProfileTaskRollbackSnapshot(const Profile& profile);
bool ApplyProfileTaskRollbackSnapshot(const std::string& snapshot, Profile& profile);
void AppendProfileAudit(const std::filesystem::path& storageDir, const std::string& profileId,
                        const std::string& action, const std::string& details = {});

// Load module toggles from environment (FORGEMIRROR_DISABLE_MODULES=tasks,pipeline,...).
ModuleToggles LoadModuleToggles();
// Storage vault data (meta/storage.json).
StorageVaultData LoadStorageVault(const std::filesystem::path& storageDir);
bool SaveStorageVault(const std::filesystem::path& storageDir, const StorageVaultData& data);

// Banner text storage (meta/banner.json).
std::filesystem::path BannerTextPath(const std::filesystem::path& storageDir);
std::vector<std::string> LoadBannerTexts(const std::filesystem::path& storageDir);
bool SaveBannerTexts(const std::filesystem::path& storageDir, const std::vector<std::string>& texts);


// Find non-whitelisted files in storage (relative paths in outSamples, truncated to maxSamples).
bool FindStrayStorageFiles(const std::filesystem::path& storageDir, size_t maxSamples,
                           std::vector<std::string>& outSamples, int& totalCount);

// Collect full list of non-whitelisted files/dirs in storage (relative paths).
bool CollectStrayStorageFiles(const std::filesystem::path& storageDir, std::vector<std::string>& outList);
