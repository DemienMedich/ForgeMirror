#pragma once

#include <array>
#include <filesystem>

#include "Profile.h"

struct GameplayConfig {
    int levelBaseXp = 1500;
    int levelLinearXp = 250;
    int levelQuadraticXp = 50;
    std::array<int, Profile::kCategoryCount> categoryBaseXp = Profile::kCategoryBaseXp;
    float focusBaseBonus = 0.6f;
    float focusAdditionalBonus = 0.4f;
    float repeatRewardFactor = 0.35f;
    float recoveryRewardFactor = 0.6f;
    int recoveryWarmupTasks = 3;
};

const GameplayConfig& GetGameplayConfig();
void SetGameplayConfig(const GameplayConfig& config);
GameplayConfig SanitizeGameplayConfig(const GameplayConfig& config);
GameplayConfig LoadGameplayConfig(const std::filesystem::path& storageDir);
bool SaveGameplayConfig(const GameplayConfig& config, const std::filesystem::path& storageDir);
std::filesystem::path GameplayConfigPath(const std::filesystem::path& storageDir);
