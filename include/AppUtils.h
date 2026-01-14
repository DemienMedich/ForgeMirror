#pragma once

#include <filesystem>
#include <string>

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
    bool view3d = true;
};

// Convert overall level to human-readable rank (Intern, Junior, etc.).
std::string DescribeOverallRank(const Profile& profile);

// Ensure default admin profile exists in storage.
void EnsureAdminProfile(IJobStorage& storage, SkillCatalog& catalog);

std::filesystem::path ResolveStorageDirectory();

// Align profile skill names/weights with the catalog definitions.
void SyncProfileWithCatalog(Profile& profile, SkillCatalog& catalog);

// Load admin password from environment or storage config.
std::string LoadAdminPassword(const std::filesystem::path& storageDir);
bool SetAdminPassword(const std::filesystem::path& storageDir, const std::string& password);

// Load module toggles from environment (FORGEMIRROR_DISABLE_MODULES=tasks,pipeline,...).
ModuleToggles LoadModuleToggles();
