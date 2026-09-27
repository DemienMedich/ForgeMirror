#include "QtProfileReportExport.h"

#include "Profile.h"
#include "SkillCatalog.h"
#include "Skill.h"
#include "AppUtils.h"
#include <QFileInfo>
#include <QSaveFile>
#include <QDateTime>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {
std::string csv(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    std::string result(bytes.constData(), size_t(bytes.size()));
    if (result.find_first_of(",\"\r\n") == std::string::npos) return result;
    std::string escaped = "\"";
    for (const char c : result) {
        if (c == '"') escaped.push_back('"');
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

QString timestamp(std::int64_t value) {
    return value > 0 ? QDateTime::fromSecsSinceEpoch(value).toString("yyyy-MM-dd HH:mm")
                     : QString::fromUtf8("нет данных");
}

QString elapsedText(std::int64_t seconds) {
    seconds = std::max<std::int64_t>(0, seconds);
    const auto days = seconds / 86400;
    if (days > 0) return QString::fromUtf8("%1 дн.").arg(days);
    const auto hours = seconds / 3600;
    if (hours > 0) return QString::fromUtf8("%1 ч.").arg(hours);
    return QString::fromUtf8("%1 мин.").arg(seconds / 60);
}

int totalSkillXp(const Skill& skill) {
    int total = skill.xp;
    for (int level = 2; level <= skill.level; ++level) total += Skill::required_xp_for(level);
    return total;
}

bool save(const QString& path, const std::string& bytes, QString* error) {
    if (path.trimmed().isEmpty() || QFileInfo(path).isDir()) {
        if (error) *error = QString::fromUtf8("Выберите имя файла отчёта.");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF", 3) != 3 ||
        file.write(bytes.data(), qint64(bytes.size())) != qint64(bytes.size()) || !file.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить отчёт профиля.");
        return false;
    }
    if (error) error->clear();
    return true;
}

std::string createReport(const Profile& profile, const SkillCatalog& catalog,
    const QString& profileId, bool asCsv, std::int64_t now) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto lastTask = profile.last_task_timestamp();
    const auto elapsed = lastTask > 0 ? std::max<std::int64_t>(0, now - lastTask) : 0;
    const QString lastActivity = lastTask > 0
        ? QStringLiteral("%1 (%2)").arg(timestamp(lastTask), elapsedText(elapsed))
        : QString::fromUtf8("нет данных");
    const auto id = profileId.toUtf8();
    const QString rank = QString::fromUtf8(DescribeOverallRank(profile).c_str());
    const int totalXp = profile.total_xp();
    const int progress = profile.level_progress();
    const int needed = progress + profile.xp_to_next_level();

    if (asCsv) {
        out << "Section,Key,Value\n"
            << "Summary,ID," << csv(profileId) << "\n"
            << "Summary,Name," << csv(QString::fromUtf8(profile.name().c_str())) << "\n"
            << "Summary,Level," << profile.overall_level() << "\n"
            << "Summary,Rank," << csv(rank) << "\n"
            << "Summary,TotalXP," << totalXp << "\n"
            << "Summary,Progress," << progress << '/' << needed << "\n"
            << "Summary,LastActivity," << csv(lastActivity) << "\n"
            << "Summary,RecoveryTasks," << profile.recovery_tasks_remaining() << "\n";
        const auto& categories = profile.category_best_scores();
        for (size_t i = 0; i < categories.size(); ++i) {
            const QString key = QString::fromUtf8("Категория %1").arg(QString::fromUtf8(Profile::kCategoryLabels[i]));
            out << "Categories," << csv(key) << ',' << categories[i] << "\n";
        }
        out << "\nSkills\nID,Name,Level,XP,XPToNext,TotalXP,Weight,BonusPercent\n";
        for (const auto& skill : profile.list_skills()) {
            const double bonus = std::max(0.0, (profile.skill_bonus_multiplier(skill.name, now) - 1.0) * 100.0);
            out << csv(QString::fromUtf8(skill.name.c_str())) << ','
                << csv(QString::fromUtf8(catalog.display_name(skill.name).c_str())) << ','
                << skill.level << ',' << skill.xp << ',' << skill.xpToNext << ','
                << totalSkillXp(skill) << ','
                << std::fixed << std::setprecision(2) << skill.weight << ','
                << std::fixed << std::setprecision(1) << bonus << "\n" << std::defaultfloat;
        }
    } else {
        out << "Отчёт профиля\n"
            << "ID: " << std::string(id.constData(), size_t(id.size())) << "\n"
            << "Имя: " << profile.name() << "\n"
            << "Уровень: " << profile.overall_level() << "\n"
            << "Ранг: " << rank.toUtf8().constData() << "\n"
            << "Всего XP: " << totalXp << "\n"
            << "Прогресс уровня: " << progress << " / " << needed << "\n"
            << "Последняя активность: " << lastActivity.toUtf8().constData() << "\n"
            << "Прогрев: " << (profile.recovery_tasks_remaining() > 0
                ? QString::fromUtf8("осталось задач %1").arg(profile.recovery_tasks_remaining()).toUtf8().constData()
                : "нет активных штрафов") << "\n"
            << "Категории:\n";
        const auto& categories = profile.category_best_scores();
        for (size_t i = 0; i < categories.size(); ++i) {
            out << "  " << Profile::kCategoryLabels[i] << "=" << categories[i] << "/10\n";
        }
        out << "\nНавыки:\nID\tНазвание\tУровень\tXP\tXP до уровня\tВес\tБонус %\n";
        for (const auto& skill : profile.list_skills()) {
            const double bonus = std::max(0.0, (profile.skill_bonus_multiplier(skill.name, now) - 1.0) * 100.0);
            out << skill.name << '\t' << catalog.display_name(skill.name) << '\t' << skill.level << '\t'
                << skill.xp << '/' << skill.xpToNext << '\t'
                << std::fixed << std::setprecision(2) << skill.weight << '\t'
                << std::fixed << std::setprecision(1) << bonus << "\n" << std::defaultfloat;
        }
    }
    return out.str();
}
}

bool ExportProfileReport(const QString& path, const Profile& profile, const SkillCatalog& catalog,
    const QString& profileId, bool asCsv, std::int64_t now, QString* error) {
    return save(path, createReport(profile, catalog, profileId, asCsv, now), error);
}
