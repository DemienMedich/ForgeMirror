#pragma once

#include <filesystem>
#include <string>

class SkillCatalog;
class IJobStorage;

// Convert overall level to human-readable rank (Intern, Junior, etc.).
std::string DescribeOverallRank(int overallLevel);

// Ensure default admin profile exists in storage.
void EnsureAdminProfile(IJobStorage& storage, SkillCatalog& catalog);

std::filesystem::path ResolveStorageDirectory();
