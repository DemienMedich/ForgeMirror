#include "QtWindow.h"
#include "QtLogSanitization.h"
#include "QtAchievements.h"
#include "QtPomodoro.h"
#include "QtProfessionEditor.h"
#include "QtRulesEditor.h"
#include "QtVaultEditor.h"
#include "QtBannerEditor.h"
#include "QtCloudSettings.h"
#include "QtCloudPull.h"
#include "QtCloudPushPreview.h"
#include "QtCloudAutoSync.h"
#include "QtCloudRelease.h"
#include "QtCloudConflict.h"
#include "QtStorageConflict.h"
#include "QtModelViewer.h"
#include "QtReportExport.h"
#include "QtProfileReportExport.h"
#include "QtProfileAnalytics.h"
#include "QtStorageHealthReport.h"
#include "QtAuditExport.h"
#include "QtReportChart.h"
#include "QtLogActivityChart.h"
#include "QtPipelineTransition.h"
#include "QtPipelineMap.h"
#include "QtPipelineEditor.h"
#include "AppTaskCompletionService.h"
#include "QtSkillEditor.h"
#include "QtTaskCompletionDialog.h"
#include "QtProfileDialogs.h"
#include "AppTaskProjectService.h"
#include "AppPipelineService.h"
#include "AppTaskWorkflowService.h"
#include "AppTeamValueReportService.h"
#include "AppProfileMutationService.h"
#include "AppProfessionService.h"
#include "AppSkillService.h"
#include "AppShortcutsService.h"
#include "CloudSync.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtWidgets>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

class QtBackgroundSurface final : public QWidget {
public:
    explicit QtBackgroundSurface(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("qtBackgroundSurface");
        setAutoFillBackground(false);
    }
    void setBackground(const std::filesystem::path& directory, const QString& relativePath,
                       double alpha, bool tiled, double tileScale) {
        if (relativePath != path_) {
            path_ = relativePath;
            image_ = LoadQtBackgroundImage(directory, path_);
        }
        setProperty("backgroundPath", path_);
        alpha_ = std::isfinite(alpha) ? std::clamp(alpha, 0.0, 1.0) : 0.25;
        tiled_ = tiled;
        tileScale_ = std::isfinite(tileScale) ? std::clamp(tileScale, 0.25, 3.0) : 1.0;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));
        if (image_.isNull() || alpha_ <= 0.0) return;
        painter.save();
        painter.setOpacity(alpha_);
        if (tiled_) {
            const QSize tileSize(std::max(1, qRound(image_.width() * tileScale_)),
                                 std::max(1, qRound(image_.height() * tileScale_)));
            painter.drawTiledPixmap(rect(), QPixmap::fromImage(image_).scaled(tileSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        } else {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(rect(), image_);
        }
        painter.restore();
    }
private:
    QString path_;
    QImage image_;
    double alpha_ = 0.25;
    double tileScale_ = 1.0;
    bool tiled_ = false;
};

namespace {
class WindowDragHandle final : public QToolButton {
public:
    using QToolButton::QToolButton;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
};
QString q(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string u(const QString& s) { return s.toUtf8().toStdString(); }
void labelForAccessibility(QWidget* widget, const QString& name, const QString& description = {}) {
    if (!widget) return;
    widget->setAccessibleName(name);
    if (!description.isEmpty()) widget->setAccessibleDescription(description);
}
QString timeText(std::int64_t t) {
    return t ? QDateTime::fromSecsSinceEpoch(t).toString("dd.MM.yyyy HH:mm") : QString::fromUtf8("—");
}
QString field(const QString& name, const std::string& value) {
    return "<p><b>" + name.toHtmlEscaped() + "</b><br>" + q(value).toHtmlEscaped().replace("\n", "<br>") + "</p>";
}
bool archivedProfileUsesProfession(const std::filesystem::path& directory, const std::string& profileId,
                                   const std::string& professionId) {
    const auto path = directory / "archive" / (profileId + ".ini");
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) return true;
    QFile file(q(path.u8string()));
    if (!file.open(QIODevice::ReadOnly)) return true;
    bool profileSection = false;
    for (const auto& raw : QString::fromUtf8(file.readAll()).split('\n')) {
        const auto line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) { profileSection = line == "[profile]"; continue; }
        if (profileSection && line.startsWith("profession=") && u(line.mid(11).trimmed()) == professionId) return true;
    }
    return false;
}
bool archivedProfileUsesSkill(const std::filesystem::path& directory, const std::string& profileId,
                              const std::string& skillId) {
    const auto path = directory / "archive" / (profileId + ".ini");
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) return true;
    QFile file(q(path.u8string()));
    if (!file.open(QIODevice::ReadOnly)) return true;
    bool skillsSection = false;
    for (const auto& raw : QString::fromUtf8(file.readAll()).split('\n')) {
        const auto line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) { skillsSection = line == "[skills]"; continue; }
        if (skillsSection && line.startsWith("names=")) {
            const auto names = line.mid(6).split(',', Qt::SkipEmptyParts);
            for (const auto& name : names) if (u(name.trimmed()) == skillId) return true;
        }
    }
    const auto achievementsPath = directory / "achievements" / (profileId + ".json");
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(achievementsPath))) return true;
    QFile achievements(q(achievementsPath.u8string()));
    if (!achievements.exists()) return false;
    if (!achievements.open(QIODevice::ReadOnly)) return true;
    const auto document = QJsonDocument::fromJson(achievements.readAll());
    if (!document.isArray()) return true;
    for (const auto& value : document.array()) if (value.toObject().value("skill").toString() == q(skillId)) return true;
    return false;
}
std::optional<std::int64_t> loadReminderCheckAt(const std::filesystem::path& directory) {
    const auto path = directory / "meta/qt-reminder-state.json";
    const QFileInfo info(QString::fromUtf8(path.u8string()));
    if (!info.exists()) return std::nullopt;
    if (info.isSymLink() || info.size() > 4096) return std::nullopt;
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto value = document.object().value("lastCheckAt");
    if (document.object().value("version").toInt() != 1 || !value.isDouble()) return std::nullopt;
    const auto timestamp = value.toVariant().toLongLong();
    if (timestamp <= 0 || timestamp > QDateTime::currentSecsSinceEpoch() + 300) return std::nullopt;
    return timestamp;
}
bool saveReminderCheckAt(const std::filesystem::path& directory, std::int64_t timestamp) {
    const auto meta = QString::fromUtf8((directory / "meta").u8string());
    const auto path = QString::fromUtf8((directory / "meta/qt-reminder-state.json").u8string());
    if (QFileInfo(meta).isSymLink() || QFileInfo(path).isSymLink()) return false;
    if (!QDir().mkpath(meta)) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"lastCheckAt", qlonglong(timestamp)}})
        .toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool pomodoroWithinWindow(const StorageVaultData& vault, std::int64_t startedAt) {
    const auto local = QDateTime::fromSecsSinceEpoch(startedAt).toLocalTime();
    const int weekday = local.date().dayOfWeek() % 7; // Sunday is 0 in vault format.
    if (!(vault.pomodoroDaysMask & (1 << weekday))) return false;
    const int minutes = local.time().hour() * 60 + local.time().minute();
    const int start = vault.pomodoroStartMinutes, end = vault.pomodoroEndMinutes;
    if (start == end) return false;
    return start < end ? minutes >= start && minutes < end : minutes >= start || minutes < end;
}
AppProfileMutationResult runWalletMutationWithAudit(
    QtWorkspace& workspace, const std::string& restoreProfileId, const std::string& profileId,
    bool includeStorageVault, const std::string& action, const std::string& details,
    const std::function<AppProfileMutationResult()>& mutation,
    const std::function<void(AppLogLevel, const std::string&)>& telemetry) {
    AppProfileMutationResult result;
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) {
        result.errorMessage = u8"Сначала завершите восстановление данных.";
        if (telemetry) telemetry(AppLogLevel::Warning, "Wallet mutation blocked pending recovery");
        return result;
    }
    bool prepared = false;
    try {
        PrepareProfileWalletRecovery(workspace.directory, profileId, includeStorageVault);
        prepared = true;
        result = mutation();
        if (!result.ok || !result.profile)
            throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось сохранить изменение кошелька." : result.errorMessage);
        if (!AppendProfileAudit(workspace.directory, profileId, action, details))
            throw std::runtime_error(u8"Не удалось записать аудит кошелька; изменение отменено.");
        CommitQtRecoveryTransaction(workspace.directory);
        if (telemetry) telemetry(AppLogLevel::Info, "Wallet mutation committed with audit");
        return result;
    } catch (const std::exception& error) {
        result.ok = false;
        result.changed = false;
        result.affectedProfiles = 0;
        result.profile.reset();
        result.errorMessage = error.what();
        if (prepared) {
            try {
                RecoverTaskCompletion(workspace.directory);
                if (!restoreProfileId.empty()) workspace.storage->set_active_profile(restoreProfileId);
                workspace.reload();
                result.errorMessage += u8" Все изменения отменены.";
                if (telemetry) telemetry(AppLogLevel::Warning, "Wallet mutation rolled back after failure");
            } catch (const std::exception&) {
                result.errorMessage += u8" Откат не завершён; журнал сохранён для восстановления при запуске.";
                if (telemetry) telemetry(AppLogLevel::Error, "Wallet mutation recovery remains pending");
            }
        } else if (telemetry) {
            telemetry(AppLogLevel::Error, "Wallet mutation failed before recovery began");
        }
        return result;
    }
}
enum Page { ProfilePage, Tasks, Projects, Catalog, Pipeline, Professions, Statistics, Audit, Pomodoro, Rules, Vault, Shortcuts, Banner, Cloud, ModelViewerPage, ModelSettingsPage, Logs, AdminProfileStats };
struct ProfileAuditRow { std::int64_t timestamp; std::string profile; std::string action; std::string details; };
std::vector<ProfileAuditRow> profileAudit(const std::filesystem::path& directory) {
    const auto path = q((directory / "meta/profile-audit.log").u8string());
    if (QFileInfo(path).isSymLink()) return {};
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    auto lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts); if (lines.size() > 500) lines = lines.mid(lines.size() - 500);
    std::vector<ProfileAuditRow> out;
    for (const auto& line : lines) {
        const auto fields = line.split('|'); bool ok = false; const auto timestamp = fields.value(0).toLongLong(&ok);
        if (ok && fields.size() >= 3) out.push_back({timestamp, u(fields[1]), u(fields[2]), u(fields.mid(3).join('|'))});
    }
    return out;
}
QString profileAuditActionLabel(const std::string& action) {
    static const std::unordered_map<std::string, QString> labels{
        {"create", QString::fromUtf8("Создание профиля")}, {"unlock", QString::fromUtf8("Вход в профиль")},
        {"trusted_unlock", QString::fromUtf8("Вход по доверенному устройству")}, {"lock", QString::fromUtf8("Выход из профиля")},
        {"trust_expired", QString::fromUtf8("Срок доверенного входа истёк")},
        {"trust_revoked", QString::fromUtf8("Доверенный вход отозван")},
        {"trust_revoke_failed", QString::fromUtf8("Не удалось отозвать доверенный вход")},
        {"password_change", QString::fromUtf8("Смена пароля")}, {"password_reset", QString::fromUtf8("Сброс пароля")},
        {"archive", QString::fromUtf8("Архивация профиля")}, {"restore", QString::fromUtf8("Восстановление профиля")},
        {"block", QString::fromUtf8("Блокировка профиля")}, {"unblock", QString::fromUtf8("Снятие блокировки")},
        {"wallet_adjustment", QString::fromUtf8("Изменение кошелька")},
        {"direct_xp", QString::fromUtf8("Ручное начисление XP")},
        {"profile_edit", QString::fromUtf8("Изменение профиля")},
        {"pomodoro_reward", QString::fromUtf8("Награда Pomodoro")}, {"spirit_purchase", QString::fromUtf8("Снятие Злого духа")},
        {"spirit", QString::fromUtf8("Изменение духа")}
    };
    const auto found = labels.find(action);
    return found == labels.end() ? q(action) : found->second;
}
QString reportPeriodLabel(int range, const QDate& from, const QDate& to) {
    switch (range) {
    case 1: return QString::fromUtf8("Задачи созданы за 30 дней");
    case 2: return QString::fromUtf8("Задачи созданы за 90 дней");
    case 3: return QString::fromUtf8("Задачи созданы с начала года");
    case 4: return QString::fromUtf8("Созданы %1–%2").arg(from.toString("dd.MM.yyyy"), to.toString("dd.MM.yyyy"));
    default: return QString::fromUtf8("За всё время");
    }
}
const std::array<std::pair<QString, int>, 16>& adminProfileRanks() {
    static const std::array<std::pair<QString, int>, 16> ranks{{
        {QString::fromUtf8("Стажёр"), 1}, {QString::fromUtf8("Джуниор I"), 10},
        {QString::fromUtf8("Джуниор II"), 20}, {QString::fromUtf8("Джуниор III"), 30},
        {QString::fromUtf8("Джуниор IV"), 40}, {QString::fromUtf8("Мидл I"), 50},
        {QString::fromUtf8("Мидл II"), 60}, {QString::fromUtf8("Мидл III"), 70},
        {QString::fromUtf8("Мидл IV"), 80}, {QString::fromUtf8("Мидл V"), 90},
        {QString::fromUtf8("Мидл VI"), 100}, {QString::fromUtf8("Сеньор I"), 150},
        {QString::fromUtf8("Сеньор II"), 160}, {QString::fromUtf8("Сеньор III"), 170},
        {QString::fromUtf8("Сеньор IV"), 180}, {QString::fromUtf8("Сеньор V"), 190}}};
    return ranks;
}
int adminProfileRankIndex(int level) {
    int rank = 0;
    const auto& ranks = adminProfileRanks();
    for (int index = 0; index < int(ranks.size()); ++index) {
        if (level >= ranks[size_t(index)].second) rank = index;
        else break;
    }
    return rank;
}
QString adminProfileRankName(int level) {
    return adminProfileRanks()[size_t(adminProfileRankIndex(level))].first;
}
QString elapsedProfileTime(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    const auto days = seconds / 86400;
    if (days > 0) return QString::fromUtf8("%1 дн.").arg(days);
    const auto hours = seconds / 3600;
    if (hours > 0) return QString::fromUtf8("%1 ч.").arg(hours);
    return QString::fromUtf8("%1 мин.").arg(seconds / 60);
}
std::vector<TaskEntry> reportTasksForRange(const std::vector<TaskEntry>& tasks, int range,
                                           QDate customFrom, QDate customTo,
                                           int* missingCreationDateCount = nullptr) {
    if (missingCreationDateCount) *missingCreationDateCount = 0;
    if (range <= 0) return tasks;
    const QDate today = QDate::currentDate();
    QDate from = customFrom, to = customTo;
    if (range == 1) { from = today.addDays(-29); to = today; }
    else if (range == 2) { from = today.addDays(-89); to = today; }
    else if (range == 3) { from = QDate(today.year(), 1, 1); to = today; }
    if (!from.isValid() || !to.isValid() || from > to) return {};
    const auto begin = QDateTime(from, QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch();
    const auto end = QDateTime(to.addDays(1), QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch();
    std::vector<TaskEntry> filtered;
    filtered.reserve(tasks.size());
    for (const auto& task : tasks) {
        if (task.createdAt <= 0) {
            if (missingCreationDateCount) ++*missingCreationDateCount;
            continue;
        }
        if (task.createdAt >= begin && task.createdAt < end) filtered.push_back(task);
    }
    return filtered;
}
bool reportPreviousRange(int range, const QDate& customFrom, const QDate& customTo,
                         QDate* from, QDate* to) {
    if (range <= 0) return false;
    const auto today = QDate::currentDate();
    if (range == 1) { *from = today.addDays(-29); *to = today; }
    else if (range == 2) { *from = today.addDays(-89); *to = today; }
    else if (range == 3) { *from = QDate(today.year(), 1, 1); *to = today; }
    else { *from = customFrom; *to = customTo; }
    if (!from->isValid() || !to->isValid() || *from > *to) return false;
    if (range == 3) { *from = from->addYears(-1); *to = to->addYears(-1); }
    else {
        const int days = int(from->daysTo(*to)) + 1;
        *to = from->addDays(-1);
        *from = to->addDays(-days + 1);
    }
    return from->isValid() && to->isValid() && *from <= *to;
}
std::string reportStageKey(const TaskEntry& task) {
    if (!task.pipelineStepId.empty()) return task.pipelineStepId;
    return task.pipelineStep.empty() ? "__no_pipeline" : "legacy:" + task.pipelineStep;
}
QString reportStageName(const std::string& id, const TaskEntry& task, const std::vector<PipelineStep>& steps) {
    if (id == "__no_pipeline") return QString::fromUtf8("Без этапа");
    const auto found = std::find_if(steps.begin(), steps.end(), [&](const auto& step) {
        return step.id == id || (id.rfind("legacy:", 0) == 0 &&
            (step.title == task.pipelineStep || step.stageCode == task.pipelineStep));
    });
    if (found != steps.end()) return q(found->stageCode + " · " + found->title);
    const auto label = task.pipelineStep.empty() ? id : task.pipelineStep;
    return QString::fromUtf8("Неизвестный этап · %1").arg(q(label));
}
struct QtReportStageGroup { std::string id; QString name; TeamValueReport report; };
std::vector<QtReportStageGroup> buildReportStageGroups(const std::vector<TaskEntry>& tasks,
    const std::vector<PipelineStep>& steps, const std::vector<ProjectEntry>& projects, std::int64_t now) {
    std::unordered_map<std::string, std::vector<TaskEntry>> grouped;
    std::unordered_map<std::string, QString> names;
    for (const auto& task : tasks) {
        const auto id = reportStageKey(task);
        grouped[id].push_back(task);
        names.emplace(id, reportStageName(id, task, steps));
    }
    std::vector<QtReportStageGroup> result;
    result.reserve(grouped.size());
    for (auto& [id, entries] : grouped)
        result.push_back({id, names.at(id), BuildTeamValueReport(entries, projects, now)});
    auto order = [&](const std::string& id) {
        const auto found = std::find_if(steps.begin(), steps.end(), [&](const auto& step) {
            return step.id == id || (id.rfind("legacy:", 0) == 0 && id.substr(7) == step.title);
        });
        return found == steps.end() ? std::ptrdiff_t(steps.size()) : std::distance(steps.begin(), found);
    };
    std::sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
        const auto ai = order(a.id), bi = order(b.id);
        if (ai != bi) return ai < bi;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}
std::string reportCategoryKey(const TaskEntry& task) {
    return "category:" + std::to_string(std::clamp(task.category, 0, 4));
}
struct QtReportCategoryGroup { std::string id; int index = 0; TeamValueReport report; };
std::vector<QtReportCategoryGroup> buildReportCategoryGroups(const std::vector<TaskEntry>& tasks,
    const std::vector<ProjectEntry>& projects, std::int64_t now) {
    std::unordered_map<std::string, std::vector<TaskEntry>> grouped;
    for (const auto& task : tasks) grouped[reportCategoryKey(task)].push_back(task);
    std::vector<QtReportCategoryGroup> result;
    result.reserve(grouped.size());
    for (auto& [id, entries] : grouped)
        result.push_back({id, std::clamp(std::atoi(id.c_str() + 9), 0, 4), BuildTeamValueReport(entries, projects, now)});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
    return result;
}
std::string reportPriorityKey(int priority) {
    return "__priority_" + std::to_string(AppNormalizeTaskPriority(priority));
}
int reportDeadlineGroup(const TaskEntry& task, std::int64_t now) {
    if (AppNormalizeTaskStatus(task.status) == 2) return 0;
    if (task.deadlineAt <= 0) return 3;
    return task.deadlineAt <= now ? 1 : 2;
}
}

QtWindow::QtWindow(QtWorkspace& workspace) : workspace_(workspace), profileSession_(workspace.directory), displaySettings_(LoadQtDisplaySettings(workspace.directory)) {
    const char* adminPasswordOverride = std::getenv("FORGEMIRROR_ADMIN_PASSWORD");
    admin_ = (!adminPasswordOverride || !*adminPasswordOverride) && LoadAdminStayLoggedIn(workspace_.directory);
    loadAppLogs();
    if (admin_) appendLog(AppLogLevel::Info, "CoreAuthentication", "Administrator session restored");
    lastCloudAutoSyncAt_ = QDateTime::currentSecsSinceEpoch();
    lastReminderCheckAt_ = loadReminderCheckAt(workspace_.directory).value_or(QDateTime::currentSecsSinceEpoch());
    ApplyQtDisplaySettings(*qApp, displaySettings_);
    setWindowOpacity(displaySettings_.windowOpacityPercent / 100.0);
    setWindowTitle(QString::fromUtf8("ForgeMirror · Qt migration · ") + APP_VERSION);
    resize(1120, 720);
    setMinimumSize(800, 520);
    setWindowFlag(Qt::FramelessWindowHint, !displaySettings_.decorated);
    if (displaySettings_.fullscreen) setWindowState(windowState() | Qt::WindowFullScreen);
    backgroundSurface_ = new QtBackgroundSurface(this);
    auto* root = backgroundSurface_;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    dragHandle_ = new WindowDragHandle;
    dragHandle_->setObjectName("windowDragHandle");
    dragHandle_->setText(QString::fromUtf8("⋮⋮"));
    dragHandle_->setToolTip(QString::fromUtf8("Перетащить окно"));
    dragHandle_->setFixedSize(28, 28);
    dragHandle_->setVisible(!displaySettings_.decorated);
    header->addWidget(dragHandle_);
    header->addWidget(new QLabel(QString::fromUtf8("Профиль:")));
    profiles_ = new QComboBox;
    profiles_->setObjectName("profiles");
    labelForAccessibility(profiles_, QString::fromUtf8("Выбранный профиль"),
        QString::fromUtf8("Список доступных активных профилей."));
    profiles_->setMinimumWidth(200);
    header->addWidget(profiles_);
    header->addStretch();
    mode_ = new QLabel;
    header->addWidget(mode_);
    auto* refresh = new QPushButton(QString::fromUtf8("Обновить"));
    refresh->setToolTip(QString::fromUtf8("Перечитать локальную копию данных без облачной синхронизации"));
    header->addWidget(refresh);
    auto* menuButton = new QToolButton;
    menuButton->setText(QString::fromUtf8("⋯"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    adminLoginAction_ = menu->addAction(QString::fromUtf8("Войти как администратор"), this, [this] { authenticate(); });
    adminLoginAction_->setObjectName("adminLoginAction");
    adminPasswordAction_ = menu->addAction(QString::fromUtf8("Сменить пароль администратора…"), this, [this] { changeAdminPassword(); });
    adminPasswordAction_->setObjectName("changeAdminPasswordAction");
    profileAccessAction_ = menu->addAction(QString::fromUtf8("Войти в выбранный профиль"), this, [this] { authenticateProfile(); });
    profileAccessAction_->setObjectName("profileAccess");
    auto* passwordAction = menu->addAction(QString::fromUtf8("Сменить пароль выбранного профиля"), this, [this] {
        const auto id = profiles_->currentData().toString();
        if (!profileSession_.isUnlocked(*workspace_.storage, u(id))) {
            render(); message(u8"Сначала войдите в выбранный профиль."); return;
        }
        if (ShowProfilePasswordDialog(this, workspace_, id, id, false)) {
            const bool forgotten = profileSession_.lock(true);
            if (!forgotten) message(u8"Пароль изменён и сеанс закрыт, но не удалось сохранить выход или удалить доверенный вход.");
        }
        render();
    });
    ownPasswordAction_ = passwordAction;
    passwordAction->setObjectName("changeOwnProfilePassword");
    storageHealthReportAction_ = menu->addAction(QString::fromUtf8("Расширенный отчёт хранилища"));
    storageHealthReportAction_->setObjectName("storageHealthReport");
    storageHealthReportAction_->setToolTip(QString::fromUtf8("Проверить sync-файлы, расхождения облачной копии и лишние элементы без изменений данных"));
    connect(storageHealthReportAction_, &QAction::triggered, this, [this] { exportStorageHealthReport(); });
    storageCleanupAction_ = menu->addAction(QString::fromUtf8("Очистить лишние файлы…"));
    storageCleanupAction_->setObjectName("storageCleanup");
    storageCleanupAction_->setToolTip(QString::fromUtf8("Показать точный список Qt-копии; удалить можно только отмеченные элементы после подтверждения"));
    connect(storageCleanupAction_, &QAction::triggered, this, [this] { cleanupStrayStorage(); });
    menu->addAction(QString::fromUtf8("Открыть папку данных Qt"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(workspace_.directory.u8string())));
    });
    auto* displaySettings = menu->addAction(QString::fromUtf8("Настройки интерфейса Qt"), this, [this] {
        if (!ShowQtDisplaySettings(this, workspace_.directory, displaySettings_)) return;
        ApplyQtDisplaySettings(*qApp, displaySettings_);
        setWindowOpacity(displaySettings_.windowOpacityPercent / 100.0);
        setWindowFlag(Qt::FramelessWindowHint, !displaySettings_.decorated);
        dragHandle_->setVisible(!displaySettings_.decorated);
        if (trayIcon_) trayIcon_->setVisible(displaySettings_.minimizeToTray);
        if (displaySettings_.fullscreen) showFullScreen(); else showNormal();
        render();
    });
    displaySettings->setObjectName("qtDisplaySettingsAction");
    auto* shortcutHelp = menu->addAction(QString::fromUtf8("Горячие клавиши"), this, [this] { showShortcutHelp(); });
    shortcutHelp->setObjectName("shortcutHelpAction");
    auto* aboutAction = menu->addAction(QString::fromUtf8("О программе"), this, [this] {
        QMessageBox about(QMessageBox::Information, QString::fromUtf8("О программе"), QString(), QMessageBox::Ok, this);
        about.setObjectName("aboutApplicationDialog");
        about.setText(QString::fromUtf8(
            "ForgeMirror — геймифицированный трекер навыков и задач.\n"
            "Помогает фиксировать прогресс, фокусироваться на развитии и видеть динамику.\n\n"
            "Авторы: ChatGPT, Codex, Роман Рощин\n"
            "Версия: %1\n\n"
            "Qt-клиент работает с отдельной копией данных; исходное рабочее место стабильной версии не изменяется.\n"
            "Перенос ещё не завершён и не заменяет стабильную версию. Доступны подтверждаемые облачные операции, "
            "настраиваемая автосинхронизация и загрузка установщика новой версии.\n"
            "Список перенесённых функций и ограничений находится в qt/README.md.").arg(QString::fromUtf8(APP_VERSION)));
        about.exec();
    });
    aboutAction->setObjectName("aboutApplicationAction");
    menuButton->setMenu(menu);
    if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()) {
        trayIcon_ = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon), this);
        trayIcon_->setToolTip(QString::fromUtf8("ForgeMirror · локальные напоминания"));
        auto* trayMenu = new QMenu(this);
        auto* openAction = trayMenu->addAction(QString::fromUtf8("Показать ForgeMirror"));
        trayMenu->addSeparator();
        auto* exitAction = trayMenu->addAction(QString::fromUtf8("Выход"));
        trayIcon_->setContextMenu(trayMenu);
        connect(openAction, &QAction::triggered, this, [this] {
            if (displaySettings_.fullscreen) showFullScreen(); else showNormal();
            raise(); activateWindow();
        });
        connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick) return;
            if (displaySettings_.fullscreen) showFullScreen(); else showNormal();
            raise(); activateWindow();
        });
        connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
        trayIcon_->setVisible(displaySettings_.minimizeToTray);
    }
    header->addWidget(menuButton);
    layout->addLayout(header);
    banner_ = new QLabel;
    banner_->setObjectName("bannerStrip"); banner_->setAlignment(Qt::AlignCenter); banner_->setFixedHeight(28);
    banner_->setProperty("banner", true); layout->addWidget(banner_);
    auto* bannerTimer = new QTimer(this); bannerTimer->setInterval(60000);
    connect(bannerTimer, &QTimer::timeout, this, [this] { ++bannerIndex_; updateBanner(); }); bannerTimer->start();
    auto* deadlineReminderTimer = new QTimer(this);
    deadlineReminderTimer->setObjectName("deadlineReminderTimer");
    deadlineReminderTimer->setInterval(60000);
    connect(deadlineReminderTimer, &QTimer::timeout, this, [this] { checkDeadlineReminders(); checkMissedDeadlineReminders(); });
    deadlineReminderTimer->start();
    cloudAutoSyncTimer_ = new QTimer(this);
    cloudAutoSyncTimer_->setObjectName("cloudAutoSyncTimer");
    cloudAutoSyncTimer_->setInterval(60000);
    connect(cloudAutoSyncTimer_, &QTimer::timeout, this, [this] { runAutomaticCloudSync(); });
    cloudAutoSyncTimer_->start();
    QTimer::singleShot(2500, this, [this] { checkDeadlineReminders(); checkMissedDeadlineReminders(); });

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->setAccessibleName(QString::fromUtf8("Разделы ForgeMirror"));
    navigation_->setAccessibleDescription(QString::fromUtf8("Переключение между модулями программы. Скрытые пункты недоступны в текущем режиме."));
    navigation_->addItems({QString::fromUtf8("Профиль"), QString::fromUtf8("Задачи"),
        QString::fromUtf8("Проекты"), QString::fromUtf8("Навыки"), QString::fromUtf8("Пайплайн"),
        QString::fromUtf8("Профессии"), QString::fromUtf8("Статистика"), QString::fromUtf8("Аудит"),
        QString::fromUtf8("Pomodoro"), QString::fromUtf8("Правила"), QString::fromUtf8("Хранилище"), QString::fromUtf8("Ярлыки"), QString::fromUtf8("Баннер"), QString::fromUtf8("Облако"),
        QString::fromUtf8("3D просмотр"), QString::fromUtf8("Настройки 3D"), QString::fromUtf8("Логи"),
        QString::fromUtf8("Статистика профилей")});
    const std::array<QString, 18> navigationHotkeys = {
        QStringLiteral("F1"), QString(), QString(), QStringLiteral("F2"), QStringLiteral("F3"), QString(),
        QStringLiteral("F5"), QStringLiteral("F6"), QString(), QStringLiteral("F4"), QString(), QString(),
        QString(), QString(), QString(), QString(), QString(), QString()
    };
    navigation_->setIconSize(QSize(18, 18));
    const auto drawNavigationIcon = [](int index, const QColor& color) {
        QPixmap pixmap(20, 20);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        switch (index) {
        case 0:
            painter.drawEllipse(QRectF(8, 2.5, 4, 4));
            path.moveTo(3, 17); path.cubicTo(3.5, 12.5, 6, 11, 10, 11); path.cubicTo(14, 11, 16.5, 12.5, 17, 17); painter.drawPath(path);
            break;
        case 1:
            painter.drawRoundedRect(QRectF(3, 2.5, 14, 15), 2, 2);
            painter.drawLine(6, 7, 7, 8); painter.drawLine(8.5, 7, 14.5, 7);
            painter.drawLine(6, 11, 7, 12); painter.drawLine(8.5, 11, 14.5, 11);
            painter.drawLine(6, 15, 7, 16); painter.drawLine(8.5, 15, 14.5, 15);
            break;
        case 2:
            path.moveTo(2.5, 5); path.lineTo(8, 5); path.lineTo(10, 7); path.lineTo(17.5, 7); path.lineTo(17.5, 16); path.lineTo(2.5, 16); path.closeSubpath(); painter.drawPath(path);
            break;
        case 3:
        case 6:
            painter.drawLine(3, 17, 17, 17);
            painter.drawRoundedRect(QRectF(4, index == 3 ? 10 : 8, 2.5, index == 3 ? 7 : 9), 1, 1);
            painter.drawRoundedRect(QRectF(8.75, index == 3 ? 5 : 11, 2.5, index == 3 ? 12 : 6), 1, 1);
            painter.drawRoundedRect(QRectF(13.5, index == 3 ? 8 : 4, 2.5, index == 3 ? 9 : 13), 1, 1);
            break;
        case 4:
            painter.drawLine(5, 5, 14, 5); painter.drawLine(5, 5, 5, 15); painter.drawLine(5, 15, 14, 15); painter.drawLine(14, 5, 14, 15);
            painter.drawEllipse(QRectF(3, 3, 4, 4)); painter.drawEllipse(QRectF(12, 3, 4, 4)); painter.drawEllipse(QRectF(3, 13, 4, 4)); painter.drawEllipse(QRectF(12, 13, 4, 4));
            break;
        case 5:
            painter.drawRoundedRect(QRectF(3.5, 3, 13, 14), 1.5, 1.5); painter.drawLine(7, 3, 7, 17); painter.drawLine(9.5, 7, 14.5, 7); painter.drawLine(9.5, 10, 14.5, 10); painter.drawLine(9.5, 13, 13, 13);
            break;
        case 7:
        case 16:
            painter.drawRoundedRect(QRectF(4, 2.5, 12, 15), 1.5, 1.5);
            painter.drawLine(7, 6, 13, 6); painter.drawLine(7, 9, 13, 9); painter.drawLine(7, 12, 13, 12);
            if (index == 7) { painter.drawLine(7, 15, 9, 16); painter.drawLine(9, 16, 13, 14); }
            else painter.drawLine(7, 15, 12, 15);
            break;
        case 8:
            painter.drawEllipse(QRectF(2.5, 2.5, 15, 15)); painter.drawLine(10, 5, 10, 10); painter.drawLine(10, 10, 13.5, 12);
            break;
        case 9:
            painter.drawEllipse(QRectF(7, 7, 6, 6));
            for (int angle = 0; angle < 360; angle += 45) { const auto radians = qDegreesToRadians(double(angle)); painter.drawLine(QPointF(10 + 4.5 * qCos(radians), 10 + 4.5 * qSin(radians)), QPointF(10 + 7.5 * qCos(radians), 10 + 7.5 * qSin(radians))); }
            break;
        case 10:
            path.moveTo(3, 6); path.cubicTo(3, 2, 17, 2, 17, 6); path.cubicTo(17, 10, 3, 10, 3, 6); path.moveTo(3, 6); path.lineTo(3, 14); path.cubicTo(3, 18, 17, 18, 17, 14); path.lineTo(17, 6); painter.drawPath(path); painter.drawArc(QRectF(3, 9, 14, 5), 0, -180 * 16);
            break;
        case 11:
            path.moveTo(4, 15); path.lineTo(15.5, 3.5); path.moveTo(9, 3.5); path.lineTo(15.5, 3.5); path.lineTo(15.5, 10); painter.drawPath(path);
            break;
        case 12:
            painter.drawLine(5, 17, 5, 3); path.moveTo(5, 4); path.lineTo(16, 4); path.lineTo(13, 8); path.lineTo(16, 12); path.lineTo(5, 12); painter.drawPath(path);
            break;
        case 13:
            path.moveTo(5, 16); path.cubicTo(1, 16, 1, 10, 5, 9); path.cubicTo(5, 4, 12, 3, 14, 7); path.cubicTo(19, 7, 19, 15, 15, 16); path.closeSubpath(); painter.drawPath(path);
            break;
        case 14:
            path.moveTo(10, 2.5); path.lineTo(17, 6.5); path.lineTo(17, 14); path.lineTo(10, 18); path.lineTo(3, 14); path.lineTo(3, 6.5); path.closeSubpath(); path.moveTo(3, 6.5); path.lineTo(10, 10.5); path.lineTo(17, 6.5); path.moveTo(10, 10.5); path.lineTo(10, 18); painter.drawPath(path);
            break;
        case 15:
            painter.drawLine(4, 3, 4, 17); painter.drawLine(10, 3, 10, 17); painter.drawLine(16, 3, 16, 17);
            painter.drawRoundedRect(QRectF(2, 6, 4, 3), 1.2, 1.2); painter.drawRoundedRect(QRectF(8, 12, 4, 3), 1.2, 1.2); painter.drawRoundedRect(QRectF(14, 5, 4, 3), 1.2, 1.2);
            break;
        case 17:
            painter.drawEllipse(QRectF(8.5, 2.5, 3.5, 3.5)); painter.drawEllipse(QRectF(2.5, 5, 3, 3)); painter.drawEllipse(QRectF(14.5, 5, 3, 3));
            path.moveTo(5, 17); path.cubicTo(5, 12.5, 7, 11, 10, 11); path.cubicTo(13, 11, 15, 12.5, 15, 17); path.moveTo(1, 16); path.cubicTo(1, 12, 2.5, 10, 5, 10); path.moveTo(19, 16); path.cubicTo(19, 12, 17.5, 10, 15, 10); painter.drawPath(path);
            break;
        }
        return pixmap;
    };
    for (int index = 0; index < navigation_->count(); ++index) {
        auto* item = navigation_->item(index);
        QIcon icon;
        icon.addPixmap(drawNavigationIcon(index, QColor("#b9b9c4")), QIcon::Normal);
        icon.addPixmap(drawNavigationIcon(index, QColor("#eeeeef")), QIcon::Selected);
        item->setIcon(icon);
        item->setData(Qt::AccessibleTextRole, item->text());
        const auto tooltip = navigationHotkeys[size_t(index)];
        if (!tooltip.isEmpty()) item->setToolTip(item->text() + QStringLiteral(" · ") + tooltip);
    }
    navigation_->setFixedWidth(168);
    body->addWidget(navigation_);
    auto* content = new QVBoxLayout;
    content->setSpacing(8);
    auto* toolbar = new QHBoxLayout;
    title_ = new QLabel;
    title_->setObjectName("title");
    toolbar->addWidget(title_);
    toolbar->addStretch();
    primary_ = new QPushButton;
    primary_->setObjectName("primary");
    primary_->setFixedHeight(32);
    toolbar->addWidget(primary_);
    content->addLayout(toolbar);
    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    content->addWidget(summary_);
    taskPipelineSummary_ = new QLabel;
    taskPipelineSummary_->setObjectName("taskPipelineSummary");
    taskPipelineSummary_->setTextFormat(Qt::RichText);
    taskPipelineSummary_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    taskPipelineSummary_->setOpenExternalLinks(false);
    taskPipelineSummary_->setWordWrap(true);
    content->addWidget(taskPipelineSummary_);
    statisticsChart_ = new QtReportChart;
    content->addWidget(statisticsChart_);
    modelSettings_ = LoadQtModelSettings(workspace_.directory);
    modelPage_ = new QWidget;
    modelPage_->setObjectName("modelPage");
    auto* modelLayout = new QVBoxLayout(modelPage_);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->setSpacing(8);
    modelStatus_ = new QLabel;
    modelStatus_->setObjectName("modelStatus");
    modelLayout->addWidget(modelStatus_);
    modelViewer_ = new QtModelViewer;
    modelViewer_->setSettings(modelSettings_);
    modelLayout->addWidget(modelViewer_, 1);
    auto* modelActions = new QHBoxLayout;
    modelActions->addStretch();
    auto* openModelSettings = new QPushButton(QString::fromUtf8("Настройки 3D"));
    openModelSettings->setObjectName("openModelSettings");
    openModelSettings->setFixedHeight(28);
    modelActions->addWidget(openModelSettings);
    modelLayout->addLayout(modelActions);
    content->addWidget(modelPage_, 1);
    modelSettingsPage_ = new QWidget;
    modelSettingsPage_->setObjectName("modelSettingsPage");
    auto* modelForm = new QFormLayout(modelSettingsPage_);
    modelForm->setContentsMargins(0, 0, 0, 0);
    modelForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    modelChoice_ = new QComboBox;
    modelChoice_->setObjectName("modelChoice");
    modelChoice_->addItem(QString::fromUtf8("Не выбрана"), QString());
    for (const auto& name : ListQtModels(workspace_.directory)) modelChoice_->addItem(name, name);
    modelPath_ = new QLineEdit;
    modelPath_->setObjectName("modelPath");
    auto* modelPathRow = new QWidget;
    auto* modelPathLayout = new QHBoxLayout(modelPathRow);
    modelPathLayout->setContentsMargins(0, 0, 0, 0);
    modelPathLayout->addWidget(modelPath_, 1);
    auto* browseModel = new QPushButton(QString::fromUtf8("Обзор…"));
    browseModel->setFixedHeight(28);
    modelPathLayout->addWidget(browseModel);
    modelYaw_ = new QSlider(Qt::Horizontal); modelYaw_->setRange(-314, 314); modelYaw_->setObjectName("modelYaw");
    modelPitch_ = new QSlider(Qt::Horizontal); modelPitch_->setRange(-157, 157); modelPitch_->setObjectName("modelPitch");
    modelZoom_ = new QSlider(Qt::Horizontal); modelZoom_->setRange(30, 300); modelZoom_->setObjectName("modelZoom");
    modelSpeed_ = new QSlider(Qt::Horizontal); modelSpeed_->setRange(0, 300); modelSpeed_->setObjectName("modelSpeed");
    modelAutoRotate_ = new QCheckBox(QString::fromUtf8("Автоматический поворот")); modelAutoRotate_->setObjectName("modelAutoRotate");
    modelColor_ = new QPushButton; modelColor_->setObjectName("modelColor"); modelColor_->setFixedHeight(28);
    modelForm->addRow(QString::fromUtf8("Модель в папке models"), modelChoice_);
    modelForm->addRow(QString::fromUtf8("Путь к OBJ / FBX"), modelPathRow);
    modelForm->addRow(QString::fromUtf8("Поворот по горизонтали"), modelYaw_);
    modelForm->addRow(QString::fromUtf8("Наклон"), modelPitch_);
    modelForm->addRow(QString::fromUtf8("Масштаб"), modelZoom_);
    modelForm->addRow(modelAutoRotate_);
    modelForm->addRow(QString::fromUtf8("Скорость поворота"), modelSpeed_);
    modelForm->addRow(QString::fromUtf8("Цвет линий"), modelColor_);
    modelForm->addRow(QString(), new QLabel(QString::fromUtf8("Перетаскивайте модель мышью, колесом меняйте масштаб. Настройки хранятся в локальном meta/ui.ini.")));
    content->addWidget(modelSettingsPage_, 1);
    modelPage_->hide(); modelSettingsPage_->hide();
    modelTimer_ = new QTimer(this);
    modelTimer_->setInterval(33);
    connect(modelTimer_, &QTimer::timeout, this, [this] {
        if (navigation_->currentRow() != ModelViewerPage || !modelSettings_.autoRotate || modelSettings_.autoSpeed <= 0) return;
        modelSettings_.yaw += modelSettings_.autoSpeed / 30.0f;
        modelViewer_->setSettings(modelSettings_);
    });
    modelTimer_->start();
    profileMetrics_ = new QWidget;
    profileMetrics_->setObjectName("profileMetrics");
    auto* metricsLayout = new QHBoxLayout(profileMetrics_);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(8);
    const QStringList metricNames = {QString::fromUtf8("Уровень"), QString::fromUtf8("Всего XP"),
        QString::fromUtf8("Выполнено задач"), QString::fromUtf8("XP до уровня"), QString::fromUtf8("Кукоины")};
    for (int i = 0; i < 5; ++i) {
        auto* metric = new QFrame;
        metric->setProperty("metric", true);
        metric->setFixedHeight(56);
        auto* box = new QVBoxLayout(metric);
        box->setContentsMargins(12, 8, 12, 8);
        box->setSpacing(0);
        box->addWidget(new QLabel(metricNames[i]));
        profileValues_[i] = new QLabel(QString::fromUtf8("—"));
        profileValues_[i]->setProperty("metricValue", true);
        box->addWidget(profileValues_[i]);
        metricsLayout->addWidget(metric, 1);
    }
    content->addWidget(profileMetrics_);
    profileViewModes_ = new QWidget;
    profileViewModes_->setObjectName("profileViewModes");
    auto* profileModesLayout = new QHBoxLayout(profileViewModes_);
    profileModesLayout->setContentsMargins(0, 0, 0, 0);
    profileModesLayout->setSpacing(6);
    profileModesLayout->addWidget(new QLabel(QString::fromUtf8("Режим:")));
    const QStringList profileModeNames = {QString::fromUtf8("Обзор"), QString::fromUtf8("Аналитика"), QString::fromUtf8("Фокус")};
    const QStringList profileModeTips = {QString::fromUtf8("Краткая сводка и три ведущих навыка."),
        QString::fromUtf8("Полная таблица навыков и достижения."), QString::fromUtf8("Ключевые показатели без таблицы деталей.")};
    for (int i = 0; i < 3; ++i) {
        profileViewModeButtons_[i] = new QPushButton(profileModeNames[i]);
        profileViewModeButtons_[i]->setObjectName(QStringLiteral("profileViewMode%1").arg(i));
        profileViewModeButtons_[i]->setCheckable(true);
        profileViewModeButtons_[i]->setToolTip(profileModeTips[i]);
        labelForAccessibility(profileViewModeButtons_[i], QString::fromUtf8("Режим профиля: %1").arg(profileModeNames[i]), profileModeTips[i]);
        profileModesLayout->addWidget(profileViewModeButtons_[i]);
        connect(profileViewModeButtons_[i], &QPushButton::clicked, this, [this, i] {
            displaySettings_.profileViewMode = i;
            saveDisplayContext();
            render();
        });
    }
    profileModesLayout->addStretch();
    content->addWidget(profileViewModes_);
    auto* taskModeButton = new QPushButton(QString::fromUtf8("Задачи"));
    taskModeButton->setObjectName("profileViewMode3");
    taskModeButton->setCheckable(true);
    taskModeButton->setToolTip(QString::fromUtf8("Назначенные задачи, приоритет по сроку и сводка XP."));
    labelForAccessibility(taskModeButton, QString::fromUtf8("Режим профиля: Задачи"), taskModeButton->toolTip());
    profileModesLayout->insertWidget(4, taskModeButton);
    connect(taskModeButton, &QPushButton::clicked, this, [this] {
        displaySettings_.profileViewMode = 3;
        saveDisplayContext();
        render();
    });
    profileTaskActions_ = new QWidget;
    profileTaskActions_->setObjectName("profileTaskActions");
    auto* profileTaskActionsLayout = new QHBoxLayout(profileTaskActions_);
    profileTaskActionsLayout->setContentsMargins(0, 0, 0, 0);
    profileTaskActionsLayout->setSpacing(6);
    const QStringList profileTaskActionNames = {QString::fromUtf8("Все задачи"), QString::fromUtf8("Активные"),
        QString::fromUtf8("Просроченные"), QString::fromUtf8("Ждут XP")};
    for (int i = 0; i < 4; ++i) {
        profileTaskFilterButtons_[i] = new QPushButton(profileTaskActionNames[i]);
        profileTaskFilterButtons_[i]->setObjectName(QStringLiteral("profileTasksFilter%1").arg(i));
        profileTaskFilterButtons_[i]->setMinimumHeight(32);
        labelForAccessibility(profileTaskFilterButtons_[i], QString::fromUtf8("Открыть задачи профиля: %1").arg(profileTaskActionNames[i]));
        profileTaskActionsLayout->addWidget(profileTaskFilterButtons_[i]);
        connect(profileTaskFilterButtons_[i], &QPushButton::clicked, this, [this, i] {
            const auto id = profiles_->currentData().toString();
            if (id.isEmpty() || !workspace_.modules.tasks) return;
            const auto set = [](QWidget* widget, auto action) { const QSignalBlocker blocker(widget); action(); };
            set(search_, [this] { search_->clear(); });
            set(statusFilter_, [this] { statusFilter_->setCurrentIndex(0); });
            set(priorityFilter_, [this] { priorityFilter_->setCurrentIndex(0); });
            set(quickTaskFilter_, [this, i] { quickTaskFilter_->setCurrentIndex(i == 1 ? 7 : i == 2 ? 3 : i == 3 ? 6 : 0); });
            set(taskCreatedRange_, [this] { taskCreatedRange_->setCurrentIndex(0); });
            set(taskAssigneeFilter_, [this, &id] { const int index = taskAssigneeFilter_->findData(id); taskAssigneeFilter_->setCurrentIndex(index >= 0 ? index : 0); });
            set(taskProjectFilter_, [this] { taskProjectFilter_->setCurrentIndex(0); });
            set(taskPipelineFilter_, [this] { taskPipelineFilter_->setCurrentIndex(0); });
            saveDisplayContext();
            navigation_->setCurrentRow(Tasks);
        });
    }
    profileTaskActionsLayout->addStretch(1);
    content->addWidget(profileTaskActions_);
    profileTaskActions_->hide();
    profileSkillFilters_ = new QWidget;
    profileSkillFilters_->setObjectName("profileSkillFilters");
    auto* skillFilterGrid = new QGridLayout(profileSkillFilters_);
    skillFilterGrid->setContentsMargins(0, 0, 0, 0);
    skillFilterGrid->setHorizontalSpacing(8);
    skillFilterGrid->setVerticalSpacing(4);
    skillFilterGrid->addWidget(new QLabel(QString::fromUtf8("Сортировка")), 0, 0);
    profileSkillSort_ = new QComboBox;
    profileSkillSort_->setObjectName("profileSkillSort");
    profileSkillSort_->addItems({QString::fromUtf8("По имени"), QString::fromUtf8("По уровню"),
        QString::fromUtf8("По XP"), QString::fromUtf8("По весу")});
    profileSkillSort_->setCurrentIndex(std::clamp(displaySettings_.profileSkillSort, 0, 3));
    labelForAccessibility(profileSkillSort_, QString::fromUtf8("Сортировка навыков"));
    skillFilterGrid->addWidget(profileSkillSort_, 0, 1);
    skillFilterGrid->addWidget(new QLabel(QString::fromUtf8("Категория веса")), 0, 2);
    profileSkillWeightCategory_ = new QComboBox;
    profileSkillWeightCategory_->setObjectName("profileSkillWeightCategory");
    profileSkillWeightCategory_->addItems({QString::fromUtf8("Все"), QString::fromUtf8("A (>=1,30)"),
        QString::fromUtf8("B (1,10-1,29)"), QString::fromUtf8("C (0,90-1,09)"),
        QString::fromUtf8("D (0,70-0,89)"), QString::fromUtf8("E (<0,70)")});
    profileSkillWeightCategory_->setCurrentIndex(std::clamp(displaySettings_.profileSkillWeightCategory, 0, 5));
    labelForAccessibility(profileSkillWeightCategory_, QString::fromUtf8("Фильтр навыков по категории веса"));
    skillFilterGrid->addWidget(profileSkillWeightCategory_, 0, 3);
    skillFilterGrid->addWidget(new QLabel(QString::fromUtf8("Вес от")), 1, 0);
    profileSkillWeightMin_ = new QDoubleSpinBox;
    profileSkillWeightMin_->setObjectName("profileSkillWeightMin");
    profileSkillWeightMin_->setRange(0.0, 2.0);
    profileSkillWeightMin_->setSingleStep(0.05);
    profileSkillWeightMin_->setDecimals(2);
    profileSkillWeightMin_->setFixedWidth(82);
    profileSkillWeightMin_->setValue(displaySettings_.profileSkillWeightMin);
    labelForAccessibility(profileSkillWeightMin_, QString::fromUtf8("Минимальный вес навыка"));
    skillFilterGrid->addWidget(profileSkillWeightMin_, 1, 1, Qt::AlignLeft);
    skillFilterGrid->addWidget(new QLabel(QString::fromUtf8("до")), 1, 2);
    profileSkillWeightMax_ = new QDoubleSpinBox;
    profileSkillWeightMax_->setObjectName("profileSkillWeightMax");
    profileSkillWeightMax_->setRange(profileSkillWeightMin_->value(), 2.0);
    profileSkillWeightMax_->setSingleStep(0.05);
    profileSkillWeightMax_->setDecimals(2);
    profileSkillWeightMax_->setFixedWidth(82);
    profileSkillWeightMax_->setValue(std::max(profileSkillWeightMin_->value(), displaySettings_.profileSkillWeightMax));
    labelForAccessibility(profileSkillWeightMax_, QString::fromUtf8("Максимальный вес навыка"));
    skillFilterGrid->addWidget(profileSkillWeightMax_, 1, 3, Qt::AlignLeft);
    profileSkillFilterReset_ = new QPushButton(QString::fromUtf8("Сбросить фильтры"));
    profileSkillFilterReset_->setObjectName("profileSkillFilterReset");
    profileSkillFilterReset_->setToolTip(QString::fromUtf8("Вернуть поиск, сортировку, категорию и диапазон веса к значениям по умолчанию"));
    labelForAccessibility(profileSkillFilterReset_, QString::fromUtf8("Сбросить фильтры навыков"));
    skillFilterGrid->addWidget(profileSkillFilterReset_, 2, 0, 1, 4, Qt::AlignLeft);
    content->addWidget(profileSkillFilters_);
    profileAnalytics_ = new QtProfileAnalytics;
    content->addWidget(profileAnalytics_);
    auto* pomodoro = new QtPomodoro(nullptr, workspace_.directory);
    pomodoro_ = pomodoro;
    pomodoro->setRewardHandler([this](int workMinutes, std::int64_t startedAt) -> QString {
        const auto id = u(profiles_->currentData().toString());
        if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) return QString::fromUtf8("Награда не начислена: требуется восстановление данных.");
        if (!profileSession_.isUnlocked(*workspace_.storage, id)) return QString::fromUtf8("Фокус завершён. Для награды нужен личный вход.");
        if (workspace_.data.vault.pomodoroCoinsPerCycle <= 0) return QString::fromUtf8("Фокус завершён. Награды отключены.");
        if (workMinutes < workspace_.data.vault.pomodoroMinMinutes) return QString::fromUtf8("Фокус завершён, но короче минимального времени награды.");
        if (!pomodoroWithinWindow(workspace_.data.vault, startedAt)) return QString::fromUtf8("Фокус завершён вне расписания наград.");
        const int amount = workspace_.data.vault.pomodoroCoinsPerCycle;
        auto result = runWalletMutationWithAudit(workspace_, id, id, false, "pomodoro_reward",
            "credit " + std::to_string(amount) + " pomodoro_focus", [&] {
                return AppAdjustProfileWallet(*workspace_.storage, id, id, double(amount));
            }, [this](AppLogLevel level, const std::string& event) { appendLog(level, "CoreWalletMutation", event); });
        if (!result.ok || !result.profile)
            return QString::fromUtf8("Награда не начислена: %1").arg(q(result.errorMessage));
        reload();
        return QString::fromUtf8("Начислено Кукоинов: +%1").arg(amount);
    });
    content->addWidget(pomodoro_, 1);
    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setObjectName("search");
    search_->setPlaceholderText(QString::fromUtf8("Поиск по текущему разделу…"));
    search_->setAccessibleName(QString::fromUtf8("Поиск в текущем разделе"));
    search_->setAccessibleDescription(QString::fromUtf8("Ctrl+K переводит сюда фокус. Esc очищает запрос."));
    search_->setClearButtonEnabled(true);
    filters->addWidget(search_);
    catalogProfessionFilter_ = new QComboBox;
    catalogProfessionFilter_->setObjectName("catalogProfessionFilter");
    labelForAccessibility(catalogProfessionFilter_, QString::fromUtf8("Фильтр навыков по профессии"));
    catalogProfessionFilter_->setMaximumWidth(190);
    catalogProfessionFilter_->setToolTip(QString::fromUtf8("Показать навыки, связанные с выбранной профессией"));
    filters->addWidget(catalogProfessionFilter_);
    statusFilter_ = new QComboBox;
    statusFilter_->setObjectName("statusFilter");
    labelForAccessibility(statusFilter_, QString::fromUtf8("Фильтр задач по статусу"));
    statusFilter_->setMaximumWidth(135);
    statusFilter_->addItems({QString::fromUtf8("Все статусы"), QString::fromUtf8("Новая"),
                            QString::fromUtf8("В работе"), QString::fromUtf8("Выполнена")});
    statusFilter_->setCurrentIndex(displaySettings_.taskStatusFilter);
    filters->addWidget(statusFilter_);
    priorityFilter_ = new QComboBox;
    priorityFilter_->setObjectName("priorityFilter");
    labelForAccessibility(priorityFilter_, QString::fromUtf8("Фильтр задач по приоритету"));
    priorityFilter_->setMaximumWidth(150);
    priorityFilter_->addItems({QString::fromUtf8("Любой приоритет"), QString::fromUtf8("Низкий"),
        QString::fromUtf8("Средний"), QString::fromUtf8("Высокий"), QString::fromUtf8("Критический")});
    priorityFilter_->setCurrentIndex(displaySettings_.taskPriorityFilter);
    filters->addWidget(priorityFilter_);
    quickTaskFilter_ = new QComboBox;
    quickTaskFilter_->setObjectName("quickTaskFilter");
    labelForAccessibility(quickTaskFilter_, QString::fromUtf8("Быстрый фильтр задач"));
    quickTaskFilter_->setMaximumWidth(155);
    quickTaskFilter_->addItems({QString::fromUtf8("Все задачи"), QString::fromUtf8("Мне назначено"),
        QString::fromUtf8("На сегодня"), QString::fromUtf8("Просрочено"), QString::fromUtf8("7 дней"),
        QString::fromUtf8("Без проекта"), QString::fromUtf8("Ждут XP"), QString::fromUtf8("Активные"),
        QString::fromUtf8("Требуют внимания"), QString::fromUtf8("Сигналы пайплайна"),
        QString::fromUtf8("Пайплайн: без этапа"), QString::fromUtf8("Пайплайн: вне схемы"),
        QString::fromUtf8("Пайплайн: ветвление"), QString::fromUtf8("Пайплайн: финал открыт")});
    quickTaskFilter_->setCurrentIndex(displaySettings_.taskQuickFilter);
    filters->addWidget(quickTaskFilter_);
    taskCreatedRange_ = new QComboBox;
    taskCreatedRange_->setObjectName("taskCreatedRange");
    labelForAccessibility(taskCreatedRange_, QString::fromUtf8("Фильтр задач по дате создания"));
    taskCreatedRange_->setMaximumWidth(115);
    taskCreatedRange_->addItems({QString::fromUtf8("Созданы: всё"), QString::fromUtf8("Созданы: 7 дн."),
        QString::fromUtf8("Созданы: 30 дн."), QString::fromUtf8("Созданы: 90 дн."), QString::fromUtf8("Созданы: 365 дн.")});
    taskCreatedRange_->setCurrentIndex(displaySettings_.taskCreatedRange);
    filters->addWidget(taskCreatedRange_);
    taskSort_ = new QComboBox;
    taskSort_->setObjectName("taskSortMode");
    labelForAccessibility(taskSort_, QString::fromUtf8("Сортировка задач"));
    taskSort_->setMaximumWidth(165);
    taskSort_->addItems({QString::fromUtf8("Сначала новые"), QString::fromUtf8("Ближайший дедлайн"), QString::fromUtf8("Высокий приоритет")});
    taskSort_->setCurrentIndex(displaySettings_.taskSortMode);
    filters->addWidget(taskSort_);
    taskAssigneeFilter_ = new QComboBox;
    taskAssigneeFilter_->setObjectName("taskAssigneeFilter");
    labelForAccessibility(taskAssigneeFilter_, QString::fromUtf8("Фильтр задач по исполнителю"));
    taskAssigneeFilter_->setMaximumWidth(190);
    filters->addWidget(taskAssigneeFilter_);
    taskProjectFilter_ = new QComboBox;
    taskProjectFilter_->setObjectName("taskProjectFilter");
    labelForAccessibility(taskProjectFilter_, QString::fromUtf8("Фильтр задач по проекту"));
    taskProjectFilter_->setMaximumWidth(170);
    filters->addWidget(taskProjectFilter_);
    taskPipelineFilter_ = new QComboBox;
    taskPipelineFilter_->setObjectName("taskPipelineFilter");
    labelForAccessibility(taskPipelineFilter_, QString::fromUtf8("Фильтр задач по этапу пайплайна"));
    taskPipelineFilter_->setMaximumWidth(180);
    filters->addWidget(taskPipelineFilter_);
    taskFilterReset_ = new QPushButton(QString::fromUtf8("Сбросить фильтры"));
    taskFilterReset_->setObjectName("taskFilterReset");
    labelForAccessibility(taskFilterReset_, QString::fromUtf8("Сбросить фильтры задач"));
    taskFilterReset_->setToolTip(QString::fromUtf8("Очистить поиск и вернуть фильтры задач к значениям по умолчанию"));
    filters->addWidget(taskFilterReset_);
    reportView_ = new QComboBox;
    reportView_->setObjectName("reportView");
    labelForAccessibility(reportView_, QString::fromUtf8("Группировка отчёта"));
    reportView_->setMaximumWidth(170);
    reportView_->addItems({QString::fromUtf8("По проектам"), QString::fromUtf8("По сотрудникам"),
        QString::fromUtf8("По этапам"), QString::fromUtf8("По категориям"), QString::fromUtf8("По статусам"),
        QString::fromUtf8("По приоритетам"), QString::fromUtf8("По срокам")});
    reportView_->setCurrentIndex(displaySettings_.reportView);
    filters->addWidget(reportView_);
    reportDateRange_ = new QComboBox;
    reportDateRange_->setObjectName("reportDateRange");
    labelForAccessibility(reportDateRange_, QString::fromUtf8("Период отчёта"));
    reportDateRange_->setMaximumWidth(180);
    reportDateRange_->addItems({QString::fromUtf8("Всё время"), QString::fromUtf8("30 дней"),
        QString::fromUtf8("90 дней"), QString::fromUtf8("С начала года"), QString::fromUtf8("Период…")});
    reportDateRange_->setCurrentIndex(displaySettings_.reportDateRange);
    reportDateRange_->setToolTip(QString::fromUtf8("Фильтр по дате создания задач; статусы и XP показываются текущие"));
    filters->addWidget(reportDateRange_);
    reportCompare_ = new QCheckBox(QString::fromUtf8("Сравнить"));
    reportCompare_->setObjectName("reportComparePrevious");
    labelForAccessibility(reportCompare_, QString::fromUtf8("Сравнить отчёт с предыдущим периодом"));
    reportCompare_->setToolTip(QString::fromUtf8("Сопоставить с равным предшествующим периодом; доступно для 30/90 дней, начала года и ручного периода"));
    reportCompare_->setMaximumWidth(105);
    reportCompare_->setChecked(displaySettings_.reportComparePrevious);
    filters->addWidget(reportCompare_);
    reportFrom_ = new QDateEdit(displaySettings_.reportDateFrom);
    reportFrom_->setObjectName("reportDateFrom");
    reportFrom_->setCalendarPopup(true);
    reportFrom_->setDisplayFormat("dd.MM.yyyy");
    reportFrom_->setMaximumWidth(118);
    labelForAccessibility(reportFrom_, QString::fromUtf8("Начало периода отчёта"));
    reportTo_ = new QDateEdit(displaySettings_.reportDateTo);
    reportTo_->setObjectName("reportDateTo");
    reportTo_->setCalendarPopup(true);
    reportTo_->setDisplayFormat("dd.MM.yyyy");
    reportTo_->setMaximumWidth(118);
    labelForAccessibility(reportTo_, QString::fromUtf8("Конец периода отчёта"));
    reportCustomRange_ = new QWidget;
    reportCustomRange_->setObjectName("reportCustomRange");
    auto* reportDateLayout = new QHBoxLayout(reportCustomRange_);
    reportDateLayout->setContentsMargins(0, 0, 0, 0);
    reportDateLayout->setSpacing(4);
    reportDateLayout->addWidget(new QLabel(QString::fromUtf8("с")));
    reportDateLayout->addWidget(reportFrom_);
    reportDateLayout->addWidget(new QLabel(QString::fromUtf8("по")));
    reportDateLayout->addWidget(reportTo_);
    filters->addWidget(reportCustomRange_);
    projectsOverdue_ = new QCheckBox(QString::fromUtf8("Просроченные"));
    projectsOverdue_->setObjectName("projectsOverdueOnly");
    projectsOverdue_->setMaximumWidth(120);
    projectsOverdue_->setChecked(displaySettings_.projectsOverdueOnly);
    filters->addWidget(projectsOverdue_);
    projectsXpPending_ = new QCheckBox(QString::fromUtf8("Ждут XP"));
    projectsXpPending_->setObjectName("projectsXpPendingOnly");
    projectsXpPending_->setMaximumWidth(105);
    projectsXpPending_->setChecked(displaySettings_.projectsXpPendingOnly);
    filters->addWidget(projectsXpPending_);
    projectSort_ = new QComboBox;
    projectSort_->setObjectName("projectSort");
    labelForAccessibility(projectSort_, QString::fromUtf8("Сортировка проектов"));
    projectSort_->setMaximumWidth(150);
    projectSort_->addItems({QString::fromUtf8("Название"), QString::fromUtf8("Число задач"),
        QString::fromUtf8("Просрочка"), QString::fromUtf8("Ожидают XP")});
    projectSort_->setCurrentIndex(displaySettings_.projectSortMode);
    filters->addWidget(projectSort_);
    auditSourceFilter_ = new QComboBox;
    auditSourceFilter_->setObjectName("auditSourceFilter");
    labelForAccessibility(auditSourceFilter_, QString::fromUtf8("Источник событий аудита"));
    auditSourceFilter_->setMaximumWidth(145);
    auditSourceFilter_->addItems({QString::fromUtf8("Все события"), QString::fromUtf8("Задачи"), QString::fromUtf8("Профили"),
        QString::fromUtf8("Приложение"), QString::fromUtf8("Хранилище"), QString::fromUtf8("Core-события")});
    auditSourceFilter_->setCurrentIndex(std::clamp(displaySettings_.auditSourceFilter, 0, 5));
    auditSourceFilter_->setToolTip(QString::fromUtf8("Показывать события выбранного источника аудита"));
    filters->addWidget(auditSourceFilter_);
    logInfo_ = new QCheckBox(QString::fromUtf8("Инфо"));
    logInfo_->setObjectName("logInfo"); logInfo_->setChecked(true);
    labelForAccessibility(logInfo_, QString::fromUtf8("Показывать информационные записи журнала"));
    filters->addWidget(logInfo_);
    logInfo_->setChecked(displaySettings_.logShowInfo);
    logWarnings_ = new QCheckBox(QString::fromUtf8("Предупреждения"));
    logWarnings_->setObjectName("logWarnings"); logWarnings_->setChecked(displaySettings_.logShowWarning);
    labelForAccessibility(logWarnings_, QString::fromUtf8("Показывать предупреждения журнала"));
    filters->addWidget(logWarnings_);
    logErrors_ = new QCheckBox(QString::fromUtf8("Ошибки"));
    logErrors_->setObjectName("logErrors"); logErrors_->setChecked(displaySettings_.logShowError);
    labelForAccessibility(logErrors_, QString::fromUtf8("Показывать ошибки журнала"));
    filters->addWidget(logErrors_);
    logSourceFilter_ = new QComboBox;
    logSourceFilter_->setObjectName("logSourceFilter");
    labelForAccessibility(logSourceFilter_, QString::fromUtf8("Источник записей журнала"));
    logSourceFilter_->setMaximumWidth(190);
    logSourceFilter_->setToolTip(QString::fromUtf8("Показывать записи выбранного источника"));
    filters->addWidget(logSourceFilter_);
    logPresetAll_ = new QPushButton(QString::fromUtf8("Все уровни"));
    logPresetAll_->setObjectName("logPresetAll");
    logPresetAll_->setToolTip(QString::fromUtf8("Показать сообщения всех уровней"));
    labelForAccessibility(logPresetAll_, QString::fromUtf8("Показать все уровни журнала"));
    filters->addWidget(logPresetAll_);
    logPresetWarningsErrors_ = new QPushButton(QString::fromUtf8("Предупреждения + ошибки"));
    logPresetWarningsErrors_->setObjectName("logPresetWarningsErrors");
    logPresetWarningsErrors_->setToolTip(QString::fromUtf8("Скрыть информационные сообщения"));
    labelForAccessibility(logPresetWarningsErrors_, QString::fromUtf8("Показать предупреждения и ошибки журнала"));
    filters->addWidget(logPresetWarningsErrors_);
    logPresetErrors_ = new QPushButton(QString::fromUtf8("Только ошибки"));
    logPresetErrors_->setObjectName("logPresetErrors");
    logPresetErrors_->setToolTip(QString::fromUtf8("Оставить только сообщения об ошибках"));
    labelForAccessibility(logPresetErrors_, QString::fromUtf8("Показать только ошибки журнала"));
    filters->addWidget(logPresetErrors_);
    content->addLayout(filters);
    auto* logOptions = new QWidget;
    logOptions->setObjectName("logOptions");
    auto* logOptionsLayout = new QHBoxLayout(logOptions);
    logOptionsLayout->setContentsMargins(0, 0, 0, 0);
    logAutoScroll_ = new QCheckBox(QString::fromUtf8("Автопрокрутка"));
    logAutoScroll_->setObjectName("logAutoScroll");
    logAutoScroll_->setChecked(displaySettings_.logAutoScroll);
    labelForAccessibility(logAutoScroll_, QString::fromUtf8("Автоматически прокручивать журнал к новым записям"));
    logOptionsLayout->addWidget(logAutoScroll_);
    logCompactView_ = new QCheckBox(QString::fromUtf8("Компактно"));
    logCompactView_->setObjectName("logCompactView");
    logCompactView_->setChecked(displaySettings_.logCompactView);
    labelForAccessibility(logCompactView_, QString::fromUtf8("Компактный вид журнала без столбцов времени и источника"));
    logOptionsLayout->addWidget(logCompactView_);
    logOptionsLayout->addStretch();
    content->addWidget(logOptions);
    logActivityChart_ = new QtLogActivityChart;
    content->addWidget(logActivityChart_);
    auditFilters_ = new QWidget;
    auditFilters_->setObjectName("auditFilters");
    auto* auditFilterLayout = new QHBoxLayout(auditFilters_);
    auditFilterLayout->setContentsMargins(0, 0, 0, 0);
    auditFilterLayout->setSpacing(6);
    auditActorFilter_ = new QLineEdit;
    auditActorFilter_->setObjectName("auditActorFilter");
    auditActorFilter_->setPlaceholderText(QString::fromUtf8("Актор"));
    labelForAccessibility(auditActorFilter_, QString::fromUtf8("Фильтр аудита по актору"));
    auditActorFilter_->setClearButtonEnabled(true);
    auditFilterLayout->addWidget(auditActorFilter_);
    auditObjectFilter_ = new QLineEdit;
    auditObjectFilter_->setObjectName("auditObjectFilter");
    auditObjectFilter_->setPlaceholderText(QString::fromUtf8("Задача / профиль / значение"));
    labelForAccessibility(auditObjectFilter_, QString::fromUtf8("Фильтр аудита по задаче, профилю или значению"));
    auditObjectFilter_->setClearButtonEnabled(true);
    auditFilterLayout->addWidget(auditObjectFilter_, 2);
    auditFieldFilter_ = new QLineEdit;
    auditFieldFilter_->setObjectName("auditFieldFilter");
    auditFieldFilter_->setPlaceholderText(QString::fromUtf8("Поле / действие"));
    labelForAccessibility(auditFieldFilter_, QString::fromUtf8("Фильтр аудита по полю или действию"));
    auditFieldFilter_->setClearButtonEnabled(true);
    auditFilterLayout->addWidget(auditFieldFilter_);
    auditFilterReset_ = new QPushButton(QString::fromUtf8("Сбросить"));
    auditFilterReset_->setObjectName("auditFilterReset");
    auditFilterReset_->setToolTip(QString::fromUtf8("Очистить поиск и все фильтры аудита"));
    auditFilterLayout->addWidget(auditFilterReset_);
    content->addWidget(auditFilters_);
    adminStatsFilters_ = new QWidget;
    adminStatsFilters_->setObjectName("adminProfileStatsFilters");
    auto* adminStatsLayout = new QGridLayout(adminStatsFilters_);
    adminStatsLayout->setContentsMargins(0, 0, 0, 0);
    adminStatsLayout->setHorizontalSpacing(8);
    adminStatsLayout->setVerticalSpacing(4);
    adminStatsSearch_ = new QLineEdit;
    adminStatsSearch_->setObjectName("adminProfileStatsSearch");
    adminStatsSearch_->setPlaceholderText(QString::fromUtf8("Фильтр по ID или имени"));
    adminStatsSearch_->setClearButtonEnabled(true);
    labelForAccessibility(adminStatsSearch_, QString::fromUtf8("Поиск по ID или имени профиля в статистике"));
    adminStatsLayout->addWidget(adminStatsSearch_, 0, 0, 1, 2);
    adminStatsArchived_ = new QCheckBox(QString::fromUtf8("Включая архив"));
    adminStatsArchived_->setObjectName("adminStatsIncludeArchived");
    adminStatsArchived_->setChecked(displaySettings_.adminStatsIncludeArchived);
    labelForAccessibility(adminStatsArchived_, QString::fromUtf8("Включить архивные профили в статистику"));
    adminStatsLayout->addWidget(adminStatsArchived_, 0, 2);
    adminStatsRank_ = new QComboBox;
    adminStatsRank_->setObjectName("adminStatsRankFilter");
    labelForAccessibility(adminStatsRank_, QString::fromUtf8("Фильтр статистики по рангу профиля"));
    adminStatsRank_->addItem(QString::fromUtf8("Все ранги"), 0);
    const auto& rankOptions = adminProfileRanks();
    for (int i = 0; i < int(rankOptions.size()); ++i)
        adminStatsRank_->addItem(rankOptions[size_t(i)].first, i + 1);
    adminStatsRank_->setCurrentIndex(std::clamp(displaySettings_.adminStatsRankFilter, 0, 16));
    adminStatsLayout->addWidget(adminStatsRank_, 0, 3);
    adminStatsView_ = new QComboBox;
    adminStatsView_->setObjectName("adminStatsView");
    labelForAccessibility(adminStatsView_, QString::fromUtf8("Представление статистики профилей"));
    adminStatsView_->addItems({QString::fromUtf8("Все профили"), QString::fromUtf8("Топ по уровню"),
        QString::fromUtf8("Топ по XP"), QString::fromUtf8("Топ по ачивкам"), QString::fromUtf8("Неактивные"),
        QString::fromUtf8("Профили на прогреве"), QString::fromUtf8("Распределение по рангам"),
        QString::fromUtf8("Средние категории")});
    adminStatsView_->setCurrentIndex(std::clamp(displaySettings_.adminStatsView, 0, 7));
    adminStatsLayout->addWidget(adminStatsView_, 0, 4);
    adminStatsInactivityLabel_ = new QLabel(QString::fromUtf8("Порог простоя, дней"));
    adminStatsLayout->addWidget(adminStatsInactivityLabel_, 1, 0);
    adminStatsInactivityDays_ = new QSpinBox;
    adminStatsInactivityDays_->setObjectName("adminStatsInactivityDays");
    adminStatsInactivityDays_->setRange(1, 365);
    adminStatsInactivityDays_->setValue(std::clamp(displaySettings_.adminStatsInactivityDays, 1, 365));
    labelForAccessibility(adminStatsInactivityDays_, QString::fromUtf8("Порог неактивности профиля в днях"));
    adminStatsLayout->addWidget(adminStatsInactivityDays_, 1, 1);
    adminStatsAutoRefresh_ = new QCheckBox(QString::fromUtf8("Автообновление"));
    adminStatsAutoRefresh_->setObjectName("adminStatsAutoRefresh");
    adminStatsAutoRefresh_->setChecked(displaySettings_.adminStatsAutoRefresh);
    labelForAccessibility(adminStatsAutoRefresh_, QString::fromUtf8("Автоматически обновлять статистику профилей"));
    adminStatsLayout->addWidget(adminStatsAutoRefresh_, 1, 2);
    adminStatsLayout->addWidget(new QLabel(QString::fromUtf8("Интервал, сек")), 1, 3);
    adminStatsRefreshSeconds_ = new QSpinBox;
    adminStatsRefreshSeconds_->setObjectName("adminStatsRefreshSeconds");
    adminStatsRefreshSeconds_->setRange(5, 120);
    adminStatsRefreshSeconds_->setValue(std::clamp(displaySettings_.adminStatsRefreshSeconds, 5, 120));
    labelForAccessibility(adminStatsRefreshSeconds_, QString::fromUtf8("Интервал автообновления статистики в секундах"));
    adminStatsLayout->addWidget(adminStatsRefreshSeconds_, 1, 4);
    adminStatsRefreshButton_ = new QPushButton(QString::fromUtf8("Обновить"));
    adminStatsRefreshButton_->setObjectName("adminStatsRefresh");
    labelForAccessibility(adminStatsRefreshButton_, QString::fromUtf8("Обновить статистику профилей сейчас"));
    adminStatsLayout->addWidget(adminStatsRefreshButton_, 1, 5);
    adminStatsReset_ = new QPushButton(QString::fromUtf8("Сбросить фильтры"));
    adminStatsReset_->setObjectName("adminStatsReset");
    labelForAccessibility(adminStatsReset_, QString::fromUtf8("Сбросить фильтры статистики профилей"));
    adminStatsLayout->addWidget(adminStatsReset_, 1, 6);
    content->addWidget(adminStatsFilters_);
    table_ = new QTableWidget;
    table_->setObjectName("records");
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(displaySettings_.compactRows ? 24 : 28);
    table_->horizontalHeader()->setStretchLastSection(true);
    content->addWidget(table_, 1);
    bottomActions_ = new QWidget;
    auto* bottom = new QHBoxLayout(bottomActions_);
    bottom->setContentsMargins(0, 0, 0, 0);
    detailsToggle_ = new QPushButton(QString::fromUtf8("Подробности"));
    detailsToggle_->setObjectName("detailsToggle");
    detailsToggle_->setCheckable(true);
    bottom->addWidget(detailsToggle_);
    changeStatus_ = new QPushButton(QString::fromUtf8("Изменить статус"));
    changeStatus_->setObjectName("changeStatus");
    changeStatus_->setToolTip(QString::fromUtf8("Переходы проверяются ядром. Завершение открывает распределение XP."));
    bottom->addWidget(changeStatus_);
    bulkEdit_ = new QPushButton(QString::fromUtf8("Массовое изменение"));
    bulkEdit_->setObjectName("bulkTaskEdit");
    bulkEdit_->setToolTip(QString::fromUtf8("Изменить статус или приоритет нескольких выбранных задач"));
    bottom->addWidget(bulkEdit_);
    bulkDelete_ = new QPushButton(QString::fromUtf8("Удалить выбранные"));
    bulkDelete_->setObjectName("bulkTaskDelete");
    bulkDelete_->setToolTip(QString::fromUtf8("Атомарно удалить несколько задач и откатить их начисленный XP, если профили не изменились после выбранных задач"));
    bottom->addWidget(bulkDelete_);
    taskSelectionTools_ = new QToolButton;
    taskSelectionTools_->setObjectName("taskSelectionTools");
    taskSelectionTools_->setText(QString::fromUtf8("Выбор задач"));
    taskSelectionTools_->setToolTip(QString::fromUtf8("Выбрать видимые задачи или снять текущий выбор"));
    taskSelectionTools_->setPopupMode(QToolButton::InstantPopup);
    auto* taskSelectionMenu = new QMenu(taskSelectionTools_);
    auto* selectVisibleTasks = taskSelectionMenu->addAction(QString::fromUtf8("Выбрать все видимые"));
    selectVisibleTasks->setObjectName("selectVisibleTasks");
    auto* clearTaskSelection = taskSelectionMenu->addAction(QString::fromUtf8("Снять выбор"));
    clearTaskSelection->setObjectName("clearTaskSelection");
    taskSelectionTools_->setMenu(taskSelectionMenu);
    bottom->addWidget(taskSelectionTools_);
    editEntry_ = new QPushButton(QString::fromUtf8("Редактировать"));
    editEntry_->setObjectName("editEntry");
    bottom->addWidget(editEntry_);
    deleteEntry_ = new QPushButton(QString::fromUtf8("Удалить"));
    deleteEntry_->setObjectName("deleteEntry");
    deleteEntry_->setToolTip(QString::fromUtf8("Удалить выбранную запись с проверкой связей"));
    bottom->addWidget(deleteEntry_);
    moveUp_ = new QPushButton(QString::fromUtf8("Выше"));
    moveUp_->setObjectName("movePipelineUp");
    moveUp_->setToolTip(QString::fromUtf8("Переместить этап на одну позицию выше"));
    bottom->addWidget(moveUp_);
    moveDown_ = new QPushButton(QString::fromUtf8("Ниже"));
    moveDown_->setObjectName("movePipelineDown");
    moveDown_->setToolTip(QString::fromUtf8("Переместить этап на одну позицию ниже"));
    bottom->addWidget(moveDown_);
    pipelineMap_ = new QPushButton(QString::fromUtf8("Карта переходов"));
    pipelineMap_->setObjectName("pipelineMap");
    pipelineMap_->setToolTip(QString::fromUtf8("Просмотреть этапы по веткам и допустимые переходы между ними"));
    bottom->addWidget(pipelineMap_);
    advanceStage_ = new QPushButton(QString::fromUtf8("Следующий этап"));
    advanceStage_->setObjectName("advanceStage");
    bottom->addWidget(advanceStage_);
    achievements_ = new QPushButton(QString::fromUtf8("Достижения"));
    achievements_->setObjectName("showAchievements");
    bottom->addWidget(achievements_);
    removeSpirit_ = new QPushButton(QString::fromUtf8("Снять Злого духа · 200"));
    removeSpirit_->setObjectName("removeEvilSpirit");
    removeSpirit_->setToolTip(QString::fromUtf8("Личная операция: списывает 200 Кукоинов и пополняет локальное хранилище."));
    bottom->addWidget(removeSpirit_);
    exportReport_ = new QPushButton(QString::fromUtf8("Экспорт CSV"));
    exportReport_->setObjectName("exportReport");
    exportReport_->setToolTip(QString::fromUtf8("Сохранить текущий локальный управленческий отчёт в UTF-8 CSV"));
    bottom->addWidget(exportReport_);
    exportTasks_ = new QToolButton;
    exportTasks_->setObjectName("exportTasks");
    exportTasks_->setText(QString::fromUtf8("Экспорт задач"));
    exportTasks_->setToolTip(QString::fromUtf8("Сохранить строки задач, видимые с текущими фильтрами и поиском"));
    exportTasks_->setPopupMode(QToolButton::InstantPopup);
    auto* taskExportMenu = new QMenu(exportTasks_);
    auto* taskCsvAction = taskExportMenu->addAction(QString::fromUtf8("В CSV…"));
    taskCsvAction->setObjectName("exportTasksCsv");
    auto* taskTxtAction = taskExportMenu->addAction(QString::fromUtf8("В TXT…"));
    taskTxtAction->setObjectName("exportTasksTxt");
    exportTasks_->setMenu(taskExportMenu);
    bottom->addWidget(exportTasks_);
    exportAudit_ = new QPushButton(QString::fromUtf8("Экспорт аудита"));
    exportAudit_->setObjectName("exportAudit");
    exportAudit_->setToolTip(QString::fromUtf8("Сохранить видимые после поиска события аудита в UTF-8 CSV"));
    bottom->addWidget(exportAudit_);
    exportLogs_ = new QPushButton(QString::fromUtf8("Экспорт логов"));
    exportLogs_->setObjectName("exportLogs");
    exportLogs_->setToolTip(QString::fromUtf8("Сохранить сообщения журнала с учётом текущих фильтров в UTF-8 TXT"));
    labelForAccessibility(exportLogs_, QString::fromUtf8("Экспортировать видимые записи журнала в UTF-8 TXT"));
    bottom->addWidget(exportLogs_);
    clearLogs_ = new QPushButton(QString::fromUtf8("Очистить логи"));
    clearLogs_->setObjectName("clearLogs");
    clearLogs_->setToolTip(QString::fromUtf8("Очистить локальный журнал после подтверждения"));
    labelForAccessibility(clearLogs_, QString::fromUtf8("Очистить журнал приложения"),
        QString::fromUtf8("Откроется запрос подтверждения; отмена сохранит журнал."));
    bottom->addWidget(clearLogs_);
    reapplyRules_ = new QPushButton(QString::fromUtf8("Пересчитать профили"));
    reapplyRules_->setObjectName("reapplyRules");
    reapplyRules_->setToolTip(QString::fromUtf8("Сохранить общий XP и пересчитать уровни всех активных и архивных профилей по текущим правилам"));
    bottom->addWidget(reapplyRules_);
    directXp_ = new QPushButton(QString::fromUtf8("Добавить XP"));
    directXp_->setObjectName("directXp");
    directXp_->setToolTip(QString::fromUtf8("Вручную начислить XP одному навыку выбранного активного профиля"));
    bottom->addWidget(directXp_);
    walletAdjust_ = new QPushButton(QString::fromUtf8("Изменить кошелёк"));
    walletAdjust_->setObjectName("adjustProfileWallet");
    walletAdjust_->setToolTip(QString::fromUtf8("Администраторское начисление или списание Кукоинов с записью в аудит"));
    bottom->addWidget(walletAdjust_);
    walletHistory_ = new QPushButton(QString::fromUtf8("История кошелька"));
    walletHistory_->setObjectName("profileWalletHistory");
    walletHistory_->setToolTip(QString::fromUtf8("Операции кошелька выбранного профиля"));
    bottom->addWidget(walletHistory_);
    profileHistory_ = new QPushButton(QString::fromUtf8("История профиля"));
    profileHistory_->setObjectName("profileActivityHistory");
    profileHistory_->setToolTip(QString::fromUtf8("События из локального аудита профилей; без истории задач и XP"));
    bottom->addWidget(profileHistory_);
    profileExport_ = new QToolButton;
    profileExport_->setObjectName("profileReportExport");
    profileExport_->setText(QString::fromUtf8("Отчёт профиля"));
    profileExport_->setToolTip(QString::fromUtf8("Экспортировать уровень, XP, активность, категории и навыки выбранного профиля"));
    profileExport_->setPopupMode(QToolButton::InstantPopup);
    auto* profileExportMenu = new QMenu(profileExport_);
    auto* profileTxtAction = profileExportMenu->addAction(QString::fromUtf8("В TXT…"));
    profileTxtAction->setObjectName("profileReportTxt");
    auto* profileCsvAction = profileExportMenu->addAction(QString::fromUtf8("В CSV…"));
    profileCsvAction->setObjectName("profileReportCsv");
    profileExport_->setMenu(profileExportMenu);
    bottom->addWidget(profileExport_);
    projectFocus_ = new QPushButton(QString::fromUtf8("Задачи проекта"));
    projectFocus_->setObjectName("focusProjectTasks");
    projectFocus_->setToolTip(QString::fromUtf8("Открыть задачи выбранного проекта с проектным фильтром"));
    bottom->addWidget(projectFocus_);
    openShortcut_ = new QPushButton(QString::fromUtf8("Открыть"));
    openShortcut_->setObjectName("openShortcut");
    openShortcut_->setToolTip(QString::fromUtf8("Открыть выбранный локальный файл через Windows"));
    bottom->addWidget(openShortcut_);
    cloudPull_ = new QPushButton(QString::fromUtf8("Получить из облака"));
    cloudPull_->setObjectName("cloudPull");
    cloudPull_->setStyleSheet("min-height: 40px; max-height: 40px;");
    cloudPull_->setToolTip(QString::fromUtf8("Ручной pull после подтверждения; перед копированием создаётся полный снимок рабочей папки"));
    bottom->addWidget(cloudPull_);
    cloudPushPreview_ = new QPushButton(QString::fromUtf8("Выгрузить всё…"));
    cloudPushPreview_->setObjectName("cloudPushPreview");
    cloudPushPreview_->setStyleSheet("min-height: 40px; max-height: 40px;");
    cloudPushPreview_->setToolTip(QString::fromUtf8("Предпросмотр, подтверждение и транзакционная выгрузка всей Qt-копии в облако"));
    bottom->addWidget(cloudPushPreview_);
    cloudResolve_ = new QPushButton(QString::fromUtf8("Сравнить версии"));
    cloudResolve_->setObjectName("cloudResolve"); cloudResolve_->setStyleSheet("min-height: 40px; max-height: 40px;");
    cloudResolve_->setToolTip(QString::fromUtf8("Сравнить задачи и пайплайн, принять облачную версию или восстановить локальный снимок"));
    bottom->addWidget(cloudResolve_);
    storageResolve_ = new QPushButton(QString::fromUtf8("Разрешить storage.json"));
    storageResolve_->setObjectName("storageResolve"); storageResolve_->setStyleSheet("min-height: 40px; max-height: 40px;");
    storageResolve_->setToolTip(QString::fromUtf8("Сравнить баланс, журнал и ревизию кошелька и выбрать целую версию"));
    bottom->addWidget(storageResolve_);
    cloudReleaseButton_ = new QToolButton;
    cloudReleaseButton_->setObjectName("cloudReleaseActions");
    cloudReleaseButton_->setText(QString::fromUtf8("Обновление клиента"));
    cloudReleaseButton_->setPopupMode(QToolButton::InstantPopup);
    auto* cloudReleaseMenu = new QMenu(cloudReleaseButton_);
    cloudReleaseDownload_ = cloudReleaseMenu->addAction(QString::fromUtf8("Скачать установщик"));
    cloudReleaseDownload_->setObjectName("cloudReleaseDownload");
    cloudReleaseLaunch_ = cloudReleaseMenu->addAction(QString::fromUtf8("Запустить установщик"));
    cloudReleaseLaunch_->setObjectName("cloudReleaseLaunch");
    cloudReleaseButton_->setMenu(cloudReleaseMenu);
    cloudReleaseButton_->setToolTip(QString::fromUtf8("Загрузить или запустить установщик более новой версии из manifest"));
    bottom->addWidget(cloudReleaseButton_);
    bottom->addStretch();
    content->addWidget(bottomActions_);
    details_ = new QTextBrowser;
    details_->setObjectName("details");
    details_->setMaximumHeight(180);
    details_->setOpenExternalLinks(false);
    details_->hide();
    content->addWidget(details_);
    body->addLayout(content, 1);
    layout->addLayout(body, 1);
    setCentralWidget(root);
    statusBar()->showMessage(QString::fromUtf8("Локальная копия · без облака · ") + q(workspace_.directory.u8string()));
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString& text) {
        if (text.isEmpty() || text.startsWith(QString::fromUtf8("Локальная копия · без облака ·"))) return;
        const bool failed = text.contains(QString::fromUtf8("не удалось"), Qt::CaseInsensitive) ||
            text.contains(QString::fromUtf8("ошибка"), Qt::CaseInsensitive);
        const bool warning = !failed && (text.contains(QString::fromUtf8("не найден"), Qt::CaseInsensitive) ||
            text.contains(QString::fromUtf8("не выбрана"), Qt::CaseInsensitive) ||
            text.contains(QString::fromUtf8("нет доступ"), Qt::CaseInsensitive));
        QString source = navigation_ && navigation_->currentItem() ? navigation_->currentItem()->text() : QString();
        source.remove(QRegularExpression(QStringLiteral("\\s+F\\d+$")));
        source = source.simplified();
        if (source.isEmpty()) source = QStringLiteral("Qt");
        appendLog(failed ? AppLogLevel::Error : warning ? AppLogLevel::Warning : AppLogLevel::Info,
            u(source), u(SanitizeQtLogMessage(text)));
        if (navigation_->currentRow() == Logs) render();
    });

    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(navigation_, &QListWidget::currentRowChanged, this, [this] {
        if (displaySettings_.lastPage == Logs) displaySettings_.logFilter = search_->text();
        saveDisplayContext();
        QSignalBlocker blocker(search_);
        search_->setText(navigation_->currentRow() == Logs ? displaySettings_.logFilter : QString());
        render();
    });
    connect(profiles_, &QComboBox::currentIndexChanged, this, [this] { profileSession_.lock(); saveDisplayContext(); render(); });
    connect(search_, &QLineEdit::textChanged, this, [this] {
        if (navigation_->currentRow() == Logs) {
            displaySettings_.logFilter = search_->text();
            saveDisplayContext();
        }
        render();
    });
    connect(profileSkillSort_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(profileSkillWeightCategory_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(profileSkillWeightMin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (value > profileSkillWeightMax_->value()) profileSkillWeightMax_->setValue(value);
        saveDisplayContext(); render();
    });
    connect(profileSkillWeightMax_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (value < profileSkillWeightMin_->value()) profileSkillWeightMin_->setValue(value);
        saveDisplayContext(); render();
    });
    connect(profileSkillFilterReset_, &QPushButton::clicked, this, [this] {
        const QSignalBlocker searchBlock(search_);
        const QSignalBlocker sortBlock(profileSkillSort_);
        const QSignalBlocker categoryBlock(profileSkillWeightCategory_);
        const QSignalBlocker minBlock(profileSkillWeightMin_);
        const QSignalBlocker maxBlock(profileSkillWeightMax_);
        search_->clear();
        profileSkillSort_->setCurrentIndex(0);
        profileSkillWeightCategory_->setCurrentIndex(0);
        profileSkillWeightMin_->setValue(0.0);
        profileSkillWeightMax_->setValue(2.0);
        saveDisplayContext();
        render();
    });
    connect(statusFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(priorityFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(quickTaskFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskPipelineSummary_, &QLabel::linkActivated, this, [this](const QString& link) {
        const int filter = link == "risk:all" ? 9 : link == "risk:missing" ? 10 : link == "risk:unknown" ? 11 :
            link == "risk:branching" ? 12 : link == "risk:final" ? 13 : 0;
        quickTaskFilter_->setCurrentIndex(filter);
    });
    connect(taskCreatedRange_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskSort_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskAssigneeFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskProjectFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskPipelineFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskFilterReset_, &QPushButton::clicked, this, [this] {
        const QSignalBlocker searchBlock(search_);
        const QSignalBlocker statusBlock(statusFilter_);
        const QSignalBlocker priorityBlock(priorityFilter_);
        const QSignalBlocker quickBlock(quickTaskFilter_);
        const QSignalBlocker ageBlock(taskCreatedRange_);
        const QSignalBlocker sortBlock(taskSort_);
        const QSignalBlocker assigneeBlock(taskAssigneeFilter_);
        const QSignalBlocker projectBlock(taskProjectFilter_);
        const QSignalBlocker pipelineBlock(taskPipelineFilter_);
        search_->clear();
        statusFilter_->setCurrentIndex(0);
        priorityFilter_->setCurrentIndex(0);
        quickTaskFilter_->setCurrentIndex(0);
        taskCreatedRange_->setCurrentIndex(0);
        taskSort_->setCurrentIndex(0);
        taskAssigneeFilter_->setCurrentIndex(0);
        taskProjectFilter_->setCurrentIndex(0);
        taskPipelineFilter_->setCurrentIndex(0);
        saveDisplayContext();
        render();
    });
    connect(catalogProfessionFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(reportView_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(reportDateRange_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(reportCompare_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    connect(reportFrom_, &QDateEdit::dateChanged, this, [this](const QDate& date) {
        if (date > reportTo_->date()) { QSignalBlocker blocker(reportTo_); reportTo_->setDate(date); }
        saveDisplayContext(); render();
    });
    connect(reportTo_, &QDateEdit::dateChanged, this, [this](const QDate& date) {
        if (date < reportFrom_->date()) { QSignalBlocker blocker(reportFrom_); reportFrom_->setDate(date); }
        saveDisplayContext(); render();
    });
    connect(projectSort_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(auditSourceFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(logInfo_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    connect(logWarnings_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    connect(logErrors_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    connect(logSourceFilter_, &QComboBox::currentIndexChanged, this, [this] {
        displaySettings_.logSourceFilter = logSourceFilter_->currentData().toString();
        saveDisplayContext(); render();
    });
    connect(logAutoScroll_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    connect(logCompactView_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    const auto setLogLevels = [this](bool info, bool warnings, bool errors) {
        const QSignalBlocker infoBlocker(logInfo_);
        const QSignalBlocker warningsBlocker(logWarnings_);
        const QSignalBlocker errorsBlocker(logErrors_);
        logInfo_->setChecked(info);
        logWarnings_->setChecked(warnings);
        logErrors_->setChecked(errors);
        saveDisplayContext();
        render();
    };
    connect(logPresetAll_, &QPushButton::clicked, this, [setLogLevels] { setLogLevels(true, true, true); });
    connect(logPresetWarningsErrors_, &QPushButton::clicked, this, [setLogLevels] { setLogLevels(false, true, true); });
    connect(logPresetErrors_, &QPushButton::clicked, this, [setLogLevels] { setLogLevels(false, false, true); });
    connect(projectsOverdue_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    connect(projectsXpPending_, &QCheckBox::toggled, this, [this] { saveDisplayContext(); render(); });
    for (auto* filter : {auditActorFilter_, auditObjectFilter_, auditFieldFilter_})
        connect(filter, &QLineEdit::textChanged, this, [this] { render(); });
    connect(auditFilterReset_, &QPushButton::clicked, this, [this] {
        search_->clear();
        auditActorFilter_->clear();
        auditObjectFilter_->clear();
        auditFieldFilter_->clear();
        auditSourceFilter_->setCurrentIndex(0);
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        details();
        const auto id = table_->currentItem() ? table_->currentItem()->data(Qt::UserRole).toString() : QString();
        projectFocus_->setEnabled(navigation_->currentRow() == Projects && !id.isEmpty() && id != QStringLiteral("__no_project"));
        const auto selectedRows = table_->selectionModel()->selectedRows();
        bool allowed = selectedRows.size() >= 2;
        bool deletable = selectedRows.size() >= 2;
        for (const auto& index : selectedRows) {
            const auto selectedId = u(table_->item(index.row(), 0)->data(Qt::UserRole).toString());
            const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
                [&](const auto& item) { return item.id == selectedId; });
            if (task == workspace_.data.tasks.end()) allowed = false;
            if (task == workspace_.data.tasks.end()) deletable = false;
        }
        bulkEdit_->setEnabled(navigation_->currentRow() == Tasks && admin_ && allowed);
        bulkDelete_->setEnabled(navigation_->currentRow() == Tasks && admin_ && deletable);
    });
    connect(table_, &QTableWidget::itemClicked, this, [this](QTableWidgetItem* item) {
        if (navigation_->currentRow() != AdminProfileStats || !item) return;
        const auto id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(),
            [&](const auto& value) { return value.id == u(id); });
        if (profile == workspace_.profiles.end()) return;
        if (profile->archived) {
            statusBar()->showMessage(QString::fromUtf8("Архивный профиль можно открыть через управление профилями."), 5000);
            return;
        }
        const int index = profiles_->findData(id);
        if (index >= 0) { profiles_->setCurrentIndex(index); navigation_->setCurrentRow(ProfilePage); }
    });
    connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* item) {
        if (navigation_->currentRow() == ProfilePage && displaySettings_.profileViewMode == 3 && item && workspace_.modules.tasks) {
            const auto taskId = item->data(Qt::UserRole).toString();
            profileTaskFilterButtons_[0]->click();
            for (int row = 0; row < table_->rowCount(); ++row) {
                if (!table_->item(row, 0) || table_->item(row, 0)->data(Qt::UserRole).toString() != taskId) continue;
                table_->selectRow(row);
                break;
            }
            return;
        }
        if (navigation_->currentRow() == Statistics) {
            if (!detailsToggle_->isChecked()) detailsToggle_->setChecked(true);
            details();
        }
    });
    connect(detailsToggle_, &QPushButton::toggled, details_, &QWidget::setVisible);
    connect(primary_, &QPushButton::clicked, this, [this] { if (navigation_->currentRow() == ModelSettingsPage) saveModelSettings(); else createEntry(); });
    connect(openModelSettings, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(ModelSettingsPage); });
    connect(browseModel, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Выбрать 3D-модель"), modelPath_->text(),
            QString::fromUtf8("Модели OBJ / FBX (*.obj *.fbx)"));
        if (path.isEmpty()) return;
        modelPath_->setText(path);
        loadSelectedModel();
    });
    connect(modelChoice_, &QComboBox::currentIndexChanged, this, [this] {
        if (restoringModelSettings_) return;
        const auto name = modelChoice_->currentData().toString();
        modelPath_->setText(name.isEmpty() ? QString() : q((workspace_.directory / "models" / u(name)).u8string()));
        loadSelectedModel();
    });
    auto modelControlChanged = [this] { if (!restoringModelSettings_) updateModelSettingsFromControls(); };
    for (auto* slider : {modelYaw_, modelPitch_, modelZoom_, modelSpeed_}) connect(slider, &QSlider::valueChanged, this, modelControlChanged);
    connect(modelAutoRotate_, &QCheckBox::toggled, this, modelControlChanged);
    connect(modelPath_, &QLineEdit::editingFinished, this, [this] { if (!restoringModelSettings_) loadSelectedModel(); });
    connect(modelColor_, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(modelSettings_.lineColor, this, QString::fromUtf8("Цвет линий"));
        if (!color.isValid()) return;
        modelSettings_.lineColor = color;
        modelColor_->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
        modelViewer_->setSettings(modelSettings_);
    });
    modelPath_->setText(modelSettings_.modelPath);
    loadSelectedModel();
    connect(editEntry_, &QPushButton::clicked, this, [this] { createEntry(true); });
    connect(deleteEntry_, &QPushButton::clicked, this, [this] { deleteEntry(); });
    connect(moveUp_, &QPushButton::clicked, this, [this] { movePipeline(-1); });
    connect(moveDown_, &QPushButton::clicked, this, [this] { movePipeline(1); });
    connect(pipelineMap_, &QPushButton::clicked, this, [this] { ShowQtPipelineMap(this, workspace_.data.pipelineSteps); });
    connect(openShortcut_, &QPushButton::clicked, this, [this] {
        if (navigation_->currentRow() != Shortcuts) return;
        const auto found = std::find_if(workspace_.data.shortcuts.begin(), workspace_.data.shortcuts.end(),
            [this](const auto& entry) { return entry.id == u(selectedId()); });
        std::error_code ec;
        const bool exists = found != workspace_.data.shortcuts.end() &&
            std::filesystem::exists(std::filesystem::u8path(found->path), ec) && !ec;
        if (!exists || !QDesktopServices::openUrl(QUrl::fromLocalFile(q(found->path)))) message(u8"Не удалось открыть ярлык.");
    });
    connect(cloudPull_, &QPushButton::clicked, this, [this] { pullCloud(); });
    connect(cloudPushPreview_, &QPushButton::clicked, this, [this] { previewCloudPush(); });
    connect(cloudResolve_, &QPushButton::clicked, this, [this] { resolveCloudConflict(); });
    connect(storageResolve_, &QPushButton::clicked, this, [this] { resolveStorageConflict(); });
    connect(cloudReleaseDownload_, &QAction::triggered, this, [this] { downloadCloudRelease(); });
    connect(cloudReleaseLaunch_, &QAction::triggered, this, [this] { launchCloudRelease(); });
    connect(achievements_, &QPushButton::clicked, this, [this] {
        ShowAchievements(this, workspace_, u(profiles_->currentData().toString()), admin_);
        render();
    });
    connect(removeSpirit_, &QPushButton::clicked, this, [this] {
        const auto id = u(profiles_->currentData().toString());
        if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) {
            message(u8"Сначала завершите восстановление данных."); return;
        }
        if (!profileSession_.isUnlocked(*workspace_.storage, id)) {
            render(); message(u8"Сначала войдите в выбранный профиль."); return;
        }
        QMessageBox confirm(QMessageBox::Question, QString::fromUtf8("Снять Злого духа"),
            QString::fromUtf8("Списать 200 Кукоинов и снять Злого духа?"),
            QMessageBox::Yes | QMessageBox::No, this);
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        auto result = runWalletMutationWithAudit(workspace_, id, id, true, "spirit_purchase",
            "evil->none cost=200", [&] {
                return AppRemoveEvilSpiritForCoins(*workspace_.storage, id, id,
                    workspace_.directory, workspace_.data.vault, 200.0);
            }, [this](AppLogLevel level, const std::string& event) { appendLog(level, "CoreWalletMutation", event); });
        if (!result.ok) { message(result.errorMessage.empty() ? u8"Не удалось снять Злого духа." : result.errorMessage); return; }
        reload();
    });
    connect(advanceStage_, &QPushButton::clicked, this, [this] {
        if (!requireAdmin() || !workspace_.modules.pipeline || navigation_->currentRow() != Tasks) return;
        if (ShowPipelineTransition(this, workspace_, u(selectedId()))) reload();
    });
    connect(changeStatus_, &QPushButton::clicked, this, [this] { changeStatus(); });
    connect(bulkEdit_, &QPushButton::clicked, this, [this] { bulkEditTasks(); });
    connect(bulkDelete_, &QPushButton::clicked, this, [this] {
        const auto selectedRows = table_->selectionModel()->selectedRows();
        std::vector<std::string> ids;
        for (const auto& index : selectedRows)
            if (auto* cell = table_->item(index.row(), 0)) ids.push_back(u(cell->data(Qt::UserRole).toString()));
        if (ids.size() < 2) return;
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить выбранные задачи"),
            QString::fromUtf8("Удалить задач: %1. Начисленный ими XP будет откачен атомарно, только если все профили соответствуют цепочке выбранных задач. Если проверка не пройдёт, ничего не удалится.").arg(ids.size()),
            QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить и откатить XP"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        const bool includesAwardedTask = std::any_of(ids.begin(), ids.end(), [&](const auto& id) {
            const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
                [&](const auto& item) { return item.id == id; });
            return task != workspace_.data.tasks.end() && !task->participants.empty();
        });
        AppContext context{workspace_.directory, *workspace_.storage, workspace_.catalog};
        const auto result = DeleteAwardedTasksWithRecovery(context, workspace_.data.tasks,
            workspace_.data.taskAudit, ids, u(profiles_->currentData().toString()), "admin/qt");
        if (!result.ok) {
            appendLog(AppLogLevel::Warning, "CoreTaskMutation", "Bulk task deletion failed or rolled back");
            message(result.errorMessage); return;
        }
        appendLog(AppLogLevel::Info, "CoreTaskMutation",
            "Bulk task deletion committed: " + std::to_string(result.changedCount));
        reload();
        statusBar()->showMessage(includesAwardedTask
            ? QString::fromUtf8("Удалено выбранных задач: %1 · XP профилей откатан транзакционно").arg(result.changedCount)
            : QString::fromUtf8("Удалено выбранных задач: %1").arg(result.changedCount), 6000);
    });
    connect(selectVisibleTasks, &QAction::triggered, this, [this] {
        if (!requireAdmin() || navigation_->currentRow() != Tasks) return;
        for (int row = 0; row < table_->rowCount(); ++row)
            table_->selectionModel()->select(table_->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    });
    connect(clearTaskSelection, &QAction::triggered, this, [this] {
        if (!requireAdmin() || navigation_->currentRow() != Tasks) return;
        table_->clearSelection();
    });
    connect(exportReport_, &QPushButton::clicked, this, [this] {
        if (navigation_->currentRow() == AdminProfileStats) exportAdminProfileStats();
        else exportReport();
    });
    const auto saveAdminStats = [this] { saveDisplayContext(); render(); };
    connect(adminStatsSearch_, &QLineEdit::textChanged, this, saveAdminStats);
    connect(adminStatsArchived_, &QCheckBox::toggled, this, saveAdminStats);
    connect(adminStatsRank_, &QComboBox::currentIndexChanged, this, saveAdminStats);
    connect(adminStatsView_, &QComboBox::currentIndexChanged, this, saveAdminStats);
    connect(adminStatsAutoRefresh_, &QCheckBox::toggled, this, saveAdminStats);
    connect(adminStatsRefreshSeconds_, &QSpinBox::valueChanged, this, saveAdminStats);
    connect(adminStatsInactivityDays_, &QSpinBox::valueChanged, this, saveAdminStats);
    connect(adminStatsRefreshButton_, &QPushButton::clicked, this, [this] { refreshAdminProfileStats(); render(); });
    connect(adminStatsReset_, &QPushButton::clicked, this, [this] {
        const QSignalBlocker searchBlock(adminStatsSearch_);
        const QSignalBlocker archivedBlock(adminStatsArchived_);
        const QSignalBlocker rankBlock(adminStatsRank_);
        const QSignalBlocker viewBlock(adminStatsView_);
        const QSignalBlocker daysBlock(adminStatsInactivityDays_);
        adminStatsSearch_->clear();
        adminStatsArchived_->setChecked(true);
        adminStatsRank_->setCurrentIndex(0);
        adminStatsView_->setCurrentIndex(0);
        adminStatsInactivityDays_->setValue(30);
        saveDisplayContext(); render();
    });
    auto* adminStatsTimer = new QTimer(this);
    adminStatsTimer->setObjectName("adminProfileStatsTimer");
    adminStatsTimer->setInterval(1000);
    connect(adminStatsTimer, &QTimer::timeout, this, [this] {
        if (navigation_->currentRow() != AdminProfileStats || !adminStatsAutoRefresh_->isChecked()) return;
        const auto now = QDateTime::currentSecsSinceEpoch();
        if (adminStatsLastRefresh_ == 0 || now - adminStatsLastRefresh_ >= adminStatsRefreshSeconds_->value()) {
            refreshAdminProfileStats();
            render();
        }
    });
    adminStatsTimer->start();
    connect(taskCsvAction, &QAction::triggered, this, [this] { exportTasks(false); });
    connect(taskTxtAction, &QAction::triggered, this, [this] { exportTasks(true); });
    connect(exportAudit_, &QPushButton::clicked, this, [this] { exportAudit(); });
    connect(exportLogs_, &QPushButton::clicked, this, [this] { exportLogs(); });
    connect(clearLogs_, &QPushButton::clicked, this, [this] {
        appLogs_.clear();
        appLogPersistenceWarning_ = !saveAppLogs();
        search_->clear(); render();
        statusBar()->showMessage(QString::fromUtf8("Журнал Qt-сессии очищен."), 4000);
    });
    connect(reapplyRules_, &QPushButton::clicked, this, [this] { reapplyRules(); });
    connect(directXp_, &QPushButton::clicked, this, [this] { grantDirectXp(); });
    connect(walletAdjust_, &QPushButton::clicked, this, [this] { adjustWallet(); });
    connect(walletHistory_, &QPushButton::clicked, this, [this] { showWalletHistory(); });
    connect(profileHistory_, &QPushButton::clicked, this, [this] { showProfileHistory(); });
    connect(profileTxtAction, &QAction::triggered, this, [this] { exportProfileReport(false); });
    connect(profileCsvAction, &QAction::triggered, this, [this] { exportProfileReport(true); });
    connect(projectFocus_, &QPushButton::clicked, this, [this] {
        if (navigation_->currentRow() != Projects || !table_->currentItem()) return;
        const auto projectId = table_->currentItem()->data(Qt::UserRole).toString();
        if (projectId.isEmpty() || projectId == QStringLiteral("__no_project")) return;
        const int projectIndex = taskProjectFilter_->findData(projectId);
        if (projectIndex < 0) return;
        {
            QSignalBlocker statusBlock(statusFilter_);
            QSignalBlocker priorityBlock(priorityFilter_);
            QSignalBlocker quickBlock(quickTaskFilter_);
            QSignalBlocker pipelineBlock(taskPipelineFilter_);
            statusFilter_->setCurrentIndex(0);
            priorityFilter_->setCurrentIndex(0);
            quickTaskFilter_->setCurrentIndex(0);
            taskPipelineFilter_->setCurrentIndex(0);
        }
        search_->clear();
        taskProjectFilter_->setCurrentIndex(projectIndex);
        navigation_->setCurrentRow(Tasks);
        statusBar()->showMessage(QString::fromUtf8("Показаны задачи выбранного проекта."), 4000);
    });
    for (const auto& shortcut : std::vector<std::pair<int, int>>{{Qt::Key_F1, ProfilePage},
             {Qt::Key_F2, Catalog}, {Qt::Key_F3, Pipeline}, {Qt::Key_F4, Rules}, {Qt::Key_F5, Statistics}, {Qt::Key_F6, Audit}}) {
        auto* action = new QShortcut(QKeySequence(shortcut.first), this);
        connect(action, &QShortcut::activated, this, [this, page = shortcut.second] {
            if (!navigation_->item(page)->isHidden()) navigation_->setCurrentRow(page);
        });
    }
    auto bindShortcut = [this](const char* name, const QKeySequence& key, auto action) {
        auto* shortcut = new QShortcut(key, this);
        shortcut->setObjectName(name);
        connect(shortcut, &QShortcut::activated, this, action);
    };
    bindShortcut("shortcutCreate", QKeySequence::New, [this] { createEntry(); });
    bindShortcut("shortcutFocusSearch", QKeySequence(QStringLiteral("Ctrl+K")), [this] {
        if (search_->isVisible() && search_->isEnabled()) { search_->setFocus(); search_->selectAll(); }
    });
    auto* clearSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), search_);
    clearSearchShortcut->setObjectName("shortcutClearSearch");
    clearSearchShortcut->setContext(Qt::WidgetShortcut);
    connect(clearSearchShortcut, &QShortcut::activated, this, [this] {
        if (!search_->text().isEmpty()) search_->clear();
    });
    bindShortcut("shortcutEdit", QKeySequence(QStringLiteral("Ctrl+E")), [this] { createEntry(true); });
    bindShortcut("shortcutDelete", QKeySequence::Delete, [this] { deleteEntry(); });
    bindShortcut("shortcutRefresh", QKeySequence::Refresh, [this] { reload(); });
    bindShortcut("shortcutDetails", QKeySequence(QStringLiteral("Ctrl+I")), [this] {
        if (detailsToggle_->isVisible() && detailsToggle_->isEnabled()) detailsToggle_->toggle();
    });
    bindShortcut("shortcutHelp", QKeySequence(QStringLiteral("Ctrl+/")), [this] { showShortcutHelp(); });
    bindShortcut("shortcutToggleWindowDecoration", QKeySequence(Qt::Key_F10), [this] {
        auto next = displaySettings_;
        next.decorated = !next.decorated;
        if (!SaveQtDisplaySettings(workspace_.directory, next)) { message(u8"Не удалось сохранить режим рамки окна."); return; }
        const bool wasVisible = isVisible();
        const bool wasFullscreen = isFullScreen();
        const bool wasMaximized = isMaximized();
        displaySettings_ = next;
        setWindowFlag(Qt::FramelessWindowHint, !next.decorated);
        dragHandle_->setVisible(!next.decorated);
        if (wasVisible) {
            if (wasFullscreen) showFullScreen();
            else if (wasMaximized) showMaximized();
            else showNormal();
        }
        statusBar()->showMessage(next.decorated ? QString::fromUtf8("Рамка окна включена.")
                                                : QString::fromUtf8("Безрамочный режим включён."), 4000);
    });
    bindShortcut("shortcutResetUiSettings", QKeySequence(QStringLiteral("Ctrl+F10")), [this] {
        if (!ResetQtUiSettings(workspace_.directory)) {
            message(u8"Не удалось сбросить настройки интерфейса; исходный файл сохранён.");
            return;
        }
        QMessageBox::information(this, QString::fromUtf8("Настройки интерфейса"),
            QString::fromUtf8("Настройки интерфейса сброшены. ForgeMirror Qt перезапустится в этом же рабочем месте."));
        QCoreApplication::instance()->setProperty("forgeRestartRequested", true);
        QCoreApplication::quit();
    });
    bindShortcut("shortcutFullscreen", QKeySequence(Qt::Key_F11), [this] {
        auto next = displaySettings_;
        next.fullscreen = !isFullScreen();
        if (!SaveQtDisplaySettings(workspace_.directory, next)) { message(u8"Не удалось сохранить режим окна."); return; }
        displaySettings_ = next;
        if (next.fullscreen) showFullScreen(); else showNormal();
    });
    reload();
}

void QtWindow::showShortcutHelp() {
    QDialog dialog(this);
    dialog.setObjectName("shortcutHelp");
    dialog.setWindowTitle(QString::fromUtf8("Горячие клавиши"));
    dialog.resize(540, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(QString::fromUtf8(
        "Команды работают в текущем разделе. Защищённые операции требуют входа администратора; локальные ярлыки доступны всем пользователям."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* table = new QTableWidget(17, 2, &dialog);
    table->setObjectName("shortcutHelpTable");
    table->setHorizontalHeaderLabels({QString::fromUtf8("Клавиша"), QString::fromUtf8("Действие")});
    const std::vector<std::pair<QString, QString>> rows = {
        {"F1", QString::fromUtf8("Профиль")}, {"F2", QString::fromUtf8("Навыки")},
        {"F3", QString::fromUtf8("Пайплайн")}, {"F4", QString::fromUtf8("Правила")},
        {"F5", QString::fromUtf8("Статистика")}, {"F6", QString::fromUtf8("Аудит")},
        {"Ctrl+K", QString::fromUtf8("Перейти к поиску текущего раздела")}, {"Esc", QString::fromUtf8("Очистить активный поиск")},
        {"Ctrl+N", QString::fromUtf8("Создать запись")}, {"Ctrl+E", QString::fromUtf8("Редактировать выбранную запись")},
        {"Delete", QString::fromUtf8("Удалить выбранный проект или этап")}, {"Ctrl+R", QString::fromUtf8("Перечитать локальные данные")},
        {"Ctrl+I", QString::fromUtf8("Показать или скрыть подробности")}, {"Ctrl+/", QString::fromUtf8("Открыть эту памятку")},
        {"F10", QString::fromUtf8("Показать или скрыть рамку окна")},
        {"Ctrl+F10", QString::fromUtf8("Сбросить настройки интерфейса")},
        {"F11", QString::fromUtf8("Переключить полноэкранный режим")}
    };
    for (int row = 0; row < int(rows.size()); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(rows[row].first));
        table->setItem(row, 1, new QTableWidgetItem(rows[row].second));
    }
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void QtWindow::message(const std::string& error) {
    appendLog(AppLogLevel::Warning, "Qt", error);
    QMessageBox::warning(this, QString::fromUtf8("ForgeMirror"), q(error));
    if (navigation_->currentRow() == Logs) render();
}

void QtWindow::appendLog(AppLogLevel level, const std::string& source, const std::string& text) {
    if (text.empty()) return;
    const auto safeText = u(SanitizeQtLogMessage(q(text)));
    if (safeText.empty()) return;
    appLogs_.push_back({QDateTime::currentSecsSinceEpoch(), level, source, safeText});
    constexpr size_t maxEntries = 200;
    if (appLogs_.size() > maxEntries) appLogs_.erase(appLogs_.begin());
    appLogPersistenceWarning_ = !saveAppLogs();
}

void QtWindow::recordRuntimeMessage(AppLogLevel level, const QString& text) {
    appendLog(level, "QtRuntime", u(text));
    if (navigation_->currentRow() == Logs) render();
}

void QtWindow::loadAppLogs() {
    const auto metaPath = q((workspace_.directory / "meta").u8string());
    const auto path = q((workspace_.directory / "meta/qt-application-log.json").u8string());
    if (QFileInfo(metaPath).isSymLink()) { appLogPersistenceWarning_ = true; return; }
    const QFileInfo info(path);
    if (!info.exists()) return;
    if (info.isSymLink() || info.size() > 4 * 1024 * 1024) {
        appLogPersistenceWarning_ = true;
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { appLogPersistenceWarning_ = true; return; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        appLogPersistenceWarning_ = true;
        return;
    }
    for (const auto& value : document.array()) {
        if (!value.isObject()) continue;
        const auto entry = value.toObject();
        const auto timestamp = entry.value("timestamp").toVariant().toLongLong();
        const auto level = entry.value("level").toInt(-1);
        if (timestamp <= 0 || level < 0 || level > 2) continue;
        appLogs_.push_back({timestamp, static_cast<AppLogLevel>(level),
            u(entry.value("source").toString()), u(SanitizeQtLogMessage(entry.value("message").toString()))});
    }
    constexpr size_t maxEntries = 200;
    if (appLogs_.size() > maxEntries)
        appLogs_.erase(appLogs_.begin(), appLogs_.end() - static_cast<std::ptrdiff_t>(maxEntries));
}

bool QtWindow::saveAppLogs() const {
    const auto metaPath = q((workspace_.directory / "meta").u8string());
    const auto path = q((workspace_.directory / "meta/qt-application-log.json").u8string());
    if (QFileInfo(metaPath).isSymLink() || QFileInfo(path).isSymLink()) return false;
    QJsonArray entries;
    for (const auto& entry : appLogs_) {
        QJsonObject value;
        value.insert("timestamp", qlonglong(entry.timestamp));
        value.insert("level", int(entry.level));
        value.insert("source", q(entry.source));
        value.insert("message", q(entry.message));
        entries.append(value);
    }
    const auto bytes = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return false;
    return true;
}

bool QtWindow::requireAdmin() {
    if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) {
        message(u8"Осталась незавершённая XP-транзакция. Перезапустите Qt для восстановления.");
        return false;
    }
    if (!admin_) message(u8"Для изменения данных войдите как администратор через меню ⋯.");
    return admin_;
}

void QtWindow::authenticateProfile() {
    const auto id = u(profiles_->currentData().toString());
    if (profileSession_.isUnlocked(*workspace_.storage, id)) {
        if (!profileSession_.lock(true)) message(u8"Сеанс закрыт, но не удалось сохранить выход или удалить доверенный вход.");
        render(); return;
    }
    if (id.empty()) return;
    QDialog dialog(this);
    dialog.setObjectName("profileLogin");
    dialog.setWindowTitle(QString::fromUtf8("Доступ к профилю"));
    dialog.setMinimumWidth(400);
    auto* layout = new QFormLayout(&dialog);
    auto* name = new QLabel(profiles_->currentText());
    name->setTextFormat(Qt::PlainText);
    name->setWordWrap(true);
    layout->addRow(name);
    auto* password = new QLineEdit;
    password->setObjectName("profileLoginPassword");
    password->setEchoMode(QLineEdit::Password);
    layout->addRow(QString::fromUtf8("Пароль"), password);
    auto* trust = new QComboBox; trust->setObjectName("profileTrust");
    trust->addItem(QString::fromUtf8("Только эта сессия"), 0); trust->addItem(QString::fromUtf8("30 дней"), 30); trust->addItem(QString::fromUtf8("90 дней"), 90);
    layout->addRow(QString::fromUtf8("Доверять устройству"), trust);
    auto* hint = new QLabel(QString::fromUtf8("Доверие хранится только в локальной копии Qt и не даёт прав администратора."));
    hint->setWordWrap(true);
    layout->addRow(hint);
    auto* notice = new QLabel;
    notice->setObjectName("profileLoginNotice");
    notice->setWordWrap(true);
    connect(password, &QLineEdit::textChanged, notice, &QLabel::clear);
    layout->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("Войти"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const bool accepted = profileSession_.unlock(*workspace_.storage, id, u(password->text()), trust->currentData().toInt());
        password->clear();
        if (accepted) dialog.accept();
        else { notice->setText(QString::fromUtf8("Вход не выполнен: проверьте пароль, состояние профиля и доступность локальной записи.")); password->setFocus(); }
    });
    dialog.exec();
    render();
}

void QtWindow::authenticate() {
    const char* overrideValue = std::getenv("FORGEMIRROR_ADMIN_PASSWORD");
    const bool envOverride = overrideValue && *overrideValue;
    if (admin_) {
        if (!envOverride && !SetAdminStayLoggedIn(workspace_.directory, false)) {
            message(u8"Не удалось сохранить выход администратора; повторите попытку.");
            return;
        }
        admin_ = false;
        appendLog(AppLogLevel::Info, "CoreAuthentication", "Administrator session ended");
    } else {
        QDialog dialog(this);
        dialog.setObjectName("adminLoginDialog");
        dialog.setWindowTitle(QString::fromUtf8("Вход администратора"));
        auto* layout = new QFormLayout(&dialog);
        auto* password = new QLineEdit;
        password->setObjectName("adminLoginPassword");
        password->setEchoMode(QLineEdit::Password);
        password->setMaxLength(512);
        layout->addRow(QString::fromUtf8("Пароль"), password);
        auto* remember = new QCheckBox(QString::fromUtf8("Не выходить после перезапуска на этом рабочем месте"));
        remember->setObjectName("adminRememberSession");
        remember->setChecked(!envOverride && LoadAdminStayLoggedIn(workspace_.directory));
        remember->setEnabled(!envOverride);
        remember->setToolTip(envOverride ? QString::fromUtf8("При заданном FORGEMIRROR_ADMIN_PASSWORD пароль из среды не записывается в настройки.")
                                         : QString::fromUtf8("Сохраняет локальный режим администратора до выхода вручную."));
        layout->addRow(remember);
        auto* workspaceHint = new QLabel(QString::fromUtf8("Рабочее место Qt: %1")
            .arg(QDir::toNativeSeparators(QString::fromStdWString(workspace_.directory.wstring()))));
        workspaceHint->setObjectName("adminLoginWorkspace");
        workspaceHint->setWordWrap(true);
        workspaceHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
        workspaceHint->setStyleSheet(QString::fromUtf8("color: palette(mid); font-size: 9pt;"));
        layout->addRow(workspaceHint);
        auto* notice = new QLabel;
        notice->setObjectName("adminLoginNotice"); notice->setWordWrap(true); layout->addRow(notice);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("Войти"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            const auto entered = password->text();
            if (u(entered) != LoadAdminPassword(workspace_.directory)) {
                appendLog(AppLogLevel::Warning, "CoreAuthentication", "Administrator login rejected");
                password->clear();
                notice->setText(QString::fromUtf8("Неверный пароль администратора."));
                password->setFocus();
                return;
            }
            if (!envOverride && !SetAdminStayLoggedIn(workspace_.directory, remember->isChecked())) {
                notice->setText(QString::fromUtf8("Не удалось сохранить настройку режима администратора."));
                return;
            }
            password->clear();
            admin_ = true;
            appendLog(AppLogLevel::Info, "CoreAuthentication", remember->isChecked()
                ? "Administrator login succeeded; persistent session enabled"
                : "Administrator login succeeded; session-only mode");
            dialog.accept();
        });
        if (dialog.exec() != QDialog::Accepted) return;
    }
    render();
}

void QtWindow::changeAdminPassword() {
    if (!requireAdmin()) return;
    const char* overrideValue = std::getenv("FORGEMIRROR_ADMIN_PASSWORD");
    if (overrideValue && *overrideValue) {
        message(u8"Пароль задан через FORGEMIRROR_ADMIN_PASSWORD; изменение через приложение недоступно.");
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName("adminPasswordDialog");
    dialog.setWindowTitle(QString::fromUtf8("Смена пароля администратора"));
    auto* layout = new QFormLayout(&dialog);
    auto makePassword = [](const char* name) {
        auto* edit = new QLineEdit;
        edit->setObjectName(QString::fromLatin1(name));
        edit->setEchoMode(QLineEdit::Password);
        edit->setMaxLength(512);
        return edit;
    };
    auto* current = makePassword("adminCurrentPassword");
    auto* next = makePassword("adminNewPassword");
    auto* confirm = makePassword("adminConfirmPassword");
    layout->addRow(QString::fromUtf8("Текущий пароль"), current);
    layout->addRow(QString::fromUtf8("Новый пароль"), next);
    layout->addRow(QString::fromUtf8("Повторите пароль"), confirm);
    auto* notice = new QLabel;
    notice->setObjectName("adminPasswordNotice"); notice->setWordWrap(true); layout->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto oldPassword = u(current->text());
        const auto newPassword = next->text();
        const auto repeatedPassword = confirm->text();
        if (oldPassword != LoadAdminPassword(workspace_.directory))
            notice->setText(QString::fromUtf8("Неверный текущий пароль."));
        else if (newPassword.trimmed().isEmpty())
            notice->setText(QString::fromUtf8("Новый пароль не может быть пустым."));
        else if (newPassword != repeatedPassword)
            notice->setText(QString::fromUtf8("Пароли не совпадают."));
        else if (!SetAdminPassword(workspace_.directory, u(newPassword.trimmed())))
            notice->setText(QString::fromUtf8("Не удалось сохранить пароль."));
        else {
            appendLog(AppLogLevel::Info, "Admin", "Administrator password changed");
            dialog.accept();
        }
        current->clear(); next->clear(); confirm->clear();
        if (dialog.result() != QDialog::Accepted) current->setFocus();
    });
    dialog.exec();
}

void QtWindow::updateBanner() {
    if (workspace_.data.bannerTexts.empty()) { banner_->clear(); banner_->hide(); bannerIndex_ = 0; return; }
    bannerIndex_ %= int(workspace_.data.bannerTexts.size());
    const auto text = q(workspace_.data.bannerTexts[size_t(bannerIndex_)]);
    banner_->setText(text); banner_->setToolTip(text); banner_->show();
}

bool QtWindow::reload() {
    const auto previous = profiles_->currentData().toString();
    try {
        workspace_.reload();
    } catch (const std::exception& error) {
        message(error.what());
        return false;
    }
    adminStatsLastRefresh_ = 0;
    adminStatsRows_.clear();
    if (workspace_.transactionRecoveryNotice) {
        appendLog(AppLogLevel::Warning, "CoreTransactionRecovery", "An interrupted local transaction was recovered from its journal");
        workspace_.transactionRecoveryNotice = false;
    }
    if (workspace_.cloudPullRecoveryNotice) {
        appendLog(AppLogLevel::Warning, "CoreTransactionRecovery", "An interrupted manual cloud pull was rolled back from its recovery journal");
        workspace_.cloudPullRecoveryNotice = false;
    }
    if (workspace_.cloudPushRecoveryNotice) {
        appendLog(AppLogLevel::Warning, "CoreTransactionRecovery", "An interrupted manual cloud push was rolled back from its recovery journal");
        workspace_.cloudPushRecoveryNotice = false;
    }
    {
        QSignalBlocker blocker(profiles_);
        profiles_->clear();
        for (const auto& profile : workspace_.profiles) {
            if (!profile.archived) profiles_->addItem(q(profile.name), q(profile.id));
        }
        const auto preferred = previous.isEmpty() ? displaySettings_.lastProfileId : previous;
        const int index = profiles_->findData(preferred);
        if (index >= 0) profiles_->setCurrentIndex(index);
    }
    refreshTaskFilterChoices();
    refreshCatalogProfessionChoices();
    const int page = std::clamp(displaySettings_.lastPage, 0, navigation_->count() - 1);
    if (page == Logs) {
        const QSignalBlocker blocker(search_);
        search_->setText(displaySettings_.logFilter);
    }
    if (!navigation_->item(page)->isHidden()) navigation_->setCurrentRow(page);
    appendLog(AppLogLevel::Info, "Qt", "Локальное рабочее пространство загружено или обновлено.");
    updateBanner(); render();
    if (!workspace_.data.recoveryWarnings.empty()) {
        QStringList warnings;
        for (const auto& warning : workspace_.data.recoveryWarnings) warnings << q(warning);
        message(u(warnings.join('\n')));
    }
    return true;
}

QString QtWindow::selectedId() const {
    const auto* item = table_->item(table_->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void QtWindow::saveDisplayContext() {
    const auto profileId = profiles_->currentData().toString();
    if (!profileId.isEmpty()) displaySettings_.lastProfileId = profileId;
    const int page = navigation_->currentRow();
    if (page >= 0) displaySettings_.lastPage = page;
    displaySettings_.profileSkillSort = profileSkillSort_->currentIndex();
    displaySettings_.profileSkillWeightCategory = profileSkillWeightCategory_->currentIndex();
    displaySettings_.profileSkillWeightMin = profileSkillWeightMin_->value();
    displaySettings_.profileSkillWeightMax = profileSkillWeightMax_->value();
    displaySettings_.taskStatusFilter = statusFilter_->currentIndex();
    displaySettings_.taskPriorityFilter = priorityFilter_->currentIndex();
    displaySettings_.taskQuickFilter = quickTaskFilter_->currentIndex();
    displaySettings_.taskCreatedRange = taskCreatedRange_->currentIndex();
    displaySettings_.taskSortMode = taskSort_->currentIndex();
    displaySettings_.taskAssigneeProfileId = taskAssigneeFilter_->currentData().toString();
    displaySettings_.taskProjectId = taskProjectFilter_->currentData().toString();
    displaySettings_.taskPipelineStepId = taskPipelineFilter_->currentData().toString();
    displaySettings_.catalogProfessionId = catalogProfessionFilter_->currentData().toString();
    displaySettings_.reportView = reportView_->currentIndex();
    displaySettings_.reportDateRange = reportDateRange_->currentIndex();
    displaySettings_.reportComparePrevious = reportCompare_->isChecked();
    displaySettings_.reportDateFrom = reportFrom_->date();
    displaySettings_.reportDateTo = reportTo_->date();
    displaySettings_.projectSortMode = projectSort_->currentIndex();
    displaySettings_.projectsOverdueOnly = projectsOverdue_->isChecked();
    displaySettings_.projectsXpPendingOnly = projectsXpPending_->isChecked();
    displaySettings_.auditSourceFilter = auditSourceFilter_->currentIndex();
    displaySettings_.logShowInfo = logInfo_->isChecked();
    displaySettings_.logShowWarning = logWarnings_->isChecked();
    displaySettings_.logShowError = logErrors_->isChecked();
    displaySettings_.logAutoScroll = logAutoScroll_->isChecked();
    displaySettings_.logCompactView = logCompactView_->isChecked();
    displaySettings_.adminStatsSearch = adminStatsSearch_->text();
    displaySettings_.adminStatsIncludeArchived = adminStatsArchived_->isChecked();
    displaySettings_.adminStatsRankFilter = adminStatsRank_->currentIndex();
    displaySettings_.adminStatsView = adminStatsView_->currentIndex();
    displaySettings_.adminStatsAutoRefresh = adminStatsAutoRefresh_->isChecked();
    displaySettings_.adminStatsRefreshSeconds = adminStatsRefreshSeconds_->value();
    displaySettings_.adminStatsInactivityDays = adminStatsInactivityDays_->value();
    if (!SaveQtDisplaySettings(workspace_.directory, displaySettings_))
        statusBar()->showMessage(QString::fromUtf8("Не удалось сохранить последний раздел и профиль."), 5000);
}

void QtWindow::refreshTaskFilterChoices() {
    const auto selectedProject = taskProjectFilter_->currentData().toString().isEmpty()
        ? displaySettings_.taskProjectId : taskProjectFilter_->currentData().toString();
    const auto selectedPipeline = taskPipelineFilter_->currentData().toString().isEmpty()
        ? displaySettings_.taskPipelineStepId : taskPipelineFilter_->currentData().toString();
    const auto selectedAssignee = taskAssigneeFilter_->currentData().toString().isEmpty()
        ? displaySettings_.taskAssigneeProfileId : taskAssigneeFilter_->currentData().toString();
    {
        QSignalBlocker blocker(taskAssigneeFilter_);
        taskAssigneeFilter_->clear();
        taskAssigneeFilter_->addItem(QString::fromUtf8("Все профили"), QString());
        for (const auto& profile : workspace_.profiles)
            taskAssigneeFilter_->addItem(QString::fromUtf8("[%1] %2").arg(q(profile.id), q(profile.name)), q(profile.id));
        const int index = taskAssigneeFilter_->findData(selectedAssignee);
        taskAssigneeFilter_->setCurrentIndex(index >= 0 ? index : 0);
        displaySettings_.taskAssigneeProfileId = taskAssigneeFilter_->currentData().toString();
    }
    {
        QSignalBlocker blocker(taskProjectFilter_);
        taskProjectFilter_->clear();
        taskProjectFilter_->addItem(QString::fromUtf8("Все проекты"), QString());
        taskProjectFilter_->addItem(QString::fromUtf8("Без проекта"), QStringLiteral("__none__"));
        for (const auto& project : workspace_.data.projects) taskProjectFilter_->addItem(q(project.name), q(project.id));
        const int index = taskProjectFilter_->findData(selectedProject);
        taskProjectFilter_->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        QSignalBlocker blocker(taskPipelineFilter_);
        taskPipelineFilter_->clear();
        taskPipelineFilter_->addItem(QString::fromUtf8("Все этапы"), QString());
        taskPipelineFilter_->addItem(QString::fromUtf8("Без этапа"), QStringLiteral("__none__"));
        for (const auto& step : workspace_.data.pipelineSteps) {
            const auto label = step.stageCode.empty() ? step.title : step.stageCode + " · " + step.title;
            taskPipelineFilter_->addItem(q(label), q(step.id));
        }
        const int index = taskPipelineFilter_->findData(selectedPipeline);
        taskPipelineFilter_->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void QtWindow::refreshCatalogProfessionChoices() {
    const auto current = catalogProfessionFilter_->currentData().toString();
    const auto selected = current.isEmpty() ? displaySettings_.catalogProfessionId : current;
    QSignalBlocker blocker(catalogProfessionFilter_);
    catalogProfessionFilter_->clear();
    catalogProfessionFilter_->addItem(QString::fromUtf8("Все профессии"), QString());
    catalogProfessionFilter_->addItem(QString::fromUtf8("Без профессии"), QStringLiteral("__none__"));
    std::unordered_set<std::string> known;
    for (const auto& profession : workspace_.data.professions) {
        known.insert(profession.id);
        catalogProfessionFilter_->addItem(q(profession.name), q(profession.id));
    }
    std::unordered_set<std::string> orphaned;
    for (const auto& skillId : workspace_.catalog.skills()) {
        for (const auto& professionId : workspace_.catalog.professions(skillId)) {
            if (known.find(professionId) == known.end() && orphaned.insert(professionId).second)
                catalogProfessionFilter_->addItem(QString::fromUtf8("Неизвестная: %1").arg(q(professionId)), q(professionId));
        }
    }
    const int index = catalogProfessionFilter_->findData(selected);
    catalogProfessionFilter_->setCurrentIndex(index >= 0 ? index : 0);
    displaySettings_.catalogProfessionId = catalogProfessionFilter_->currentData().toString();
}

void QtWindow::loadSelectedModel() {
    modelSettings_.modelPath = modelPath_->text().trimmed();
    modelViewer_->setSettings(modelSettings_);
    const auto result = modelViewer_->loadModel(std::filesystem::u8path(u(modelSettings_.modelPath)));
    modelStatus_->setText(result.ok
        ? QString::fromUtf8("%1 треугольников · %2").arg(result.triangles).arg(QFileInfo(modelSettings_.modelPath).fileName())
        : result.error);
}

void QtWindow::updateModelSettingsFromControls() {
    modelSettings_.yaw = modelYaw_->value() / 100.0f;
    modelSettings_.pitch = modelPitch_->value() / 100.0f;
    modelSettings_.zoom = modelZoom_->value() / 100.0f;
    modelSettings_.autoSpeed = modelSpeed_->value() / 100.0f;
    modelSettings_.autoRotate = modelAutoRotate_->isChecked();
    modelSettings_.modelPath = modelPath_->text().trimmed();
    modelViewer_->setSettings(modelSettings_);
}

void QtWindow::saveModelSettings() {
    updateModelSettingsFromControls();
    if (!SaveQtModelSettings(workspace_.directory, modelSettings_)) {
        message(u8"Не удалось сохранить настройки 3D в meta/ui.ini.");
        return;
    }
    loadSelectedModel();
    message(u8"Настройки 3D сохранены.");
}

void QtWindow::render() {
    const auto profileId = u(profiles_->currentData().toString());
    const bool unlocked = profileSession_.isUnlocked(*workspace_.storage, profileId);
    profileAccessAction_->setText(QString::fromUtf8(unlocked ? "Выйти из профиля" : "Войти в выбранный профиль"));
    profileAccessAction_->setEnabled(!profileId.empty());
    adminLoginAction_->setText(QString::fromUtf8(admin_ ? "Выйти из режима администратора" : "Войти как администратор"));
    adminLoginAction_->setToolTip(QString::fromUtf8(admin_
        ? "Завершить режим администратора и отключить его восстановление после перезапуска."
        : "Открыть вход администратора; после входа можно включить восстановление на этом рабочем месте."));
    adminPasswordAction_->setVisible(admin_);
    const char* adminPasswordOverride = std::getenv("FORGEMIRROR_ADMIN_PASSWORD");
    adminPasswordAction_->setEnabled(!(adminPasswordOverride && *adminPasswordOverride));
    ownPasswordAction_->setEnabled(unlocked);
    storageHealthReportAction_->setVisible(admin_);
    storageCleanupAction_->setVisible(admin_);
    navigation_->item(ModelViewerPage)->setHidden(!workspace_.modules.view3d);
    navigation_->item(ModelSettingsPage)->setHidden(!workspace_.modules.view3d || !admin_);
    navigation_->item(Tasks)->setHidden(!workspace_.modules.tasks);
    navigation_->item(Pipeline)->setHidden(!workspace_.modules.pipeline);
    navigation_->item(Pomodoro)->setHidden(!workspace_.modules.pomodoro);
    navigation_->item(Shortcuts)->setHidden(!workspace_.modules.shortcuts);
    navigation_->item(Professions)->setHidden(!workspace_.modules.professions || !admin_);
    navigation_->item(Cloud)->setHidden(!workspace_.modules.cloud);
    for (int page : {Projects, Statistics, Rules, Vault, Banner, AdminProfileStats}) navigation_->item(page)->setHidden(!admin_);
    navigation_->item(Audit)->setHidden(!workspace_.modules.tasks);
    int page = navigation_->currentRow();
    if (page < 0) return;
    if (navigation_->item(page)->isHidden()) {
        navigation_->setCurrentRow(ProfilePage);
        return;
    }
    backgroundSurface_->setBackground(workspace_.directory, displaySettings_.windowBackgrounds[size_t(page)],
        displaySettings_.backgroundAlpha, displaySettings_.backgroundTiled, displaySettings_.backgroundTileScale);
    const auto previous = selectedId();
    table_->verticalHeader()->setDefaultSectionSize(displaySettings_.compactRows ? 24 : 28);
    const auto& data = workspace_.data;
    table_->setSelectionMode(page == Tasks ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    QSignalBlocker blocker(table_);
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setRowCount(0);
    auto headers = [this](QStringList labels) {
        table_->setColumnCount(labels.size());
        table_->setHorizontalHeaderLabels(labels);
        for (int column = 0; column < labels.size(); ++column) table_->setColumnHidden(column, false);
    };
    auto row = [this](const std::string& id, const QStringList& values) {
        if (!values.join(' ').contains(search_->text(), Qt::CaseInsensitive)) return;
        int index = table_->rowCount();
        table_->insertRow(index);
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]);
            item->setToolTip(values[col]);
            item->setData(Qt::UserRole, q(id));
            table_->setItem(index, col, item);
        }
    };
    title_->setText(navigation_->item(page)->text());
    table_->setAccessibleName(QString::fromUtf8("Данные раздела «%1»").arg(title_->text()));
    mode_->setText(admin_ ? QString::fromUtf8("Администратор · Qt") : QString::fromUtf8(unlocked ?
        (profileSession_.isTrusted() ? "Доверенный доступ · Qt" : "Личный доступ · Qt") : "Просмотр · Qt"));
    const bool timerPage = page == Pomodoro;
    const bool modelPage = page == ModelViewerPage || page == ModelSettingsPage;
    summary_->setVisible(!timerPage && !modelPage);
    taskPipelineSummary_->setVisible(page == Tasks);
    statisticsChart_->setVisible(page == Statistics);
    search_->setVisible(!timerPage && !modelPage && page != AdminProfileStats);
    table_->setVisible(!timerPage && !modelPage);
    bottomActions_->setVisible(!timerPage && !modelPage);
    const bool hasDetails = page == Tasks || page == Statistics;
    detailsToggle_->setVisible(hasDetails);
    details_->setVisible(hasDetails && detailsToggle_->isChecked());
    pomodoro_->setVisible(timerPage);
    modelPage_->setVisible(page == ModelViewerPage);
    modelSettingsPage_->setVisible(page == ModelSettingsPage);
    static_cast<QtPomodoro*>(pomodoro_)->setAdministrator(admin_);
    statusFilter_->setVisible(page == Tasks);
    priorityFilter_->setVisible(page == Tasks);
    quickTaskFilter_->setVisible(page == Tasks);
    taskCreatedRange_->setVisible(page == Tasks);
    taskSort_->setVisible(page == Tasks);
    taskAssigneeFilter_->setVisible(page == Tasks);
    taskProjectFilter_->setVisible(page == Tasks);
    taskPipelineFilter_->setVisible(page == Tasks);
    taskFilterReset_->setVisible(page == Tasks);
    catalogProfessionFilter_->setVisible(page == Catalog);
    reportView_->setVisible(page == Statistics);
    reportDateRange_->setVisible(page == Statistics);
    reportCompare_->setVisible(page == Statistics);
    reportCompare_->setEnabled(reportDateRange_->currentIndex() != 0);
    reportCustomRange_->setVisible(page == Statistics && reportDateRange_->currentIndex() == 4);
    adminStatsFilters_->setVisible(page == AdminProfileStats && admin_);
    adminStatsInactivityLabel_->setVisible(page == AdminProfileStats && admin_ && adminStatsView_->currentIndex() == 4);
    adminStatsInactivityDays_->setVisible(adminStatsView_->currentIndex() == 4);
    adminStatsRefreshSeconds_->setEnabled(adminStatsAutoRefresh_->isChecked());
    projectsOverdue_->setVisible(page == Projects);
    projectsXpPending_->setVisible(page == Projects);
    projectSort_->setVisible(page == Projects);
    auditSourceFilter_->setVisible(page == Audit && admin_);
    auditFilters_->setVisible(page == Audit);
    logInfo_->setVisible(page == Logs);
    logWarnings_->setVisible(page == Logs);
    logErrors_->setVisible(page == Logs);
    logSourceFilter_->setVisible(page == Logs);
    logAutoScroll_->parentWidget()->setVisible(page == Logs);
    logActivityChart_->setVisible(page == Logs);
    logPresetAll_->setVisible(page == Logs);
    logPresetWarningsErrors_->setVisible(page == Logs);
    logPresetErrors_->setVisible(page == Logs);
    primary_->setVisible(page == Shortcuts || page == Cloud || (page == ModelSettingsPage && admin_) || ((page == ProfilePage || page == Tasks || page == Projects || page == Catalog || page == Pipeline || page == Professions || page == Rules || page == Vault || page == Banner) && admin_));
    primary_->setText(page == ProfilePage ? QString::fromUtf8("Управление профилями") :
        (page == Projects ? QString::fromUtf8("Создать проект") :
         page == Catalog ? QString::fromUtf8("Создать навык") : page == Pipeline ? QString::fromUtf8("Создать этап") : page == Professions ? QString::fromUtf8("Создать профессию") : page == Rules ? QString::fromUtf8("Изменить правила") : page == Vault ? QString::fromUtf8("Настройки хранилища") : page == Shortcuts ? QString::fromUtf8("Добавить ярлык") : page == Banner ? QString::fromUtf8("Добавить фразу") : page == Cloud ? QString::fromUtf8("Настроить облако") : page == ModelSettingsPage ? QString::fromUtf8("Сохранить настройки") : QString::fromUtf8("Создать задачу")));
    editEntry_->setVisible(admin_ && (page == Projects || page == Catalog || page == Tasks || page == Pipeline || page == Professions || page == Banner));
    deleteEntry_->setVisible(page == Shortcuts || (admin_ && (page == Tasks || page == Projects || page == Catalog || page == Pipeline || page == Professions || page == Banner)));
    moveUp_->setVisible(page == Shortcuts || (admin_ && page == Pipeline));
    moveDown_->setVisible(page == Shortcuts || (admin_ && page == Pipeline));
    pipelineMap_->setVisible(page == Pipeline && !workspace_.data.pipelineSteps.empty());
    openShortcut_->setVisible(page == Shortcuts);
    cloudPull_->setVisible(page == Cloud);
    cloudPushPreview_->setVisible(page == Cloud);
    cloudResolve_->setVisible(page == Cloud);
    storageResolve_->setVisible(page == Cloud);
    cloudReleaseButton_->setVisible(page == Cloud);
    profileMetrics_->setVisible(page == ProfilePage);
    profileViewModes_->setVisible(page == ProfilePage);
    if (!workspace_.modules.tasks && displaySettings_.profileViewMode == 3) displaySettings_.profileViewMode = 1;
    const int profileMode = std::clamp(displaySettings_.profileViewMode, 0, 3);
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker blocker(profileViewModeButtons_[i]);
        profileViewModeButtons_[i]->setChecked(i == profileMode);
    }
    if (auto* taskMode = findChild<QPushButton*>("profileViewMode3")) {
        const QSignalBlocker blocker(taskMode);
        taskMode->setChecked(profileMode == 3);
        taskMode->setVisible(workspace_.modules.tasks);
    }
    table_->setVisible(!timerPage && !modelPage && (page != ProfilePage || profileMode != 2));
    profileSkillFilters_->setVisible(page == ProfilePage && profileMode == 1);
    profileAnalytics_->setVisible(page == ProfilePage && profileMode == 1);
    profileTaskActions_->setVisible(page == ProfilePage && profileMode == 3 && workspace_.modules.tasks);
    achievements_->setVisible(page == ProfilePage && profileMode != 2 && profileMode != 3 && workspace_.modules.achievements);
    achievements_->setEnabled(!profiles_->currentData().toString().isEmpty());
    removeSpirit_->setVisible(page == ProfilePage && unlocked);
    exportReport_->setVisible(admin_ && (page == Statistics || page == AdminProfileStats));
    exportReport_->setText(page == AdminProfileStats ? QString::fromUtf8("Экспорт статистики") : QString::fromUtf8("Экспорт CSV"));
    exportTasks_->setVisible(page == Tasks);
    exportAudit_->setVisible(page == Audit);
    exportLogs_->setVisible(page == Logs);
    clearLogs_->setVisible(page == Logs);
    reapplyRules_->setVisible(admin_ && page == Rules);
    directXp_->setVisible(admin_ && page == ProfilePage);
    directXp_->setEnabled(!profiles_->currentData().toString().isEmpty());
    walletAdjust_->setVisible(admin_ && page == ProfilePage);
    walletAdjust_->setEnabled(!profiles_->currentData().toString().isEmpty());
    walletHistory_->setVisible(page == ProfilePage && (admin_ || unlocked));
    walletHistory_->setEnabled(!profiles_->currentData().toString().isEmpty());
    profileHistory_->setVisible(page == ProfilePage && (admin_ || unlocked));
    profileHistory_->setEnabled(!profiles_->currentData().toString().isEmpty());
    profileExport_->setVisible(page == ProfilePage && admin_);
    profileExport_->setEnabled(!profiles_->currentData().toString().isEmpty());
    projectFocus_->setVisible(page == Projects);
    const bool projectSelected = page == Projects && table_->currentItem() &&
        !table_->currentItem()->data(Qt::UserRole).toString().isEmpty() &&
        table_->currentItem()->data(Qt::UserRole).toString() != QStringLiteral("__no_project");
    projectFocus_->setEnabled(projectSelected);
    removeSpirit_->setEnabled(false);
    for (auto* value : profileValues_) value->setText(QString::fromUtf8("—"));
    changeStatus_->setVisible(page == Tasks && admin_);
    bulkEdit_->setVisible(page == Tasks && admin_);
    bulkEdit_->setEnabled(false);
    bulkDelete_->setVisible(page == Tasks && admin_);
    bulkDelete_->setEnabled(false);
    taskSelectionTools_->setVisible(page == Tasks && admin_);
    advanceStage_->setVisible(page == Tasks && admin_ && workspace_.modules.pipeline);
    summary_->clear();

    if (modelPage) {
        table_->setRowCount(0);
        table_->setColumnCount(0);
        if (page == ModelViewerPage) {
            modelStatus_->setText(modelViewer_->triangleCount()
                ? QString::fromUtf8("%1 треугольников · %2").arg(modelViewer_->triangleCount()).arg(QFileInfo(modelPath_->text()).fileName())
                : QString::fromUtf8("Модель не загружена"));
        } else {
            restoringModelSettings_ = true;
            modelPath_->setText(modelSettings_.modelPath);
            const auto choice = modelChoice_->findData(QFileInfo(modelSettings_.modelPath).fileName());
            modelChoice_->setCurrentIndex(choice >= 0 ? choice : 0);
            modelYaw_->setValue(qRound(modelSettings_.yaw * 100));
            modelPitch_->setValue(qRound(modelSettings_.pitch * 100));
            modelZoom_->setValue(qRound(modelSettings_.zoom * 100));
            modelSpeed_->setValue(qRound(modelSettings_.autoSpeed * 100));
            modelAutoRotate_->setChecked(modelSettings_.autoRotate);
            modelColor_->setStyleSheet(QStringLiteral("background-color: %1;").arg(modelSettings_.lineColor.name()));
            restoringModelSettings_ = false;
        }
        statusBar()->showMessage(QString::fromUtf8("Локальная копия · без облака · ") + q(workspace_.directory.u8string()));
        return;
    }

    if (page == Pomodoro) {
        summary_->clear();
    } else if (page == ProfilePage) {
        if (profileMode == 3) headers({QString::fromUtf8("Задача"), QString::fromUtf8("Статус"), QString::fromUtf8("Срок"),
            QString::fromUtf8("Проект"), QString::fromUtf8("Этап процесса")});
        else headers({QString::fromUtf8("Навык"), QString::fromUtf8("Уровень"), "XP", QString::fromUtf8("Всего XP"), QString::fromUtf8("Вес")});
        const auto id = u(profiles_->currentData().toString());
        std::optional<Profile> profile;
        // Viewing a profile must not invoke LoadActiveProfile: that legacy helper saves on read.
        if (!id.empty() && workspace_.storage->set_active_profile(id)) profile = workspace_.storage->load_profile();
        if (profile) {
            QString profession = q(profile->profession_id());
            for (const auto& item : data.professions) if (item.id == profile->profession_id()) profession = q(item.name);
            summary_->setText(q(DescribeOverallRank(*profile)) + (profession.isEmpty() ? "" : " · " + profession) +
                " · " + q(ProfileSpiritLabel(profile->spirit())) + (profile->is_blocked() ? QString::fromUtf8(" · Заблокирован") : ""));
            profileValues_[0]->setText(QString::number(profile->overall_level()));
            profileValues_[1]->setText(QString::number(profile->total_xp()));
            profileValues_[2]->setText(QString::number(profile->tasks_completed()));
            profileValues_[3]->setText(QString::number(profile->xp_to_next_level()));
            profileValues_[4]->setText(QString::number(profile->wallet_balance(), 'f', 0));
            removeSpirit_->setEnabled(unlocked && profile->spirit() == ProfileSpirit::Evil && profile->wallet_balance() + 0.000001 >= 200.0);
            if (profileMode == 3) {
                const auto now = QDateTime::currentSecsSinceEpoch();
                int active = 0, overdue = 0, xpPending = 0;
                std::vector<const TaskEntry*> tasks;
                for (const auto& task : data.tasks) {
                    const bool assigned = std::find(task.assignees.begin(), task.assignees.end(), id) != task.assignees.end();
                    const bool participated = std::any_of(task.participants.begin(), task.participants.end(),
                        [&](const auto& item) { return item.profileId == id; });
                    if (!assigned && !participated) continue;
                    const int status = AppNormalizeTaskStatus(task.status);
                    const bool hasXp = std::any_of(task.participants.begin(), task.participants.end(),
                        [](const auto& item) { return item.globalXp > 0 || item.skillXp > 0; });
                    if (status == 2 && assigned && !hasXp) ++xpPending;
                    if (status == 2) continue;
                    ++active;
                    if (task.deadlineAt > 0 && task.deadlineAt < now) ++overdue;
                    tasks.push_back(&task);
                }
                std::stable_sort(tasks.begin(), tasks.end(), [now](const TaskEntry* a, const TaskEntry* b) {
                    const bool ao = a->deadlineAt > 0 && a->deadlineAt < now;
                    const bool bo = b->deadlineAt > 0 && b->deadlineAt < now;
                    if (ao != bo) return ao;
                    if ((a->deadlineAt > 0) != (b->deadlineAt > 0)) return a->deadlineAt > 0;
                    if (a->deadlineAt > 0 && a->deadlineAt != b->deadlineAt) return a->deadlineAt < b->deadlineAt;
                    return a->createdAt > b->createdAt;
                });
                summary_->setText(QString::fromUtf8("Назначенные задачи: %1 активных · %2 просрочено · %3 завершено, ждёт XP. Первые задачи отсортированы по срочности.")
                    .arg(active).arg(overdue).arg(xpPending));
                for (const auto* task : tasks) {
                    const auto project = std::find_if(data.projects.begin(), data.projects.end(),
                        [&](const auto& item) { return !task->projectId.empty() && item.id == task->projectId; });
                    const auto stage = std::find_if(data.pipelineSteps.begin(), data.pipelineSteps.end(),
                        [&](const auto& item) { return !task->pipelineStepId.empty() && item.id == task->pipelineStepId; });
                    row(task->id, {q(AppTaskDisplayTitle(*task)), q(AppTaskStatusLabel(task->status)), timeText(task->deadlineAt),
                        q(project == data.projects.end() ? task->project : project->name),
                        q(stage == data.pipelineSteps.end() ? task->pipelineStep : stage->title)});
                    const int taskRow = table_->rowCount() - 1;
                    if (task->deadlineAt > 0 && task->deadlineAt < now)
                        for (int col = 0; col < table_->columnCount(); ++col)
                            if (auto* cell = table_->item(taskRow, col)) cell->setBackground(palette().color(QPalette::Base).darker(108));
                }
                if (tasks.empty()) summary_->setText(summary_->text() + QString::fromUtf8(" · Активных задач нет."));
                table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
                table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
                table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
            }
            auto profileSkills = profile->list_skills();
            auto totalSkillXp = [](const Skill& skill) {
                int total = skill.xp;
                for (int level = 2; level <= skill.level; ++level) total += Skill::required_xp_for(level);
                return total;
            };
            QStringList categoryLabels;
            for (int index = 0; index < Profile::kCategoryCount; ++index)
                categoryLabels << QString::fromUtf8(Profile::kCategoryLabels[size_t(index)]);
            std::vector<QtProfileSkillMetric> analyticsSkills;
            analyticsSkills.reserve(profileSkills.size());
            for (const auto& skill : profileSkills) {
                analyticsSkills.push_back({q(workspace_.catalog.display_name(skill.name)), skill.level, skill.xp,
                    skill.xpToNext, totalSkillXp(skill), skill.weight});
            }
            profileAnalytics_->setData(profile->category_best_scores(), categoryLabels, std::move(analyticsSkills));
            const int skillSort = profileMode == 1 ? profileSkillSort_->currentIndex() : 2;
            auto compareDisplayName = [&](const Skill& left, const Skill& right) {
                return QString::localeAwareCompare(q(workspace_.catalog.display_name(left.name)),
                    q(workspace_.catalog.display_name(right.name))) < 0;
            };
            std::sort(profileSkills.begin(), profileSkills.end(), [&](const Skill& left, const Skill& right) {
                const int leftXp = totalSkillXp(left), rightXp = totalSkillXp(right);
                if (skillSort == 0) return compareDisplayName(left, right);
                if (skillSort == 1 && left.level != right.level) return left.level > right.level;
                if (skillSort == 2 && leftXp != rightXp) return leftXp > rightXp;
                if (skillSort == 3 && left.weight != right.weight) return left.weight > right.weight;
                if (leftXp != rightXp) return leftXp > rightXp;
                return compareDisplayName(left, right);
            });
            if (profileMode != 3) for (const auto& skill : profileSkills) {
                const int weightCategory = skill.weight >= 1.3 ? 1 : skill.weight >= 1.1 ? 2 :
                    skill.weight >= 0.9 ? 3 : skill.weight >= 0.7 ? 4 : 5;
                if (profileMode == 1 && (skill.weight < profileSkillWeightMin_->value() ||
                    skill.weight > profileSkillWeightMax_->value() ||
                    (profileSkillWeightCategory_->currentIndex() > 0 &&
                     profileSkillWeightCategory_->currentIndex() != weightCategory))) continue;
                row(skill.name, {q(workspace_.catalog.display_name(skill.name)), QString::number(skill.level),
                    QString::number(skill.xp), QString::number(totalSkillXp(skill)),
                    QString::number(skill.weight, 'f', 2)});
            }
        } else summary_->setText(QString::fromUtf8("Нет доступного профиля. Администратор может создать его через «Управление профилями»."));
        if (profileMode != 3) for (int rowIndex = 0; rowIndex < table_->rowCount(); ++rowIndex)
            table_->setRowHidden(rowIndex, profileMode == 2 || (profileMode == 0 && rowIndex >= 3));
    } else if (page == Tasks) {
        headers({QString::fromUtf8("Задача"), QString::fromUtf8("Дата"), QString::fromUtf8("Проект"),
                 QString::fromUtf8("Исполнители"), QString::fromUtf8("Статус"), QString::fromUtf8("Приоритет"),
                 QString::fromUtf8("Срок"), QString::fromUtf8("Пайплайн")});
        const auto selectedProject = u(taskProjectFilter_->currentData().toString());
        const auto selectedPipeline = u(taskPipelineFilter_->currentData().toString());
        const int quickFilter = quickTaskFilter_->currentIndex();
        const int createdRange = taskCreatedRange_->currentIndex();
        const int sortMode = taskSort_->currentIndex();
        const auto selectedAssignee = u(taskAssigneeFilter_->currentData().toString());
        const auto now = QDateTime::currentDateTime();
        const auto todayStart = now.date().startOfDay(now.timeZone()).toSecsSinceEpoch();
        const auto tomorrowStart = now.date().addDays(1).startOfDay(now.timeZone()).toSecsSinceEpoch();
        const auto nextWeekStart = now.date().addDays(7).startOfDay(now.timeZone()).toSecsSinceEpoch();
        const auto nowSeconds = now.toSecsSinceEpoch();
        constexpr std::int64_t createdDays[] = {0, 7, 30, 90, 365};
        const std::int64_t createdMin = nowSeconds - createdDays[createdRange] * 86400;
        const auto activeProfileId = u(profiles_->currentData().toString());
        enum class PipelineRisk { None, Missing, Unknown, Branching, Final };
        struct TaskTableRow { const TaskEntry* task; bool xpPending; bool overdue; bool pipelineSignal; PipelineRisk pipelineRisk; QStringList alerts; };
        std::vector<TaskTableRow> visibleTasks;
        for (const auto& task : data.tasks) {
            if (statusFilter_->currentIndex() && task.status != statusFilter_->currentIndex() - 1) continue;
            if (priorityFilter_->currentIndex() && AppNormalizeTaskPriority(task.priority) != priorityFilter_->currentIndex() - 1) continue;
            const auto project = std::find_if(data.projects.begin(), data.projects.end(),
                [&](const auto& entry) { return !task.projectId.empty() && entry.id == task.projectId; });
            const auto stage = std::find_if(data.pipelineSteps.begin(), data.pipelineSteps.end(),
                [&](const auto& entry) { return !task.pipelineStepId.empty() && entry.id == task.pipelineStepId; });
            const auto resolvedProjectName = project == data.projects.end() ? task.project : project->name;
            const auto resolvedPipelineName = stage == data.pipelineSteps.end() ? task.pipelineStep : stage->title;
            if (selectedProject == "__none__" && !resolvedProjectName.empty()) continue;
            if (!selectedProject.empty() && selectedProject != "__none__") {
                const auto selected = std::find_if(data.projects.begin(), data.projects.end(), [&selectedProject](const auto& entry) { return entry.id == selectedProject; });
                if (selected == data.projects.end() || (task.projectId != selectedProject && resolvedProjectName != selected->name)) continue;
            }
            if (selectedPipeline == "__none__" && !resolvedPipelineName.empty()) continue;
            if (!selectedPipeline.empty() && selectedPipeline != "__none__") {
                const auto selected = std::find_if(data.pipelineSteps.begin(), data.pipelineSteps.end(), [&selectedPipeline](const auto& entry) { return entry.id == selectedPipeline; });
                const auto selectedLabel = selected == data.pipelineSteps.end() ? std::string() :
                    (selected->stageCode.empty() ? selected->title : selected->stageCode + " · " + selected->title);
                if (selected == data.pipelineSteps.end() || (task.pipelineStepId != selectedPipeline &&
                    resolvedPipelineName != selected->title && task.pipelineStep != selectedLabel)) continue;
            }
            if (createdRange > 0 && task.createdAt < createdMin) continue;
            if (!selectedAssignee.empty() &&
                std::find(task.assignees.begin(), task.assignees.end(), selectedAssignee) == task.assignees.end() &&
                std::none_of(task.participants.begin(), task.participants.end(), [&selectedAssignee](const auto& participant) {
                    return participant.profileId == selectedAssignee;
                })) continue;
            const auto taskStatus = AppNormalizeTaskStatus(task.status);
            const bool assignedToProfile = !activeProfileId.empty() &&
                (std::find(task.assignees.begin(), task.assignees.end(), activeProfileId) != task.assignees.end() ||
                 std::any_of(task.participants.begin(), task.participants.end(), [&activeProfileId](const auto& item) { return item.profileId == activeProfileId; }));
            const bool needsXp = taskStatus == 2 && std::none_of(task.participants.begin(), task.participants.end(),
                [](const auto& item) { return item.globalXp > 0 || item.skillXp > 0; });
            const bool overdue = task.deadlineAt > 0 && task.deadlineAt < nowSeconds && taskStatus != 2;
            bool pipelineSignal = false;
            auto pipelineRisk = PipelineRisk::None;
            QStringList attentionReasons;
            if (needsXp) attentionReasons << QString::fromUtf8("Ожидает выдачи XP");
            if (overdue) attentionReasons << QString::fromUtf8("Просрочен срок");
            if (!data.pipelineSteps.empty()) {
                int pipelineIndex = -1;
                if (!task.pipelineStepId.empty()) {
                    for (int index = 0; index < int(data.pipelineSteps.size()); ++index)
                        if (data.pipelineSteps[size_t(index)].id == task.pipelineStepId) { pipelineIndex = index; break; }
                }
                if (pipelineIndex < 0 && !task.pipelineStep.empty()) {
                    for (int index = 0; index < int(data.pipelineSteps.size()); ++index) {
                        const auto& candidate = data.pipelineSteps[size_t(index)];
                        const auto code = candidate.stageCode.empty() ? std::to_string(index + 1) : candidate.stageCode;
                        if (task.pipelineStep == candidate.title ||
                            task.pipelineStep == code + "  " + candidate.title) { pipelineIndex = index; break; }
                    }
                }
                if (pipelineIndex < 0) {
                    pipelineSignal = true;
                    pipelineRisk = task.pipelineStepId.empty() && task.pipelineStep.empty() ? PipelineRisk::Missing : PipelineRisk::Unknown;
                    attentionReasons << (task.pipelineStepId.empty() && task.pipelineStep.empty()
                        ? QString::fromUtf8("Не указан этап процесса") : QString::fromUtf8("Этап процесса не найден"));
                } else {
                    const auto& currentStage = data.pipelineSteps[size_t(pipelineIndex)];
                    if (currentStage.nextIds.empty() && taskStatus != 2) {
                        pipelineSignal = true;
                        pipelineRisk = PipelineRisk::Final;
                        attentionReasons << QString::fromUtf8("Открытый handoff конечного этапа");
                    } else if (currentStage.nextIds.size() > 1) {
                        pipelineSignal = true;
                        pipelineRisk = PipelineRisk::Branching;
                        attentionReasons << QString::fromUtf8("Ветвящийся этап пайплайна (%1 направления)").arg(currentStage.nextIds.size());
                    } else if (currentStage.nextIds.size() == 1 && std::none_of(data.pipelineSteps.begin(), data.pipelineSteps.end(),
                        [&](const auto& candidate) { return candidate.id == currentStage.nextIds.front(); })) {
                        pipelineSignal = true;
                        pipelineRisk = PipelineRisk::Unknown;
                        attentionReasons << QString::fromUtf8("Следующий этап пайплайна не найден");
                    }
                }
            }
            if (quickFilter == 1 && !assignedToProfile) continue;
            if (quickFilter == 2 && (task.deadlineAt < todayStart || task.deadlineAt >= tomorrowStart)) continue;
            if (quickFilter == 3 && (task.deadlineAt <= 0 || task.deadlineAt >= nowSeconds || taskStatus == 2)) continue;
            if (quickFilter == 4 && (task.deadlineAt < todayStart || task.deadlineAt >= nextWeekStart)) continue;
            if (quickFilter == 5 && !resolvedProjectName.empty()) continue;
            if (quickFilter == 6 && !needsXp) continue;
            if (quickFilter == 7 && taskStatus == 2) continue;
            if (quickFilter == 8 && attentionReasons.isEmpty()) continue;
            if (quickFilter == 9 && !pipelineSignal) continue;
            if (quickFilter == 10 && pipelineRisk != PipelineRisk::Missing) continue;
            if (quickFilter == 11 && pipelineRisk != PipelineRisk::Unknown) continue;
            if (quickFilter == 12 && pipelineRisk != PipelineRisk::Branching) continue;
            if (quickFilter == 13 && pipelineRisk != PipelineRisk::Final) continue;
            visibleTasks.push_back({&task, needsXp, overdue, pipelineSignal, pipelineRisk, attentionReasons});
        }
        std::sort(visibleTasks.begin(), visibleTasks.end(), [sortMode](const TaskTableRow& left, const TaskTableRow& right) {
            const auto* a = left.task;
            const auto* b = right.task;
            if (sortMode == 1) {
                const auto ad = a->deadlineAt > 0 ? a->deadlineAt : std::numeric_limits<std::int64_t>::max();
                const auto bd = b->deadlineAt > 0 ? b->deadlineAt : std::numeric_limits<std::int64_t>::max();
                if (ad != bd) return ad < bd;
            } else if (sortMode == 2) {
                const int ap = AppNormalizeTaskPriority(a->priority), bp = AppNormalizeTaskPriority(b->priority);
                if (ap != bp) return ap > bp;
            }
            if (a->createdAt != b->createdAt) return a->createdAt > b->createdAt;
            return a->id < b->id;
        });
        const TaskEntry* focusTask = nullptr;
        const auto promotesFocus = [nowSeconds](const TaskEntry* candidate, const TaskEntry* current) {
            if (!candidate) return false;
            if (!current) return true;
            const bool candidateOverdue = candidate->deadlineAt > 0 && candidate->deadlineAt < nowSeconds &&
                AppNormalizeTaskStatus(candidate->status) != 2;
            const bool currentOverdue = current->deadlineAt > 0 && current->deadlineAt < nowSeconds &&
                AppNormalizeTaskStatus(current->status) != 2;
            if (candidateOverdue != currentOverdue) return candidateOverdue;
            const int candidatePriority = AppNormalizeTaskPriority(candidate->priority);
            const int currentPriority = AppNormalizeTaskPriority(current->priority);
            if (candidatePriority != currentPriority) return candidatePriority > currentPriority;
            const auto candidateDeadline = candidate->deadlineAt > 0 ? candidate->deadlineAt : std::numeric_limits<std::int64_t>::max();
            const auto currentDeadline = current->deadlineAt > 0 ? current->deadlineAt : std::numeric_limits<std::int64_t>::max();
            if (candidateDeadline != currentDeadline) return candidateDeadline < currentDeadline;
            const int candidateStatus = AppNormalizeTaskStatus(candidate->status);
            const int currentStatus = AppNormalizeTaskStatus(current->status);
            if (candidateStatus != currentStatus) return candidateStatus > currentStatus;
            return candidate->createdAt > current->createdAt;
        };
        int pipelineMissingCount = 0, pipelineUnknownCount = 0, pipelineBranchingCount = 0, pipelineFinalCount = 0;
        for (const auto& view : visibleTasks) {
            if (AppNormalizeTaskStatus(view.task->status) != 2 && promotesFocus(view.task, focusTask))
                focusTask = view.task;
        }
        for (const auto& view : visibleTasks) {
            const auto* task = view.task;
            const auto project = std::find_if(data.projects.begin(), data.projects.end(), [&](const auto& entry) { return !task->projectId.empty() && entry.id == task->projectId; });
            const auto stage = std::find_if(data.pipelineSteps.begin(), data.pipelineSteps.end(), [&](const auto& entry) { return !task->pipelineStepId.empty() && entry.id == task->pipelineStepId; });
            std::vector<std::string> assigneeIds = task->assignees;
            if (assigneeIds.empty()) for (const auto& participant : task->participants)
                if (!participant.profileId.empty()) assigneeIds.push_back(participant.profileId);
            QStringList assigneeNames;
            for (const auto& id : assigneeIds) {
                const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(), [&](const auto& item) { return item.id == id; });
                assigneeNames << (profile == workspace_.profiles.end() ? q(id) : q(profile->name));
            }
            QString assigneeSummary = assigneeNames.isEmpty() ? QString::fromUtf8("Не назначена")
                : assigneeNames.mid(0, 2).join(QStringLiteral(", "));
            if (assigneeNames.size() > 2) assigneeSummary += QStringLiteral(" +%1").arg(assigneeNames.size() - 2);
            const int previousRowCount = table_->rowCount();
            row(task->id, {q(AppTaskDisplayTitle(*task)), timeText(task->createdAt),
                q(project == data.projects.end() ? task->project : project->name), assigneeSummary,
                q(AppTaskStatusLabel(task->status)), q(AppTaskPriorityLabel(task->priority)), timeText(task->deadlineAt),
                q(stage == data.pipelineSteps.end() ? task->pipelineStep : stage->title)});
            if (table_->rowCount() > previousRowCount) {
                if (AppNormalizeTaskStatus(task->status) != 2) {
                    switch (view.pipelineRisk) {
                    case PipelineRisk::Missing: ++pipelineMissingCount; break;
                    case PipelineRisk::Unknown: ++pipelineUnknownCount; break;
                    case PipelineRisk::Branching: ++pipelineBranchingCount; break;
                    case PipelineRisk::Final: ++pipelineFinalCount; break;
                    case PipelineRisk::None: break;
                    }
                }
                auto* titleItem = table_->item(previousRowCount, 0);
                const bool isFocus = focusTask && focusTask->id == task->id;
                if (isFocus || view.overdue) {
                    const QColor base = palette().color(QPalette::Base);
                    const QColor accent = palette().color(QPalette::Highlight);
                    const QColor tint = isFocus ? base.lighter(112) : QColor::fromRgb(
                        (base.red() * 5 + accent.red()) / 6,
                        (base.green() * 5 + accent.green()) / 6,
                        (base.blue() * 5 + accent.blue()) / 6);
                    for (int column = 0; column < table_->columnCount(); ++column)
                        if (auto* item = table_->item(previousRowCount, column)) item->setBackground(tint);
                }
                QStringList badges;
                if (view.xpPending) badges << QStringLiteral("XP");
                if (!view.alerts.isEmpty()) badges << QStringLiteral("!");
                if (isFocus) badges << QString::fromUtf8("Фокус");
                if (!badges.isEmpty()) titleItem->setText(titleItem->text() + QStringLiteral("  [%1]").arg(badges.join(' ')));
                if (!view.alerts.isEmpty()) {
                    const auto explanation = view.alerts.join('\n');
                    titleItem->setToolTip(titleItem->toolTip() + QStringLiteral("\n\n%1").arg(explanation));
                    titleItem->setData(Qt::AccessibleDescriptionRole, explanation);
                }
            }
        }
        const auto report = BuildTeamValueReport(data.tasks, data.projects, QDateTime::currentSecsSinceEpoch());
        summary_->setText(QString::fromUtf8("Активных: %1  ·  просрочено: %2  ·  ждут XP: %3  ·  показано: %4")
            .arg(report.activeTasks).arg(report.overdueTasks).arg(report.xpPendingTasks).arg(table_->rowCount()));
        const auto linkColor = palette().color(QPalette::Highlight).name();
        taskPipelineSummary_->setText(QString::fromUtf8(
            "<span style=\"color:%1\">Пайплайн-сигналы:</span> "
            "<a style=\"color:%1\" href=\"risk:all\">все %2</a> · "
            "<a style=\"color:%1\" href=\"risk:missing\">без этапа %3</a> · "
            "<a style=\"color:%1\" href=\"risk:unknown\">вне схемы %4</a> · "
            "<a style=\"color:%1\" href=\"risk:branching\">ветвление %5</a> · "
            "<a style=\"color:%1\" href=\"risk:final\">финал открыт %6</a>")
            .arg(linkColor).arg(pipelineMissingCount + pipelineUnknownCount + pipelineBranchingCount + pipelineFinalCount)
            .arg(pipelineMissingCount).arg(pipelineUnknownCount).arg(pipelineBranchingCount).arg(pipelineFinalCount));
    } else if (page == Projects) {
        headers({QString::fromUtf8("Проект"), QString::fromUtf8("Описание"), QString::fromUtf8("Создан")});
        const auto report = BuildTeamValueReport(data.tasks, data.projects, QDateTime::currentSecsSinceEpoch());
        auto metrics = report.projects;
        const int sortMode = std::clamp(projectSort_->currentIndex(), 0, 3);
        std::sort(metrics.begin(), metrics.end(), [sortMode](const auto& a, const auto& b) {
            if (sortMode == 1 && a.totalTasks != b.totalTasks) return a.totalTasks > b.totalTasks;
            if (sortMode == 2 && a.overdueTasks != b.overdueTasks) return a.overdueTasks > b.overdueTasks;
            if (sortMode == 3 && a.xpPendingTasks != b.xpPendingTasks) return a.xpPendingTasks > b.xpPendingTasks;
            return q(a.name).compare(q(b.name), Qt::CaseInsensitive) < 0;
        });
        for (const auto& metric : metrics) {
            const auto project = std::find_if(data.projects.begin(), data.projects.end(), [&metric](const auto& item) { return item.id == metric.id; });
            if (project == data.projects.end()) continue;
            if (projectsOverdue_->isChecked() && metric.overdueTasks == 0) continue;
            if (projectsXpPending_->isChecked() && metric.xpPendingTasks == 0) continue;
            row(project->id, {q(project->name), q(project->description), timeText(project->createdAt)});
        }
        summary_->setText(QString::fromUtf8("Проектов: %1 · просрочка: %2 · ждут XP: %3 · показано: %4")
            .arg(report.totalProjects).arg(report.projectsWithOverdue).arg(report.projectsWithXpPending).arg(table_->rowCount()));
    } else if (page == Catalog) {
        headers({QString::fromUtf8("Навык"), QString::fromUtf8("Вес"), QString::fromUtf8("Описание"), QString::fromUtf8("Профессии")});
        const auto professionFilter = catalogProfessionFilter_->currentData().toString();
        for (const auto& id : workspace_.catalog.skills()) {
            const auto bindings = workspace_.catalog.professions(id);
            if (professionFilter == QStringLiteral("__none__") && !bindings.empty()) continue;
            if (!professionFilter.isEmpty() && professionFilter != QStringLiteral("__none__") &&
                std::find(bindings.begin(), bindings.end(), u(professionFilter)) == bindings.end()) continue;
            QStringList names;
            for (const auto& binding : bindings) {
                const auto found = std::find_if(data.professions.begin(), data.professions.end(), [&](const auto& p) { return p.id == binding; });
                names << q(found == data.professions.end() ? binding : found->name);
            }
            row(id, {q(workspace_.catalog.display_name(id)), QString::number(workspace_.catalog.weight(id)),
                q(workspace_.catalog.description(id)), names.join(", ")});
        }
        summary_->setText(QString::fromUtf8("Навыков: %1 · показано: %2")
            .arg(workspace_.catalog.skills().size()).arg(table_->rowCount()));
    } else if (page == Pipeline) {
        headers({QString::fromUtf8("Этап"), QString::fromUtf8("Название"), QString::fromUtf8("Ответственный"), QString::fromUtf8("Следующий шаг")});
        for (const auto& step : data.pipelineSteps) row(step.id, {q(step.stageCode), q(step.title), q(step.owner), q(step.nextStageLabel)});
    } else if (page == Professions) {
        headers({QString::fromUtf8("Профессия"), QString::fromUtf8("Описание")});
        for (const auto& item : data.professions) row(item.id, {q(item.name), q(item.description)});
    } else if (page == AdminProfileStats && admin_) {
        if (adminStatsLastRefresh_ == 0 || (adminStatsAutoRefresh_->isChecked() &&
            QDateTime::currentSecsSinceEpoch() - adminStatsLastRefresh_ >= adminStatsRefreshSeconds_->value()))
            refreshAdminProfileStats();
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto total = int(adminStatsRows_.size());
        int archived = 0, maxLevel = 0, achievementsTotal = 0, achievementsActive = 0;
        int recovery = 0, noActivity = 0, noAchievements = 0;
        long long totalXp = 0, totalLevels = 0;
        std::array<long long, Profile::kCategoryCount> categoryTotals{};
        std::array<int, Profile::kCategoryCount> categoryCounts{};
        for (const auto& item : adminStatsRows_) {
            archived += item.archived;
            maxLevel = std::max(maxLevel, item.level);
            totalXp += item.totalXp;
            totalLevels += item.level;
            achievementsTotal += item.achievementsTotal;
            achievementsActive += item.achievementsActive;
            recovery += item.recoveryTasksRemaining > 0;
            noActivity += item.lastTaskTimestamp <= 0;
            noAchievements += item.achievementsTotal == 0;
            for (size_t i = 0; i < item.categoryScores.size(); ++i) {
                categoryTotals[i] += item.categoryScores[i];
                ++categoryCounts[i];
            }
        }
        const auto active = total - archived;
        const auto averageLevel = total ? double(totalLevels) / total : 0.0;
        const auto averageXp = total ? double(totalXp) / total : 0.0;
        summary_->setAccessibleName(QString::fromUtf8("Сводные показатели статистики профилей"));
        summary_->setText(QString::fromUtf8(
            "Профилей: %1 · активных: %2 · архивных: %3\nСредний уровень: %4 · максимум: %5 · общий XP: %6 · средний XP: %7\n"
            "Ачивки: %8 всего, %9 активных · прогрев: %10 · без активности: %11 · без ачивок: %12\n"
            "Обновлено: %13%14")
            .arg(total).arg(active).arg(archived).arg(averageLevel, 0, 'f', 1).arg(maxLevel)
            .arg(totalXp).arg(averageXp, 0, 'f', 0).arg(achievementsTotal).arg(achievementsActive)
            .arg(recovery).arg(noActivity).arg(noAchievements)
            .arg(QDateTime::fromSecsSinceEpoch(adminStatsLastRefresh_).toString("dd.MM.yyyy HH:mm:ss"))
            .arg(adminStatsUnreadableProfiles_ ? QString::fromUtf8(" · не удалось прочитать: %1").arg(adminStatsUnreadableProfiles_) : QString()));

        std::vector<QtAdminProfileStatsRow> filtered;
        const auto query = adminStatsSearch_->text().trimmed();
        const int selectedRank = adminStatsRank_->currentIndex();
        for (const auto& item : adminStatsRows_) {
            if (!adminStatsArchived_->isChecked() && item.archived) continue;
            if (selectedRank > 0 && adminProfileRankIndex(item.level) != selectedRank - 1) continue;
            if (!query.isEmpty() && !QString::fromStdString(item.id + " " + item.name).contains(query, Qt::CaseInsensitive)) continue;
            filtered.push_back(item);
        }
        const auto view = adminStatsView_->currentIndex();
        auto installProfileRows = [this, now, &headers](const std::vector<QtAdminProfileStatsRow>& items) {
            headers({QString::fromUtf8("ID"), QString::fromUtf8("Имя"), QString::fromUtf8("Ранг"),
                QString::fromUtf8("Уровень"), "XP", QString::fromUtf8("Последняя активность"),
                QString::fromUtf8("Простой"), QString::fromUtf8("Прогрев"), QString::fromUtf8("Ачивки · акт./всего")});
            constexpr int widths[] = {72, 150, 92, 62, 78, 136, 72, 72, 130};
            for (int column = 0; column < int(std::size(widths)); ++column) {
                table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
                table_->setColumnWidth(column, widths[column]);
            }
            for (const auto& item : items) {
                const bool hasActivity = item.lastTaskTimestamp > 0;
                const auto elapsed = hasActivity ? now - item.lastTaskTimestamp : 0;
                QStringList values{q(item.id), q(item.name) + (item.archived ? QString::fromUtf8(" (архив)") : QString()),
                    adminProfileRankName(item.level), QString::number(item.level), QString::number(item.totalXp),
                    hasActivity ? QDateTime::fromSecsSinceEpoch(item.lastTaskTimestamp).toString("yyyy-MM-dd HH:mm") : QString::fromUtf8("нет данных"),
                    hasActivity ? elapsedProfileTime(elapsed) : QString::fromUtf8("—"),
                    QString::number(item.recoveryTasksRemaining),
                    QString::fromUtf8("%1 / %2").arg(item.achievementsActive).arg(item.achievementsTotal)};
                const int rowIndex = table_->rowCount();
                table_->insertRow(rowIndex);
                for (int column = 0; column < values.size(); ++column) {
                    auto* cell = new QTableWidgetItem(values[column]);
                    cell->setToolTip(values[column]);
                    if (column == 0) cell->setData(Qt::UserRole, q(item.id));
                    if (item.archived && column == 1) cell->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
                    table_->setItem(rowIndex, column, cell);
                }
            }
        };
        if (view <= 5) {
            auto displayed = filtered;
            if (view == 1) std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                return a.level != b.level ? a.level > b.level : a.totalXp > b.totalXp;
            });
            else if (view == 2) std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                return a.totalXp != b.totalXp ? a.totalXp > b.totalXp : a.level > b.level;
            });
            else if (view == 3) std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                if (a.achievementsActive != b.achievementsActive) return a.achievementsActive > b.achievementsActive;
                if (a.achievementsTotal != b.achievementsTotal) return a.achievementsTotal > b.achievementsTotal;
                return a.level > b.level;
            });
            else if (view == 4) {
                const auto threshold = std::int64_t(adminStatsInactivityDays_->value()) * 86400;
                displayed.erase(std::remove_if(displayed.begin(), displayed.end(), [&](const auto& item) {
                    return item.lastTaskTimestamp > 0 && now - item.lastTaskTimestamp < threshold;
                }), displayed.end());
                std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                    if (a.lastTaskTimestamp != b.lastTaskTimestamp) {
                        if (a.lastTaskTimestamp <= 0) return true;
                        if (b.lastTaskTimestamp <= 0) return false;
                        return a.lastTaskTimestamp < b.lastTaskTimestamp;
                    }
                    return a.totalXp > b.totalXp;
                });
            } else if (view == 5) {
                displayed.erase(std::remove_if(displayed.begin(), displayed.end(), [](const auto& item) {
                    return item.recoveryTasksRemaining <= 0;
                }), displayed.end());
                std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                    return a.recoveryTasksRemaining != b.recoveryTasksRemaining
                        ? a.recoveryTasksRemaining > b.recoveryTasksRemaining : a.totalXp > b.totalXp;
                });
            }
            const int matched = int(displayed.size());
            if (view >= 1 && view <= 5 && displayed.size() > 6) displayed.resize(6);
            installProfileRows(displayed);
            if (view == 4 || view == 5)
                summary_->setText(summary_->text() + QString::fromUtf8("\nПоказано: %1 из %2").arg(displayed.size()).arg(matched));
            else summary_->setText(summary_->text() + QString::fromUtf8("\nПодходит фильтрам: %1 из %2").arg(filtered.size()).arg(total));
        } else if (view == 6) {
            headers({QString::fromUtf8("Ранг"), QString::fromUtf8("Профилей"), QString::fromUtf8("Доля")});
            std::array<int, 16> counts{};
            for (const auto& item : filtered) ++counts[size_t(adminProfileRankIndex(item.level))];
            const int denominator = std::max(1, int(filtered.size()));
            for (size_t i = 0; i < counts.size(); ++i) {
                const int index = table_->rowCount(); table_->insertRow(index);
                for (int column = 0; column < 3; ++column) {
                    const QString value = column == 0 ? adminProfileRanks()[i].first : column == 1
                        ? QString::number(counts[i]) : QString::number(double(counts[i]) * 100.0 / denominator, 'f', 1) + "%";
                    table_->setItem(index, column, new QTableWidgetItem(value));
                }
            }
            summary_->setText(summary_->text() + QString::fromUtf8("\nПоказано профилей после фильтров: %1").arg(filtered.size()));
        } else {
            headers({QString::fromUtf8("Категория"), QString::fromUtf8("Среднее"), QString::fromUtf8("Профилей")});
            std::array<double, Profile::kCategoryCount> averages{};
            int minIndex = -1;
            double minimum = 0.0, maximum = 0.0;
            for (size_t i = 0; i < averages.size(); ++i) {
                if (categoryCounts[i] == 0) continue;
                averages[i] = double(categoryTotals[i]) / categoryCounts[i];
                if (minIndex < 0 || averages[i] < minimum) { minIndex = int(i); minimum = averages[i]; }
                maximum = std::max(maximum, averages[i]);
            }
            for (size_t i = 0; i < averages.size(); ++i) {
                const auto label = QString::fromUtf8("Категория %1").arg(QString::fromUtf8(Profile::kCategoryLabels[i]));
                const int index = table_->rowCount(); table_->insertRow(index);
                const QStringList values{label, categoryCounts[i] ? QString::fromUtf8("%1/10").arg(averages[i], 0, 'f', 1) : QString::fromUtf8("—"),
                    QString::number(categoryCounts[i])};
                for (int column = 0; column < values.size(); ++column)
                    table_->setItem(index, column, new QTableWidgetItem(values[column]));
                if (int(i) == minIndex && maximum - minimum >= 1.0) {
                    for (int column = 0; column < table_->columnCount(); ++column) {
                        if (auto* cell = table_->item(table_->rowCount() - 1, column))
                            cell->setToolTip(QString::fromUtf8("Зона внимания: минимальное среднее среди категорий"));
                    }
                }
            }
            if (minIndex >= 0 && maximum - minimum >= 1.0)
                summary_->setText(summary_->text() + QString::fromUtf8("\nЗона внимания: категория %1 (%2/10)")
                    .arg(QString::fromUtf8(Profile::kCategoryLabels[size_t(minIndex)])).arg(minimum, 0, 'f', 1));
        }
    } else if (page == Statistics) {
        int missingCreationDates = 0;
        const auto reportTasks = reportTasksForRange(data.tasks, reportDateRange_->currentIndex(),
            reportFrom_->date(), reportTo_->date(), &missingCreationDates);
        const auto report = BuildTeamValueReport(reportTasks, data.projects, QDateTime::currentSecsSinceEpoch());
        const auto periodLabel = reportPeriodLabel(reportDateRange_->currentIndex(), reportFrom_->date(), reportTo_->date());
        QDate previousFrom, previousTo;
        const bool comparePrevious = reportCompare_->isChecked() && reportPreviousRange(reportDateRange_->currentIndex(),
            reportFrom_->date(), reportTo_->date(), &previousFrom, &previousTo);
        const auto previousTasks = comparePrevious
            ? reportTasksForRange(data.tasks, 4, previousFrom, previousTo) : std::vector<TaskEntry>{};
        const auto previousReport = BuildTeamValueReport(previousTasks, data.projects, QDateTime::currentSecsSinceEpoch());
        const auto comparisonLabel = comparePrevious
            ? QString::fromUtf8(" · сравнение с %1–%2").arg(previousFrom.toString("dd.MM.yyyy"), previousTo.toString("dd.MM.yyyy"))
            : QString();
        statisticsChart_->setValues(report.newTasks, report.inProgressTasks, report.doneTasks, periodLabel);
        const auto today = QDate::currentDate();
        const auto firstTrendMonth = QDate(today.year(), today.month(), 1).addMonths(-11);
        const auto firstTrendTimestamp = QDateTime(firstTrendMonth, QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch();
        const auto trendAudit = LoadTaskAuditData(workspace_.directory, 0, firstTrendTimestamp);
        statisticsChart_->setCompletionTrend(QtReportChart::BuildMonthlyCompletionTrend(trendAudit, today));
        const auto missingNote = missingCreationDates
            ? QString::fromUtf8(" · без даты создания исключено: %1").arg(missingCreationDates) : QString();
        if (reportView_->currentIndex() == 0) {
            auto displayed = report.projects;
            if (comparePrevious) for (const auto& old : previousReport.projects) {
                const auto found = std::find_if(displayed.begin(), displayed.end(), [&](const auto& item) { return item.id == old.id; });
                if (found == displayed.end()) {
                    auto empty = old; empty.totalTasks = empty.activeTasks = empty.doneTasks = empty.overdueTasks =
                        empty.xpPendingTasks = empty.withoutPipelineTasks = empty.totalGlobalXp = empty.totalSkillXp = 0;
                    displayed.push_back(std::move(empty));
                }
            }
            if (comparePrevious) std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                return q(a.name).compare(q(b.name), Qt::CaseInsensitive) < 0;
            });
            QStringList columns{QString::fromUtf8("Проект"), QString::fromUtf8("Активно"), QString::fromUtf8("Выполнено"),
                QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP")};
            if (comparePrevious) columns << QString::fromUtf8("Активно · пред." ) << QString::fromUtf8("Выполнено · пред.")
                << QString::fromUtf8("Просрочено · пред.") << QString::fromUtf8("Ждут XP · пред.");
            headers(columns);
            for (const auto& item : displayed) {
                QStringList values{q(item.name), QString::number(item.activeTasks), QString::number(item.doneTasks),
                    QString::number(item.overdueTasks), QString::number(item.xpPendingTasks)};
                if (comparePrevious) {
                    const auto old = std::find_if(previousReport.projects.begin(), previousReport.projects.end(),
                        [&](const auto& value) { return value.id == item.id; });
                    values << QString::number(old == previousReport.projects.end() ? 0 : old->activeTasks)
                        << QString::number(old == previousReport.projects.end() ? 0 : old->doneTasks)
                        << QString::number(old == previousReport.projects.end() ? 0 : old->overdueTasks)
                        << QString::number(old == previousReport.projects.end() ? 0 : old->xpPendingTasks);
                }
                row(item.id.empty() ? "__no_project" : item.id, values);
            }
            summary_->setText(QString::fromUtf8("%1 · задач: %2 · проектов в каталоге: %3 · XP: %4 · состояние на сейчас%5%6")
                .arg(periodLabel).arg(report.totalTasks).arg(report.totalProjects).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        } else if (reportView_->currentIndex() == 1) {
            auto displayed = report.assignees;
            if (comparePrevious) for (const auto& old : previousReport.assignees) {
                const auto found = std::find_if(displayed.begin(), displayed.end(), [&](const auto& item) { return item.profileId == old.profileId; });
                if (found == displayed.end()) {
                    auto empty = old; empty.totalTasks = empty.activeTasks = empty.doneTasks = empty.overdueTasks =
                        empty.xpPendingTasks = empty.totalGlobalXp = empty.totalSkillXp = 0;
                    displayed.push_back(std::move(empty));
                }
            }
            if (comparePrevious) std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                return a.profileId < b.profileId;
            });
            QStringList columns{QString::fromUtf8("Сотрудник"), "ID", QString::fromUtf8("Активно"), QString::fromUtf8("Выполнено"),
                QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"), QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")};
            if (comparePrevious) columns << QString::fromUtf8("Активно · пред.") << QString::fromUtf8("Выполнено · пред.")
                << QString::fromUtf8("Просрочено · пред.") << QString::fromUtf8("Ждут XP · пред.")
                << QString::fromUtf8("Глобальный XP · пред.") << QString::fromUtf8("XP навыков · пред.");
            headers(columns);
            for (const auto& item : displayed) {
                const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(),
                    [&](const auto& value) { return value.id == item.profileId; });
                QStringList values{profile == workspace_.profiles.end() ? q(item.profileId) : q(profile->name), q(item.profileId),
                    QString::number(item.activeTasks), QString::number(item.doneTasks), QString::number(item.overdueTasks),
                    QString::number(item.xpPendingTasks), QString::number(item.totalGlobalXp), QString::number(item.totalSkillXp)};
                if (comparePrevious) {
                    const auto old = std::find_if(previousReport.assignees.begin(), previousReport.assignees.end(),
                        [&](const auto& value) { return value.profileId == item.profileId; });
                    values << QString::number(old == previousReport.assignees.end() ? 0 : old->activeTasks)
                        << QString::number(old == previousReport.assignees.end() ? 0 : old->doneTasks)
                        << QString::number(old == previousReport.assignees.end() ? 0 : old->overdueTasks)
                        << QString::number(old == previousReport.assignees.end() ? 0 : old->xpPendingTasks)
                        << QString::number(old == previousReport.assignees.end() ? 0 : old->totalGlobalXp)
                        << QString::number(old == previousReport.assignees.end() ? 0 : old->totalSkillXp);
                }
                row(item.profileId, values);
            }
            summary_->setText(QString::fromUtf8("%1 · сотрудников в задачах: %2 · без исполнителя: %3 · XP: %4 · состояние на сейчас%5%6")
                .arg(periodLabel).arg(int(report.assignees.size())).arg(report.unassignedTasks).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        } else if (reportView_->currentIndex() == 2) {
            const auto now = QDateTime::currentSecsSinceEpoch();
            auto displayed = buildReportStageGroups(reportTasks, data.pipelineSteps, data.projects, now);
            const auto previous = comparePrevious
                ? buildReportStageGroups(previousTasks, data.pipelineSteps, data.projects, now) : std::vector<QtReportStageGroup>{};
            if (comparePrevious) for (const auto& old : previous) {
                if (std::none_of(displayed.begin(), displayed.end(), [&](const auto& item) { return item.id == old.id; })) {
                    auto empty = old;
                    empty.report = TeamValueReport{};
                    displayed.push_back(std::move(empty));
                }
            }
            if (comparePrevious) std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) {
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            QStringList columns{QString::fromUtf8("Этап"), QString::fromUtf8("Задач"), QString::fromUtf8("Активно"),
                QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"),
                QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")};
            if (comparePrevious) columns << QString::fromUtf8("Задач · пред.") << QString::fromUtf8("Активно · пред.")
                << QString::fromUtf8("Выполнено · пред.") << QString::fromUtf8("Просрочено · пред.")
                << QString::fromUtf8("Ждут XP · пред.") << QString::fromUtf8("Глобальный XP · пред.")
                << QString::fromUtf8("XP навыков · пред.");
            headers(columns);
            for (const auto& item : displayed) {
                const auto& m = item.report;
                QStringList values{item.name, QString::number(m.totalTasks), QString::number(m.activeTasks),
                    QString::number(m.doneTasks), QString::number(m.overdueTasks), QString::number(m.xpPendingTasks),
                    QString::number(m.totalGlobalXp), QString::number(m.totalSkillXp)};
                if (comparePrevious) {
                    const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& candidate) { return candidate.id == item.id; });
                    const auto& p = old == previous.end() ? TeamValueReport{} : old->report;
                    values << QString::number(p.totalTasks) << QString::number(p.activeTasks) << QString::number(p.doneTasks)
                        << QString::number(p.overdueTasks) << QString::number(p.xpPendingTasks)
                        << QString::number(p.totalGlobalXp) << QString::number(p.totalSkillXp);
                }
                row(item.id, values);
            }
            summary_->setText(QString::fromUtf8("%1 · этапов в задачах: %2 · задач: %3 · XP: %4 · состояние на сейчас%5%6")
                .arg(periodLabel).arg(displayed.size()).arg(report.totalTasks).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        } else if (reportView_->currentIndex() == 3) {
            const auto now = QDateTime::currentSecsSinceEpoch();
            auto displayed = buildReportCategoryGroups(reportTasks, data.projects, now);
            const auto previous = comparePrevious
                ? buildReportCategoryGroups(previousTasks, data.projects, now) : std::vector<QtReportCategoryGroup>{};
            if (comparePrevious) for (const auto& old : previous) {
                if (std::none_of(displayed.begin(), displayed.end(), [&](const auto& item) { return item.id == old.id; })) {
                    auto empty = old; empty.report = TeamValueReport{};
                    displayed.push_back(std::move(empty));
                }
            }
            std::sort(displayed.begin(), displayed.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
            QStringList columns{QString::fromUtf8("Категория"), QString::fromUtf8("Задач"), QString::fromUtf8("Активно"),
                QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"),
                QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")};
            if (comparePrevious) columns << QString::fromUtf8("Задач · пред.") << QString::fromUtf8("Активно · пред.")
                << QString::fromUtf8("Выполнено · пред.") << QString::fromUtf8("Просрочено · пред.")
                << QString::fromUtf8("Ждут XP · пред.") << QString::fromUtf8("Глобальный XP · пред.")
                << QString::fromUtf8("XP навыков · пред.");
            headers(columns);
            for (const auto& item : displayed) {
                const auto& m = item.report;
                QStringList values{QString::fromUtf8(Profile::kCategoryLabels[size_t(item.index)]), QString::number(m.totalTasks),
                    QString::number(m.activeTasks), QString::number(m.doneTasks), QString::number(m.overdueTasks),
                    QString::number(m.xpPendingTasks), QString::number(m.totalGlobalXp), QString::number(m.totalSkillXp)};
                if (comparePrevious) {
                    const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& candidate) { return candidate.id == item.id; });
                    const auto& p = old == previous.end() ? TeamValueReport{} : old->report;
                    values << QString::number(p.totalTasks) << QString::number(p.activeTasks) << QString::number(p.doneTasks)
                        << QString::number(p.overdueTasks) << QString::number(p.xpPendingTasks)
                        << QString::number(p.totalGlobalXp) << QString::number(p.totalSkillXp);
                }
                row(item.id, values);
            }
            summary_->setText(QString::fromUtf8("%1 · категорий в задачах: %2 · задач: %3 · XP: %4 · состояние на сейчас%5%6")
                .arg(periodLabel).arg(displayed.size()).arg(report.totalTasks).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        } else if (reportView_->currentIndex() == 4) {
            std::array<std::vector<TaskEntry>, 3> currentByStatus, previousByStatus;
            for (const auto& task : reportTasks) {
                const int status = AppNormalizeTaskStatus(task.status);
                currentByStatus[size_t(status)].push_back(task);
            }
            if (comparePrevious) for (const auto& task : previousTasks)
                previousByStatus[size_t(AppNormalizeTaskStatus(task.status))].push_back(task);
            const auto now = QDateTime::currentSecsSinceEpoch();
            QStringList columns{QString::fromUtf8("Статус"), QString::fromUtf8("Задач"), QString::fromUtf8("Активно"),
                QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"),
                QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")};
            if (comparePrevious) columns << QString::fromUtf8("Задач · пред.") << QString::fromUtf8("Активно · пред.")
                << QString::fromUtf8("Выполнено · пред.") << QString::fromUtf8("Просрочено · пред.")
                << QString::fromUtf8("Ждут XP · пред.") << QString::fromUtf8("Глобальный XP · пред.")
                << QString::fromUtf8("XP навыков · пред.");
            headers(columns);
            for (int status = 0; status < 3; ++status) {
                const auto current = BuildTeamValueReport(currentByStatus[size_t(status)], data.projects, now);
                const auto previous = comparePrevious
                    ? BuildTeamValueReport(previousByStatus[size_t(status)], data.projects, now) : TeamValueReport{};
                QStringList values{q(AppTaskStatusLabel(status)), QString::number(current.totalTasks),
                    QString::number(current.activeTasks), QString::number(current.doneTasks),
                    QString::number(current.overdueTasks), QString::number(current.xpPendingTasks),
                    QString::number(current.totalGlobalXp), QString::number(current.totalSkillXp)};
                if (comparePrevious) values << QString::number(previous.totalTasks) << QString::number(previous.activeTasks)
                    << QString::number(previous.doneTasks) << QString::number(previous.overdueTasks)
                    << QString::number(previous.xpPendingTasks) << QString::number(previous.totalGlobalXp)
                    << QString::number(previous.totalSkillXp);
                row("__status_" + std::to_string(status), values);
            }
            summary_->setText(QString::fromUtf8("%1 · задач по текущему статусу · %2 задач · XP: %3 · состояние на сейчас%4%5")
                .arg(periodLabel).arg(report.totalTasks).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        } else if (reportView_->currentIndex() == 5) {
            std::array<std::vector<TaskEntry>, 4> currentByPriority, previousByPriority;
            for (const auto& task : reportTasks)
                currentByPriority[size_t(AppNormalizeTaskPriority(task.priority))].push_back(task);
            if (comparePrevious) for (const auto& task : previousTasks)
                previousByPriority[size_t(AppNormalizeTaskPriority(task.priority))].push_back(task);
            const auto now = QDateTime::currentSecsSinceEpoch();
            QStringList columns{QString::fromUtf8("Приоритет"), QString::fromUtf8("Задач"), QString::fromUtf8("Активно"),
                QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"),
                QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")};
            if (comparePrevious) columns << QString::fromUtf8("Задач · пред.") << QString::fromUtf8("Активно · пред.")
                << QString::fromUtf8("Выполнено · пред.") << QString::fromUtf8("Просрочено · пред.")
                << QString::fromUtf8("Ждут XP · пред.") << QString::fromUtf8("Глобальный XP · пред.")
                << QString::fromUtf8("XP навыков · пред.");
            headers(columns);
            for (int priority = 0; priority < 4; ++priority) {
                const auto current = BuildTeamValueReport(currentByPriority[size_t(priority)], data.projects, now);
                const auto previous = comparePrevious
                    ? BuildTeamValueReport(previousByPriority[size_t(priority)], data.projects, now) : TeamValueReport{};
                QStringList values{q(AppTaskPriorityLabel(priority)), QString::number(current.totalTasks),
                    QString::number(current.activeTasks), QString::number(current.doneTasks),
                    QString::number(current.overdueTasks), QString::number(current.xpPendingTasks),
                    QString::number(current.totalGlobalXp), QString::number(current.totalSkillXp)};
                if (comparePrevious) values << QString::number(previous.totalTasks) << QString::number(previous.activeTasks)
                    << QString::number(previous.doneTasks) << QString::number(previous.overdueTasks)
                    << QString::number(previous.xpPendingTasks) << QString::number(previous.totalGlobalXp)
                    << QString::number(previous.totalSkillXp);
                row(reportPriorityKey(priority), values);
            }
            summary_->setText(QString::fromUtf8("%1 · задач по приоритетам · %2 задач · XP: %3 · состояние на сейчас%4%5")
                .arg(periodLabel).arg(report.totalTasks).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        } else {
            std::array<std::vector<TaskEntry>, 4> currentByDeadline, previousByDeadline;
            const auto now = QDateTime::currentSecsSinceEpoch();
            for (const auto& task : reportTasks)
                currentByDeadline[size_t(reportDeadlineGroup(task, now))].push_back(task);
            if (comparePrevious) for (const auto& task : previousTasks)
                previousByDeadline[size_t(reportDeadlineGroup(task, now))].push_back(task);
            static const std::array<QString, 4> labels{
                QString::fromUtf8("Выполнена"), QString::fromUtf8("Просрочена"),
                QString::fromUtf8("В сроке"), QString::fromUtf8("Без срока")};
            QStringList columns{QString::fromUtf8("Состояние срока"), QString::fromUtf8("Задач"), QString::fromUtf8("Активно"),
                QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"),
                QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")};
            if (comparePrevious) columns << QString::fromUtf8("Задач · пред.") << QString::fromUtf8("Активно · пред.")
                << QString::fromUtf8("Выполнено · пред.") << QString::fromUtf8("Просрочено · пред.")
                << QString::fromUtf8("Ждут XP · пред.") << QString::fromUtf8("Глобальный XP · пред.")
                << QString::fromUtf8("XP навыков · пред.");
            headers(columns);
            for (int group = 0; group < int(labels.size()); ++group) {
                const auto current = BuildTeamValueReport(currentByDeadline[size_t(group)], data.projects, now);
                const auto previous = comparePrevious
                    ? BuildTeamValueReport(previousByDeadline[size_t(group)], data.projects, now) : TeamValueReport{};
                QStringList values{labels[size_t(group)], QString::number(current.totalTasks),
                    QString::number(current.activeTasks), QString::number(current.doneTasks),
                    QString::number(current.overdueTasks), QString::number(current.xpPendingTasks),
                    QString::number(current.totalGlobalXp), QString::number(current.totalSkillXp)};
                if (comparePrevious) values << QString::number(previous.totalTasks) << QString::number(previous.activeTasks)
                    << QString::number(previous.doneTasks) << QString::number(previous.overdueTasks)
                    << QString::number(previous.xpPendingTasks) << QString::number(previous.totalGlobalXp)
                    << QString::number(previous.totalSkillXp);
                row("__deadline_" + std::to_string(group), values);
            }
            summary_->setText(QString::fromUtf8("%1 · задач по состоянию срока · %2 задач · XP: %3 · состояние на сейчас%4%5")
                .arg(periodLabel).arg(report.totalTasks).arg(report.totalGlobalXp).arg(missingNote).arg(comparisonLabel));
        }
    } else if (page == Audit) {
        if (!admin_ && auditSourceFilter_->currentIndex() != 1) {
            const QSignalBlocker sourceBlocker(auditSourceFilter_);
            auditSourceFilter_->setCurrentIndex(1);
        }
        headers({QString::fromUtf8("Источник"), QString::fromUtf8("Время"), QString::fromUtf8("Автор"), QString::fromUtf8("Объект"),
            QString::fromUtf8("Поле"), QString::fromUtf8("Было"), QString::fromUtf8("Стало")});
        struct AuditDisplayRow { std::int64_t timestamp; int source; std::string id; QStringList values; };
        std::vector<AuditDisplayRow> entries;
        entries.reserve(data.taskAudit.size());
        for (const auto& entry : data.taskAudit) entries.push_back({entry.timestamp, 1, entry.taskId,
            {QString::fromUtf8("Задача"), timeText(entry.timestamp), q(entry.actor), q(entry.taskId), q(entry.field), q(entry.oldValue), q(entry.newValue)}});
        if (admin_) {
            const auto profileEntries = profileAudit(workspace_.directory);
            entries.reserve(entries.size() + profileEntries.size());
            for (const auto& entry : profileEntries) entries.push_back({entry.timestamp, 2, entry.profile,
                {QString::fromUtf8("Профиль"), timeText(entry.timestamp), QString::fromUtf8("локально"), q(entry.profile), q(entry.action), QString(), q(entry.details)}});
            entries.reserve(entries.size() + appLogs_.size());
            for (size_t index = 0; index < appLogs_.size(); ++index) {
                const auto& entry = appLogs_[index];
                const bool coreWalletEvent = entry.source == "CoreWalletMutation";
                const bool coreCloudEvent = entry.source == "CoreCloudTransaction";
                const bool coreProfileEvent = entry.source == "CoreProfileMutation";
                const bool coreRecoveryEvent = entry.source == "CoreTransactionRecovery";
                const bool coreTaskMutationEvent = entry.source == "CoreTaskMutation";
                const bool coreCatalogMutationEvent = entry.source == "CoreCatalogMutation";
                const bool coreAuthenticationEvent = entry.source == "CoreAuthentication";
                const bool coreStorageEvent = entry.source == "StorageCleanup" || entry.source == "StorageHealthReport";
                const bool coreReleaseEvent = entry.source == "CloudRelease";
                const bool coreEvent = coreWalletEvent || coreCloudEvent || coreProfileEvent || coreRecoveryEvent ||
                    coreTaskMutationEvent || coreCatalogMutationEvent || coreAuthenticationEvent || coreStorageEvent ||
                    coreReleaseEvent || entry.source == "CoreTaskCompletion";
                const QString sourceLabel = coreWalletEvent ? QString::fromUtf8("Операция кошелька")
                    : coreCloudEvent ? QString::fromUtf8("Облачный перенос")
                    : coreProfileEvent ? QString::fromUtf8("Операция профиля")
                    : coreRecoveryEvent ? QString::fromUtf8("Восстановление транзакции")
                    : coreTaskMutationEvent ? QString::fromUtf8("Изменение задач")
                    : coreCatalogMutationEvent ? QString::fromUtf8("Изменение справочников")
                    : coreAuthenticationEvent ? QString::fromUtf8("Аутентификация администратора")
                    : coreStorageEvent ? QString::fromUtf8("Проверка и очистка хранилища")
                    : coreReleaseEvent ? QString::fromUtf8("Установка обновления")
                    : QString::fromUtf8("Завершение XP");
                const auto level = entry.level == AppLogLevel::Info ? QString::fromUtf8("Инфо")
                    : entry.level == AppLogLevel::Warning ? QString::fromUtf8("Предупреждение") : QString::fromUtf8("Ошибка");
                entries.push_back({entry.timestamp, coreEvent ? 5 : 3, std::to_string(index),
                    {coreEvent ? QString::fromUtf8("Core-событие") : QString::fromUtf8("Приложение"),
                        timeText(entry.timestamp), q(entry.source),
                        coreEvent ? sourceLabel : QString::fromUtf8("Журнал Qt"),
                        level, QString(), q(entry.message)}});
            }
            entries.reserve(entries.size() + data.vault.log.size());
            for (size_t index = 0; index < data.vault.log.size(); ++index) {
                const auto& entry = data.vault.log[index];
                const auto amount = QString::number(entry.amount, 'f', 2) + QLatin1Char(' ') +
                    q(data.vault.currencyCode.empty() ? std::string(u8"Кукоин") : data.vault.currencyCode);
                entries.push_back({entry.timestamp, 4, std::to_string(index),
                    {QString::fromUtf8("Хранилище"), timeText(entry.timestamp), QString::fromUtf8("локально"),
                        q(entry.action), QString::fromUtf8("Сумма"), QString(), amount + QStringLiteral(" · ") + q(entry.note)}});
            }
        }
        std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return left.timestamp > right.timestamp;
        });
        int sourceCount = 0;
        for (const auto& entry : entries) {
            if (auditSourceFilter_->currentIndex() != 0 && entry.source != auditSourceFilter_->currentIndex()) continue;
            ++sourceCount;
            const auto& values = entry.values;
            if (!auditActorFilter_->text().trimmed().isEmpty() &&
                !values[2].contains(auditActorFilter_->text().trimmed(), Qt::CaseInsensitive)) continue;
            const auto searchableObject = values[3] + QLatin1Char(' ') + values[5] + QLatin1Char(' ') + values[6];
            if (!auditObjectFilter_->text().trimmed().isEmpty() &&
                !searchableObject.contains(auditObjectFilter_->text().trimmed(), Qt::CaseInsensitive)) continue;
            if (!auditFieldFilter_->text().trimmed().isEmpty() &&
                !values[4].contains(auditFieldFilter_->text().trimmed(), Qt::CaseInsensitive)) continue;
            row(entry.id, entry.values);
        }
        summary_->setText(QString::fromUtf8("Событий: %1 · источник: %2 · показано: %3 · сначала новые")
            .arg(entries.size()).arg(sourceCount).arg(table_->rowCount()));
    } else if (page == Logs) {
        logActivityChart_->setEntries(appLogs_);
        headers({QString::fromUtf8("#"), QString::fromUtf8("Время"), QString::fromUtf8("Уровень"),
            QString::fromUtf8("Источник"), QString::fromUtf8("Сообщение")});
        table_->setColumnHidden(1, logCompactView_->isChecked());
        table_->setColumnHidden(3, logCompactView_->isChecked());
        const auto currentSource = logSourceFilter_->currentData().toString();
        const auto selectedSource = currentSource.isEmpty() ? displaySettings_.logSourceFilter : currentSource;
        QStringList sources;
        for (const auto& entry : appLogs_) {
            const auto source = q(entry.source);
            if (!source.isEmpty() && !sources.contains(source)) sources.push_back(source);
        }
        sources.sort(Qt::CaseInsensitive);
        {
            const QSignalBlocker sourceBlocker(logSourceFilter_);
            logSourceFilter_->clear();
            logSourceFilter_->addItem(QString::fromUtf8("Все источники"), QString());
            for (const auto& source : sources) logSourceFilter_->addItem(source, source);
            const int selectedIndex = logSourceFilter_->findData(selectedSource);
            logSourceFilter_->setCurrentIndex(std::max(0, selectedIndex));
        }
        const auto sourceFilter = logSourceFilter_->currentData().toString();
        int total = 0;
        int visible = 0;
        int infoCount = 0;
        int warningCount = 0;
        int errorCount = 0;
        for (auto it = appLogs_.rbegin(); it != appLogs_.rend(); ++it) {
            ++total;
            if (it->level == AppLogLevel::Info) ++infoCount;
            else if (it->level == AppLogLevel::Warning) ++warningCount;
            else ++errorCount;
            const bool enabled = it->level == AppLogLevel::Info ? logInfo_->isChecked()
                : it->level == AppLogLevel::Warning ? logWarnings_->isChecked() : logErrors_->isChecked();
            if (!enabled) continue;
            if (!sourceFilter.isEmpty() && q(it->source) != sourceFilter) continue;
            const QString level = it->level == AppLogLevel::Info ? QString::fromUtf8("Инфо")
                : it->level == AppLogLevel::Warning ? QString::fromUtf8("Предупреждение") : QString::fromUtf8("Ошибка");
            const QStringList values{QString::number(total), timeText(it->timestamp), level, q(it->source), q(it->message)};
            if (!values.join(' ').contains(search_->text(), Qt::CaseInsensitive)) continue;
            ++visible;
            row(std::to_string(total), values);
        }
        summary_->setText(QString::fromUtf8("Показано: %1 из %2 · Инфо: %3 · Предупреждения: %4 · Ошибки: %5 · %6")
            .arg(visible).arg(total).arg(infoCount).arg(warningCount).arg(errorCount)
            .arg(appLogPersistenceWarning_ ? QString::fromUtf8("ошибка сохранения") : QString::fromUtf8("сохранено локально")));
        if (logAutoScroll_->isChecked()) table_->scrollToTop();
    } else if (page == Rules) {
        headers({QString::fromUtf8("Параметр"), QString::fromUtf8("Значение")});
        const auto& rules = data.rulesConfig;
        row("level_base", {QString::fromUtf8("Базовый XP уровня"), QString::number(rules.levelBaseXp)});
        row("level_linear", {QString::fromUtf8("Линейный прирост"), QString::number(rules.levelLinearXp)});
        row("level_quadratic", {QString::fromUtf8("Квадратичный прирост"), QString::number(rules.levelQuadraticXp)});
        for (size_t i = 0; i < rules.categoryBaseXp.size(); ++i)
            row("category_" + std::to_string(i), {QString::fromUtf8("Категория %1 · базовый XP").arg(QString::fromUtf8(Profile::kCategoryLabels[i])), QString::number(rules.categoryBaseXp[i])});
        row("focus_base", {QString::fromUtf8("Базовый фокус-бонус"), QString::number(rules.focusBaseBonus, 'f', 2)});
        row("focus_extra", {QString::fromUtf8("Дополнительный фокус-бонус"), QString::number(rules.focusAdditionalBonus, 'f', 2)});
        row("repeat", {QString::fromUtf8("Коэффициент повтора"), QString::number(rules.repeatRewardFactor, 'f', 2)});
        row("recovery", {QString::fromUtf8("Коэффициент прогрева"), QString::number(rules.recoveryRewardFactor, 'f', 2)});
        row("warmup", {QString::fromUtf8("Задач прогрева"), QString::number(rules.recoveryWarmupTasks)});
        summary_->setText(QString::fromUtf8("Правила применяются к будущим расчётам · накопленный прогресс не пересчитывается автоматически"));
    } else if (page == Vault) {
        headers({QString::fromUtf8("Время"), QString::fromUtf8("Действие"), QString::fromUtf8("Сумма"), QString::fromUtf8("Примечание")});
        const auto& vault = data.vault;
        for (int index = int(vault.log.size()) - 1; index >= 0; --index) {
            const auto& entry = vault.log[size_t(index)];
            row(std::to_string(index), {timeText(entry.timestamp), q(entry.action), QString::number(entry.amount, 'f', 2), q(entry.note)});
        }
        summary_->setText(QString::fromUtf8("Баланс хранилища: %1 %2 · журнал: %3 из %4 · Pomodoro: %5 монет, %6–%7")
            .arg(vault.balance, 0, 'f', 2).arg(q(vault.currencyCode)).arg(vault.log.size()).arg(vault.logLimit)
            .arg(vault.pomodoroCoinsPerCycle)
            .arg(QTime(vault.pomodoroStartMinutes / 60, vault.pomodoroStartMinutes % 60).toString("HH:mm"))
            .arg(QTime(vault.pomodoroEndMinutes / 60, vault.pomodoroEndMinutes % 60).toString("HH:mm")));
    } else if (page == Shortcuts) {
        headers({QString::fromUtf8("Название"), QString::fromUtf8("Путь"), QString::fromUtf8("Состояние")});
        for (const auto& shortcut : data.shortcuts) {
            std::error_code ec;
            const bool exists = std::filesystem::exists(std::filesystem::u8path(shortcut.path), ec) && !ec;
            row(shortcut.id, {q(shortcut.label), q(shortcut.path), QString::fromUtf8(exists ? "Доступен" : "Файл не найден")});
        }
        summary_->setText(QString::fromUtf8("Локальные ярлыки: %1 · запуск выполняется через системное приложение Windows").arg(data.shortcuts.size()));
    } else if (page == Banner) {
        headers({QString::fromUtf8("Фраза")});
        for (size_t index = 0; index < data.bannerTexts.size(); ++index) row(std::to_string(index), {q(data.bannerTexts[index])});
        summary_->setText(QString::fromUtf8("Фраз в ротации: %1 · смена каждые 60 секунд").arg(data.bannerTexts.size()));
    } else if (page == Cloud) {
        headers({QString::fromUtf8("Параметр"), QString::fromUtf8("Значение")});
        const auto config = LoadCloudSyncConfig(workspace_.directory);
        const auto root = ResolveCloudRootPath(config, workspace_.directory);
        std::error_code ec; const bool rootExists = std::filesystem::is_directory(root, ec) && !ec;
        row("enabled", {QString::fromUtf8("Конфигурация"), QString::fromUtf8(config.enabled ? "Включена" : "Выключена")});
        row("root", {QString::fromUtf8("Папка"), q(root.u8string())});
        row("ready", {QString::fromUtf8("Доступность папки"), QString::fromUtf8(rootExists ? "Готова" : "Не найдена")});
        row("auto", {QString::fromUtf8("Автосинхронизация"), QString::fromUtf8("pull: %1 · admin push: %2 · %3 · каждые %4 мин")
            .arg(config.autoPull ? QString::fromUtf8("да") : QString::fromUtf8("нет"))
            .arg(config.autoPush ? QString::fromUtf8("да") : QString::fromUtf8("нет"))
            .arg(config.enabled && config.autoSyncEnabled ? QString::fromUtf8("включена") : QString::fromUtf8("выключена"))
            .arg(std::clamp(config.autoSyncMinutes, 1, 120))});
        int driftCount = 0;
        if (config.enabled && rootExists) {
            const auto drift = InspectCloudWorkspaceDrift(config, workspace_.directory, 0); driftCount = drift.issueCount;
            row("drift", {QString::fromUtf8("Различия до первой синхронизации"), QString::number(drift.issueCount)});
        }
        const auto manifest = LoadCloudManifest(config, workspace_.directory);
        row("manifest", {QString::fromUtf8("Версия в manifest"), manifest.appVersion.empty() ? QString::fromUtf8("—") : q(manifest.appVersion)});
        row("clientVersion", {QString::fromUtf8("Версия Qt"), QString::fromUtf8(APP_VERSION)});
        const bool updateAvailable = IsUpdateAvailable(manifest, APP_VERSION);
        row("update", {QString::fromUtf8("Обновление клиента"), QString::fromUtf8(updateAvailable ? "Доступно" : "Нет новой версии")});
        row("release", {QString::fromUtf8("Установщик в manifest"), manifest.releaseFile.empty() ? QString::fromUtf8("—") : q(manifest.releaseFile)});
        if (!manifest.notes.empty()) row("releaseNotes", {QString::fromUtf8("Примечание к выпуску"), q(manifest.notes)});
        const auto releaseTarget = QtCloudReleaseTargetPath(workspace_.directory, manifest);
        std::error_code releaseError;
        const bool installerReady = releaseTarget && std::filesystem::is_regular_file(*releaseTarget, releaseError) && !releaseError &&
            !QFileInfo(QString::fromUtf8(releaseTarget->u8string())).isSymLink();
        cloudReleaseDownload_->setEnabled(config.enabled && rootExists && updateAvailable && bool(releaseTarget));
        cloudReleaseLaunch_->setEnabled(updateAvailable && installerReady);
        cloudPull_->setEnabled(config.enabled && rootExists);
        cloudPushPreview_->setEnabled(admin_ && config.enabled && rootExists);
        const bool hasBackups = !ListCloudWorkspaceBackups(workspace_.directory).empty();
        cloudResolve_->setEnabled((config.enabled && rootExists && driftCount > 0) || hasBackups);
        storageResolve_->setEnabled(admin_ && config.enabled && rootExists && HasQtStorageConflict(workspace_.directory));
        summary_->setText(QString::fromUtf8("Ручные pull и полный push требуют подтверждения и снимка облачных данных · автосинхронизация: %1 · интервал: %2 мин")
            .arg(config.enabled && config.autoSyncEnabled ? QString::fromUtf8("включена") : QString::fromUtf8("выключена"))
            .arg(std::clamp(config.autoSyncMinutes, 1, 120)));
    }
    if (summary_->text().isEmpty()) summary_->setText(QString::fromUtf8("Записей: %1 · просмотр данных существующего ядра").arg(table_->rowCount()));
    table_->horizontalHeader()->setStretchLastSection(false);
    if (page == AdminProfileStats) {
        constexpr int widths[] = {72, 150, 92, 62, 78, 136, 72, 72, 130};
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        for (int col = 0; col < std::min(table_->columnCount(), int(std::size(widths))); ++col)
            table_->setColumnWidth(col, widths[col]);
        if (table_->columnCount() > 1) table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    } else {
        table_->resizeColumnsToContents();
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        for (int col = 0; col < table_->columnCount(); ++col)
            table_->setColumnWidth(col, std::clamp(table_->columnWidth(col), 96, 280));
    }
    QStringList accessibleColumns;
    for (int col = 0; col < table_->columnCount(); ++col)
        if (!table_->isColumnHidden(col) && table_->horizontalHeaderItem(col))
            accessibleColumns << table_->horizontalHeaderItem(col)->text();
    table_->setAccessibleDescription(QString::fromUtf8("Строк: %1. Видимые столбцы: %2.")
        .arg(table_->rowCount()).arg(accessibleColumns.join(QString::fromUtf8(", "))));
    // Give free width to readable content instead of stretching the final numeric column.
    if (page != AdminProfileStats) {
        int stretchColumn = 0;
        if (page == Catalog) stretchColumn = 2;
        else if (page == Pipeline || page == Projects || page == Professions || page == Shortcuts) stretchColumn = 1;
        table_->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
    }
    table_->setSortingEnabled(page != Pipeline && page != Shortcuts);
    for (int index = 0; index < table_->rowCount(); ++index) {
        if (table_->item(index, 0)->data(Qt::UserRole).toString() == previous) { table_->selectRow(index); break; }
    }
    details();
}

void QtWindow::reapplyRules() {
    if (!requireAdmin() || navigation_->currentRow() != Rules) return;
    const auto profiles = workspace_.storage->list_profiles();
    if (profiles.empty()) { message(u8"Нет профилей для пересчёта."); return; }
    const int archived = int(std::count_if(profiles.begin(), profiles.end(), [](const auto& item) { return item.archived; }));
    QMessageBox confirm(QMessageBox::Question, QString::fromUtf8("Пересчитать профили"),
        QString::fromUtf8("Пересчитать уровни по текущей кривой, сохранив общий XP?\nПрофилей: %1, из них архивных: %2.")
            .arg(int(profiles.size())).arg(archived), QMessageBox::Yes | QMessageBox::No, this);
    confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Пересчитать"));
    confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
    confirm.setDefaultButton(QMessageBox::No);
    if (confirm.exec() != QMessageBox::Yes) return;
    AppContext context{workspace_.directory, *workspace_.storage, workspace_.catalog};
    const auto result = ReapplyRulesWithRecovery(context, u(profiles_->currentData().toString()));
    if (!result.ok) {
        const bool pending = std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction");
        appendLog(pending ? AppLogLevel::Error : AppLogLevel::Warning, "CoreProfileMutation",
            pending ? "Rules reapply recovery pending" : "Rules reapply failed or rolled back");
        message(result.errorMessage.empty() ? u8"Не удалось пересчитать профили." : result.errorMessage);
        return;
    }
    appendLog(AppLogLevel::Info, "CoreProfileMutation", "Rules reapply transaction committed");
    reload();
    statusBar()->showMessage(QString::fromUtf8("Профили пересчитаны: %1 · общий XP сохранён").arg(result.affectedProfiles), 5000);
}

void QtWindow::grantDirectXp() {
    if (!requireAdmin() || navigation_->currentRow() != ProfilePage) return;
    const auto profileId = u(profiles_->currentData().toString());
    if (profileId.empty()) { message(u8"Сначала выберите профиль."); return; }
    const auto skills = workspace_.catalog.skills();
    if (skills.empty()) { message(u8"Каталог навыков пуст."); return; }
    if (!workspace_.storage->set_active_profile(profileId)) { message(u8"Активный профиль недоступен."); return; }
    const auto profile = workspace_.storage->load_profile();
    if (!profile) { message(u8"Не удалось загрузить профиль."); return; }
    QDialog dialog(this);
    dialog.setObjectName("directXpDialog");
    dialog.setWindowTitle(QString::fromUtf8("Ручное начисление XP"));
    dialog.setMinimumWidth(440);
    auto* form = new QFormLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8(
        "Базовая сумма добавляется в общий XP. Навык получает эту сумму с бонусом активного достижения. Дух, повтор и прогрев здесь не применяются."));
    hint->setWordWrap(true); form->addRow(hint);
    auto* skill = new QComboBox; skill->setObjectName("directXpSkill");
    for (const auto& id : skills) skill->addItem(q(workspace_.catalog.display_name(id)), q(id));
    auto* amount = new QSpinBox; amount->setObjectName("directXpAmount"); amount->setRange(1, 100000000); amount->setValue(100);
    amount->setGroupSeparatorShown(true);
    auto* preview = new QLabel; preview->setObjectName("directXpPreview"); preview->setWordWrap(true);
    form->addRow(QString::fromUtf8("Навык"), skill);
    form->addRow(QString::fromUtf8("Базовый XP"), amount);
    form->addRow(QString::fromUtf8("Результат"), preview);
    auto updatePreview = [=] {
        const auto id = u(skill->currentData().toString());
        const double multiplier = profile->skill_bonus_multiplier(id, QDateTime::currentSecsSinceEpoch());
        const auto finalSkill = qRound64(double(amount->value()) * multiplier);
        preview->setText(QString::fromUtf8("Общий XP: +%1 · навык: +%2 XP%3")
            .arg(amount->value()).arg(finalSkill)
            .arg(multiplier > 1.000001 ? QString::fromUtf8(" · бонус достижения %1%").arg((multiplier - 1.0) * 100.0, 0, 'f', 1) : QString()));
    };
    connect(skill, &QComboBox::currentIndexChanged, &dialog, updatePreview);
    connect(amount, &QSpinBox::valueChanged, &dialog, updatePreview);
    updatePreview();
    auto* notice = new QLabel; notice->setObjectName("directXpNotice"); notice->setWordWrap(true); form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Начислить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        AppContext context{workspace_.directory, *workspace_.storage, workspace_.catalog};
        const auto result = GrantDirectSkillXpWithRecovery(context, profileId, profileId,
            u(skill->currentData().toString()), amount->value(), QDateTime::currentSecsSinceEpoch());
        if (!result.ok) {
            const bool pending = std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction");
            appendLog(pending ? AppLogLevel::Error : AppLogLevel::Warning, "CoreProfileMutation",
                pending ? "Direct skill XP recovery pending" : "Direct skill XP failed or rolled back");
            notice->setText(q(result.errorMessage)); return;
        }
        appendLog(AppLogLevel::Info, "CoreProfileMutation", "Direct skill XP transaction committed");
        dialog.setProperty("awardedGlobalXp", result.awardedGlobalXp);
        dialog.setProperty("awardedSkillXp", result.awardedSkillXp);
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const int globalXp = dialog.property("awardedGlobalXp").toInt();
    const int skillXp = dialog.property("awardedSkillXp").toInt();
    reload();
    statusBar()->showMessage(QString::fromUtf8("Начислено: общий XP +%1 · навык +%2 XP").arg(globalXp).arg(skillXp), 5000);
}

void QtWindow::adjustWallet() {
    if (!requireAdmin() || navigation_->currentRow() != ProfilePage) return;
    const auto profileId = u(profiles_->currentData().toString());
    if (profileId.empty()) { message(u8"Сначала выберите профиль."); return; }
    if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) {
        message(u8"Сначала завершите восстановление данных."); return;
    }
    if (!workspace_.storage->set_active_profile(profileId)) { message(u8"Активный профиль недоступен."); return; }
    const auto profile = workspace_.storage->load_profile();
    if (!profile) { message(u8"Не удалось загрузить профиль."); return; }

    QDialog dialog(this);
    dialog.setObjectName("walletAdjustmentDialog");
    dialog.setWindowTitle(QString::fromUtf8("Изменение кошелька профиля"));
    dialog.setMinimumWidth(420);
    auto* form = new QFormLayout(&dialog);
    form->addRow(QString::fromUtf8("Профиль"), new QLabel(q(profile->name())));
    auto* operation = new QComboBox;
    operation->setObjectName("walletOperation");
    operation->addItems({QString::fromUtf8("Начислить"), QString::fromUtf8("Списать")});
    auto* amount = new QDoubleSpinBox;
    amount->setObjectName("walletAmount");
    amount->setDecimals(2);
    amount->setRange(0.01, 1000000000.0);
    amount->setValue(1.0);
    amount->setGroupSeparatorShown(true);
    amount->setSuffix(QStringLiteral(" ") + q(workspace_.data.vault.currencyName.empty()
        ? (workspace_.data.vault.currencyCode.empty() ? std::string(u8"Кукоин") : workspace_.data.vault.currencyCode)
        : workspace_.data.vault.currencyName));
    auto* reason = new QLineEdit;
    reason->setObjectName("walletReason");
    reason->setMaxLength(120);
    reason->setPlaceholderText(QString::fromUtf8("Например: корректировка награды"));
    auto* preview = new QLabel;
    preview->setObjectName("walletPreview");
    preview->setWordWrap(true);
    auto* notice = new QLabel;
    notice->setObjectName("walletNotice");
    notice->setWordWrap(true);
    form->addRow(QString::fromUtf8("Операция"), operation);
    form->addRow(QString::fromUtf8("Сумма"), amount);
    form->addRow(QString::fromUtf8("Основание"), reason);
    form->addRow(QString::fromUtf8("Баланс"), preview);
    form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Применить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    const auto currency = amount->suffix();
    auto updatePreview = [=] {
        const bool debit = operation->currentIndex() == 1;
        const double value = amount->value();
        const double after = profile->wallet_balance() + (debit ? -value : value);
        preview->setText(QString::fromUtf8("%1 → %2%3")
            .arg(profile->wallet_balance(), 0, 'f', 2)
            .arg(after, 0, 'f', 2).arg(currency));
        buttons->button(QDialogButtonBox::Save)->setEnabled(!debit || value <= profile->wallet_balance() + 0.000001);
    };
    connect(operation, &QComboBox::currentIndexChanged, &dialog, updatePreview);
    connect(amount, &QDoubleSpinBox::valueChanged, &dialog, updatePreview);
    updatePreview();
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const bool debit = operation->currentIndex() == 1;
        const double value = amount->value();
        const auto memo = reason->text().trimmed();
        if (memo.isEmpty()) { notice->setText(QString::fromUtf8("Укажите основание операции для аудита.")); return; }
        if (debit && value > profile->wallet_balance() + 0.000001) {
            notice->setText(QString::fromUtf8("Сумма списания превышает баланс профиля.")); return;
        }
        QMessageBox confirm(QMessageBox::Question, QString::fromUtf8("Подтвердить операцию"),
            QString::fromUtf8("%1 %2 профилю «%3»?\nБаланс: %4 → %5\nОснование: %6")
                .arg(debit ? QString::fromUtf8("Списать") : QString::fromUtf8("Начислить"))
                .arg(currency.trimmed())
                .arg(q(profile->name()))
                .arg(profile->wallet_balance(), 0, 'f', 2)
                .arg(profile->wallet_balance() + (debit ? -value : value), 0, 'f', 2)
                .arg(memo), QMessageBox::Yes | QMessageBox::No, &dialog);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Подтвердить"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        const QString audit = QStringLiteral("%1|%2|%3")
            .arg(debit ? QStringLiteral("debit") : QStringLiteral("credit"))
            .arg(value, 0, 'f', 2).arg(memo);
        const auto result = runWalletMutationWithAudit(workspace_, profileId, profileId, false,
            "wallet_adjustment", u(audit), [&] {
                return AppAdjustProfileWallet(*workspace_.storage, profileId, profileId, debit ? -value : value);
            }, [this](AppLogLevel level, const std::string& event) { appendLog(level, "CoreWalletMutation", event); });
        if (!result.ok || !result.profile) {
            notice->setText(result.errorMessage.empty() ? QString::fromUtf8("Не удалось сохранить кошелёк.") : q(result.errorMessage));
            updatePreview();
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    reload();
    statusBar()->showMessage(QString::fromUtf8("Кошелёк профиля обновлён, запись добавлена в аудит."), 7000);
}

void QtWindow::showProfileHistory() {
    const auto profileId = u(profiles_->currentData().toString());
    if (navigation_->currentRow() != ProfilePage || profileId.empty() ||
        (!admin_ && !profileSession_.isUnlocked(*workspace_.storage, profileId))) return;

    QDialog dialog(this);
    dialog.setObjectName("profileActivityHistoryDialog");
    dialog.setWindowTitle(QString::fromUtf8("История профиля"));
    dialog.resize(820, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto exportHistoryTable = [this, &dialog, profileId](QTableWidget* source, const QString& kind) {
        if (!source) return;
        const QString reportsPath = QString::fromStdWString((workspace_.directory / "meta" / "reports").wstring());
        if (!QDir().mkpath(reportsPath)) { statusBar()->showMessage(QString::fromUtf8("Не удалось создать папку отчётов."), 5000); return; }
        const QString safeId = QString::fromUtf8(profileId.c_str()).replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
        QFileDialog picker(&dialog, QString::fromUtf8("Экспорт истории профиля"),
            QDir(reportsPath).filePath(QStringLiteral("profile-%1-%2-%3.csv")
                .arg(safeId, kind, QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))));
        picker.setAcceptMode(QFileDialog::AcceptSave);
        picker.setFileMode(QFileDialog::AnyFile);
        picker.setNameFilter(QString::fromUtf8("CSV-файлы (*.csv)"));
        picker.setDefaultSuffix("csv");
        if (picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty()) return;
        QString path = picker.selectedFiles().front();
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".csv");
        QByteArray payload("\xEF\xBB\xBF", 3);
        auto appendRecord = [&payload](const QStringList& cells) {
            QByteArray line;
            for (int column = 0; column < cells.size(); ++column) {
                if (column) line += ',';
                QByteArray value = cells[column].toUtf8();
                if (value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r')) {
                    value.replace("\"", "\"\"");
                    line += '"' + value + '"';
                } else line += value;
            }
            line += "\r\n";
            payload += line;
        };
        QStringList headers;
        for (int column = 0; column < source->columnCount(); ++column)
            headers << (source->horizontalHeaderItem(column) ? source->horizontalHeaderItem(column)->text() : QString());
        appendRecord(headers);
        for (int row = 0; row < source->rowCount(); ++row) {
            if (source->isRowHidden(row)) continue;
            QStringList values;
            for (int column = 0; column < source->columnCount(); ++column)
                values << (source->item(row, column) ? source->item(row, column)->text() : QString());
            appendRecord(values);
        }
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
            statusBar()->showMessage(QString::fromUtf8("Не удалось атомарно сохранить CSV истории."), 7000);
            return;
        }
        statusBar()->showMessage(QString::fromUtf8("CSV истории сохранён: %1").arg(QDir::toNativeSeparators(path)), 7000);
    };
    auto* tabs = new QTabWidget(&dialog);
    tabs->setObjectName("profileHistoryTabs");
    layout->addWidget(tabs, 1);

    auto* eventsPage = new QWidget(tabs);
    auto* eventsLayout = new QVBoxLayout(eventsPage);
    auto* summary = new QLabel(QString::fromUtf8("Локальные события входа, управления профилем и кошельком · читаются последние 500 событий аудита по рабочему пространству. История задач и XP ведётся отдельно."));
    summary->setObjectName("profileActivityHistorySummary");
    summary->setWordWrap(true);
    eventsLayout->addWidget(summary);
    auto* exportEvents = new QPushButton(QString::fromUtf8("Экспорт видимых событий в CSV"), eventsPage);
    exportEvents->setObjectName("exportProfileEvents");
    labelForAccessibility(exportEvents, QString::fromUtf8("Экспортировать события профиля в CSV"),
        QString::fromUtf8("В файл попадут строки, показанные в таблице событий."));
    eventsLayout->addWidget(exportEvents);
    auto* table = new QTableWidget(eventsPage);
    table->setObjectName("profileActivityHistoryTable");
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QString::fromUtf8("Дата"), QString::fromUtf8("Событие"), QString::fromUtf8("Детали")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(true);
    table->verticalHeader()->hide();
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(false);
    auto entries = profileAudit(workspace_.directory);
    std::reverse(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        if (entry.profile != profileId) continue;
        const int row = table->rowCount();
        table->insertRow(row);
        const QStringList values{timeText(entry.timestamp), profileAuditActionLabel(entry.action), q(entry.details).isEmpty() ? QString::fromUtf8("—") : q(entry.details)};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            table->setItem(row, column, item);
        }
    }
    summary->setText(QString::fromUtf8("Событий профиля: %1 · локальный аудит, последние 500 событий по рабочему пространству. История задач и XP ведётся отдельно.")
        .arg(table->rowCount()));
    table->setColumnWidth(0, 142);
    table->setColumnWidth(1, 210);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    eventsLayout->addWidget(table, 1);
    connect(exportEvents, &QPushButton::clicked, &dialog, [exportHistoryTable, table] {
        exportHistoryTable(table, QStringLiteral("events"));
    });
    tabs->addTab(eventsPage, QString::fromUtf8("События профиля"));

    auto* tasksPage = new QWidget(tabs);
    auto* tasksLayout = new QVBoxLayout(tasksPage);
    auto* taskSummary = new QLabel(tasksPage);
    taskSummary->setObjectName("profileTaskXpHistorySummary");
    taskSummary->setWordWrap(true);
    tasksLayout->addWidget(taskSummary);
    auto* taskFilter = new QLineEdit(tasksPage);
    taskFilter->setObjectName("profileTaskXpHistoryFilter");
    taskFilter->setClearButtonEnabled(true);
    taskFilter->setPlaceholderText(QString::fromUtf8("Фильтр по задаче или проекту"));
    auto* taskTools = new QHBoxLayout;
    taskTools->addWidget(taskFilter, 1);
    auto* exportTasks = new QPushButton(QString::fromUtf8("Экспорт CSV"), tasksPage);
    exportTasks->setObjectName("exportProfileTaskHistory");
    labelForAccessibility(exportTasks, QString::fromUtf8("Экспортировать видимую историю задач и XP в CSV"),
        QString::fromUtf8("Учитывает текущий фильтр по задаче и проекту."));
    taskTools->addWidget(exportTasks);
    tasksLayout->addLayout(taskTools);
    auto* taskTable = new QTableWidget(tasksPage);
    taskTable->setObjectName("profileTaskXpHistoryTable");
    taskTable->setColumnCount(7);
    taskTable->setHorizontalHeaderLabels({QString::fromUtf8("Дата задачи"), QString::fromUtf8("Проект"),
        QString::fromUtf8("Задача"), QString::fromUtf8("Статус"), QString::fromUtf8("Участие"),
        QString::fromUtf8("Общий XP"), QString::fromUtf8("XP навыков")});
    taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    taskTable->setAlternatingRowColors(true);
    taskTable->setShowGrid(false);
    taskTable->setWordWrap(false);
    taskTable->verticalHeader()->hide();
    taskTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    taskTable->horizontalHeader()->setStretchLastSection(false);
    struct ProfileTaskRow { std::int64_t createdAt; QStringList values; bool pending; };
    std::vector<ProfileTaskRow> taskRows;
    for (const auto& task : workspace_.data.tasks) {
        const auto participant = std::find_if(task.participants.begin(), task.participants.end(),
            [&](const auto& item) { return item.profileId == profileId; });
        const bool assigned = std::find(task.assignees.begin(), task.assignees.end(), profileId) != task.assignees.end();
        const bool hasRecordedXp = std::any_of(task.participants.begin(), task.participants.end(),
            [](const auto& item) { return item.globalXp > 0 || item.skillXp > 0; });
        const bool awaitingXp = AppNormalizeTaskStatus(task.status) == 2 && !hasRecordedXp && assigned;
        if (participant == task.participants.end() && !awaitingXp) continue;
        const auto project = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(),
            [&](const auto& item) { return !task.projectId.empty() && item.id == task.projectId; });
        const QString projectName = project == workspace_.data.projects.end() ? q(task.project) : q(project->name);
        const QString taskTitle = q(AppTaskDisplayTitle(task));
        const QString status = awaitingXp ? QString::fromUtf8("Выполнена · ждёт XP") : q(AppTaskStatusLabel(task.status));
        taskRows.push_back({task.createdAt, {timeText(task.createdAt), projectName.isEmpty() ? QString::fromUtf8("—") : projectName,
            taskTitle.isEmpty() ? QString::fromUtf8("Без названия") : taskTitle, status,
            participant == task.participants.end() ? QString::fromUtf8("Ожидает") : QString::fromUtf8("%1%").arg(participant->percent),
            participant == task.participants.end() ? QString::fromUtf8("—") : QString::number(participant->globalXp),
            participant == task.participants.end() ? QString::fromUtf8("—") : QString::number(participant->skillXp)}, awaitingXp});
    }
    std::stable_sort(taskRows.begin(), taskRows.end(), [](const auto& left, const auto& right) {
        return left.createdAt > right.createdAt;
    });
    auto renderTaskHistory = [taskRows, taskTable, taskFilter, taskSummary] {
        taskTable->setRowCount(0);
        int totalGlobalXp = 0, totalSkillXp = 0, pendingCount = 0;
        for (const auto& entry : taskRows) {
            if (!taskFilter->text().trimmed().isEmpty() &&
                !(entry.values[1] + QLatin1Char(' ') + entry.values[2]).contains(taskFilter->text().trimmed(), Qt::CaseInsensitive)) continue;
            const int row = taskTable->rowCount();
            taskTable->insertRow(row);
            for (int column = 0; column < entry.values.size(); ++column) {
                auto* item = new QTableWidgetItem(entry.values[column]);
                item->setToolTip(entry.values[column]);
                taskTable->setItem(row, column, item);
            }
            if (entry.pending) { ++pendingCount; continue; }
            totalGlobalXp += entry.values[5].toInt();
            totalSkillXp += entry.values[6].toInt();
        }
        taskSummary->setText(QString::fromUtf8("Записей задач: %1 · начислено XP: %2 общий / %3 навыков · ждут начисления: %4. Показываются задачи, сохранённые в текущем журнале.")
            .arg(taskTable->rowCount()).arg(totalGlobalXp).arg(totalSkillXp).arg(pendingCount));
    };
    connect(taskFilter, &QLineEdit::textChanged, &dialog, renderTaskHistory);
    renderTaskHistory();
    connect(exportTasks, &QPushButton::clicked, &dialog, [exportHistoryTable, taskTable] {
        exportHistoryTable(taskTable, QStringLiteral("tasks-xp"));
    });
    taskTable->setColumnWidth(0, 135);
    taskTable->setColumnWidth(1, 145);
    taskTable->setColumnWidth(3, 135);
    taskTable->setColumnWidth(4, 80);
    taskTable->setColumnWidth(5, 80);
    taskTable->setColumnWidth(6, 90);
    taskTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tasksLayout->addWidget(taskTable, 1);
    tabs->addTab(tasksPage, QString::fromUtf8("Задачи и XP"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void QtWindow::exportProfileReport(bool asCsv) {
    if (!requireAdmin() || navigation_->currentRow() != ProfilePage) return;
    const auto profileId = u(profiles_->currentData().toString());
    if (profileId.empty() || !workspace_.storage->set_active_profile(profileId)) {
        message(u8"Не удалось выбрать профиль для отчёта.");
        return;
    }
    const auto profile = workspace_.storage->load_profile();
    if (!profile) {
        message(u8"Не удалось загрузить профиль для отчёта.");
        return;
    }
    const auto reportDirectory = QString::fromStdWString((workspace_.directory / "meta" / "reports").wstring());
    if (!QDir().mkpath(reportDirectory)) {
        message(u8"Не удалось создать папку отчётов.");
        return;
    }
    const QString suffix = asCsv ? QStringLiteral("csv") : QStringLiteral("txt");
    const QString safeId = QString::fromUtf8(profileId.c_str()).replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    const QString defaultPath = QDir(reportDirectory).filePath(QStringLiteral("profile-%1-%2.%3")
        .arg(safeId, QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"), suffix));
    const QString filter = asCsv ? QString::fromUtf8("CSV (*.csv)") : QString::fromUtf8("Текстовый файл (*.txt)");
    const QString path = QFileDialog::getSaveFileName(this,
        asCsv ? QString::fromUtf8("Экспорт отчёта профиля в CSV") : QString::fromUtf8("Экспорт отчёта профиля в TXT"),
        defaultPath, filter);
    if (path.isEmpty()) return;
    QString outputPath = path;
    if (QFileInfo(outputPath).suffix().isEmpty()) outputPath += QStringLiteral(".") + suffix;
    QString error;
    if (!ExportProfileReport(outputPath, *profile, workspace_.catalog,
            QString::fromUtf8(profileId.c_str()), asCsv, QDateTime::currentSecsSinceEpoch(), &error)) {
        message(error.toUtf8().toStdString());
        return;
    }
    statusBar()->showMessage(QString::fromUtf8("Отчёт профиля сохранён: %1").arg(QDir::toNativeSeparators(outputPath)), 7000);
}

void QtWindow::exportStorageHealthReport() {
    if (!requireAdmin()) return;
    QString report;
    QString error;
    if (!BuildQtStorageHealthReport(workspace_.directory, workspace_.modules,
            QDateTime::currentSecsSinceEpoch(), &report, &error)) {
        message(error.toUtf8().toStdString());
        return;
    }
    const QString reportsPath = QString::fromStdWString((workspace_.directory / "meta" / "reports").wstring());
    if (!QDir().mkpath(reportsPath)) {
        message(u8"Не удалось создать папку отчётов.");
        return;
    }
    const QString defaultPath = QDir(reportsPath).filePath(QStringLiteral("storage-health-%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8("Расширенный отчёт хранилища"),
        defaultPath, QString::fromUtf8("Текстовый файл (*.txt)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".txt");
    if (!ExportQtStorageHealthReport(path, report, &error)) {
        message(error.toUtf8().toStdString());
        return;
    }
    appendLog(AppLogLevel::Info, "StorageHealthReport", "Read-only storage health report exported");
    statusBar()->showMessage(QString::fromUtf8("Отчёт хранилища сохранён: %1").arg(QDir::toNativeSeparators(path)), 7000);
}

void QtWindow::cleanupStrayStorage() {
    if (!requireAdmin()) return;
    std::vector<QtStorageStrayEntry> inventory;
    QString error;
    if (!BuildQtStorageStrayInventory(workspace_.directory, &inventory, &error)) {
        message(error.toUtf8().toStdString());
        return;
    }
    if (inventory.empty()) {
        statusBar()->showMessage(QString::fromUtf8("Лишние элементы в локальной Qt-копии не найдены."), 5000);
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName("storageCleanupDialog");
    dialog.setWindowTitle(QString::fromUtf8("Очистка локальной Qt-копии"));
    dialog.resize(720, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* warning = new QLabel(QString::fromUtf8(
        "Будут удалены только отмеченные элементы из изолированной локальной Qt-копии. "
        "Содержимое каждой папки показано отдельными строками; папка удаляется только когда отмечено всё её содержимое. "
        "Операция необратима. Если данные изменятся после сканирования, удаление будет отменено."), &dialog);
    warning->setObjectName("storageCleanupWarning");
    warning->setWordWrap(true);
    layout->addWidget(warning);
    int fileCount = 0, directoryCount = 0;
    std::uint64_t totalBytes = 0;
    for (const auto& entry : inventory) {
        if (entry.directory) ++directoryCount;
        else { ++fileCount; totalBytes += entry.size; }
    }
    auto* summary = new QLabel(QString::fromUtf8("Элементов: %1 · файлов: %2 · папок/ссылок: %3 · размер файлов: %4 байт")
        .arg(qulonglong(inventory.size())).arg(fileCount).arg(directoryCount).arg(qulonglong(totalBytes)), &dialog);
    summary->setObjectName("storageCleanupSummary");
    layout->addWidget(summary);
    auto* selectionTools = new QHBoxLayout;
    auto* selectAll = new QPushButton(QString::fromUtf8("Выбрать всё"), &dialog);
    selectAll->setObjectName("storageCleanupSelectAll");
    auto* selectNone = new QPushButton(QString::fromUtf8("Снять выбор"), &dialog);
    selectNone->setObjectName("storageCleanupSelectNone");
    selectionTools->addWidget(selectAll);
    selectionTools->addWidget(selectNone);
    selectionTools->addStretch();
    layout->addLayout(selectionTools);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("storageCleanupInventory");
    labelForAccessibility(list, QString::fromUtf8("Точный список лишних элементов локальной Qt-копии"),
        QString::fromUtf8("Снятие отметки сохраняет элемент. Отмеченные папки удаляются только при отметке всех вложенных элементов."));
    for (const auto& entry : inventory) {
        const QString relative = QString::fromUtf8(entry.relativePath.data(), int(entry.relativePath.size()));
        const QString kind = entry.reparsePoint ? QString::fromUtf8("ссылка")
            : entry.directory ? QString::fromUtf8("папка") : QString::fromUtf8("файл · %1 байт").arg(qulonglong(entry.size));
        auto* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(kind, relative), list);
        item->setData(Qt::UserRole, QString::fromUtf8(entry.relativePath.data(), int(entry.relativePath.size())));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setToolTip(relative);
    }
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* removeButton = buttons->addButton(QString::fromUtf8("Удалить отмеченные ( %1 )").arg(inventory.size()), QDialogButtonBox::AcceptRole);
    removeButton->setObjectName("storageCleanupConfirm");
    removeButton->setAutoDefault(false);
    removeButton->setDefault(false);
    auto* cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    cancelButton->setText(QString::fromUtf8("Отмена"));
    cancelButton->setObjectName("storageCleanupCancel");
    cancelButton->setDefault(true);
    labelForAccessibility(removeButton, QString::fromUtf8("Удалить отмеченные элементы"),
        QString::fromUtf8("Начинает необратимое удаление только выбранных путей после проверки, что список не изменился."));
    connect(selectAll, &QPushButton::clicked, &dialog, [list] {
        for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Checked);
    });
    connect(selectNone, &QPushButton::clicked, &dialog, [list] {
        for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Unchecked);
    });
    connect(list, &QListWidget::itemChanged, &dialog, [list, removeButton] {
        int selected = 0;
        for (int row = 0; row < list->count(); ++row) selected += list->item(row)->checkState() == Qt::Checked;
        removeButton->setText(QString::fromUtf8("Удалить отмеченные ( %1 )").arg(selected));
        removeButton->setEnabled(selected > 0);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    std::vector<std::string> approved;
    for (int row = 0; row < list->count(); ++row) {
        const auto* item = list->item(row);
        if (item->checkState() != Qt::Checked) continue;
        approved.push_back(item->data(Qt::UserRole).toString().toUtf8().toStdString());
    }
    int removed = 0;
    if (!RemoveQtStorageStrayEntries(workspace_.directory, inventory, approved, &removed, &error)) {
        if (removed > 0) appendLog(AppLogLevel::Warning, "StorageCleanup", "Approved stray cleanup stopped after a partial removal");
        message(error.toUtf8().toStdString());
        return;
    }
    appendLog(AppLogLevel::Info, "StorageCleanup", "Administrator removed explicitly approved stray workspace entries");
    statusBar()->showMessage(QString::fromUtf8("Удалено элементов локальной Qt-копии: %1").arg(removed), 7000);
}

void QtWindow::showWalletHistory() {
    const auto profileId = u(profiles_->currentData().toString());
    if (navigation_->currentRow() != ProfilePage || profileId.empty() ||
        (!admin_ && !profileSession_.isUnlocked(*workspace_.storage, profileId))) return;

    QDialog dialog(this);
    dialog.setObjectName("profileWalletHistoryDialog");
    dialog.setWindowTitle(QString::fromUtf8("История кошелька профиля"));
    dialog.resize(720, 400);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog);
    table->setObjectName("profileWalletHistoryTable");
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({QString::fromUtf8("Дата"), QString::fromUtf8("Операция"),
        QString::fromUtf8("Сумма"), QString::fromUtf8("Основание")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    const QString currency = q(workspace_.data.vault.currencyName.empty()
        ? (workspace_.data.vault.currencyCode.empty() ? std::string(u8"Кукоин") : workspace_.data.vault.currencyCode)
        : workspace_.data.vault.currencyName);
    auto entries = profileAudit(workspace_.directory);
    std::reverse(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        if (entry.profile != profileId) continue;
        QString operation, amount, reason;
        const auto details = q(entry.details);
        const auto parts = details.split('|');
        if (entry.action == "wallet_adjustment" || entry.action == "pomodoro_reward") {
            const bool pipeFormat = parts.size() >= 2;
            const auto direction = pipeFormat ? parts.value(0) : details.section(' ', 0, 0);
            bool amountOk = false;
            const double parsedAmount = pipeFormat ? parts.value(1).toDouble(&amountOk)
                : details.section(' ', 1, 1).toDouble(&amountOk);
            if (!amountOk) continue;
            const bool debit = direction == "debit";
            operation = entry.action == "pomodoro_reward" ? QString::fromUtf8("Награда Pomodoro")
                : debit ? QString::fromUtf8("Списание") : QString::fromUtf8("Начисление");
            amount = (debit ? "−" : "+") + QString::number(parsedAmount, 'f', 2) + " " + currency;
            reason = pipeFormat ? parts.mid(2).join('|') : details.section(' ', 2);
            if (entry.action == "pomodoro_reward" && reason == "pomodoro_focus")
                reason = QString::fromUtf8("Завершённый фокус");
        } else if (entry.action == "spirit_purchase") {
            const auto marker = details.indexOf("cost=");
            bool ok = false;
            const double value = marker >= 0 ? details.mid(marker + 5).toDouble(&ok) : 0.0;
            if (!ok) continue;
            operation = QString::fromUtf8("Снятие Злого духа");
            amount = "−" + QString::number(value, 'f', 2) + " " + currency;
            reason = QString::fromUtf8("Перевод в хранилище");
        } else continue;

        const int row = table->rowCount();
        table->insertRow(row);
        const QStringList values{timeText(entry.timestamp), operation, amount, reason};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            table->setItem(row, column, item);
        }
    }
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void QtWindow::closeEvent(QCloseEvent* event) {
    if (displaySettings_.minimizeToTray && trayIcon_ && trayIcon_->isVisible()) {
        event->ignore();
        hide();
        trayIcon_->showMessage(QString::fromUtf8("ForgeMirror работает в фоне"),
            QString::fromUtf8("Напоминания о сроках продолжаются. Для полного выхода выберите «Выход» в меню значка."),
            QSystemTrayIcon::Information, 5000);
        return;
    }
    QMainWindow::closeEvent(event);
}

void QtWindow::checkDeadlineReminders() {
    const bool background = !isVisible() && displaySettings_.minimizeToTray && trayIcon_ && trayIcon_->isVisible();
    if (!isVisible() && !background) return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto limit = now + 24 * 60 * 60;
    const TaskEntry* nearest = nullptr;
    for (const auto& task : workspace_.data.tasks) {
        if (task.deadlineAt <= now || task.deadlineAt > limit || AppNormalizeTaskStatus(task.status) == 2 ||
            remindedDeadlineTaskIds_.count(task.id)) continue;
        if (!nearest || task.deadlineAt < nearest->deadlineAt) nearest = &task;
    }
    if (!nearest) return;
    remindedDeadlineTaskIds_.insert(nearest->id);
    const auto title = AppTaskDisplayTitle(*nearest);
    const auto text = QString::fromUtf8("Срок задачи «%1» наступит %2.")
        .arg(q(title), timeText(nearest->deadlineAt));
    if (background) {
        trayIcon_->showMessage(QString::fromUtf8("ForgeMirror · срок задачи"), text,
            QSystemTrayIcon::Information, 10000);
        appendLog(AppLogLevel::Info, "Reminder", u(text));
    } else statusBar()->showMessage(text, 10000);
}

void QtWindow::checkMissedDeadlineReminders() {
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (now <= lastReminderCheckAt_) return;
    std::vector<const TaskEntry*> missed;
    for (const auto& task : workspace_.data.tasks) {
        if (task.deadlineAt > lastReminderCheckAt_ && task.deadlineAt <= now &&
            AppNormalizeTaskStatus(task.status) != 2) missed.push_back(&task);
    }
    std::sort(missed.begin(), missed.end(), [](const auto* left, const auto* right) {
        if (left->deadlineAt != right->deadlineAt) return left->deadlineAt < right->deadlineAt;
        return left->id < right->id;
    });
    if (missed.empty()) {
        lastReminderCheckAt_ = now;
        if (!saveReminderCheckAt(workspace_.directory, now) && !reminderStatePersistenceWarning_) {
            reminderStatePersistenceWarning_ = true;
            appendLog(AppLogLevel::Warning, "Reminder", "Не удалось сохранить время проверки пропущенных сроков; при следующем запуске возможен повтор.");
        }
        return;
    }
    constexpr size_t visibleLimit = 3;
    QStringList titles;
    for (size_t index = 0; index < std::min(missed.size(), visibleLimit); ++index)
        titles.push_back(q(AppTaskDisplayTitle(*missed[index])));
    if (missed.size() > visibleLimit)
        titles.push_back(QString::fromUtf8("и ещё %1").arg(missed.size() - visibleLimit));
    auto text = QString::fromUtf8("С прошлого запуска срок прошёл у %1 активных задач: %2")
        .arg(missed.size()).arg(titles.join(QString::fromUtf8(" · ")));
    const bool background = !isVisible() && displaySettings_.minimizeToTray && trayIcon_ && trayIcon_->isVisible();
    if (background) trayIcon_->showMessage(QString::fromUtf8("ForgeMirror · пропущенные сроки"), text,
        QSystemTrayIcon::Warning, 15000);
    else {
        const auto upcoming = statusBar()->currentMessage();
        if (upcoming.contains(QString::fromUtf8("Срок задачи"))) text = upcoming + QString::fromUtf8(" | ") + text;
        statusBar()->showMessage(text, 20000);
    }
    appendLog(AppLogLevel::Warning, "Reminder", u(text));
    lastReminderCheckAt_ = now;
    if (!saveReminderCheckAt(workspace_.directory, now) && !reminderStatePersistenceWarning_) {
        reminderStatePersistenceWarning_ = true;
        appendLog(AppLogLevel::Warning, "Reminder", "Не удалось сохранить время проверки пропущенных сроков; при следующем запуске возможен повтор.");
    }
}

void QtWindow::refreshAdminProfileStats() {
    if (!admin_) return;
    std::vector<QtAdminProfileStatsRow> rows;
    rows.reserve(workspace_.profiles.size());
    int unreadable = 0;
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const auto& info : workspace_.profiles) {
        try {
            const auto loaded = workspace_.storage->load_profile_snapshot(info.id, true);
            if (!loaded) { ++unreadable; continue; }
            QtAdminProfileStatsRow row;
            row.id = info.id;
            row.name = loaded->name();
            row.level = loaded->overall_level();
            row.totalXp = loaded->total_xp();
            row.lastTaskTimestamp = loaded->last_task_timestamp();
            row.recoveryTasksRemaining = loaded->recovery_tasks_remaining();
            row.achievementsTotal = int(loaded->achievements().size());
            row.achievementsActive = int(std::count_if(loaded->achievements().begin(), loaded->achievements().end(),
                [now](const Achievement& item) { return item.is_active(now); }));
            row.categoryScores = loaded->category_best_scores();
            row.archived = info.archived;
            rows.push_back(std::move(row));
        } catch (const std::exception&) {
            ++unreadable;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    adminStatsRows_ = std::move(rows);
    adminStatsUnreadableProfiles_ = unreadable;
    adminStatsLastRefresh_ = now;
}

void QtWindow::exportAdminProfileStats() {
    if (!requireAdmin() || navigation_->currentRow() != AdminProfileStats) return;
    if (adminStatsLastRefresh_ == 0) refreshAdminProfileStats();
    std::vector<QtAdminProfileStatsRow> rows;
    const auto query = adminStatsSearch_->text().trimmed();
    const int selectedRank = adminStatsRank_->currentIndex();
    for (const auto& item : adminStatsRows_) {
        if (!adminStatsArchived_->isChecked() && item.archived) continue;
        if (selectedRank > 0 && adminProfileRankIndex(item.level) != selectedRank - 1) continue;
        if (!query.isEmpty() && !QString::fromStdString(item.id + " " + item.name).contains(query, Qt::CaseInsensitive)) continue;
        rows.push_back(item);
    }
    if (rows.empty()) { statusBar()->showMessage(QString::fromUtf8("Нет профилей для экспорта с текущими фильтрами."), 5000); return; }
    QFileDialog dialog(this, QString::fromUtf8("Экспорт статистики профилей"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QString::fromUtf8("CSV-файлы (*.csv)"));
    dialog.setDefaultSuffix("csv");
    dialog.selectFile(QString::fromUtf8("ForgeMirror-profile-stats-%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    auto path = dialog.selectedFiles().front();
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";

    auto csvRecord = [](const QStringList& cells) {
        QByteArray bytes;
        for (int i = 0; i < cells.size(); ++i) {
            if (i) bytes += ',';
            auto value = cells[i].toUtf8();
            if (cells[i].contains(',') || cells[i].contains('"') || cells[i].contains('\n') || cells[i].contains('\r')) {
                value.replace("\"", "\"\"");
                bytes += '"' + value + '"';
            } else bytes += value;
        }
        bytes += "\r\n";
        return bytes;
    };
    QByteArray payload("\xEF\xBB\xBF", 3);
    const QStringList csvHeaders{"ID", "Name", "Level", "Rank", "TotalXP", "LastActivity", "InactiveFor", "Archived",
        "RecoveryTasks", "AchievementsTotal", "AchievementsActive"};
    payload += csvRecord(csvHeaders);
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const auto& item : rows) {
        const bool hasActivity = item.lastTaskTimestamp > 0;
        payload += csvRecord({q(item.id), q(item.name), QString::number(item.level), adminProfileRankName(item.level),
            QString::number(item.totalXp), hasActivity ? QDateTime::fromSecsSinceEpoch(item.lastTaskTimestamp).toString("yyyy-MM-dd HH:mm") : QString::fromUtf8("нет данных"),
            hasActivity ? elapsedProfileTime(now - item.lastTaskTimestamp) : QString::fromUtf8("—"), item.archived ? "yes" : "no",
            QString::number(item.recoveryTasksRemaining), QString::number(item.achievementsTotal), QString::number(item.achievementsActive)});
    }
    std::ostringstream teamReport;
    if (!WriteTeamValueReportCsv(teamReport, BuildTeamValueReport(workspace_.data.tasks, workspace_.data.projects, now))) {
        message(u8"Не удалось сформировать сводку командных метрик для CSV."); return;
    }
    const auto reportBytes = teamReport.str();
    payload += QByteArray::fromStdString(reportBytes);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (QFileInfo(path).isDir() || !file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        message(u8"Не удалось атомарно сохранить CSV статистики профилей."); return;
    }
    statusBar()->showMessage(QString::fromUtf8("Экспортировано профилей: %1 · %2")
        .arg(rows.size()).arg(QDir::toNativeSeparators(path)), 6000);
}

void QtWindow::exportReport() {
    if (!requireAdmin() || navigation_->currentRow() != Statistics) return;
    const auto suggested = QString::fromUtf8("ForgeMirror-report-%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFileDialog dialog(this, QString::fromUtf8("Экспорт управленческого отчёта"));
    dialog.setWindowTitle(QString::fromUtf8("Экспорт отчёта · %1")
        .arg(reportPeriodLabel(reportDateRange_->currentIndex(), reportFrom_->date(), reportTo_->date())));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QString::fromUtf8("CSV-файлы (*.csv)"));
    dialog.setDefaultSuffix("csv");
    dialog.selectFile(suggested);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    auto path = dialog.selectedFiles().front();
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    if (reportView_->currentIndex() >= 2) {
        QStringList columns;
        for (int column = 0; column < table_->columnCount(); ++column) {
            const auto* item = table_->horizontalHeaderItem(column);
            columns << (item ? item->text() : QString::fromUtf8("Колонка %1").arg(column + 1));
        }
        QVector<QStringList> rows;
        rows.reserve(table_->rowCount());
        for (int rowIndex = 0; rowIndex < table_->rowCount(); ++rowIndex) {
            QStringList values;
            for (int column = 0; column < table_->columnCount(); ++column) {
                const auto* item = table_->item(rowIndex, column);
                values << (item ? item->text() : QString());
            }
            rows.push_back(std::move(values));
        }
        QString error;
        if (!ExportQtTableCsv(path, columns, rows, &error)) { message(error.toUtf8().toStdString()); return; }
        statusBar()->showMessage(QString::fromUtf8("Сводный отчёт сохранён: %1").arg(QDir::toNativeSeparators(path)), 6000);
        return;
    }
    const auto tasks = reportTasksForRange(workspace_.data.tasks, reportDateRange_->currentIndex(),
        reportFrom_->date(), reportTo_->date());
    const auto report = BuildTeamValueReport(tasks, workspace_.data.projects, QDateTime::currentSecsSinceEpoch());
    QString error;
    QDate previousFrom, previousTo;
    const bool comparePrevious = reportCompare_->isChecked() && reportPreviousRange(reportDateRange_->currentIndex(),
        reportFrom_->date(), reportTo_->date(), &previousFrom, &previousTo);
    const bool exported = comparePrevious
        ? ExportTeamValueReportComparisonCsv(path, report, reportPeriodLabel(reportDateRange_->currentIndex(), reportFrom_->date(), reportTo_->date()),
            BuildTeamValueReport(reportTasksForRange(workspace_.data.tasks, 4, previousFrom, previousTo),
                workspace_.data.projects, QDateTime::currentSecsSinceEpoch()),
            QString::fromUtf8("%1–%2").arg(previousFrom.toString("dd.MM.yyyy"), previousTo.toString("dd.MM.yyyy")), &error)
        : ExportTeamValueReportCsv(path, report, &error);
    if (!exported) { message(error.toUtf8().toStdString()); return; }
    statusBar()->showMessage(QString::fromUtf8("Отчёт сохранён: %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void QtWindow::exportAudit() {
    if (navigation_->currentRow() != Audit || (!admin_ && auditSourceFilter_->currentIndex() != 1)) return;
    QStringList headers;
    headers.reserve(table_->columnCount());
    for (int column = 0; column < table_->columnCount(); ++column)
        headers << table_->horizontalHeaderItem(column)->text();
    QVector<QStringList> rows;
    rows.reserve(table_->rowCount());
    for (int index = 0; index < table_->rowCount(); ++index) {
        QStringList values;
        values.reserve(table_->columnCount());
        for (int column = 0; column < table_->columnCount(); ++column)
            values << table_->item(index, column)->text();
        if (!admin_ && values.value(0) != QString::fromUtf8("Задача")) {
            message(u8"Экспорт для пользователя может содержать только аудит задач.");
            return;
        }
        rows.push_back(std::move(values));
    }
    if (rows.isEmpty()) {
        statusBar()->showMessage(QString::fromUtf8("Нет видимых событий для экспорта."), 5000);
        return;
    }
    QFileDialog dialog(this, QString::fromUtf8("Экспорт видимых событий аудита"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QString::fromUtf8("CSV-файлы (*.csv)"));
    dialog.setDefaultSuffix("csv");
    dialog.selectFile(QString::fromUtf8("ForgeMirror-audit-%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    auto path = dialog.selectedFiles().front();
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    QString error;
    if (!ExportQtAuditCsv(path, headers, rows, &error)) {
        message(u(error));
        return;
    }
    statusBar()->showMessage(QString::fromUtf8("Экспортировано событий: %1 · %2")
        .arg(rows.size()).arg(QDir::toNativeSeparators(path)), 7000);
}

void QtWindow::exportTasks(bool textFormat) {
    if (navigation_->currentRow() != Tasks) return;
    QVector<const TaskEntry*> visibleTasks;
    visibleTasks.reserve(table_->rowCount());
    for (int index = 0; index < table_->rowCount(); ++index) {
        const auto id = u(table_->item(index, 0)->data(Qt::UserRole).toString());
        const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
            [&id](const auto& candidate) { return candidate.id == id; });
        if (task != workspace_.data.tasks.end()) visibleTasks.push_back(&*task);
    }
    if (visibleTasks.isEmpty()) {
        statusBar()->showMessage(QString::fromUtf8("Нет видимых задач для экспорта."), 5000);
        return;
    }
    const auto reportsDirectory = workspace_.directory / "meta" / "reports";
    std::error_code ec;
    std::filesystem::create_directories(reportsDirectory, ec);
    if (ec) { message("Не удалось создать каталог экспорта задач: " + ec.message()); return; }
    const QString suffix = textFormat ? QStringLiteral("txt") : QStringLiteral("csv");
    const auto filename = std::filesystem::u8path("tasks-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss").toStdString() + "." + suffix.toStdString());
    const auto suggestedPath = reportsDirectory / filename;
    const auto suggestedUtf8 = suggestedPath.u8string();
    const QString suggested = QString::fromUtf8(reinterpret_cast<const char*>(suggestedUtf8.data()), int(suggestedUtf8.size()));
    QFileDialog dialog(this, QString::fromUtf8("Экспорт видимых задач"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(textFormat ? QString::fromUtf8("Текстовые файлы (*.txt)") : QString::fromUtf8("CSV-файлы (*.csv)"));
    dialog.setDefaultSuffix(suffix);
    dialog.selectFile(suggested);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    auto path = dialog.selectedFiles().front();
    if (!path.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive)) path += QLatin1Char('.') + suffix;

    auto csvCell = [](QString value) {
        value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        if (value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r'))
            value = QLatin1Char('"') + value + QLatin1Char('"');
        return value;
    };
    auto timestamp = [](std::int64_t value) {
        return value > 0 ? QDateTime::fromSecsSinceEpoch(value).toString("yyyy-MM-dd HH:mm") : QStringLiteral("-");
    };
    auto participantLabels = [this](const TaskEntry& task) {
        QStringList labels;
        for (const auto& participant : task.participants) {
            const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(), [&](const auto& item) { return item.id == participant.profileId; });
            QString label = profile == workspace_.profiles.end() ? q(participant.profileId) : q(profile->name);
            if (participant.percent > 0) label += QStringLiteral(" %1%").arg(participant.percent);
            labels << label;
        }
        if (labels.isEmpty()) for (const auto& id : task.assignees) {
            const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(), [&](const auto& item) { return item.id == id; });
            labels << (profile == workspace_.profiles.end() ? q(id) : q(profile->name));
        }
        return labels.join(QStringLiteral(", "));
    };
    QByteArray bytes("\xEF\xBB\xBF", 3);
    if (!textFormat) bytes += "Date,Deadline,Project,PipelineStep,Task,Description,Status,Priority,Category,Score,Participants,BaseXP,BasePool\n";
    else bytes += QString::fromUtf8("Выполненные задачи\n").toUtf8();
    for (const auto* task : visibleTasks) {
        const auto project = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(), [&](const auto& item) { return !task->projectId.empty() && item.id == task->projectId; });
        const auto step = std::find_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(), [&](const auto& item) { return !task->pipelineStepId.empty() && item.id == task->pipelineStepId; });
        const QString projectName = project == workspace_.data.projects.end() ? q(task->project) : q(project->name);
        const QString stageName = step == workspace_.data.pipelineSteps.end() ? q(task->pipelineStep) : q(step->title);
        const QString created = timestamp(task->createdAt);
        const QString deadline = task->deadlineAt > 0 ? timestamp(task->deadlineAt) : QString::fromUtf8("без дедлайна");
        const QString participants = participantLabels(*task);
        const QString category = QString::fromUtf8(Profile::kCategoryLabels[std::clamp(task->category, 0, 4)]);
        if (!textFormat) {
            const QStringList fields{created, task->deadlineAt > 0 ? timestamp(task->deadlineAt) : QString(), projectName,
                stageName, q(task->title), q(task->description), q(AppTaskStatusLabel(task->status)),
                q(AppTaskPriorityLabel(task->priority)), category, QString::number(task->score), participants,
                QString::number(task->baseXp), QString::number(task->basePool)};
            QStringList escaped;
            for (const auto& field : fields) escaped << csvCell(field);
            bytes += (escaped.join(',') + QLatin1Char('\n')).toUtf8();
        } else {
            auto clean = [](QString value) { return value.replace('\r', ' ').replace('\n', ' ').replace('|', '/'); };
            const QStringList fields{created, deadline, projectName, stageName, q(task->title),
                q(AppTaskStatusLabel(task->status)), q(AppTaskPriorityLabel(task->priority)), category,
                QString::number(task->score) + QStringLiteral("/10"), participants};
            bytes += (QStringLiteral("- ") + fields.join(QStringLiteral(" | ")).replace('\r', ' ').replace('\n', ' ') + QLatin1Char('\n')).toUtf8();
            if (!task->description.empty()) bytes += (QStringLiteral("  ") + clean(q(task->description)) + QLatin1Char('\n')).toUtf8();
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        message("Не удалось атомарно сохранить экспорт задач: " + output.errorString().toStdString());
        return;
    }
    statusBar()->showMessage(QString::fromUtf8("Экспортировано видимых задач: %1 · %2")
        .arg(visibleTasks.size()).arg(QDir::toNativeSeparators(path)), 7000);
}

void QtWindow::exportLogs() {
    if (navigation_->currentRow() != Logs) return;
    const auto suggested = QString::fromUtf8("ForgeMirror-app-log-%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFileDialog dialog(this, QString::fromUtf8("Экспорт журнала Qt"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QString::fromUtf8("Текстовые файлы (*.txt)"));
    dialog.setDefaultSuffix("txt");
    dialog.selectFile(suggested);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    auto path = dialog.selectedFiles().front();
    if (!path.endsWith(".txt", Qt::CaseInsensitive)) path += ".txt";
    QByteArray bytes("\xEF\xBB\xBF", 3);
    int count = 0;
    int rowNumber = 0;
    const auto sourceFilter = logSourceFilter_->currentData().toString();
    for (auto it = appLogs_.rbegin(); it != appLogs_.rend(); ++it) {
        ++rowNumber;
        const bool enabled = it->level == AppLogLevel::Info ? logInfo_->isChecked()
            : it->level == AppLogLevel::Warning ? logWarnings_->isChecked() : logErrors_->isChecked();
        if (!sourceFilter.isEmpty() && q(it->source) != sourceFilter) continue;
        const QString level = it->level == AppLogLevel::Info ? QString::fromUtf8("Инфо")
            : it->level == AppLogLevel::Warning ? QString::fromUtf8("Предупреждение") : QString::fromUtf8("Ошибка");
        const QStringList searchableValues{QString::number(rowNumber), timeText(it->timestamp), level, q(it->source), q(it->message)};
        if (!enabled || !searchableValues.join(' ').contains(search_->text(), Qt::CaseInsensitive)) continue;
        const QStringList values{QString::number(count + 1), searchableValues[1], searchableValues[2], searchableValues[3], searchableValues[4]};
        auto clean = [](QString value) {
            return value.replace('\r', ' ').replace('\n', ' ').replace('|', '/');
        };
        bytes += QStringLiteral("%1 | %2 | %3 | %4 | %5\n")
            .arg(count + 1).arg(clean(values[1]), clean(values[2]), clean(values[3]), clean(values[4])).toUtf8();
        ++count;
    }
    if (count == 0) {
        statusBar()->showMessage(QString::fromUtf8("Нет видимых записей для экспорта."), 5000);
        return;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        message(u8"Не удалось сохранить журнал Qt.");
        return;
    }
    statusBar()->showMessage(QString::fromUtf8("Экспортировано записей: %1 · %2")
        .arg(count).arg(QDir::toNativeSeparators(path)), 7000);
}

void QtWindow::details() {
    const auto id = u(selectedId());
    details_->clear();
    changeStatus_->setEnabled(!id.empty());
    editEntry_->setEnabled(!id.empty());
    deleteEntry_->setEnabled(!id.empty());
    moveUp_->setEnabled(false);
    moveDown_->setEnabled(false);
    openShortcut_->setEnabled(false);
    advanceStage_->setEnabled(!id.empty());
    const int page = navigation_->currentRow();
    if (page == Pipeline) {
        const auto step = std::find_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
            [&](const auto& item) { return item.id == id; });
        if (step != workspace_.data.pipelineSteps.end() &&
            std::count_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(), [&](const auto& item) { return item.id == id; }) == 1) {
            const auto index = std::distance(workspace_.data.pipelineSteps.begin(), step);
            moveUp_->setEnabled(index > 0);
            moveDown_->setEnabled(index + 1 < std::ptrdiff_t(workspace_.data.pipelineSteps.size()));
        }
    } else if (page == Shortcuts) {
        const auto found = std::find_if(workspace_.data.shortcuts.begin(), workspace_.data.shortcuts.end(),
            [&](const auto& item) { return item.id == id; });
        if (found != workspace_.data.shortcuts.end()) {
            const auto index = std::distance(workspace_.data.shortcuts.begin(), found);
            moveUp_->setEnabled(index > 0);
            moveDown_->setEnabled(index + 1 < std::ptrdiff_t(workspace_.data.shortcuts.size()));
            std::error_code ec;
            openShortcut_->setEnabled(std::filesystem::exists(std::filesystem::u8path(found->path), ec) && !ec);
        }
    }
    if (page == Tasks) {
        for (const auto& task : workspace_.data.tasks) if (task.id == id) {
            QStringList assignees;
            for (const auto& value : task.assignees) assignees << q(value);
            std::string xp;
            for (const auto& participant : task.participants) {
                const auto found = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(),
                    [&](const auto& profile) { return profile.id == participant.profileId; });
                xp += (found == workspace_.profiles.end() ? participant.profileId : found->name) + " (" +
                    std::to_string(participant.percent) + "%): " + std::to_string(participant.globalXp) +
                    u8" XP, навыки " + std::to_string(participant.skillXp) + " XP\n";
            }
            details_->setHtml(field(QString::fromUtf8("Задача"), task.title) + field(QString::fromUtf8("Описание"), task.description)
                + field(QString::fromUtf8("Исполнители"), u(assignees.join(", ")))
                + field(QString::fromUtf8("Категория"), Profile::kCategoryLabels[std::clamp(task.category, 0, 4)])
                + field(QString::fromUtf8("Штраф за срок"), std::to_string(task.deadlinePenaltyPercent) + "%")
                + (xp.empty() ? QString() : field(QString::fromUtf8("Начисленный XP"), xp)));
        }
    } else if (page == Statistics && table_->currentRow() >= 0) {
        const auto targetId = selectedId();
        const auto targetKey = u(targetId);
        const bool employeeView = reportView_->currentIndex() == 1;
        const bool stageView = reportView_->currentIndex() == 2;
        const bool categoryView = reportView_->currentIndex() == 3;
        const bool statusView = reportView_->currentIndex() == 4;
        const bool priorityView = reportView_->currentIndex() == 5;
        const bool deadlineView = reportView_->currentIndex() == 6;
        const int statusKey = statusView && targetKey.rfind("__status_", 0) == 0
            ? std::clamp(std::atoi(targetKey.c_str() + 9), 0, 2) : -1;
        const int priorityKey = priorityView && targetKey.rfind("__priority_", 0) == 0
            ? std::clamp(std::atoi(targetKey.c_str() + 11), 0, 3) : -1;
        const int deadlineKey = deadlineView && targetKey.rfind("__deadline_", 0) == 0
            ? std::clamp(std::atoi(targetKey.c_str() + 11), 0, 3) : -1;
        const auto targetName = table_->item(table_->currentRow(), 0)->text();
        const auto currentTasks = reportTasksForRange(workspace_.data.tasks, reportDateRange_->currentIndex(),
            reportFrom_->date(), reportTo_->date());
        QDate previousFrom, previousTo;
        const bool comparePrevious = reportCompare_->isChecked() && reportPreviousRange(reportDateRange_->currentIndex(),
            reportFrom_->date(), reportTo_->date(), &previousFrom, &previousTo);
        const auto previousTasks = comparePrevious ? reportTasksForRange(workspace_.data.tasks, 4, previousFrom, previousTo)
                                                  : std::vector<TaskEntry>{};
        std::unordered_set<std::string> currentTaskIds;
        for (const auto& task : currentTasks) currentTaskIds.insert(task.id);
        auto reportTasks = currentTasks;
        reportTasks.insert(reportTasks.end(), previousTasks.begin(), previousTasks.end());
        const QString groupLabel = employeeView ? QString::fromUtf8("Сотрудник")
            : stageView ? QString::fromUtf8("Этап пайплайна")
            : categoryView ? QString::fromUtf8("Категория")
            : statusView ? QString::fromUtf8("Статус задачи")
            : priorityView ? QString::fromUtf8("Приоритет")
            : deadlineView ? QString::fromUtf8("Состояние срока") : QString::fromUtf8("Проект");
        QString html = field(groupLabel, u(targetName));
        std::vector<TaskEntry> matchedTasks;
        std::vector<TaskEntry> matchedCurrent, matchedPrevious;
        for (const auto& task : reportTasks) {
            bool belongs = false;
            if (employeeView) {
                belongs = std::find(task.assignees.begin(), task.assignees.end(), targetKey) != task.assignees.end() ||
                    std::any_of(task.participants.begin(), task.participants.end(), [&](const auto& participant) {
                        return participant.profileId == targetKey;
                    });
            } else if (stageView) {
                belongs = reportStageKey(task) == targetKey;
            } else if (categoryView) {
                belongs = reportCategoryKey(task) == targetKey;
            } else if (statusView) {
                belongs = statusKey >= 0 && AppNormalizeTaskStatus(task.status) == statusKey;
            } else if (priorityView) {
                belongs = priorityKey >= 0 && AppNormalizeTaskPriority(task.priority) == priorityKey;
            } else if (deadlineView) {
                belongs = deadlineKey >= 0 && reportDeadlineGroup(task, QDateTime::currentSecsSinceEpoch()) == deadlineKey;
            } else {
                const std::string projectKey = !task.projectId.empty() ? task.projectId :
                    (task.project.empty() ? "__no_project" : "name:" + task.project);
                belongs = projectKey == targetKey;
            }
            if (!belongs) continue;
            matchedTasks.push_back(task);
            if (currentTaskIds.count(task.id)) matchedCurrent.push_back(task);
            else matchedPrevious.push_back(task);
        }
        const auto groupReport = BuildTeamValueReport(matchedCurrent, workspace_.data.projects, QDateTime::currentSecsSinceEpoch());
        const auto previousGroupReport = BuildTeamValueReport(matchedPrevious, workspace_.data.projects, QDateTime::currentSecsSinceEpoch());
        int groupGlobalXp = groupReport.totalGlobalXp, groupSkillXp = groupReport.totalSkillXp;
        if (employeeView) {
            const auto metric = std::find_if(groupReport.assignees.begin(), groupReport.assignees.end(),
                [&](const auto& item) { return item.profileId == targetKey; });
            groupGlobalXp = metric == groupReport.assignees.end() ? 0 : metric->totalGlobalXp;
            groupSkillXp = metric == groupReport.assignees.end() ? 0 : metric->totalSkillXp;
        }
        html += QString::fromUtf8("<h3>Показатели выбранной группы</h3>");
        html += field(QString::fromUtf8("Задач"), std::to_string(groupReport.totalTasks));
        html += field(QString::fromUtf8("Статусы: новые / в работе / завершены"),
            std::to_string(groupReport.newTasks) + " / " + std::to_string(groupReport.inProgressTasks) + " / " + std::to_string(groupReport.doneTasks));
        html += field(QString::fromUtf8("Просрочено / ожидают XP"),
            std::to_string(groupReport.overdueTasks) + " / " + std::to_string(groupReport.xpPendingTasks));
        html += field(QString::fromUtf8("Глобальный XP / XP навыков"),
            std::to_string(groupGlobalXp) + " / " + std::to_string(groupSkillXp));
        if (comparePrevious) {
            int oldGlobalXp = previousGroupReport.totalGlobalXp, oldSkillXp = previousGroupReport.totalSkillXp;
            if (employeeView) {
                const auto metric = std::find_if(previousGroupReport.assignees.begin(), previousGroupReport.assignees.end(),
                    [&](const auto& item) { return item.profileId == targetKey; });
                oldGlobalXp = metric == previousGroupReport.assignees.end() ? 0 : metric->totalGlobalXp;
                oldSkillXp = metric == previousGroupReport.assignees.end() ? 0 : metric->totalSkillXp;
            }
            html += QString::fromUtf8("<h3>Предыдущий период · %1–%2</h3>")
                .arg(previousFrom.toString("dd.MM.yyyy"), previousTo.toString("dd.MM.yyyy"));
            html += field(QString::fromUtf8("Задач"), std::to_string(previousGroupReport.totalTasks));
            html += field(QString::fromUtf8("Статусы: новые / в работе / завершены"),
                std::to_string(previousGroupReport.newTasks) + " / " + std::to_string(previousGroupReport.inProgressTasks) + " / " + std::to_string(previousGroupReport.doneTasks));
            html += field(QString::fromUtf8("Просрочено / ожидают XP"),
                std::to_string(previousGroupReport.overdueTasks) + " / " + std::to_string(previousGroupReport.xpPendingTasks));
            html += field(QString::fromUtf8("Глобальный XP / XP навыков"), std::to_string(oldGlobalXp) + " / " + std::to_string(oldSkillXp));
        }
        if (matchedTasks.empty()) {
            html += QString::fromUtf8("<p>В выбранном периоде связанных задач нет.</p>");
        } else {
            html += QString::fromUtf8("<h3>Задачи в выбранном периоде</h3>");
        }
        for (const auto& task : matchedTasks) {
            int globalXp = 0, skillXp = 0;
            for (const auto& participant : task.participants) {
                if (employeeView && participant.profileId != targetKey) continue;
                globalXp += std::max(0, participant.globalXp);
                skillXp += std::max(0, participant.skillXp);
            }
            const auto project = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(),
                [&](const auto& item) { return !task.projectId.empty() && item.id == task.projectId; });
            const auto stage = std::find_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
                [&](const auto& item) { return !task.pipelineStepId.empty() && item.id == task.pipelineStepId; });
            QStringList involved;
            auto addProfile = [&](const std::string& profileId) {
                if (profileId.empty()) return;
                const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(),
                    [&](const auto& item) { return item.id == profileId; });
                const auto label = profile == workspace_.profiles.end() ? q(profileId) : q(profile->name);
                if (!involved.contains(label)) involved.push_back(label);
            };
            for (const auto& profileId : task.assignees) addProfile(profileId);
            for (const auto& participant : task.participants) addProfile(participant.profileId);
            const auto projectName = project != workspace_.data.projects.end() ? project->name
                : !task.project.empty() ? task.project
                : !task.projectId.empty() ? task.projectId : u8"Без проекта";
            const auto stageName = stage != workspace_.data.pipelineSteps.end() ? stage->title
                : !task.pipelineStep.empty() ? task.pipelineStep : u8"Без этапа";
            const auto periodPrefix = comparePrevious
                ? (currentTaskIds.count(task.id) ? QString::fromUtf8("Текущий · ") : QString::fromUtf8("Предыдущий · "))
                : QString();
            const QString taskTitle = QString::fromUtf8("%1%2 · %3 · %4")
                .arg(periodPrefix, q(AppTaskDisplayTitle(task)), q(AppTaskStatusLabel(task.status)), q(AppTaskPriorityLabel(task.priority)));
            html += field(taskTitle, u(timeText(task.createdAt)) + u8" · срок: " + timeText(task.deadlineAt).toUtf8().toStdString())
                + field(QString::fromUtf8("Проект / этап"), projectName + u8" / " + stageName)
                + field(QString::fromUtf8("Исполнители и участники"), u(involved.join(QString::fromUtf8(", "))))
                + field(QString::fromUtf8("XP: глобальный / навыки"), std::to_string(globalXp) + " / " + std::to_string(skillXp));
        }
        details_->setHtml(html);
    } else if (page == Pipeline) {
        for (const auto& step : workspace_.data.pipelineSteps) if (step.id == id)
            details_->setHtml(field(QString::fromUtf8("Описание"), step.description) + field(QString::fromUtf8("Вход"), step.input)
                + field(QString::fromUtf8("Выход"), step.output) + field(QString::fromUtf8("Готово, когда"), step.doneCriteria)
                + field(QString::fromUtf8("Риск"), step.risk));
    } else if (table_->currentRow() >= 0) {
        QString html;
        for (int col = 0; col < table_->columnCount(); ++col)
            html += field(table_->horizontalHeaderItem(col)->text(), u(table_->item(table_->currentRow(), col)->text()));
        details_->setHtml(html);
    }
}

void QtWindow::createEntry(bool edit) {
    if (navigation_->currentRow() == Cloud) {
        if (!edit && ShowCloudSettings(this, workspace_.directory)) render();
        return;
    }
    if (navigation_->currentRow() == Shortcuts) {
        if (edit) return;
        QDialog dialog(this); dialog.setObjectName("shortcutEditor"); dialog.setWindowTitle(QString::fromUtf8("Добавить ярлык")); dialog.setMinimumWidth(520);
        auto* form = new QFormLayout(&dialog);
        auto* hint = new QLabel(QString::fromUtf8("Выберите существующий локальный файл. ForgeMirror хранит только название и путь, сам файл не копируется."));
        hint->setWordWrap(true); form->addRow(hint);
        auto* label = new QLineEdit; label->setObjectName("shortcutLabel"); label->setMaxLength(96);
        auto* path = new QLineEdit; path->setObjectName("shortcutPath"); path->setReadOnly(true);
        auto* browse = new QPushButton(QString::fromUtf8("Выбрать файл…")); browse->setObjectName("shortcutBrowse");
        form->addRow(QString::fromUtf8("Название"), label); form->addRow(QString::fromUtf8("Путь"), path); form->addRow(browse);
        auto* notice = new QLabel; notice->setObjectName("shortcutNotice"); notice->setWordWrap(true); form->addRow(notice);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Добавить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
        connect(browse, &QPushButton::clicked, &dialog, [&] {
            QFileDialog picker(&dialog, QString::fromUtf8("Выберите файл ярлыка")); picker.setOption(QFileDialog::DontUseNativeDialog);
            picker.setFileMode(QFileDialog::ExistingFile);
            if (picker.exec() == QDialog::Accepted && !picker.selectedFiles().isEmpty()) path->setText(QDir::toNativeSeparators(picker.selectedFiles().front()));
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            const auto result = AppAddShortcut(workspace_.directory, workspace_.data.shortcuts,
                u(label->text().trimmed()), u(QDir::fromNativeSeparators(path->text().trimmed())));
            if (!result.ok) { notice->setText(q(result.errorMessage)); return; }
            dialog.accept();
        });
        if (dialog.exec() == QDialog::Accepted) { reload(); statusBar()->showMessage(QString::fromUtf8("Ярлык добавлен"), 3000); }
        return;
    }
    if (!requireAdmin()) return;
    if (edit && selectedId().isEmpty()) return;
    if (navigation_->currentRow() == Banner) {
        bool ok = false; const int index = edit ? selectedId().toInt(&ok) : -1;
        if (edit && !ok) return;
        if (ShowBannerEditor(this, workspace_, index)) { bannerIndex_ = std::max(0, index); updateBanner(); render(); }
        return;
    }
    if (navigation_->currentRow() == Rules) {
        if (ShowRulesEditor(this, workspace_)) reload();
        return;
    }
    if (navigation_->currentRow() == Vault) {
        if (ShowVaultEditor(this, workspace_)) reload();
        return;
    }
    if (navigation_->currentRow() == Professions) {
        if (ShowProfessionEditor(this, workspace_, edit ? u(selectedId()) : std::string(),
                                 u(profiles_->currentData().toString()))) {
            appendLog(AppLogLevel::Info, "CoreCatalogMutation", edit
                ? "Profession edit committed" : "Profession creation committed");
            reload();
        }
        return;
    }
    if (navigation_->currentRow() == Pipeline) {
        if (ShowPipelineEditor(this, workspace_, edit ? u(selectedId()) : std::string())) {
            appendLog(AppLogLevel::Info, "CoreCatalogMutation", edit
                ? "Pipeline stage edit committed" : "Pipeline stage creation committed");
            reload();
        }
        return;
    }
    if (navigation_->currentRow() == Catalog) {
        if (ShowSkillEditor(this, workspace_, edit ? u(selectedId()) : std::string(),
                            u(profiles_->currentData().toString()))) {
            appendLog(AppLogLevel::Info, "CoreCatalogMutation", edit
                ? "Skill edit committed" : "Skill creation committed");
            reload();
        }
        return;
    }
    if (navigation_->currentRow() == ProfilePage) {
        ShowProfileManager(this, workspace_, profiles_->currentData().toString());
        reload();
        return;
    }
    const bool projectMode = navigation_->currentRow() == Projects;
    if (!projectMode && navigation_->currentRow() != Tasks) return;
    const auto foundTask = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
        [&](const auto& entry) { return entry.id == u(selectedId()); });
    if (edit && !projectMode && foundTask == workspace_.data.tasks.end()) return;
    const TaskEntry originalTask = edit && !projectMode ? *foundTask : TaskEntry{};
    const auto projectId = edit ? u(selectedId()) : std::string();
    const auto foundProject = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(),
        [&](const auto& entry) { return entry.id == projectId; });
    if (edit && projectMode && foundProject == workspace_.data.projects.end()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(projectMode ? QString::fromUtf8("Новый проект") : QString::fromUtf8("Новая задача"));
    if (edit) dialog.setWindowTitle(QString::fromUtf8(projectMode ? "Редактирование проекта" : "Редактирование задачи"));
    dialog.setObjectName(projectMode ? "projectEditor" : "taskEditor");
    dialog.setMinimumWidth(480);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit;
    name->setObjectName("entryTitle");
    auto* description = new QPlainTextEdit;
    description->setMaximumHeight(96);
    description->setObjectName("entryDescription");
    if (edit) {
        name->setText(q(projectMode ? foundProject->name : originalTask.title));
        description->setPlainText(q(projectMode ? foundProject->description : originalTask.description));
    }
    form->addRow(QString::fromUtf8("Название"), name);
    form->addRow(QString::fromUtf8("Описание"), description);
    auto* project = new QComboBox;
    auto* priority = new QComboBox;
    auto* category = new QComboBox;
    auto* pipeline = new QComboBox;
    auto* deadline = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1));
    auto* hasDeadline = new QCheckBox(QString::fromUtf8("Указать срок"));
    auto* assignees = new QListWidget;
    auto* skills = new QListWidget;
    auto* penalty = new QSpinBox;
    project->setObjectName("taskProject");
    priority->setObjectName("taskPriority");
    category->setObjectName("taskCategory");
    pipeline->setObjectName("taskPipeline");
    deadline->setObjectName("taskDeadline");
    hasDeadline->setObjectName("taskHasDeadline");
    assignees->setObjectName("taskAssignees");
    skills->setObjectName("taskSkills");
    penalty->setObjectName("taskPenalty");
    // Parent optional controls to the dialog even in the project-only form.
    for (QWidget* control : std::initializer_list<QWidget*>{project, priority, category, pipeline, deadline, hasDeadline, assignees, skills, penalty}) {
        control->setParent(&dialog);
        control->setVisible(!projectMode);
    }
    if (!projectMode) {
        project->addItem(QString::fromUtf8("Без проекта"), "");
        for (const auto& item : workspace_.data.projects) project->addItem(q(item.name), q(item.id));
        for (int i = 0; i < 3; ++i) priority->addItem(q(AppTaskPriorityLabel(i)), i);
        priority->setCurrentIndex(1);
        for (auto label : Profile::kCategoryLabels) category->addItem(label);
        pipeline->addItem(QString::fromUtf8("Без этапа"), "");
        for (const auto& step : workspace_.data.pipelineSteps) pipeline->addItem(q(step.title), q(step.id));
        deadline->setCalendarPopup(true);
        deadline->setDisplayFormat("dd.MM.yyyy HH:mm");
        deadline->setEnabled(false);
        connect(hasDeadline, &QCheckBox::toggled, deadline, &QWidget::setEnabled);
        penalty->setRange(0, 100);
        penalty->setSuffix(" %");
        for (const auto& info : workspace_.profiles) if (!info.archived) {
            auto* item = new QListWidgetItem(q(info.name), assignees);
            item->setData(Qt::UserRole, q(info.id));
            item->setCheckState(Qt::Unchecked);
        }
        for (const auto& id : workspace_.catalog.skills()) {
            auto* item = new QListWidgetItem(q(workspace_.catalog.display_name(id)), skills);
            item->setData(Qt::UserRole, q(id));
            item->setCheckState(Qt::Unchecked);
        }
        if (edit) {
            auto selectReference = [&](QComboBox* box, const std::string& id, const std::string& label) {
                int index = box->findData(q(id));
                if (index < 0 || (id.empty() && !label.empty())) {
                    box->addItem(q(label.empty() ? id : label), q(id));
                    index = box->count() - 1;
                }
                box->setCurrentIndex(index);
            };
            selectReference(project, originalTask.projectId, originalTask.project);
            selectReference(pipeline, originalTask.pipelineStepId, originalTask.pipelineStep);
            priority->setCurrentIndex(originalTask.priority);
            category->setCurrentIndex(originalTask.category);
            penalty->setValue(originalTask.deadlinePenaltyPercent);
            hasDeadline->setChecked(originalTask.deadlineAt > 0);
            if (originalTask.deadlineAt > 0) deadline->setDateTime(QDateTime::fromSecsSinceEpoch(originalTask.deadlineAt));
            auto selectIds = [&](QListWidget* list, const std::vector<std::string>& ids) {
                for (const auto& id : ids) {
                    QListWidgetItem* found = nullptr;
                    for (int i = 0; i < list->count(); ++i)
                        if (list->item(i)->data(Qt::UserRole).toString() == q(id)) found = list->item(i);
                    if (!found) {
                        found = new QListWidgetItem(q(id) + QString::fromUtf8(" · недоступен"), list);
                        found->setData(Qt::UserRole, q(id));
                    }
                    found->setCheckState(Qt::Checked);
                }
            };
            selectIds(assignees, originalTask.assignees);
            selectIds(skills, originalTask.skillIds);
            if (!originalTask.participants.empty()) {
                for (QWidget* control : std::initializer_list<QWidget*>{category, penalty, assignees, skills}) {
                    control->setEnabled(false);
                    control->setToolTip(QString::fromUtf8("Зафиксировано при начислении XP"));
                }
            }
        }
        assignees->setMaximumHeight(96);
        skills->setMaximumHeight(96);
        form->addRow(QString::fromUtf8("Проект"), project);
        form->addRow(QString::fromUtf8("Приоритет"), priority);
        form->addRow(QString::fromUtf8("Категория"), category);
        form->addRow(QString::fromUtf8("Этап"), pipeline);
        form->addRow(hasDeadline, deadline);
        form->addRow(QString::fromUtf8("Штраф за срок"), penalty);
        form->addRow(QString::fromUtf8("Исполнители"), assignees);
        form->addRow(QString::fromUtf8("Навыки"), skills);
        if (edit && !originalTask.participants.empty()) {
            auto* hint = new QLabel(QString::fromUtf8("XP уже начислен. Участники и параметры начисления зафиксированы."));
            hint->setWordWrap(true);
            form->addRow(hint);
        }
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty()) { name->setFocus(); return; }
        if (projectMode) {
            const auto current = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(),
                [&](const auto& entry) { return entry.id == projectId; });
            if (edit && current == workspace_.data.projects.end()) return;
            const int index = edit ? int(std::distance(workspace_.data.projects.begin(), current)) : -1;
            auto result = AppSaveProjectEntry(workspace_.directory, workspace_.data.projects, index,
                u(name->text().trimmed()), u(description->toPlainText()));
            if (!result.ok) {
                appendLog(AppLogLevel::Warning, "CoreCatalogMutation", edit
                    ? "Project edit failed or rolled back" : "Project creation failed or rolled back");
                message(result.errorMessage); return;
            }
            appendLog(AppLogLevel::Info, "CoreCatalogMutation", edit
                ? "Project edit committed" : "Project creation committed");
        } else {
            TaskEntry task = originalTask;
            if (!edit) task.id = u(QUuid::createUuid().toString(QUuid::WithoutBraces));
            task.title = u(name->text().trimmed());
            task.description = u(description->toPlainText());
            if (!edit) task.createdAt = QDateTime::currentSecsSinceEpoch();
            task.projectId = u(project->currentData().toString());
            task.project = project->currentIndex() > 0 ? u(project->currentText()) : std::string();
            task.priority = priority->currentData().toInt();
            task.category = category->currentIndex();
            task.pipelineStepId = u(pipeline->currentData().toString());
            task.pipelineStep = pipeline->currentIndex() > 0 ? u(pipeline->currentText()) : std::string();
            task.deadlineAt = hasDeadline->isChecked() ? deadline->dateTime().toSecsSinceEpoch() : 0;
            task.deadlinePenaltyPercent = penalty->value();
            auto collectIds = [&](QListWidget* list, const std::vector<std::string>& previous) {
                std::vector<std::string> selected;
                for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked)
                    selected.push_back(u(list->item(i)->data(Qt::UserRole).toString()));
                std::vector<std::string> ordered;
                for (const auto& id : previous)
                    if (std::find(selected.begin(), selected.end(), id) != selected.end()) ordered.push_back(id);
                for (const auto& id : selected)
                    if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) ordered.push_back(id);
                return ordered;
            };
            task.assignees = collectIds(assignees, originalTask.assignees);
            task.skillIds = collectIds(skills, originalTask.skillIds);
            auto result = edit
                ? EditTaskDetails(workspace_.directory, workspace_.data.tasks, workspace_.data.taskAudit, task, "admin/qt")
                : CreateTaskWithRecovery(workspace_.directory, workspace_.data.tasks, workspace_.data.taskAudit, task, "admin/qt");
            if (!result.ok) {
                appendLog(AppLogLevel::Warning, "CoreTaskMutation", edit
                    ? "Task edit failed or rolled back" : "Task creation failed or rolled back");
                message(result.errorMessage); return;
            }
            appendLog(AppLogLevel::Info, "CoreTaskMutation", edit ? "Task edit committed" : "Task creation committed");
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) reload();
}

void QtWindow::previewCloudPush() {
    if (!workspace_.modules.cloud || !requireAdmin() || navigation_->currentRow() != Cloud) return;
    const auto config = LoadCloudSyncConfig(workspace_.directory);
    const auto preview = PreviewQtCloudWorkspacePush(config, workspace_.directory, CloudRole::Admin);
    if (!preview.sync.ok) {
        QMessageBox::warning(this, QString::fromUtf8("Предпросмотр выгрузки"), q(preview.message));
        return;
    }
    if (!preview.sync.changed) {
        QMessageBox::information(this, QString::fromUtf8("Предпросмотр выгрузки"), q(preview.message));
        return;
    }
    QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Выгрузить всё рабочее пространство?"),
        q(preview.message) + QString::fromUtf8("\n\nБудет создан локальный снимок старых облачных файлов. "
        "Лишние файлы в облаке будут удалены; конфликт storage.json запрещает операцию."),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Выгрузить"));
    confirm.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    if (confirm.exec() != QMessageBox::Yes) return;
    const auto result = RunQtCloudWorkspacePush(config, workspace_.directory, CloudRole::Admin, &preview);
    if (!result.sync.ok) {
        const bool pending = std::filesystem::exists(workspace_.directory / "meta/qt-cloud-push.json");
        appendLog(pending ? AppLogLevel::Error : AppLogLevel::Warning, "CoreCloudTransaction",
            pending ? "Manual cloud push recovery pending" : "Manual cloud push failed");
        QMessageBox::warning(this, QString::fromUtf8("Выгрузка не выполнена"), q(result.message));
    } else {
        appendLog(AppLogLevel::Info, "CoreCloudTransaction", "Manual cloud push committed");
        QMessageBox::information(this, QString::fromUtf8("Выгрузка завершена"), q(result.message));
    }
    render();
}

void QtWindow::pullCloud() {
    if (!workspace_.modules.cloud) return;
    const auto config = LoadCloudSyncConfig(workspace_.directory);
    const auto root = ResolveCloudRootPath(config, workspace_.directory);
    std::error_code ec;
    if (!config.enabled || !std::filesystem::is_directory(root, ec) || ec) {
        message(u8"Ручной pull недоступен: включите облако и проверьте внешнюю папку.");
        return;
    }
    const auto drift = InspectCloudWorkspaceDrift(config, workspace_.directory, 0);
    const QString prompt = QString::fromUtf8(
        "Получить данные из облачной папки?\n\n%1\n\n"
        "Облачные файлы заменят соответствующие локальные данные, включая локальные изменения. "
        "Перед применением будет создана полная резервная копия. "
        "Различий в задачах и пайплайне: %2. Конфликт хранилища отменит получение целиком.")
        .arg(q(root.u8string())).arg(drift.issueCount);
    if (QMessageBox::warning(this, QString::fromUtf8("Ручной pull"), prompt,
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = RunQtCloudPullTransaction(config, workspace_.directory,
                                                   admin_ ? CloudRole::Admin : CloudRole::Viewer);
    QApplication::restoreOverrideCursor();
    if (!result.sync.ok) {
        const bool pending = std::filesystem::exists(workspace_.directory / "meta/qt-cloud-pull.json");
        appendLog(pending || !result.rolledBack ? AppLogLevel::Error : AppLogLevel::Warning,
            "CoreCloudTransaction", pending ? "Manual cloud pull recovery pending"
                : result.rolledBack ? "Manual cloud pull rolled back" : "Manual cloud pull failed");
        if (pending) {
            setEnabled(false);
            for (auto* timer : findChildren<QTimer*>()) timer->stop();
        }
        message(result.message);
        if (pending) QCoreApplication::exit(1);
        return;
    }
    appendLog(AppLogLevel::Info, "CoreCloudTransaction",
        result.sync.changed ? "Manual cloud pull committed" : "Manual cloud pull unchanged");
    profileSession_.lock();
    if (!reload()) return;
    statusBar()->showMessage(q(result.message), 15000);
    if (result.sync.storageConflict) {
        QMessageBox::warning(this, QString::fromUtf8("Конфликт storage.json"), q(result.message));
    }
}

void QtWindow::runAutomaticCloudSync() {
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (!workspace_.modules.cloud) {
        lastCloudAutoSyncAt_ = now;
        return;
    }
    const auto config = LoadCloudSyncConfig(workspace_.directory);
    if (!config.enabled || !config.autoSyncEnabled) {
        lastCloudAutoSyncAt_ = now;
        return;
    }
    if (lastCloudAutoSyncAt_ <= 0) lastCloudAutoSyncAt_ = now;
    if (!QtCloudAutoSyncDue(config, now, lastCloudAutoSyncAt_)) return;
    // Never replace the workspace beneath an editor or confirmation dialog.
    if (QApplication::activeModalWidget()) return;

    lastCloudAutoSyncAt_ = now;
    std::string walletProfile;
    const auto profileId = u(profiles_->currentData().toString());
    const auto role = admin_ ? CloudRole::Admin : CloudRole::Viewer;
    if (!admin_ && !profileId.empty() && profileSession_.isUnlocked(*workspace_.storage, profileId))
        walletProfile = profileId;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = RunQtCloudAutoSync(config, workspace_.directory, role, walletProfile);
    QApplication::restoreOverrideCursor();
    if (!result.attempted) {
        if (!result.ok) statusBar()->showMessage(q(result.message), 8000);
        return;
    }
    if (!result.ok) {
        appendLog(result.recoveryPending ? AppLogLevel::Error : AppLogLevel::Warning,
            "CoreCloudTransaction", result.recoveryPending
                ? "Automatic cloud sync recovery pending" : "Automatic cloud sync failed");
        statusBar()->showMessage(q(result.message), 15000);
        if (result.recoveryPending) {
            setEnabled(false);
            for (auto* timer : findChildren<QTimer*>()) timer->stop();
            QCoreApplication::exit(1);
        }
        return;
    }
    if (result.pullChanged) {
        profileSession_.lock();
        if (!reload()) return;
    }
    if (result.changed) {
        appendLog(AppLogLevel::Info, "CoreCloudTransaction", "Automatic cloud sync committed");
        statusBar()->showMessage(q(result.message), 10000);
    }
}

void QtWindow::downloadCloudRelease() {
    if (!workspace_.modules.cloud) return;
    const auto config = LoadCloudSyncConfig(workspace_.directory);
    const auto manifest = LoadCloudManifest(config, workspace_.directory);
    if (!IsUpdateAvailable(manifest, APP_VERSION)) {
        message(u8"В облачном manifest нет более новой версии.");
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = DownloadQtCloudRelease(config, workspace_.directory, manifest);
    QApplication::restoreOverrideCursor();
    if (!result.ok) {
        appendLog(AppLogLevel::Warning, "CloudRelease", "Cloud release download failed");
        message(result.message);
        return;
    }
    appendLog(AppLogLevel::Info, "CloudRelease", "Cloud release installer downloaded and local copy hashed");
    statusBar()->showMessage(q(result.message), 20000);
    render();
}

void QtWindow::launchCloudRelease() {
    if (!workspace_.modules.cloud) return;
    const auto config = LoadCloudSyncConfig(workspace_.directory);
    const auto manifest = LoadCloudManifest(config, workspace_.directory);
    const auto target = QtCloudReleaseTargetPath(workspace_.directory, manifest);
    if (!IsUpdateAvailable(manifest, APP_VERSION) || !target) {
        message(u8"В manifest нет доступного установщика новой версии.");
        return;
    }
    const QFileInfo installer(QString::fromUtf8(target->u8string()));
    if (!installer.isFile() || installer.isSymLink()) {
        message(u8"Сначала скачайте установщик новой версии.");
        return;
    }
    QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Запустить установщик ForgeMirror?"),
        QString::fromUtf8("Версия %1 будет установлена поверх текущей программы. Закройте ForgeMirror, если установщик попросит об этом. Пользовательские данные хранятся отдельно от файлов программы.")
            .arg(q(manifest.appVersion)), QMessageBox::Yes | QMessageBox::Cancel, this);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Запустить"));
    confirm.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    if (confirm.exec() != QMessageBox::Yes) return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(installer.absoluteFilePath()))) {
        message(u8"Не удалось запустить установщик.");
        return;
    }
    appendLog(AppLogLevel::Info, "CloudRelease", "Cloud release installer launched");
    statusBar()->showMessage(QString::fromUtf8("Установщик запущен."), 15000);
}

void QtWindow::resolveCloudConflict() {
    if (!workspace_.modules.cloud) return;
    if (!ShowCloudConflictResolver(this, workspace_.directory)) return;
    profileSession_.lock();
    if (reload()) statusBar()->showMessage(QString::fromUtf8("Локальная версия обновлена; облако не изменялось."), 15000);
}

void QtWindow::resolveStorageConflict() {
    if (!workspace_.modules.cloud || !requireAdmin()) return;
    bool localChanged = false;
    if (!ShowQtStorageConflictResolver(this, workspace_.directory, &localChanged)) return;
    if (localChanged) { profileSession_.lock(); if (!reload()) return; }
    else render();
    statusBar()->showMessage(QString::fromUtf8("Конфликт storage.json разрешён; обе исходные версии сохранены в meta/updates."), 15000);
}

void QtWindow::deleteEntry() {
    const int page = navigation_->currentRow();
    if (page == Shortcuts) {
        const auto id = u(selectedId());
        const auto found = std::find_if(workspace_.data.shortcuts.begin(), workspace_.data.shortcuts.end(), [&](const auto& item) { return item.id == id; });
        if (found == workspace_.data.shortcuts.end()) return;
        QMessageBox confirm(QMessageBox::Question, QString::fromUtf8("Удалить ярлык"),
            QString::fromUtf8("Удалить ярлык «%1»? Сам файл останется на месте.").arg(q(found->label)), QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить")); confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена")); confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        const auto result = AppDeleteShortcut(workspace_.directory, workspace_.data.shortcuts, int(std::distance(workspace_.data.shortcuts.begin(), found)));
        if (!result.ok) { message(result.errorMessage); return; }
        render(); statusBar()->showMessage(QString::fromUtf8("Ярлык удалён · файл сохранён"), 3000); return;
    }
    if (!requireAdmin()) return;
    if (page == Banner) {
        bool ok = false; const int index = selectedId().toInt(&ok);
        if (!ok || index < 0 || index >= int(workspace_.data.bannerTexts.size())) return;
        QMessageBox confirm(QMessageBox::Question, QString::fromUtf8("Удалить фразу"),
            QString::fromUtf8("Удалить выбранную фразу из ротации баннера?"), QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить")); confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена")); confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        QString error; if (!DeleteBannerTextChecked(workspace_, index, &error)) { message(u(error)); return; }
        bannerIndex_ = 0; updateBanner(); render(); statusBar()->showMessage(QString::fromUtf8("Фраза удалена"), 3000); return;
    }
    if (page != Tasks && page != Projects && page != Catalog && page != Pipeline && page != Professions) return;
    if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) {
        message(u8"Сначала завершите восстановление данных."); return;
    }
    const auto id = u(selectedId());
    if (page == Catalog) {
        if (id.empty() || std::count(workspace_.catalog.skills().begin(), workspace_.catalog.skills().end(), id) != 1) {
            message(u8"Навык с неоднозначным ID нельзя удалить автоматически."); return;
        }
        for (const auto& info : workspace_.profiles) if (info.archived &&
            archivedProfileUsesSkill(workspace_.directory, info.id, id)) {
            message(u8"Навык используется архивным профилем. Сначала восстановите профиль и перенесите его данные."); return;
        }
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить навык"),
            QString::fromUtf8("Удалить неиспользуемый навык «%1»?\nСвязи с задачами, профилями и достижениями будут проверены повторно.")
                .arg(q(workspace_.catalog.display_name(id))), QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        bool prepared = false;
        try {
            PrepareSkillDeletionRecovery(workspace_.directory);
            prepared = true;
            const auto result = AppDeleteUnusedSkill(workspace_.directory, workspace_.catalog, *workspace_.storage,
                workspace_.profiles, workspace_.data.tasks, u(profiles_->currentData().toString()), id);
            if (!result.ok) {
                auto error = result.errorMessage;
                if (result.linkedTasks || result.linkedProfiles || result.linkedAchievements)
                    error += u8" Задачи: " + std::to_string(result.linkedTasks) + u8", профили: " +
                        std::to_string(result.linkedProfiles) + u8", достижения: " + std::to_string(result.linkedAchievements) + ".";
                throw std::runtime_error(error);
            }
            CommitQtRecoveryTransaction(workspace_.directory);
        } catch (const std::exception& error) {
            appendLog(AppLogLevel::Warning, "CoreCatalogMutation", "Skill deletion failed or rolled back");
            std::string text = error.what();
            if (prepared) {
                try { RecoverTaskCompletion(workspace_.directory); text += u8" Изменения полностью отменены."; }
                catch (const std::exception&) { text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
            }
            reload(); message(text); return;
        }
        appendLog(AppLogLevel::Info, "CoreCatalogMutation", "Skill deletion committed");
        reload();
        statusBar()->showMessage(QString::fromUtf8("Навык удалён"), 4000);
        return;
    }
    if (page == Professions) {
        const auto matches = std::count_if(workspace_.data.professions.begin(), workspace_.data.professions.end(),
            [&](const auto& profession) { return profession.id == id; });
        const auto profession = std::find_if(workspace_.data.professions.begin(), workspace_.data.professions.end(),
            [&](const auto& item) { return item.id == id; });
        if (id.empty() || matches != 1 || profession == workspace_.data.professions.end()) {
            message(u8"Профессию с неоднозначным ID нельзя удалить автоматически."); return;
        }
        int linkedProfiles = 0, linkedArchivedProfiles = 0;
        std::vector<std::string> profileJournalPaths;
        std::vector<IJobStorage::ProfileInfo> professionProfiles;
        for (const auto& info : workspace_.profiles) {
            profileJournalPaths.push_back((info.archived ? std::string("archive/") : std::string()) + info.id);
            if (info.archived) {
                if (archivedProfileUsesProfession(workspace_.directory, info.id, id)) {
                    ++linkedArchivedProfiles;
                    professionProfiles.push_back(info);
                }
                continue;
            }
            professionProfiles.push_back(info);
            if (workspace_.storage->set_active_profile(info.id)) {
                const auto profile = workspace_.storage->load_profile();
                if (profile && profile->profession_id() == id) ++linkedProfiles;
            }
        }
        workspace_.storage->set_active_profile(u(profiles_->currentData().toString()));
        int linkedSkills = 0;
        for (const auto& skillId : workspace_.catalog.skills()) if (workspace_.catalog.has_profession(skillId, id)) ++linkedSkills;
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить профессию"),
            QString::fromUtf8("Удалить профессию «%1»?\nНазначение будет снято с профилей: активных — %2, архивных — %3.\nСвязи с навыками будут удалены: %4.")
                .arg(q(profession->name)).arg(linkedProfiles).arg(linkedArchivedProfiles).arg(linkedSkills),
            QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        bool prepared = false;
        AppProfessionMutationResult result;
        try {
            PrepareProfessionDeletionRecovery(workspace_.directory, profileJournalPaths);
            prepared = true;
            result = AppDeleteProfessionEntry(workspace_.directory, workspace_.data.professions, *workspace_.storage,
                professionProfiles, workspace_.catalog, u(profiles_->currentData().toString()), id, true);
            if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось удалить профессию." : result.errorMessage);
            CommitQtRecoveryTransaction(workspace_.directory);
        } catch (const std::exception& error) {
            appendLog(AppLogLevel::Warning, "CoreCatalogMutation", "Profession deletion failed or rolled back");
            std::string text = error.what();
            if (prepared) {
                try { RecoverTaskCompletion(workspace_.directory); text += u8" Изменения полностью отменены."; }
                catch (const std::exception&) { text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
            }
            reload();
            message(text);
            return;
        }
        appendLog(AppLogLevel::Info, "CoreCatalogMutation", "Profession deletion committed");
        reload();
        statusBar()->showMessage(QString::fromUtf8("Профессия удалена · профилей очищено: %1 · навыков: %2")
            .arg(result.affectedProfiles).arg(result.affectedSkills), 5000);
        return;
    }
    if (page == Tasks) {
        const auto matches = std::count_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
            [&](const auto& task) { return task.id == id; });
        const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
            [&](const auto& item) { return item.id == id; });
        if (id.empty() || matches != 1 || task == workspace_.data.tasks.end()) {
            message(u8"Задача с неоднозначным ID не может быть удалена автоматически."); return;
        }
        const bool awarded = !task->participants.empty();
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить задачу"),
            (awarded
                ? QString::fromUtf8("Удалить задачу «%1» и откатить её XP?\nОперация остановится, если после неё профиль менялся.")
                : QString::fromUtf8("Удалить задачу «%1»?\nЭто действие будет записано в аудит."))
                .arg(q(AppTaskDisplayTitle(*task))),
            QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        AppContext context{workspace_.directory, *workspace_.storage, workspace_.catalog};
        const auto result = awarded
            ? DeleteAwardedTaskWithRecovery(context, workspace_.data.tasks, workspace_.data.taskAudit,
                id, u(profiles_->currentData().toString()), "admin/qt")
            : DeleteTaskWithRecovery(workspace_.directory, workspace_.data.tasks, workspace_.data.taskAudit, id, "admin/qt");
        auto deleteResult = result;
        bool keptAwardedXp = false;
        if (!deleteResult.ok && deleteResult.awardRollbackUnavailable) {
            QMessageBox keepXp(QMessageBox::Warning, QString::fromUtf8("Откат XP недоступен"),
                QString::fromUtf8("%1\n\nМожно удалить только запись задачи. Начисленные XP, уровни, навыки и счётчики профилей останутся без изменений.").arg(q(deleteResult.errorMessage)),
                QMessageBox::Yes | QMessageBox::No, this);
            keepXp.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить запись, оставить XP"));
            keepXp.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
            keepXp.setDefaultButton(QMessageBox::No);
            if (keepXp.exec() == QMessageBox::Yes) {
                deleteResult = DeleteAwardedTaskRecordKeepXpWithRecovery(workspace_.directory,
                    workspace_.data.tasks, workspace_.data.taskAudit, id, "admin/qt");
                keptAwardedXp = deleteResult.ok;
            }
        }
        if (!deleteResult.ok) { message(deleteResult.errorMessage); return; }
        appendLog(AppLogLevel::Info, "CoreTaskMutation", keptAwardedXp
            ? "Task record deleted; awarded XP preserved" : awarded ? "Task and awarded XP deletion committed" : "Task deletion committed");
        reload();
        statusBar()->showMessage(keptAwardedXp
            ? QString::fromUtf8("Запись задачи удалена · начисленный XP сохранён без изменений")
            : QString::fromUtf8("Задача удалена"), 5000);
        return;
    }
    if (page == Pipeline) {
        const auto duplicateIds = std::count_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
            [&](const auto& item) { return item.id == id; });
        if (id.empty() || duplicateIds != 1) { message(u8"Этап с неоднозначным ID нельзя удалить автоматически."); return; }
        const auto step = std::find_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
            [&](const auto& item) { return item.id == id; });
        if (step == workspace_.data.pipelineSteps.end()) return;
        const auto linkedTasks = std::count_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
            [&](const auto& task) { return task.pipelineStepId == id; });
        if (linkedTasks > 0) {
            message(u8"Этап используется задачами. Сначала переведите или отвяжите их."); return;
        }
        int inboundLinks = 0;
        for (const auto& source : workspace_.data.pipelineSteps)
            inboundLinks += int(std::count(source.nextIds.begin(), source.nextIds.end(), id));
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить этап"),
            QString::fromUtf8("Удалить этап «%1»?\nВходящих переходов будет удалено: %2.")
                .arg(q(step->title)).arg(inboundLinks), QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        const int index = int(std::distance(workspace_.data.pipelineSteps.begin(), step));
        const auto result = AppDeletePipelineStep(workspace_.directory, workspace_.data.pipelineSteps, index);
        if (!result.ok) {
            appendLog(AppLogLevel::Warning, "CoreCatalogMutation", "Pipeline stage deletion failed or rolled back");
            message(result.errorMessage.empty() ? u8"Не удалось удалить этап." : result.errorMessage); return;
        }
        appendLog(AppLogLevel::Info, "CoreCatalogMutation", "Pipeline stage deletion committed");
        reload();
        statusBar()->showMessage(QString::fromUtf8("Этап удалён · переходов очищено: %1").arg(inboundLinks), 5000);
        return;
    }
    const auto project = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(),
        [&](const auto& item) { return item.id == id; });
    if (project == workspace_.data.projects.end()) return;
    const auto linked = std::count_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
        [&](const auto& task) { return task.projectId == id; });
    QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить проект"),
        QString::fromUtf8("Удалить проект «%1»?\nСвязанные задачи: %2. Они сохранятся без проекта.")
            .arg(q(project->name)).arg(linked), QMessageBox::Yes | QMessageBox::No, this);
    confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить"));
    confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
    confirm.setDefaultButton(QMessageBox::No);
    if (confirm.exec() != QMessageBox::Yes) return;
    AppProjectDeleteResult result;
    bool prepared = false;
    try {
        PrepareProjectDeletionRecovery(workspace_.directory);
        prepared = true;
        result = AppDeleteProjectAndDetachTasks(workspace_.directory, workspace_.data.projects,
            workspace_.data.tasks, id, "admin", &workspace_.data.taskAudit);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось удалить проект." : result.errorMessage);
        CommitQtRecoveryTransaction(workspace_.directory);
    } catch (const std::exception& error) {
        appendLog(AppLogLevel::Warning, "CoreCatalogMutation", "Project deletion failed or rolled back");
        std::string text = error.what();
        bool recovered = !prepared;
        if (prepared) {
            try { RecoverTaskCompletion(workspace_.directory); recovered = true; text += u8" Изменения полностью отменены."; }
            catch (const std::exception&) { text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
        }
        if (recovered && prepared) reload();
        message(text);
        return;
    }
    appendLog(AppLogLevel::Info, "CoreCatalogMutation", "Project deletion committed: detached=" +
        std::to_string(result.detachedTasks));
    reload();
    statusBar()->showMessage(QString::fromUtf8("Проект удалён · задач отвязано: %1").arg(result.detachedTasks), 5000);
}

void QtWindow::movePipeline(int delta) {
    const int page = navigation_->currentRow();
    if (page == Shortcuts && (delta == -1 || delta == 1)) {
        const auto id = u(selectedId());
        const auto found = std::find_if(workspace_.data.shortcuts.begin(), workspace_.data.shortcuts.end(), [&](const auto& item) { return item.id == id; });
        if (found == workspace_.data.shortcuts.end()) return;
        const int from = int(std::distance(workspace_.data.shortcuts.begin(), found)), to = from + delta;
        if (to < 0 || to >= int(workspace_.data.shortcuts.size())) return;
        const auto result = AppMoveShortcut(workspace_.directory, workspace_.data.shortcuts, from, to);
        if (!result.ok) { message(result.errorMessage); return; }
        render(); statusBar()->showMessage(QString::fromUtf8("Порядок ярлыков сохранён"), 3000); return;
    }
    if (!requireAdmin() || page != Pipeline || (delta != -1 && delta != 1)) return;
    if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) {
        message(u8"Сначала завершите восстановление данных."); return;
    }
    const auto id = u(selectedId());
    const auto matches = std::count_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
        [&](const auto& item) { return item.id == id; });
    const auto found = std::find_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
        [&](const auto& item) { return item.id == id; });
    if (id.empty() || matches != 1 || found == workspace_.data.pipelineSteps.end()) {
        message(u8"Этап с неоднозначным ID нельзя переместить автоматически."); return;
    }
    const int from = int(std::distance(workspace_.data.pipelineSteps.begin(), found));
    const int to = from + delta;
    if (to < 0 || to >= int(workspace_.data.pipelineSteps.size())) return;
    const auto result = AppMovePipelineStep(workspace_.directory, workspace_.data.pipelineSteps, from, to);
    if (!result.ok) {
        appendLog(AppLogLevel::Warning, "CoreCatalogMutation", "Pipeline stage reordering failed or rolled back");
        message(result.errorMessage.empty() ? u8"Не удалось изменить порядок этапов." : result.errorMessage); return;
    }
    appendLog(AppLogLevel::Info, "CoreCatalogMutation", "Pipeline stage order changed");
    render();
    statusBar()->showMessage(QString::fromUtf8("Порядок этапов сохранён"), 3000);
}

void QtWindow::bulkEditTasks() {
    if (!requireAdmin() || navigation_->currentRow() != Tasks) return;
    if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) {
        message(u8"Сначала завершите восстановление данных.");
        return;
    }
    std::unordered_set<std::string> taskIds;
    bool hasCompleted = false;
    for (const auto& index : table_->selectionModel()->selectedRows()) {
        const auto id = u(table_->item(index.row(), 0)->data(Qt::UserRole).toString());
        const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
            [&](const auto& item) { return item.id == id; });
        if (task == workspace_.data.tasks.end()) {
            message(u8"Одна из выбранных задач больше недоступна.");
            return;
        }
        hasCompleted |= AppNormalizeTaskStatus(task->status) == 2;
        taskIds.insert(id);
    }
    if (taskIds.size() < 2) {
        message(u8"Выберите не менее двух задач.");
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName("bulkTaskEditDialog");
    dialog.setWindowTitle(QString::fromUtf8("Массовое изменение задач"));
    dialog.setMinimumWidth(380);
    auto* form = new QFormLayout(&dialog);
    auto* count = new QLabel(QString::fromUtf8("Выбрано задач: %1").arg(taskIds.size()));
    auto* operation = new QComboBox;
    operation->setObjectName("bulkTaskOperation");
    operation->addItem(QString::fromUtf8("Статус"), QStringLiteral("status"));
    operation->addItem(QString::fromUtf8("Приоритет"), QStringLiteral("priority"));
    operation->addItem(QString::fromUtf8("Проект"), QStringLiteral("project"));
    if (workspace_.modules.pipeline) operation->addItem(QString::fromUtf8("Этап процесса"), QStringLiteral("pipeline"));
    operation->addItem(QString::fromUtf8("Срок"), QStringLiteral("deadline"));
    operation->addItem(QString::fromUtf8("Исполнители"), QStringLiteral("assignees"));
    if (hasCompleted) operation->setCurrentIndex(1);
    auto* target = new QComboBox;
    target->setObjectName("bulkTaskTarget");
    auto* targetLabel = new QLabel(QString::fromUtf8("Новое значение"));
    auto* deadline = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1));
    deadline->setObjectName("bulkTaskDeadline");
    deadline->setCalendarPopup(true);
    deadline->setDisplayFormat("dd.MM.yyyy HH:mm");
    auto* hasDeadline = new QCheckBox(QString::fromUtf8("Указать срок"));
    hasDeadline->setObjectName("bulkTaskHasDeadline");
    hasDeadline->setChecked(true);
    auto updateTargets = [this, operation, target, targetLabel, deadline, hasDeadline] {
        const QSignalBlocker blocker(target);
        target->clear();
        const auto mode = operation->currentData().toString();
        const bool targetMode = mode != QStringLiteral("assignees") && mode != QStringLiteral("deadline");
        target->setVisible(targetMode);
        targetLabel->setVisible(targetMode);
        const bool deadlineMode = mode == QStringLiteral("deadline");
        deadline->setVisible(deadlineMode);
        hasDeadline->setVisible(deadlineMode);
        if (mode == QStringLiteral("project")) {
            target->addItem(QString::fromUtf8("Без проекта"), QString());
            for (const auto& project : workspace_.data.projects) target->addItem(q(project.name), q(project.id));
        } else if (mode == QStringLiteral("pipeline")) {
            target->addItem(QString::fromUtf8("Без этапа"), QString());
            for (const auto& step : workspace_.data.pipelineSteps) target->addItem(q(step.title), q(step.id));
        } else if (mode == QStringLiteral("status")) {
            target->addItem(QString::fromUtf8("Новая"), 0);
            target->addItem(QString::fromUtf8("В работе"), 1);
            target->setCurrentIndex(1);
        } else if (mode == QStringLiteral("priority")) {
            target->addItem(QString::fromUtf8("Низкий"), 0);
            target->addItem(QString::fromUtf8("Средний"), 1);
            target->addItem(QString::fromUtf8("Высокий"), 2);
            target->addItem(QString::fromUtf8("Критический"), 3);
            target->setCurrentIndex(1);
        }
    };
    if (hasCompleted) operation->removeItem(0);
    updateTargets();
    auto* assigneeList = new QListWidget;
    assigneeList->setObjectName("bulkTaskAssignees");
    assigneeList->setMaximumHeight(180);
    for (const auto& profile : workspace_.profiles) {
        auto* item = new QListWidgetItem(q(profile.name), assigneeList);
        item->setData(Qt::UserRole, q(profile.id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
            (profile.archived ? Qt::ItemFlags{} : Qt::ItemIsUserCheckable));
        item->setCheckState(Qt::Unchecked);
        if (profile.archived) item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
    }
    auto* assigneeLabel = new QLabel(QString::fromUtf8("Исполнители"));
    const bool assigningInitially = operation->currentData().toString() == QStringLiteral("assignees");
    assigneeList->setVisible(assigningInitially);
    assigneeLabel->setVisible(assigningInitially);
    int skippedCount = 0;
    for (const auto& id : taskIds) {
        const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
            [&](const auto& item) { return item.id == id; });
        if (task != workspace_.data.tasks.end() && !task->participants.empty()) ++skippedCount;
    }
    auto* hint = new QLabel;
    auto updateHint = [hint, operation, skippedCount] {
        const auto mode = operation->currentData().toString();
        if (mode == QStringLiteral("status")) hint->setText(QString::fromUtf8("Завершение и начисление XP выполняются отдельно для каждой задачи."));
        else if (mode == QStringLiteral("priority")) hint->setText(QString::fromUtf8("Изменение приоритета не меняет статус и начисление XP."));
        else if (mode == QStringLiteral("assignees")) hint->setText(QString::fromUtf8("Задачи с XP-участниками будут пропущены: %1. Выберите хотя бы одного активного исполнителя.").arg(skippedCount));
        else if (mode == QStringLiteral("project")) hint->setText(QString::fromUtf8("Выбранный проект будет назначен всем выбранным задачам."));
        else if (mode == QStringLiteral("pipeline")) hint->setText(QString::fromUtf8("Выбранный этап будет назначен всем выбранным задачам."));
        else hint->setText(QString::fromUtf8("Срок можно снять, отключив флажок «Указать срок»."));
    };
    updateHint();
    hint->setWordWrap(true);
    form->addRow(count);
    form->addRow(QString::fromUtf8("Поле"), operation);
    form->addRow(targetLabel, target);
    form->addRow(QString::fromUtf8("Срок"), deadline);
    form->addRow(QString(), hasDeadline);
    deadline->setVisible(false);
    hasDeadline->setVisible(false);
    connect(hasDeadline, &QCheckBox::toggled, deadline, &QWidget::setEnabled);
    form->addRow(assigneeLabel, assigneeList);
    form->addRow(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* applyButton = buttons->button(QDialogButtonBox::Save);
    applyButton->setText(QString::fromUtf8("Применить"));
    applyButton->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    auto updateAssigneeVisibility = [operation, assigneeList, assigneeLabel] {
        const bool visible = operation->currentData().toString() == QStringLiteral("assignees");
        assigneeList->setVisible(visible);
        assigneeLabel->setVisible(visible);
    };
    auto updateApplyEnabled = [operation, assigneeList, applyButton] {
        if (operation->currentData().toString() != QStringLiteral("assignees")) {
            applyButton->setEnabled(true);
            return;
        }
        bool selected = false;
        for (int index = 0; index < assigneeList->count(); ++index) {
            const auto* item = assigneeList->item(index);
            if (item->flags().testFlag(Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked) selected = true;
        }
        applyButton->setEnabled(selected);
    };
    connect(operation, &QComboBox::currentIndexChanged, &dialog,
        [updateTargets, updateHint, updateApplyEnabled, updateAssigneeVisibility] {
            updateTargets(); updateHint(); updateAssigneeVisibility(); updateApplyEnabled();
        });
    connect(assigneeList, &QListWidget::itemChanged, &dialog, [updateApplyEnabled] { updateApplyEnabled(); });
    updateApplyEnabled();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    std::vector<std::string> assignees;
    if (operation->currentData().toString() == QStringLiteral("assignees")) {
        for (int index = 0; index < assigneeList->count(); ++index) {
            const auto* item = assigneeList->item(index);
            if (item->flags().testFlag(Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
                assignees.push_back(u(item->data(Qt::UserRole).toString()));
        }
        if (assignees.empty()) return;
    }

    const auto mode = operation->currentData().toString();
    const auto referenceId = u(target->currentData().toString());
    std::string referenceName;
    if (mode == QStringLiteral("project") && !referenceId.empty()) {
        const auto found = std::find_if(workspace_.data.projects.begin(), workspace_.data.projects.end(),
            [&](const auto& item) { return item.id == referenceId; });
        if (found == workspace_.data.projects.end()) { message(u8"Выбранный проект больше недоступен."); return; }
        referenceName = found->name;
    } else if (mode == QStringLiteral("pipeline") && !referenceId.empty()) {
        const auto found = std::find_if(workspace_.data.pipelineSteps.begin(), workspace_.data.pipelineSteps.end(),
            [&](const auto& item) { return item.id == referenceId; });
        if (found == workspace_.data.pipelineSteps.end()) { message(u8"Выбранный этап больше недоступен."); return; }
        referenceName = found->title;
    }
    const auto result = mode == QStringLiteral("status")
        ? AppBulkUpdateTaskStatus(workspace_.directory, workspace_.data.tasks, taskIds,
            target->currentData().toInt(), "admin", &workspace_.data.taskAudit)
        : mode == QStringLiteral("priority")
        ? AppBulkUpdateTaskPriority(workspace_.directory, workspace_.data.tasks, taskIds,
            target->currentData().toInt(), "admin", &workspace_.data.taskAudit)
        : mode == QStringLiteral("project")
        ? AppBulkUpdateTaskProject(workspace_.directory, workspace_.data.tasks, taskIds,
            referenceId, referenceName, "admin", &workspace_.data.taskAudit)
        : mode == QStringLiteral("pipeline")
        ? AppBulkUpdateTaskPipelineStep(workspace_.directory, workspace_.data.tasks, taskIds,
            referenceId, referenceName, "admin", &workspace_.data.taskAudit)
        : mode == QStringLiteral("deadline")
        ? AppBulkUpdateTaskDeadline(workspace_.directory, workspace_.data.tasks, taskIds,
            hasDeadline->isChecked() ? std::optional<std::int64_t>(deadline->dateTime().toSecsSinceEpoch()) : std::nullopt,
            "admin", &workspace_.data.taskAudit)
        : AppBulkUpdateTaskAssignees(workspace_.directory, workspace_.data.tasks, taskIds,
            assignees, "admin", &workspace_.data.taskAudit);
    if (!result.ok) {
        appendLog(AppLogLevel::Warning, "CoreTaskMutation", "Bulk task update failed or rolled back");
        reload();
        message(result.errorMessage.empty() ? std::string(u8"Не удалось применить массовое изменение.") : result.errorMessage);
        return;
    }
    appendLog(AppLogLevel::Info, "CoreTaskMutation", "Bulk task update committed: changed=" +
        std::to_string(result.changedCount) + " skipped=" + std::to_string(result.skippedCount));
    reload();
    statusBar()->showMessage(QString::fromUtf8("Обновлено: %1 · пропущено: %2")
        .arg(result.changedCount).arg(result.skippedCount), 7000);
}

void QtWindow::changeStatus() {
    if (!requireAdmin()) return;
    const auto id = u(selectedId());
    auto found = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(), [&](const auto& task) { return task.id == id; });
    if (found == workspace_.data.tasks.end()) return;
    QStringList labels;
    std::vector<int> states;
    for (int state : {0, 1}) if (state != found->status && AppTaskWorkflowService::IsStatusTransitionAllowed(found->status, state)) {
        states.push_back(state);
        labels << q(AppTaskStatusLabel(state));
    }
    // Completed legacy tasks without XP can also finish their pending handoff.
    if (found->participants.empty()) {
        states.push_back(2);
        labels << QString::fromUtf8("Выполнена — начислить XP");
    } else if (found->status != 2) {
        states.push_back(2);
        labels << QString::fromUtf8("Выполнена — XP уже начислен");
    }
    QInputDialog statusDialog(this);
    statusDialog.setWindowTitle(QString::fromUtf8("Статус задачи"));
    statusDialog.setLabelText(QString::fromUtf8("Новый статус:"));
    statusDialog.setComboBoxItems(labels);
    statusDialog.setComboBoxEditable(false);
    statusDialog.setOkButtonText(QString::fromUtf8("Применить"));
    statusDialog.setCancelButtonText(QString::fromUtf8("Отмена"));
    if (statusDialog.exec() != QDialog::Accepted) return;
    const auto selected = statusDialog.textValue();
    if (labels.indexOf(selected) < 0) return;
    const int next = states[size_t(labels.indexOf(selected))];
    if (next == 2 && found->participants.empty()) {
        ShowTaskCompletionDialog(this, workspace_, q(id), profiles_->currentData().toString(),
            [this](AppLogLevel level, const std::string& event) { appendLog(level, "CoreTaskCompletion", event); });
        reload();
        return;
    }
    const auto result = UpdateTaskStatusWithRecovery(workspace_.directory, workspace_.data.tasks,
        workspace_.data.taskAudit, id, next, "admin/qt");
    if (!result.ok) {
        appendLog(AppLogLevel::Warning, "CoreTaskMutation", "Task status update failed or rolled back");
        message(result.errorMessage);
    } else {
        appendLog(AppLogLevel::Info, "CoreTaskMutation", "Task status update committed");
        reload();
    }
}
