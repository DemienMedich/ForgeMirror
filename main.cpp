// JobSkill console client: profile issuing, leveling, and simple CLI session

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <cctype>
#include <optional>
#include <unordered_set>
#include <sstream>

#include "Profile.h"
#include "IJobStorage.h"
#include "IApiClient.h"
#include "AppUtils.h"
#include "SkillCatalog.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

struct ProfileBlueprint {
    std::string name;
    std::vector<std::string> skills;
};

struct ActiveProfile {
    Profile profile;
    std::string id;
};

static const std::vector<ProfileBlueprint> kIssuedProfiles = {};

static const ProfileBlueprint* find_blueprint(const std::string& name) {
    for (const auto& bp : kIssuedProfiles) {
        if (bp.name == name) return &bp;
    }
    return nullptr;
}

static bool is_profile_id(const std::string& value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

static std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

static Profile make_profile_from_blueprint(const ProfileBlueprint& bp, SkillCatalog& catalog) {
    Profile profile(bp.name);
    for (const auto& skill : bp.skills) {
        if (auto canonical = catalog.canonical(skill)) {
            profile.add_skill(*canonical, 1, catalog.weight(*canonical));
        } else {
            profile.add_skill(skill, 1, catalog.weight(skill));
        }
    }
    return profile;
}

static void print_profile(const Profile& p) {
    std::cout << "=== Profile: " << p.name() << " ===\n";
    std::cout << "Overall level: " << p.overall_level() << " (" << DescribeOverallRank(p.overall_level()) << ")\n";
    std::cout << "Skills:\n";
    auto skills = p.list_skills();
    for (const auto& s : skills) {
        std::cout << " - " << std::left << std::setw(14) << s.name
                  << " L" << s.level
                  << " | XP: " << s.xp << "/" << s.xpToNext
                  << " | W: " << std::fixed << std::setprecision(2) << s.weight << std::defaultfloat
                  << "\n";
    }
}

static void show_skill_catalog(const SkillCatalog& catalog) {
    std::cout << "\nSkill Catalog:\n";
    for (const auto& skill : catalog.skills()) {
        std::cout << " - " << skill << " (weight "
                  << std::fixed << std::setprecision(2) << catalog.weight(skill) << std::defaultfloat
                  << ")\n";
    }
}

static void show_available_profiles(IJobStorage& storage) {
    auto stored = storage.list_profiles();
    std::unordered_set<std::string> existing;

    std::cout << "\nSaved profiles:";
    if (stored.empty()) {
        std::cout << "\n - (none yet)\n";
    } else {
        std::cout << "\n";
        for (const auto& info : stored) {
            existing.insert(info.name);
            std::cout << " - [" << info.id << "] " << info.name;
            std::cout << (info.archived ? " (archived)" : " (active)") << "\n";
        }
    }

    std::cout << "Issued profiles:";
    if (kIssuedProfiles.empty()) {
        std::cout << "\n - (none)\n";
    } else {
        std::cout << "\n";
        for (const auto& bp : kIssuedProfiles) {
            std::cout << " - " << bp.name;
            if (existing.count(bp.name)) {
                std::cout << " (already created)";
            }
            std::cout << "\n";
        }
    }
}

static std::optional<ActiveProfile> acquire_profile(IJobStorage& storage, SkillCatalog& catalog, const std::string& token) {
    if (is_profile_id(token)) {
        if (!storage.set_active_profile(token)) {
            std::cout << "Profile ID not found or archived.\n";
            return std::nullopt;
        }
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            storage.save_profile(*profile);
            return ActiveProfile{*profile, token};
        }
        std::cout << "Profile data is missing.\n";
        return std::nullopt;
    }

    auto stored = storage.list_profiles();
    std::vector<IJobStorage::ProfileInfo> matches;
    for (const auto& info : stored) {
        if (info.name == token) matches.push_back(info);
    }

    if (!matches.empty()) {
        std::optional<IJobStorage::ProfileInfo> chosen;
        for (const auto& info : matches) {
            if (!info.archived) { chosen = info; break; }
        }
        if (!chosen) {
            std::cout << "All profiles named '" << token << "' are archived. Use restore <id>.\n";
            return std::nullopt;
        }
        if (!storage.set_active_profile(chosen->id)) {
            std::cout << "Unable to open profile ID " << chosen->id << ".\n";
            return std::nullopt;
        }
        if (auto profile = storage.load_profile()) {
            SyncProfileWithCatalog(*profile, catalog);
            storage.save_profile(*profile);
            return ActiveProfile{*profile, chosen->id};
        }
        std::cout << "Failed to load profile data.\n";
        return std::nullopt;
    }

    Profile profile(token);
    if (auto bp = find_blueprint(token)) {
        profile = make_profile_from_blueprint(*bp, catalog);
    } else {
        std::string confirm;
        std::cout << "Profile '" << token << "' not found. Create new profile? (yes/no): ";
        if (!(std::cin >> confirm) || confirm != "yes") {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Creation cancelled.\n";
            return std::nullopt;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (auto skill = catalog.canonical("Modeling")) profile.add_skill(*skill, 1, catalog.weight(*skill));
        if (auto skill = catalog.canonical("Texturing")) profile.add_skill(*skill, 1, catalog.weight(*skill));
    }

    SyncProfileWithCatalog(profile, catalog);
    auto info = storage.create_profile(profile);
    if (!info) {
        std::cout << "Failed to create profile.\n";
        return std::nullopt;
    }
    storage.save_profile(profile);
    std::cout << "Created profile '" << profile.name() << "' with ID " << info->id << ".\n";
    return ActiveProfile{profile, info->id};
}

static bool run_profile_session(Profile& profile, const std::string& profileId, IJobStorage& storage, IApiClient& api, SkillCatalog& catalog) {
    auto sync_now = [&](bool verbose = true) {
        bool ok = storage.save_profile(profile);
        if (!ok) {
            if (verbose) std::cout << "Warning: failed to save profile locally.\n";
        } else if (verbose) {
            std::cout << "Sync complete.\n";
        }
        return ok;
    };

    sync_now(false);

    std::cout << "\nLogged in as " << profile.name() << " [" << profileId << "]." << std::endl;
    print_profile(profile);
    std::cout << "\nCommands: addxp <skill> <amount> | show | sync | logout | quit\n";

    std::string cmd;
    while (true) {
        std::cout << "> ";
        if (!(std::cin >> cmd)) return true; // EOF => exit app
        if (cmd == "show") {
            print_profile(profile);
            continue;
        }
        if (cmd == "sync") {
            sync_now();
            continue;
        }
        if (cmd == "logout") {
            sync_now(false);
            std::cout << "Logged out.\n";
            return false;
        }
        if (cmd == "quit" || cmd == "exit") {
            sync_now(false);
            return true;
        }
        if (cmd == "addxp") {
            std::string skill; int amount;
            if (std::cin >> skill >> amount) {
                if (amount <= 0) {
                    std::cout << "Amount must be positive.\n";
                    continue;
                }
                auto canonical = catalog.canonical(skill);
                if (!canonical) {
                    std::cout << "Skill not in catalog. Ask admin to add it first.\n";
                    continue;
                }
                const std::string skillName = *canonical;
                const double skillWeight = catalog.weight(skillName);
                profile.add_skill(skillName, 1, skillWeight);
                const bool leveled = profile.grant_xp(skillName, amount);
                const bool apiOk = api.post_xp(skillName, amount);
                const bool synced = sync_now(false);
                std::cout << (leveled ? "Level up!" : "XP added.") << "\n";
                if (!apiOk) std::cout << "Warning: failed to notify server.\n";
                if (synced) std::cout << "Auto-sync complete.\n";
                else std::cout << "Warning: auto-sync failed. Try 'sync'.\n";
            } else {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Invalid input.\n";
            }
            continue;
        }
        std::cout << "Unknown command. Use: addxp / show / sync / logout / quit\n";
    }
}

constexpr const char* kAdminPassword = "admin123";

int main() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    extern IJobStorage* CreateFakeStorage();
    extern IJobStorage* CreateFileStorage(const std::filesystem::path& dir);
    extern IApiClient* CreateFakeApi();

    auto storageDir = ResolveStorageDirectory();
    SkillCatalog catalog(storageDir);

    std::unique_ptr<IJobStorage> storage(CreateFileStorage(storageDir));
    EnsureAdminProfile(*storage, catalog);

    std::unique_ptr<IApiClient> api(CreateFakeApi());

    std::cout << "Welcome to JobSkill profiles";
#ifdef APP_VERSION
    std::cout << " (version " << APP_VERSION << ")";
#endif
    std::cout << "!\n";

    bool exitApp = false;
    while (!exitApp) {
        std::cout << "\n=== Main Menu ===\n";
        std::cout << "Commands: list | skills | archive <id> | restore <id> | delete <id> | login <id|name> | help | quit\n> ";
        std::string menuCmd;
        if (!(std::cin >> menuCmd)) break;

        if (menuCmd == "quit" || menuCmd == "exit") {
            exitApp = true;
            break;
        }

        if (menuCmd == "help") {
            std::cout << "Use 'list' to see available profiles.\n"
                         "Use 'skills' to show the skill catalog.\n"
                         "Use 'archive <id>' / 'restore <id>' to move profiles.\n"
                         "Use 'delete <id>' to remove a profile permanently.\n"
                         "Use 'login <id|name>' to enter a profile session.\n"
                         "Use 'quit' to close the application.\n";
            continue;
        }

        if (menuCmd == "list") {
            show_available_profiles(*storage);
            continue;
        }

        if (menuCmd == "skills") {
            show_skill_catalog(catalog);
            continue;
        }


        if (menuCmd == "archive" || menuCmd == "restore" || menuCmd == "delete") {
            std::string profileId;
            if (!(std::cin >> profileId)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Invalid input.\n";
                continue;
            }

            if (!is_profile_id(profileId)) {
                std::cout << "Use numeric profile IDs (see 'list').\n";
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }

            if (menuCmd == "archive") {
                if (storage->set_archived(profileId, true)) {
                    std::cout << "Profile archived.\n";
                } else {
                    std::cout << "Failed to archive profile.\n";
                }
                continue;
            }

            if (menuCmd == "restore") {
                if (storage->set_archived(profileId, false)) {
                    std::cout << "Profile restored.\n";
                } else {
                    std::cout << "Failed to restore profile.\n";
                }
                continue;
            }

            // delete
            std::string confirm;
            std::cout << "Type 'yes' to confirm deletion of " << profileId << ": ";
            std::cin >> confirm;
            if (confirm == "yes" && storage->delete_profile(profileId)) {
                std::cout << "Profile deleted.\n";
            } else {
                std::cout << "Deletion cancelled or failed.\n";
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }

        if (menuCmd == "login") {
            std::string token;
            if (!(std::cin >> token)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Invalid input.\n";
                continue;
            }

            auto activeProfile = acquire_profile(*storage, catalog, token);
            if (!activeProfile) {
                continue;
            }

            auto authToken = api->login(activeProfile->profile.name(), "default");
            if (authToken) {
                storage->save_token(*authToken);
                api->set_token(*authToken);
            } else {
                std::cout << "Warning: unable to authenticate with server stub.\n";
            }

            bool requestedExit = run_profile_session(activeProfile->profile, activeProfile->id, *storage, *api, catalog);
            if (requestedExit) {
                exitApp = true;
            }
            continue;
        }

        std::cout << "Unknown command. Use: list / login / help / quit\n";
    }

    std::cout << "See you soon!\n";
    return 0;
}



