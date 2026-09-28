#pragma once

#include "AppDomainTypes.h"
#include <filesystem>
#include <functional>
#include <string>

class IJobStorage;
class SkillCatalog;

struct AppContext {
    std::filesystem::path storageDir;
    IJobStorage& storage;
    SkillCatalog& catalog;
    // Optional observer for generic, privacy-safe core outcomes. An empty sink
    // keeps non-Qt clients and existing callers behaviorally unchanged.
    std::function<void(AppLogLevel, const std::string&)> eventLogger{};
};
