#include "IJobStorage.h"

#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace {
std::string generate_id(int value) {
    std::ostringstream ss;
    ss << std::setw(4) << std::setfill('0') << value;
    return ss.str();
}
}

class FakeStorage : public IJobStorage {
public:
    bool set_active_profile(const std::string& id) override {
        auto it = entries_.find(id);
        if (it == entries_.end() || it->second.archived) return false;
        activeId_ = id;
        return true;
    }

    std::vector<ProfileInfo> list_profiles() override {
        std::vector<ProfileInfo> out;
        out.reserve(entries_.size());
        for (const auto& [id, entry] : entries_) {
            out.push_back(ProfileInfo{id, entry.profile.name(), entry.archived});
        }
        return out;
    }

    std::optional<Profile> load_profile() override {
        auto it = entries_.find(activeId_);
        if (it == entries_.end() || it->second.archived) return std::nullopt;
        return it->second.profile;
    }

    bool save_profile(const Profile& profile) override {
        auto it = entries_.find(activeId_);
        if (it == entries_.end() || it->second.archived) return false;
        it->second.profile = profile;
        return true;
    }

    std::optional<ProfileInfo> create_profile(const Profile& profile) override {
        const std::string id = generate_id(nextId_++);
        entries_.emplace(id, Entry{profile});
        activeId_ = id;
        return ProfileInfo{id, profile.name(), false};
    }

    bool set_archived(const std::string& id, bool archived) override {
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        it->second.archived = archived;
        if (archived && activeId_ == id) activeId_.clear();
        return true;
    }

    bool delete_profile(const std::string& id) override {
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        entries_.erase(it);
        tokens_.erase(id);
        queues_.erase(id);
        if (activeId_ == id) activeId_.clear();
        return true;
    }

    std::optional<std::string> load_token() override {
        auto it = tokens_.find(activeId_);
        if (it == tokens_.end()) return std::nullopt;
        return it->second;
    }

    bool save_token(const std::string& token) override {
        if (activeId_.empty()) return false;
        tokens_[activeId_] = token;
        return true;
    }

    std::vector<XpEvent> load_queue() override {
        auto it = queues_.find(activeId_);
        if (it == queues_.end()) return {};
        return it->second;
    }

    bool save_queue(const std::vector<XpEvent>& q) override {
        if (activeId_.empty()) return false;
        queues_[activeId_] = q;
        return true;
    }

private:
    struct Entry {
        Profile profile;
        bool archived = false;
    };

    std::string activeId_;
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, std::string> tokens_;
    std::unordered_map<std::string, std::vector<XpEvent>> queues_;
    int nextId_ = 1;
};

IJobStorage* CreateFakeStorage() { return new FakeStorage(); }

