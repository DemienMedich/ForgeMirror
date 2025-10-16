#include "IApiClient.h"
#include <optional>
#include <unordered_map>
#include <algorithm>

// Простейшая заглушка API: хранит состояние локально, имитирует сервер
class FakeApiClient : public IApiClient {
public:
    void set_base_url(std::string) override {}

    void set_token(std::string t) override {
        token_ = std::move(t);
        // токен формата fake-token-<user>
        constexpr std::string_view prefix = "fake-token-";
        if (token_.rfind(prefix, 0) == 0) {
            currentUser_ = token_.substr(prefix.size());
        }
    }

    std::optional<std::string> login(const std::string& user, const std::string&) override {
        if (user.empty()) return std::nullopt;
        currentUser_ = user;
        token_ = "fake-token-" + user;
        if (profiles_.count(user) == 0) {
            profiles_.emplace(user, Profile(user));
        }
        return token_;
    }

    std::optional<Profile> get_my_profile() override {
        if (currentUser_.empty()) return std::nullopt;
        auto it = profiles_.find(currentUser_);
        if (it == profiles_.end()) return std::nullopt;
        return it->second;
    }

    bool post_xp(const std::string& skill, int amount) override {
        if (currentUser_.empty() || amount <= 0) return false;
        auto it = profiles_.find(currentUser_);
        if (it == profiles_.end()) return false;
        it->second.add_skill(skill);
        it->second.grant_xp(skill, amount);
        return true;
    }

    std::vector<LeaderEntry> leaderboard() override {
        std::vector<LeaderEntry> out;
        out.reserve(profiles_.size());
        for (const auto& kv : profiles_) {
            out.push_back(LeaderEntry{kv.second.name(), kv.second.overall_level()});
        }
        std::sort(out.begin(), out.end(), [](const LeaderEntry& a, const LeaderEntry& b) {
            return a.overall > b.overall;
        });
        return out;
    }

private:
    std::string token_;
    std::string currentUser_;
    std::unordered_map<std::string, Profile> profiles_;
};

IApiClient* CreateFakeApi() { return new FakeApiClient(); }
