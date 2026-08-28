#include "QtProfileSession.h"
#include "AppUtils.h"
#include "Profile.h"
#include <QCryptographicHash>
#include <algorithm>

namespace {
std::optional<Profile> available(IJobStorage& storage, const std::string& id) {
    const auto profiles = storage.list_profiles();
    if (id.empty() || std::none_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == id && !p.archived; })) return {};
    if (!storage.set_active_profile(id)) return {};
    auto profile = storage.load_profile();
    if (!profile || profile->is_blocked() || profile->password_encoded().empty()) return {};
    return profile;
}
QByteArray fingerprint(const Profile& profile) {
    const auto& value = profile.password_encoded();
    return QCryptographicHash::hash(QByteArray(value.data(), int(value.size())), QCryptographicHash::Sha256);
}
}
bool QtProfileSession::unlock(IJobStorage& storage, const std::string& id, const std::string& password) {
    lock();
    try {
        const auto profile = available(storage, id);
        if (!profile || password.empty() || DecodePassword(profile->password_encoded()) != password) return false;
        id_ = id;
        fingerprint_ = fingerprint(*profile);
        return true;
    } catch (...) { return false; }
}
bool QtProfileSession::isUnlocked(IJobStorage& storage, const std::string& id) {
    if (id_ != id || id_.empty()) { lock(); return false; }
    try {
        const auto profile = available(storage, id);
        if (profile && fingerprint(*profile) == fingerprint_) return true;
    } catch (...) {}
    lock();
    return false;
}
