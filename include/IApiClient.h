#pragma once
#include <string>
#include <vector>
#include <optional>
#include "Profile.h"

// Интерфейс сетевого клиента (онлайн API)
class IApiClient {
public:
    virtual ~IApiClient() = default;

    virtual void set_base_url(std::string url) = 0;
    virtual void set_token(std::string token) = 0;

    // Аутентификация
    virtual std::optional<std::string> login(const std::string& user, const std::string& pass) = 0;

    // Профиль
    virtual std::optional<Profile> get_my_profile() = 0;
    virtual bool post_xp(const std::string& skill, int amount) = 0;

    // Лидерборд (минимум: имя и уровень)
    struct LeaderEntry { std::string name; int overall = 1; };
    virtual std::vector<LeaderEntry> leaderboard() = 0;
};

