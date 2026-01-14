#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "AppUtils.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include "GameplayConfig.h"
#include "CloudSync.h"

static bool WriteFile(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    return out.good();
}

static bool TestProfileRoundtrip(const std::filesystem::path& dir) {
    Profile p("Test");
    p.add_skill("sk_test", 2, 1.0);
    p.add_skill("sk_other", 5, 1.0);
    p.set_total_xp(1234);
    p.set_level_progress(34);
    p.set_xp_to_next_level(200);
    const auto path = dir / "0001.ini";
    if (!p.save(path)) return false;
    if (!std::filesystem::exists(path)) return false;
    auto loaded = Profile::load(path);
    if (!loaded) return false;
    return loaded->overall_level() == p.overall_level();
}

static bool TestGameplayConfig(const std::filesystem::path& dir) {
    GameplayConfig cfg;
    cfg.levelBaseXp = 10;
    cfg.levelLinearXp = 2;
    cfg.levelQuadraticXp = 1;
    auto path = dir / "meta" / "gameplay.ini";
    if (!SaveGameplayConfig(cfg, dir)) return false;
    GameplayConfig loaded = LoadGameplayConfig(dir);
    return loaded.levelBaseXp == cfg.levelBaseXp && loaded.levelLinearXp == cfg.levelLinearXp;
}

static bool TestTasksPipelineRoundtrip(const std::filesystem::path& dir) {
    const std::string tasks = R"([{"id":"t1","title":"Task","project":"P"}])";
    const std::string pipeline = R"({"steps":[{"title":"A"}]})";
    if (!WriteFile(dir / "meta" / "tasks.json", tasks)) return false;
    if (!WriteFile(dir / "meta" / "pipeline.json", pipeline)) return false;
    return std::filesystem::exists(dir / "meta" / "tasks.json") &&
           std::filesystem::exists(dir / "meta" / "pipeline.json");
}

static bool TestWhitelist(const std::filesystem::path& dir) {
    WriteFile(dir / "bad.txt", "x");
    WriteFile(dir / "meta" / "bad.json", "{}");
    int removed = 0;
    RemoveStrayFiles(dir, removed);
    return removed >= 2 && !std::filesystem::exists(dir / "bad.txt");
}

int main() {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "forgemirror_smoke";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);

    const bool okProfile = TestProfileRoundtrip(tmp);
    const bool okRules = TestGameplayConfig(tmp);
    const bool okTasks = TestTasksPipelineRoundtrip(tmp);
    const bool okWhitelist = TestWhitelist(tmp);

    if (okProfile && okRules && okTasks && okWhitelist) {
        std::cout << "smoke_core: OK\n";
        return 0;
    }
    std::cerr << "smoke_core failed: "
              << "profile=" << okProfile
              << " rules=" << okRules
              << " tasks=" << okTasks
              << " whitelist=" << okWhitelist << "\n";
    return 1;
}
