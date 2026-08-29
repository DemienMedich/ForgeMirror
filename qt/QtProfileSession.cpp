#include "QtProfileSession.h"
#include "AppUtils.h"
#include "Profile.h"
#include <QtCore>
#include <algorithm>
#include <iterator>
#include <map>

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
QString uiPath(const std::filesystem::path& directory) { return QString::fromUtf8((directory / "meta/ui.ini").u8string()); }
std::map<std::string, std::int64_t> parseTrusted(const QString& raw) {
    std::map<std::string, std::int64_t> result;
    for (const auto& item : raw.split(',', Qt::SkipEmptyParts)) {
        const auto at = item.lastIndexOf(':'); bool ok = false;
        const auto value = item.mid(at + 1).trimmed().toLongLong(&ok);
        const auto id = item.left(at).trimmed().toUtf8().toStdString();
        if (at > 0 && ok && value > 0 && !id.empty()) result[id] = std::max(result[id], std::int64_t(value));
    }
    return result;
}
QString joinTrusted(const std::map<std::string, std::int64_t>& trusted) {
    QStringList items;
    for (const auto& [id, expiry] : trusted) if (!id.empty() && expiry > 0) items << QString::fromUtf8(id.data(), int(id.size())) + ':' + QString::number(expiry);
    return items.join(',');
}
std::map<std::string, std::int64_t> loadTrusted(const std::filesystem::path& directory) {
    const auto path = uiPath(directory);
    if (QFileInfo(QString::fromUtf8((directory / "meta").u8string())).isSymLink() || QFileInfo(path).isSymLink()) return {};
    QFile input(path); if (!input.open(QIODevice::ReadOnly)) return {};
    auto bytes = input.readAll(); if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    bool profile = false;
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const auto line = raw.trimmed();
        if (line.startsWith('[')) { profile = line == "[profile]"; continue; }
        if (profile && line.section('=', 0, 0).trimmed() == "trusted") return parseTrusted(line.section('=', 1));
    }
    return {};
}
bool changeTrusted(const std::filesystem::path& directory, const std::string& id, std::int64_t expiresAt) {
    const auto path = uiPath(directory);
    if (QFileInfo(QString::fromUtf8((directory / "meta").u8string())).isSymLink() || QFileInfo(path).isSymLink()) return false;
    QFile input(path); QByteArray original; if (input.open(QIODevice::ReadOnly)) original = input.readAll(); input.close();
    bool bom = original.startsWith("\xEF\xBB\xBF"); if (bom) original.remove(0, 3);
    auto lines = QString::fromUtf8(original).split('\n'); int begin = -1, end = lines.size(), trustedLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        if (line == "[profile]") { begin = i; continue; }
        if (begin >= 0 && i > begin && line.startsWith('[')) { end = i; break; }
        if (begin >= 0 && line.section('=', 0, 0).trimmed() == "trusted") trustedLine = i;
    }
    if (begin < 0) { if (!lines.isEmpty() && !lines.back().isEmpty()) lines << ""; begin = lines.size(); lines << "[profile]"; end = lines.size(); }
    auto trusted = trustedLine >= 0 ? parseTrusted(lines[trustedLine].section('=', 1)) : std::map<std::string, std::int64_t>{};
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (auto it = trusted.begin(); it != trusted.end();) it = it->second <= now ? trusted.erase(it) : std::next(it);
    if (expiresAt > now) trusted[id] = expiresAt; else trusted.erase(id);
    const auto replacement = "trusted=" + joinTrusted(trusted);
    if (trustedLine >= 0) lines[trustedLine] = replacement; else lines.insert(end, replacement);
    const auto bytes = (bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + lines.join('\n').toUtf8();
    QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile output(path); output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}
}

bool QtProfileSession::updateTrust(const std::string& id, std::int64_t expiresAt) { return !directory_.empty() && changeTrusted(directory_, id, expiresAt); }
bool QtProfileSession::unlock(IJobStorage& storage, const std::string& id, const std::string& password, int trustDays) {
    lock();
    try {
        const auto profile = available(storage, id);
        if (!profile || password.empty() || DecodePassword(profile->password_encoded()) != password) return false;
        const int days = trustDays >= 90 ? 90 : trustDays >= 30 ? 30 : 0;
        const auto expiry = days ? QDateTime::currentSecsSinceEpoch() + std::int64_t(days) * 86400 : 0;
        if (days && !updateTrust(id, expiry)) return false;
        id_ = id; fingerprint_ = fingerprint(*profile); trusted_ = days > 0; trustedUntil_ = expiry;
        AppendProfileAudit(directory_, id, "unlock", "trust_days=" + std::to_string(days)); return true;
    } catch (...) { return false; }
}
bool QtProfileSession::restoreTrusted(IJobStorage& storage, const std::string& id) {
    const auto trusted = loadTrusted(directory_); const auto found = trusted.find(id); const auto now = QDateTime::currentSecsSinceEpoch();
    if (found == trusted.end() || found->second <= now) { if (found != trusted.end()) updateTrust(id, 0); return false; }
    try {
        const auto profile = available(storage, id); if (!profile) { updateTrust(id, 0); return false; }
        id_ = id; fingerprint_ = fingerprint(*profile); trusted_ = true; trustedUntil_ = found->second;
        AppendProfileAudit(directory_, id, "trusted_unlock"); return true;
    } catch (...) { updateTrust(id, 0); return false; }
}
bool QtProfileSession::isUnlocked(IJobStorage& storage, const std::string& id) {
    if (id_ != id || id_.empty()) { lock(); if (!restoreTrusted(storage, id)) return false; }
    try { const auto profile = available(storage, id); if (profile && fingerprint(*profile) == fingerprint_) return true; } catch (...) {}
    lock(true); return false;
}
bool QtProfileSession::lock(bool forgetTrust) {
    const auto old = id_; bool saved = true;
    if (forgetTrust && !old.empty()) saved = updateTrust(old, 0);
    if (forgetTrust && !old.empty() && saved) AppendProfileAudit(directory_, old, "lock");
    id_.clear(); fingerprint_.clear(); trusted_ = false; trustedUntil_ = 0; return saved;
}
