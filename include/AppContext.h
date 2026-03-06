#pragma once

#include <filesystem>

class IJobStorage;
class SkillCatalog;

struct AppContext {
    std::filesystem::path storageDir;
    IJobStorage& storage;
    SkillCatalog& catalog;
};
