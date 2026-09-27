#pragma once

#include <QString>
#include <cstdint>

class Profile;
class SkillCatalog;

bool ExportProfileReport(const QString& path, const Profile& profile, const SkillCatalog& catalog,
    const QString& profileId, bool csv, std::int64_t now, QString* error = nullptr);
