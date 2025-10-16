#pragma once

#include <string>
#include <vector>
#include <optional>

#include "Profile.h"

// Storage abstraction for local persistence and offline queues.
class IJobStorage {
public:
    virtual ~IJobStorage() = default;

    // Profile lifecycle
    virtual bool set_active_profile(const std::string& id) = 0;
    struct ProfileInfo { std::string id; std::string name; bool archived = false; };
    virtual std::vector<ProfileInfo> list_profiles() = 0;
    virtual std::optional<Profile> load_profile() = 0;
    virtual bool save_profile(const Profile& profile) = 0;
    virtual std::optional<ProfileInfo> create_profile(const Profile& profile) = 0;
    virtual bool set_archived(const std::string& name, bool archived) = 0;
    virtual bool delete_profile(const std::string& name) = 0;

    // Auth token persistence
    virtual std::optional<std::string> load_token() = 0;
    virtual bool save_token(const std::string& token) = 0;

    // Offline XP queue
    struct XpEvent { std::string skill; int amount = 0; };
    virtual std::vector<XpEvent> load_queue() = 0;
    virtual bool save_queue(const std::vector<XpEvent>& queue) = 0;
};
