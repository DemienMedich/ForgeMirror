#pragma once
#include "IJobStorage.h"
#include <QByteArray>
#include <filesystem>
#include <cstdint>
#include <utility>

// Local UI authorization only; optional trust persists an ID and expiry, never a credential.
class QtProfileSession {
public:
    explicit QtProfileSession(std::filesystem::path directory = {}) : directory_(std::move(directory)) {}
    bool unlock(IJobStorage& storage, const std::string& id, const std::string& password, int trustDays = 0);
    bool isUnlocked(IJobStorage& storage, const std::string& id);
    bool lock(bool forgetTrust = false);
    bool isTrusted() const { return trusted_; }
    std::int64_t trustedUntil() const { return trustedUntil_; }
private:
    bool restoreTrusted(IJobStorage& storage, const std::string& id);
    bool updateTrust(const std::string& id, std::int64_t expiresAt);
    std::filesystem::path directory_;
    std::string id_;
    QByteArray fingerprint_;
    bool trusted_ = false;
    std::int64_t trustedUntil_ = 0;
};
