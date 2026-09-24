#include "QtWindow.h"
#include "QtAchievements.h"
#include "QtPomodoro.h"
#include "QtProfessionEditor.h"
#include "QtRulesEditor.h"
#include "QtVaultEditor.h"
#include "QtBannerEditor.h"
#include "QtCloudSettings.h"
#include "QtCloudPull.h"
#include "QtCloudConflict.h"
#include "QtStorageConflict.h"
#include "QtModelViewer.h"
#include "QtReportExport.h"
#include "QtAuditExport.h"
#include "QtReportChart.h"
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
#include <unordered_map>

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
bool pomodoroWithinWindow(const StorageVaultData& vault, std::int64_t startedAt) {
    const auto local = QDateTime::fromSecsSinceEpoch(startedAt).toLocalTime();
    const int weekday = local.date().dayOfWeek() % 7; // Sunday is 0 in vault format.
    if (!(vault.pomodoroDaysMask & (1 << weekday))) return false;
    const int minutes = local.time().hour() * 60 + local.time().minute();
    const int start = vault.pomodoroStartMinutes, end = vault.pomodoroEndMinutes;
    if (start == end) return false;
    return start < end ? minutes >= start && minutes < end : minutes >= start || minutes < end;
}
enum Page { ProfilePage, Tasks, Projects, Catalog, Pipeline, Professions, Statistics, Audit, Pomodoro, Rules, Vault, Shortcuts, Banner, Cloud, ModelViewerPage, ModelSettingsPage, Logs };
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
        {"password_change", QString::fromUtf8("Смена пароля")}, {"password_reset", QString::fromUtf8("Сброс пароля")},
        {"block", QString::fromUtf8("Блокировка профиля")}, {"unblock", QString::fromUtf8("Снятие блокировки")},
        {"wallet_adjustment", QString::fromUtf8("Изменение кошелька")},
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
}

QtWindow::QtWindow(QtWorkspace& workspace) : workspace_(workspace), profileSession_(workspace.directory), displaySettings_(LoadQtDisplaySettings(workspace.directory)) {
    loadAppLogs();
    ApplyQtDisplaySettings(*qApp, displaySettings_);
    setWindowTitle(QString::fromUtf8("ForgeMirror · Qt migration · ") + APP_VERSION);
    resize(1120, 720);
    setMinimumSize(800, 520);
    setWindowFlag(Qt::FramelessWindowHint, !displaySettings_.decorated);
    if (displaySettings_.fullscreen) setWindowState(windowState() | Qt::WindowFullScreen);
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    dragHandle_ = new WindowDragHandle;
    dragHandle_->setText(QString::fromUtf8("⋮⋮"));
    dragHandle_->setToolTip(QString::fromUtf8("Перетащить окно"));
    dragHandle_->setFixedSize(28, 28);
    dragHandle_->setVisible(!displaySettings_.decorated);
    header->addWidget(dragHandle_);
    header->addWidget(new QLabel(QString::fromUtf8("Профиль:")));
    profiles_ = new QComboBox;
    profiles_->setObjectName("profiles");
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
    menu->addAction(QString::fromUtf8("Вход / выход администратора"), this, [this] { authenticate(); });
    profileAccessAction_ = menu->addAction(QString::fromUtf8("Войти в выбранный профиль"), this, [this] { authenticateProfile(); });
    profileAccessAction_->setObjectName("profileAccess");
    auto* passwordAction = menu->addAction(QString::fromUtf8("Сменить пароль выбранного профиля"), this, [this] {
        const auto id = profiles_->currentData().toString();
        if (!profileSession_.isUnlocked(*workspace_.storage, u(id))) {
            render(); message(u8"Сначала войдите в выбранный профиль."); return;
        }
        if (ShowProfilePasswordDialog(this, workspace_, id, id, false)) {
            const bool forgotten = profileSession_.lock(true);
            AppendProfileAudit(workspace_.directory, u(id), "password_change");
            if (!forgotten) message(u8"Пароль изменён, но запись доверия удалить не удалось. Она может восстановить доступ после обновления.");
        }
        render();
    });
    ownPasswordAction_ = passwordAction;
    passwordAction->setObjectName("changeOwnProfilePassword");
    menu->addAction(QString::fromUtf8("Открыть папку данных Qt"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(workspace_.directory.u8string())));
    });
    auto* displaySettings = menu->addAction(QString::fromUtf8("Настройки интерфейса Qt"), this, [this] {
        if (!ShowQtDisplaySettings(this, workspace_.directory, displaySettings_)) return;
        ApplyQtDisplaySettings(*qApp, displaySettings_);
        setWindowFlag(Qt::FramelessWindowHint, !displaySettings_.decorated);
        dragHandle_->setVisible(!displaySettings_.decorated);
        if (trayIcon_) trayIcon_->setVisible(displaySettings_.minimizeToTray);
        if (displaySettings_.fullscreen) showFullScreen(); else showNormal();
        render();
    });
    displaySettings->setObjectName("qtDisplaySettingsAction");
    auto* shortcutHelp = menu->addAction(QString::fromUtf8("Горячие клавиши"), this, [this] { showShortcutHelp(); });
    shortcutHelp->setObjectName("shortcutHelpAction");
    menu->addAction(QString::fromUtf8("О переносе"), this, [this] {
        QMessageBox::information(this, QString::fromUtf8("Перенос на Qt"), QString::fromUtf8(
            "Перенос ещё не завершён; это не замена стабильной версии.\n"
            "Qt работает с отдельной копией данных. Доступен только подтверждаемый ручной pull с полной резервной копией.\n"
            "Список перенесённых функций и ограничений находится в qt/README.md."));
    });
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
    connect(deadlineReminderTimer, &QTimer::timeout, this, [this] { checkDeadlineReminders(); });
    deadlineReminderTimer->start();
    QTimer::singleShot(2500, this, [this] { checkDeadlineReminders(); });

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->addItems({QString::fromUtf8("Профиль  F1"), QString::fromUtf8("Задачи"),
        QString::fromUtf8("Проекты"), QString::fromUtf8("Навыки  F2"), QString::fromUtf8("Пайплайн  F3"),
        QString::fromUtf8("Профессии"), QString::fromUtf8("Статистика  F5"), QString::fromUtf8("Аудит  F6"),
        QString::fromUtf8("Pomodoro"), QString::fromUtf8("Правила  F4"), QString::fromUtf8("Хранилище"), QString::fromUtf8("Ярлыки"), QString::fromUtf8("Баннер"), QString::fromUtf8("Облако"),
        QString::fromUtf8("3D просмотр"), QString::fromUtf8("Настройки 3D"), QString::fromUtf8("Логи")});
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
    auto* pomodoro = new QtPomodoro(nullptr, workspace_.directory);
    pomodoro_ = pomodoro;
    pomodoro->setRewardHandler([this](int workMinutes, std::int64_t startedAt) -> QString {
        const auto id = u(profiles_->currentData().toString());
        if (std::filesystem::exists(workspace_.directory / "meta/qt-xp-transaction")) return QString::fromUtf8("Награда не начислена: требуется восстановление данных.");
        if (!profileSession_.isUnlocked(*workspace_.storage, id)) return QString::fromUtf8("Фокус завершён. Для награды нужен личный вход.");
        if (workspace_.data.vault.pomodoroCoinsPerCycle <= 0) return QString::fromUtf8("Фокус завершён. Награды отключены.");
        if (workMinutes < workspace_.data.vault.pomodoroMinMinutes) return QString::fromUtf8("Фокус завершён, но короче минимального времени награды.");
        if (!pomodoroWithinWindow(workspace_.data.vault, startedAt)) return QString::fromUtf8("Фокус завершён вне расписания наград.");
        auto result = AppAdjustProfileWallet(*workspace_.storage, id, id, double(workspace_.data.vault.pomodoroCoinsPerCycle));
        if (!result.ok || !result.profile) return QString::fromUtf8("Не удалось сохранить награду.");
        const int amount = workspace_.data.vault.pomodoroCoinsPerCycle;
        const bool auditRecorded = AppendProfileAudit(workspace_.directory, id, "pomodoro_reward",
            "credit " + std::to_string(amount) + " pomodoro_focus");
        reload();
        return auditRecorded ? QString::fromUtf8("Начислено Кукоинов: +%1").arg(amount)
            : QString::fromUtf8("Начислено Кукоинов: +%1; запись в историю не сохранена.").arg(amount);
    });
    content->addWidget(pomodoro_, 1);
    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setObjectName("search");
    search_->setPlaceholderText(QString::fromUtf8("Поиск по текущему разделу…"));
    search_->setClearButtonEnabled(true);
    filters->addWidget(search_);
    catalogProfessionFilter_ = new QComboBox;
    catalogProfessionFilter_->setObjectName("catalogProfessionFilter");
    catalogProfessionFilter_->setMaximumWidth(190);
    catalogProfessionFilter_->setToolTip(QString::fromUtf8("Показать навыки, связанные с выбранной профессией"));
    filters->addWidget(catalogProfessionFilter_);
    statusFilter_ = new QComboBox;
    statusFilter_->setObjectName("statusFilter");
    statusFilter_->setMaximumWidth(135);
    statusFilter_->addItems({QString::fromUtf8("Все статусы"), QString::fromUtf8("Новая"),
                            QString::fromUtf8("В работе"), QString::fromUtf8("Выполнена")});
    statusFilter_->setCurrentIndex(displaySettings_.taskStatusFilter);
    filters->addWidget(statusFilter_);
    priorityFilter_ = new QComboBox;
    priorityFilter_->setObjectName("priorityFilter");
    priorityFilter_->setMaximumWidth(150);
    priorityFilter_->addItems({QString::fromUtf8("Любой приоритет"), QString::fromUtf8("Низкий"),
        QString::fromUtf8("Средний"), QString::fromUtf8("Высокий"), QString::fromUtf8("Критический")});
    priorityFilter_->setCurrentIndex(displaySettings_.taskPriorityFilter);
    filters->addWidget(priorityFilter_);
    quickTaskFilter_ = new QComboBox;
    quickTaskFilter_->setObjectName("quickTaskFilter");
    quickTaskFilter_->setMaximumWidth(155);
    quickTaskFilter_->addItems({QString::fromUtf8("Все задачи"), QString::fromUtf8("Мне назначено"),
        QString::fromUtf8("На сегодня"), QString::fromUtf8("Просрочено"), QString::fromUtf8("7 дней"),
        QString::fromUtf8("Без проекта"), QString::fromUtf8("Ждут XP"), QString::fromUtf8("Активные")});
    quickTaskFilter_->setCurrentIndex(displaySettings_.taskQuickFilter);
    filters->addWidget(quickTaskFilter_);
    taskProjectFilter_ = new QComboBox;
    taskProjectFilter_->setObjectName("taskProjectFilter");
    taskProjectFilter_->setMaximumWidth(170);
    filters->addWidget(taskProjectFilter_);
    taskPipelineFilter_ = new QComboBox;
    taskPipelineFilter_->setObjectName("taskPipelineFilter");
    taskPipelineFilter_->setMaximumWidth(180);
    filters->addWidget(taskPipelineFilter_);
    reportView_ = new QComboBox;
    reportView_->setObjectName("reportView");
    reportView_->setMaximumWidth(145);
    reportView_->addItems({QString::fromUtf8("По проектам"), QString::fromUtf8("По сотрудникам")});
    reportView_->setCurrentIndex(displaySettings_.reportView);
    filters->addWidget(reportView_);
    reportDateRange_ = new QComboBox;
    reportDateRange_->setObjectName("reportDateRange");
    reportDateRange_->setMaximumWidth(180);
    reportDateRange_->addItems({QString::fromUtf8("Всё время"), QString::fromUtf8("30 дней"),
        QString::fromUtf8("90 дней"), QString::fromUtf8("С начала года"), QString::fromUtf8("Период…")});
    reportDateRange_->setCurrentIndex(displaySettings_.reportDateRange);
    reportDateRange_->setToolTip(QString::fromUtf8("Фильтр по дате создания задач; статусы и XP показываются текущие"));
    filters->addWidget(reportDateRange_);
    reportFrom_ = new QDateEdit(displaySettings_.reportDateFrom);
    reportFrom_->setObjectName("reportDateFrom");
    reportFrom_->setCalendarPopup(true);
    reportFrom_->setDisplayFormat("dd.MM.yyyy");
    reportFrom_->setMaximumWidth(118);
    reportTo_ = new QDateEdit(displaySettings_.reportDateTo);
    reportTo_->setObjectName("reportDateTo");
    reportTo_->setCalendarPopup(true);
    reportTo_->setDisplayFormat("dd.MM.yyyy");
    reportTo_->setMaximumWidth(118);
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
    projectSort_->setMaximumWidth(150);
    projectSort_->addItems({QString::fromUtf8("Название"), QString::fromUtf8("Число задач"),
        QString::fromUtf8("Просрочка"), QString::fromUtf8("Ожидают XP")});
    projectSort_->setCurrentIndex(displaySettings_.projectSortMode);
    filters->addWidget(projectSort_);
    auditSourceFilter_ = new QComboBox;
    auditSourceFilter_->setObjectName("auditSourceFilter");
    auditSourceFilter_->setMaximumWidth(145);
    auditSourceFilter_->addItems({QString::fromUtf8("Все события"), QString::fromUtf8("Задачи"), QString::fromUtf8("Профили")});
    auditSourceFilter_->setCurrentIndex(std::clamp(displaySettings_.auditSourceFilter, 0, 2));
    auditSourceFilter_->setToolTip(QString::fromUtf8("Показывать события выбранного источника аудита"));
    filters->addWidget(auditSourceFilter_);
    logInfo_ = new QCheckBox(QString::fromUtf8("Инфо"));
    logInfo_->setObjectName("logInfo"); logInfo_->setChecked(true);
    filters->addWidget(logInfo_);
    logWarnings_ = new QCheckBox(QString::fromUtf8("Предупреждения"));
    logWarnings_->setObjectName("logWarnings"); logWarnings_->setChecked(true);
    filters->addWidget(logWarnings_);
    logErrors_ = new QCheckBox(QString::fromUtf8("Ошибки"));
    logErrors_->setObjectName("logErrors"); logErrors_->setChecked(true);
    filters->addWidget(logErrors_);
    content->addLayout(filters);
    auditFilters_ = new QWidget;
    auditFilters_->setObjectName("auditFilters");
    auto* auditFilterLayout = new QHBoxLayout(auditFilters_);
    auditFilterLayout->setContentsMargins(0, 0, 0, 0);
    auditFilterLayout->setSpacing(6);
    auditActorFilter_ = new QLineEdit;
    auditActorFilter_->setObjectName("auditActorFilter");
    auditActorFilter_->setPlaceholderText(QString::fromUtf8("Актор"));
    auditActorFilter_->setClearButtonEnabled(true);
    auditFilterLayout->addWidget(auditActorFilter_);
    auditObjectFilter_ = new QLineEdit;
    auditObjectFilter_->setObjectName("auditObjectFilter");
    auditObjectFilter_->setPlaceholderText(QString::fromUtf8("Задача / профиль / значение"));
    auditObjectFilter_->setClearButtonEnabled(true);
    auditFilterLayout->addWidget(auditObjectFilter_, 2);
    auditFieldFilter_ = new QLineEdit;
    auditFieldFilter_->setObjectName("auditFieldFilter");
    auditFieldFilter_->setPlaceholderText(QString::fromUtf8("Поле / действие"));
    auditFieldFilter_->setClearButtonEnabled(true);
    auditFilterLayout->addWidget(auditFieldFilter_);
    auditFilterReset_ = new QPushButton(QString::fromUtf8("Сбросить"));
    auditFilterReset_->setObjectName("auditFilterReset");
    auditFilterReset_->setToolTip(QString::fromUtf8("Очистить поиск и все фильтры аудита"));
    auditFilterLayout->addWidget(auditFilterReset_);
    content->addWidget(auditFilters_);
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
    exportAudit_ = new QPushButton(QString::fromUtf8("Экспорт аудита"));
    exportAudit_->setObjectName("exportAudit");
    exportAudit_->setToolTip(QString::fromUtf8("Сохранить видимые после поиска события аудита в UTF-8 CSV"));
    bottom->addWidget(exportAudit_);
    exportLogs_ = new QPushButton(QString::fromUtf8("Экспорт логов"));
    exportLogs_->setObjectName("exportLogs");
    exportLogs_->setToolTip(QString::fromUtf8("Сохранить найденные сообщения текущей Qt-сессии в UTF-8 TXT"));
    bottom->addWidget(exportLogs_);
    clearLogs_ = new QPushButton(QString::fromUtf8("Очистить логи"));
    clearLogs_->setObjectName("clearLogs");
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
    cloudResolve_ = new QPushButton(QString::fromUtf8("Сравнить версии"));
    cloudResolve_->setObjectName("cloudResolve"); cloudResolve_->setStyleSheet("min-height: 40px; max-height: 40px;");
    cloudResolve_->setToolTip(QString::fromUtf8("Сравнить задачи и пайплайн, принять облачную версию или восстановить локальный снимок"));
    bottom->addWidget(cloudResolve_);
    storageResolve_ = new QPushButton(QString::fromUtf8("Разрешить storage.json"));
    storageResolve_->setObjectName("storageResolve"); storageResolve_->setStyleSheet("min-height: 40px; max-height: 40px;");
    storageResolve_->setToolTip(QString::fromUtf8("Сравнить баланс, журнал и ревизию кошелька и выбрать целую версию"));
    bottom->addWidget(storageResolve_);
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
        if (text.isEmpty()) return;
        const bool failed = text.contains(QString::fromUtf8("не удалось"), Qt::CaseInsensitive) ||
            text.contains(QString::fromUtf8("ошибка"), Qt::CaseInsensitive);
        appendLog(failed ? AppLogLevel::Error : AppLogLevel::Info, "Qt", u(text));
        if (navigation_->currentRow() == Logs) render();
    });

    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(navigation_, &QListWidget::currentRowChanged, this, [this] {
        QSignalBlocker blocker(search_);
        search_->clear();
        saveDisplayContext();
        render();
    });
    connect(profiles_, &QComboBox::currentIndexChanged, this, [this] { profileSession_.lock(); saveDisplayContext(); render(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { render(); });
    connect(statusFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(priorityFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(quickTaskFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskProjectFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(taskPipelineFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(catalogProfessionFilter_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(reportView_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
    connect(reportDateRange_, &QComboBox::currentIndexChanged, this, [this] { saveDisplayContext(); render(); });
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
    connect(logInfo_, &QCheckBox::toggled, this, [this] { render(); });
    connect(logWarnings_, &QCheckBox::toggled, this, [this] { render(); });
    connect(logErrors_, &QCheckBox::toggled, this, [this] { render(); });
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
        bool allowed = table_->selectionModel()->selectedRows().size() >= 2;
        for (const auto& index : table_->selectionModel()->selectedRows()) {
            const auto selectedId = u(table_->item(index.row(), 0)->data(Qt::UserRole).toString());
            const auto task = std::find_if(workspace_.data.tasks.begin(), workspace_.data.tasks.end(),
                [&](const auto& item) { return item.id == selectedId; });
            if (task == workspace_.data.tasks.end()) allowed = false;
        }
        bulkEdit_->setEnabled(navigation_->currentRow() == Tasks && admin_ && allowed);
    });
    connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
        if (navigation_->currentRow() != Statistics) return;
        if (!detailsToggle_->isChecked()) detailsToggle_->setChecked(true);
        details();
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
    connect(cloudResolve_, &QPushButton::clicked, this, [this] { resolveCloudConflict(); });
    connect(storageResolve_, &QPushButton::clicked, this, [this] { resolveStorageConflict(); });
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
        auto result = AppRemoveEvilSpiritForCoins(*workspace_.storage, id, id,
            workspace_.directory, workspace_.data.vault, 200.0);
        if (!result.ok) { message(result.errorMessage.empty() ? u8"Не удалось снять Злого духа." : result.errorMessage); return; }
        if (!AppendProfileAudit(workspace_.directory, id, "spirit_purchase", "evil->none cost=200"))
            statusBar()->showMessage(QString::fromUtf8("Дух снят, но запись в историю кошелька не сохранена."), 7000);
        reload();
    });
    connect(advanceStage_, &QPushButton::clicked, this, [this] {
        if (!requireAdmin() || !workspace_.modules.pipeline || navigation_->currentRow() != Tasks) return;
        if (ShowPipelineTransition(this, workspace_, u(selectedId()))) reload();
    });
    connect(changeStatus_, &QPushButton::clicked, this, [this] { changeStatus(); });
    connect(bulkEdit_, &QPushButton::clicked, this, [this] { bulkEditTasks(); });
    connect(exportReport_, &QPushButton::clicked, this, [this] { exportReport(); });
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
    bindShortcut("shortcutEdit", QKeySequence(QStringLiteral("Ctrl+E")), [this] { createEntry(true); });
    bindShortcut("shortcutDelete", QKeySequence::Delete, [this] { deleteEntry(); });
    bindShortcut("shortcutRefresh", QKeySequence::Refresh, [this] { reload(); });
    bindShortcut("shortcutDetails", QKeySequence(QStringLiteral("Ctrl+I")), [this] {
        if (detailsToggle_->isVisible() && detailsToggle_->isEnabled()) detailsToggle_->toggle();
    });
    bindShortcut("shortcutHelp", QKeySequence(QStringLiteral("Ctrl+/")), [this] { showShortcutHelp(); });
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
    auto* table = new QTableWidget(13, 2, &dialog);
    table->setObjectName("shortcutHelpTable");
    table->setHorizontalHeaderLabels({QString::fromUtf8("Клавиша"), QString::fromUtf8("Действие")});
    const std::vector<std::pair<QString, QString>> rows = {
        {"F1", QString::fromUtf8("Профиль")}, {"F2", QString::fromUtf8("Навыки")},
        {"F3", QString::fromUtf8("Пайплайн")}, {"F4", QString::fromUtf8("Правила")},
        {"F5", QString::fromUtf8("Статистика")}, {"F6", QString::fromUtf8("Аудит")},
        {"Ctrl+N", QString::fromUtf8("Создать запись")}, {"Ctrl+E", QString::fromUtf8("Редактировать выбранную запись")},
        {"Delete", QString::fromUtf8("Удалить выбранный проект или этап")}, {"Ctrl+R", QString::fromUtf8("Перечитать локальные данные")},
        {"Ctrl+I", QString::fromUtf8("Показать или скрыть подробности")}, {"Ctrl+/", QString::fromUtf8("Открыть эту памятку")},
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
    appLogs_.push_back({QDateTime::currentSecsSinceEpoch(), level, source, text});
    constexpr size_t maxEntries = 200;
    if (appLogs_.size() > maxEntries) appLogs_.erase(appLogs_.begin());
    appLogPersistenceWarning_ = !saveAppLogs();
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
            u(entry.value("source").toString()), u(entry.value("message").toString())});
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
        if (!profileSession_.lock(true)) message(u8"Не удалось удалить доверенный вход. Он может восстановиться после обновления.");
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
        else { notice->setText(QString::fromUtf8("Неверный пароль или профиль недоступен. Для восстановления обратитесь к администратору.")); password->setFocus(); }
    });
    dialog.exec();
    render();
}

void QtWindow::authenticate() {
    if (admin_) {
        admin_ = false;
    } else {
        bool ok = false;
        const auto password = QInputDialog::getText(this, QString::fromUtf8("Администратор"),
            QString::fromUtf8("Пароль администратора:"), QLineEdit::Password, {}, &ok);
        if (!ok) return;
        if (u(password) != LoadAdminPassword(workspace_.directory)) {
            message(u8"Неверный пароль.");
            return;
        }
        admin_ = true;
    }
    render();
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
    displaySettings_.taskStatusFilter = statusFilter_->currentIndex();
    displaySettings_.taskPriorityFilter = priorityFilter_->currentIndex();
    displaySettings_.taskQuickFilter = quickTaskFilter_->currentIndex();
    displaySettings_.taskProjectId = taskProjectFilter_->currentData().toString();
    displaySettings_.taskPipelineStepId = taskPipelineFilter_->currentData().toString();
    displaySettings_.catalogProfessionId = catalogProfessionFilter_->currentData().toString();
    displaySettings_.reportView = reportView_->currentIndex();
    displaySettings_.reportDateRange = reportDateRange_->currentIndex();
    displaySettings_.reportDateFrom = reportFrom_->date();
    displaySettings_.reportDateTo = reportTo_->date();
    displaySettings_.projectSortMode = projectSort_->currentIndex();
    displaySettings_.projectsOverdueOnly = projectsOverdue_->isChecked();
    displaySettings_.projectsXpPendingOnly = projectsXpPending_->isChecked();
    displaySettings_.auditSourceFilter = auditSourceFilter_->currentIndex();
    if (!SaveQtDisplaySettings(workspace_.directory, displaySettings_))
        statusBar()->showMessage(QString::fromUtf8("Не удалось сохранить последний раздел и профиль."), 5000);
}

void QtWindow::refreshTaskFilterChoices() {
    const auto selectedProject = taskProjectFilter_->currentData().toString().isEmpty()
        ? displaySettings_.taskProjectId : taskProjectFilter_->currentData().toString();
    const auto selectedPipeline = taskPipelineFilter_->currentData().toString().isEmpty()
        ? displaySettings_.taskPipelineStepId : taskPipelineFilter_->currentData().toString();
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
    ownPasswordAction_->setEnabled(unlocked);
    navigation_->item(ModelViewerPage)->setHidden(!workspace_.modules.view3d);
    navigation_->item(ModelSettingsPage)->setHidden(!workspace_.modules.view3d || !admin_);
    navigation_->item(Tasks)->setHidden(!workspace_.modules.tasks);
    navigation_->item(Pipeline)->setHidden(!workspace_.modules.pipeline);
    navigation_->item(Pomodoro)->setHidden(!workspace_.modules.pomodoro);
    navigation_->item(Shortcuts)->setHidden(!workspace_.modules.shortcuts);
    navigation_->item(Professions)->setHidden(!workspace_.modules.professions || !admin_);
    for (int page : {Projects, Statistics, Audit, Rules, Vault, Banner}) navigation_->item(page)->setHidden(!admin_);
    int page = navigation_->currentRow();
    if (page < 0) return;
    if (navigation_->item(page)->isHidden()) {
        navigation_->setCurrentRow(ProfilePage);
        return;
    }
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
    mode_->setText(admin_ ? QString::fromUtf8("Администратор · Qt") : QString::fromUtf8(unlocked ?
        (profileSession_.isTrusted() ? "Доверенный доступ · Qt" : "Личный доступ · Qt") : "Просмотр · Qt"));
    const bool timerPage = page == Pomodoro;
    const bool modelPage = page == ModelViewerPage || page == ModelSettingsPage;
    summary_->setVisible(!timerPage && !modelPage);
    statisticsChart_->setVisible(page == Statistics);
    search_->setVisible(!timerPage && !modelPage);
    table_->setVisible(!timerPage && !modelPage);
    bottomActions_->setVisible(!timerPage && !modelPage);
    details_->setVisible(!timerPage && !modelPage && details_->isVisible());
    pomodoro_->setVisible(timerPage);
    modelPage_->setVisible(page == ModelViewerPage);
    modelSettingsPage_->setVisible(page == ModelSettingsPage);
    static_cast<QtPomodoro*>(pomodoro_)->setAdministrator(admin_);
    statusFilter_->setVisible(page == Tasks);
    priorityFilter_->setVisible(page == Tasks);
    quickTaskFilter_->setVisible(page == Tasks);
    taskProjectFilter_->setVisible(page == Tasks);
    taskPipelineFilter_->setVisible(page == Tasks);
    catalogProfessionFilter_->setVisible(page == Catalog);
    reportView_->setVisible(page == Statistics);
    reportDateRange_->setVisible(page == Statistics);
    reportCustomRange_->setVisible(page == Statistics && reportDateRange_->currentIndex() == 4);
    projectsOverdue_->setVisible(page == Projects);
    projectsXpPending_->setVisible(page == Projects);
    projectSort_->setVisible(page == Projects);
    auditSourceFilter_->setVisible(page == Audit);
    auditFilters_->setVisible(page == Audit);
    logInfo_->setVisible(page == Logs);
    logWarnings_->setVisible(page == Logs);
    logErrors_->setVisible(page == Logs);
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
    cloudResolve_->setVisible(page == Cloud);
    storageResolve_->setVisible(page == Cloud);
    profileMetrics_->setVisible(page == ProfilePage);
    achievements_->setVisible(page == ProfilePage);
    achievements_->setEnabled(!profiles_->currentData().toString().isEmpty());
    removeSpirit_->setVisible(page == ProfilePage && unlocked);
    exportReport_->setVisible(admin_ && page == Statistics);
    exportAudit_->setVisible(admin_ && page == Audit);
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
        headers({QString::fromUtf8("Навык"), QString::fromUtf8("Уровень"), "XP", QString::fromUtf8("Вес")});
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
            for (const auto& skill : profile->list_skills())
                row(skill.name, {q(workspace_.catalog.display_name(skill.name)), QString::number(skill.level),
                    QString::number(skill.xp), QString::number(skill.weight)});
        } else summary_->setText(QString::fromUtf8("Нет доступного профиля. Администратор может создать его через «Управление профилями»."));
    } else if (page == Tasks) {
        headers({QString::fromUtf8("Задача"), QString::fromUtf8("Проект"), QString::fromUtf8("Статус"),
                 QString::fromUtf8("Приоритет"), QString::fromUtf8("Срок"), QString::fromUtf8("Пайплайн")});
        const auto selectedProject = u(taskProjectFilter_->currentData().toString());
        const auto selectedPipeline = u(taskPipelineFilter_->currentData().toString());
        const int quickFilter = quickTaskFilter_->currentIndex();
        const auto now = QDateTime::currentDateTime();
        const auto todayStart = now.date().startOfDay(now.timeZone()).toSecsSinceEpoch();
        const auto tomorrowStart = now.date().addDays(1).startOfDay(now.timeZone()).toSecsSinceEpoch();
        const auto nextWeekStart = now.date().addDays(7).startOfDay(now.timeZone()).toSecsSinceEpoch();
        const auto nowSeconds = now.toSecsSinceEpoch();
        const auto activeProfileId = u(profiles_->currentData().toString());
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
            const auto taskStatus = AppNormalizeTaskStatus(task.status);
            const bool assignedToProfile = !activeProfileId.empty() &&
                (std::find(task.assignees.begin(), task.assignees.end(), activeProfileId) != task.assignees.end() ||
                 std::any_of(task.participants.begin(), task.participants.end(), [&activeProfileId](const auto& item) { return item.profileId == activeProfileId; }));
            const bool needsXp = taskStatus == 2 && std::none_of(task.participants.begin(), task.participants.end(),
                [](const auto& item) { return item.globalXp > 0 || item.skillXp > 0; });
            if (quickFilter == 1 && !assignedToProfile) continue;
            if (quickFilter == 2 && (task.deadlineAt < todayStart || task.deadlineAt >= tomorrowStart)) continue;
            if (quickFilter == 3 && (task.deadlineAt <= 0 || task.deadlineAt >= nowSeconds || taskStatus == 2)) continue;
            if (quickFilter == 4 && (task.deadlineAt < todayStart || task.deadlineAt >= nextWeekStart)) continue;
            if (quickFilter == 5 && !resolvedProjectName.empty()) continue;
            if (quickFilter == 6 && !needsXp) continue;
            if (quickFilter == 7 && taskStatus == 2) continue;
            row(task.id, {q(AppTaskDisplayTitle(task)), q(project == data.projects.end() ? task.project : project->name), q(AppTaskStatusLabel(task.status)),
                q(AppTaskPriorityLabel(task.priority)), timeText(task.deadlineAt), q(stage == data.pipelineSteps.end() ? task.pipelineStep : stage->title)});
        }
        const auto report = BuildTeamValueReport(data.tasks, data.projects, QDateTime::currentSecsSinceEpoch());
        summary_->setText(QString::fromUtf8("Активных: %1  ·  просрочено: %2  ·  ждут XP: %3  ·  показано: %4")
            .arg(report.activeTasks).arg(report.overdueTasks).arg(report.xpPendingTasks).arg(table_->rowCount()));
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
    } else if (page == Statistics) {
        int missingCreationDates = 0;
        const auto reportTasks = reportTasksForRange(data.tasks, reportDateRange_->currentIndex(),
            reportFrom_->date(), reportTo_->date(), &missingCreationDates);
        const auto report = BuildTeamValueReport(reportTasks, data.projects, QDateTime::currentSecsSinceEpoch());
        const auto periodLabel = reportPeriodLabel(reportDateRange_->currentIndex(), reportFrom_->date(), reportTo_->date());
        statisticsChart_->setValues(report.newTasks, report.inProgressTasks, report.doneTasks, periodLabel);
        statisticsChart_->setCompletionTrend(QtReportChart::BuildMonthlyCompletionTrend(data.taskAudit));
        const auto missingNote = missingCreationDates
            ? QString::fromUtf8(" · без даты создания исключено: %1").arg(missingCreationDates) : QString();
        if (reportView_->currentIndex() == 0) {
            headers({QString::fromUtf8("Проект"), QString::fromUtf8("Активно"), QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP")});
            for (const auto& item : report.projects) row(item.id.empty() ? "__no_project" : item.id, {q(item.name), QString::number(item.activeTasks),
                QString::number(item.doneTasks), QString::number(item.overdueTasks), QString::number(item.xpPendingTasks)});
            summary_->setText(QString::fromUtf8("%1 · задач: %2 · проектов в каталоге: %3 · XP: %4 · состояние на сейчас%5")
                .arg(periodLabel).arg(report.totalTasks).arg(report.totalProjects).arg(report.totalGlobalXp).arg(missingNote));
        } else {
            headers({QString::fromUtf8("Сотрудник"), "ID", QString::fromUtf8("Активно"), QString::fromUtf8("Выполнено"),
                QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP"), QString::fromUtf8("Глобальный XP"), QString::fromUtf8("XP навыков")});
            for (const auto& item : report.assignees) {
                const auto profile = std::find_if(workspace_.profiles.begin(), workspace_.profiles.end(),
                    [&](const auto& value) { return value.id == item.profileId; });
                row(item.profileId, {profile == workspace_.profiles.end() ? q(item.profileId) : q(profile->name), q(item.profileId),
                    QString::number(item.activeTasks), QString::number(item.doneTasks), QString::number(item.overdueTasks),
                    QString::number(item.xpPendingTasks), QString::number(item.totalGlobalXp), QString::number(item.totalSkillXp)});
            }
            summary_->setText(QString::fromUtf8("%1 · сотрудников в задачах: %2 · без исполнителя: %3 · XP: %4 · состояние на сейчас%5")
                .arg(periodLabel).arg(int(report.assignees.size())).arg(report.unassignedTasks).arg(report.totalGlobalXp).arg(missingNote));
        }
    } else if (page == Audit) {
        headers({QString::fromUtf8("Источник"), QString::fromUtf8("Время"), QString::fromUtf8("Автор"), QString::fromUtf8("Объект"),
            QString::fromUtf8("Поле"), QString::fromUtf8("Было"), QString::fromUtf8("Стало")});
        struct AuditDisplayRow { std::int64_t timestamp; int source; std::string id; QStringList values; };
        std::vector<AuditDisplayRow> entries;
        entries.reserve(data.taskAudit.size());
        for (const auto& entry : data.taskAudit) entries.push_back({entry.timestamp, 1, entry.taskId,
            {QString::fromUtf8("Задача"), timeText(entry.timestamp), q(entry.actor), q(entry.taskId), q(entry.field), q(entry.oldValue), q(entry.newValue)}});
        const auto profileEntries = profileAudit(workspace_.directory);
        entries.reserve(entries.size() + profileEntries.size());
        for (const auto& entry : profileEntries) entries.push_back({entry.timestamp, 2, entry.profile,
            {QString::fromUtf8("Профиль"), timeText(entry.timestamp), QString::fromUtf8("локально"), q(entry.profile), q(entry.action), QString(), q(entry.details)}});
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
        headers({QString::fromUtf8("#"), QString::fromUtf8("Время"), QString::fromUtf8("Уровень"),
            QString::fromUtf8("Источник"), QString::fromUtf8("Сообщение")});
        int total = 0;
        int visible = 0;
        for (auto it = appLogs_.rbegin(); it != appLogs_.rend(); ++it) {
            ++total;
            const bool enabled = it->level == AppLogLevel::Info ? logInfo_->isChecked()
                : it->level == AppLogLevel::Warning ? logWarnings_->isChecked() : logErrors_->isChecked();
            if (!enabled) continue;
            const QString level = it->level == AppLogLevel::Info ? QString::fromUtf8("Инфо")
                : it->level == AppLogLevel::Warning ? QString::fromUtf8("Предупреждение") : QString::fromUtf8("Ошибка");
            const QStringList values{QString::number(total), timeText(it->timestamp), level, q(it->source), q(it->message)};
            if (!values.join(' ').contains(search_->text(), Qt::CaseInsensitive)) continue;
            ++visible;
            row(std::to_string(total), values);
        }
        summary_->setText(QString::fromUtf8("Показано: %1 из %2 · история между запусками · %3")
            .arg(visible).arg(total).arg(appLogPersistenceWarning_ ? QString::fromUtf8("ошибка сохранения") : QString::fromUtf8("сохранено локально")));
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
        row("auto", {QString::fromUtf8("Автоматизация стабильной версии"), QString::fromUtf8("pull: %1 · push: %2 · каждые %3 мин")
            .arg(config.autoPull ? QString::fromUtf8("да") : QString::fromUtf8("нет"))
            .arg(config.autoPush ? QString::fromUtf8("да") : QString::fromUtf8("нет")).arg(config.autoSyncMinutes)});
        int driftCount = 0;
        if (config.enabled && rootExists) {
            const auto drift = InspectCloudWorkspaceDrift(config, workspace_.directory, 0); driftCount = drift.issueCount;
            row("drift", {QString::fromUtf8("Различия до первой синхронизации"), QString::number(drift.issueCount)});
        }
        const auto manifest = LoadCloudManifest(config, workspace_.directory);
        row("manifest", {QString::fromUtf8("Версия в manifest"), manifest.appVersion.empty() ? QString::fromUtf8("—") : q(manifest.appVersion)});
        cloudPull_->setEnabled(config.enabled && rootExists);
        const bool hasBackups = !ListCloudWorkspaceBackups(workspace_.directory).empty();
        cloudResolve_->setEnabled((config.enabled && rootExists && driftCount > 0) || hasBackups);
        storageResolve_->setEnabled(admin_ && config.enabled && rootExists && HasQtStorageConflict(workspace_.directory));
        summary_->setText(QString::fromUtf8("Ручные pull и отправка отдельных tasks/pipeline требуют подтверждения и резервной копии · автоматическая синхронизация заблокирована"));
    }
    if (summary_->text().isEmpty()) summary_->setText(QString::fromUtf8("Записей: %1 · просмотр данных существующего ядра").arg(table_->rowCount()));
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for (int col = 0; col < table_->columnCount(); ++col)
        table_->setColumnWidth(col, std::clamp(table_->columnWidth(col), 96, 280));
    // Give free width to readable content instead of stretching the final numeric column.
    int stretchColumn = 0;
    if (page == Catalog) stretchColumn = 2;
    else if (page == Pipeline || page == Projects || page == Professions || page == Shortcuts) stretchColumn = 1;
    table_->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
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
    if (!result.ok) { message(result.errorMessage.empty() ? u8"Не удалось пересчитать профили." : result.errorMessage); return; }
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
        if (!result.ok) { notice->setText(q(result.errorMessage)); return; }
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
        const auto result = AppAdjustProfileWallet(*workspace_.storage, profileId, profileId, debit ? -value : value);
        if (!result.ok || !result.profile) {
            notice->setText(result.errorMessage.empty() ? QString::fromUtf8("Не удалось сохранить кошелёк.") : q(result.errorMessage));
            return;
        }
        const QString audit = QStringLiteral("%1|%2|%3")
            .arg(debit ? QStringLiteral("debit") : QStringLiteral("credit"))
            .arg(value, 0, 'f', 2).arg(memo);
        const bool auditRecorded = AppendProfileAudit(workspace_.directory, profileId, "wallet_adjustment", u(audit));
        dialog.setProperty("walletAuditRecorded", auditRecorded);
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const bool auditRecorded = dialog.property("walletAuditRecorded").toBool();
    reload();
    statusBar()->showMessage(auditRecorded
        ? QString::fromUtf8("Кошелёк профиля обновлён, запись добавлена в аудит.")
        : QString::fromUtf8("Баланс обновлён, но запись в аудит не удалось сохранить."), 7000);
}

void QtWindow::showProfileHistory() {
    const auto profileId = u(profiles_->currentData().toString());
    if (navigation_->currentRow() != ProfilePage || profileId.empty() ||
        (!admin_ && !profileSession_.isUnlocked(*workspace_.storage, profileId))) return;

    QDialog dialog(this);
    dialog.setObjectName("profileActivityHistoryDialog");
    dialog.setWindowTitle(QString::fromUtf8("История событий профиля"));
    dialog.resize(820, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(QString::fromUtf8("Локальные события входа, управления профилем и кошельком · читаются последние 500 событий аудита по рабочему пространству. История задач и XP ведётся отдельно."));
    summary->setObjectName("profileActivityHistorySummary");
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto* table = new QTableWidget(&dialog);
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
    layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
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
    const auto tasks = reportTasksForRange(workspace_.data.tasks, reportDateRange_->currentIndex(),
        reportFrom_->date(), reportTo_->date());
    const auto report = BuildTeamValueReport(tasks, workspace_.data.projects, QDateTime::currentSecsSinceEpoch());
    QString error;
    if (!ExportTeamValueReportCsv(path, report, &error)) { message(error.toUtf8().toStdString()); return; }
    statusBar()->showMessage(QString::fromUtf8("Отчёт сохранён: %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void QtWindow::exportAudit() {
    if (!requireAdmin() || navigation_->currentRow() != Audit) return;
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

void QtWindow::exportLogs() {
    if (navigation_->currentRow() != Logs) return;
    const auto suggested = QString::fromUtf8("ForgeMirror-app-log-%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFileDialog dialog(this, QString::fromUtf8("Экспорт логов Qt-сессии"));
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
    for (auto it = appLogs_.rbegin(); it != appLogs_.rend(); ++it) {
        const bool enabled = it->level == AppLogLevel::Info ? logInfo_->isChecked()
            : it->level == AppLogLevel::Warning ? logWarnings_->isChecked() : logErrors_->isChecked();
        const QString level = it->level == AppLogLevel::Info ? QString::fromUtf8("Инфо")
            : it->level == AppLogLevel::Warning ? QString::fromUtf8("Предупреждение") : QString::fromUtf8("Ошибка");
        const QStringList values{QString::number(count + 1), timeText(it->timestamp), level, q(it->source), q(it->message)};
        if (!enabled || !values.join(' ').contains(search_->text(), Qt::CaseInsensitive)) continue;
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
        message(u8"Не удалось сохранить журнал Qt-сессии.");
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
        const auto targetName = table_->item(table_->currentRow(), 0)->text();
        const auto reportTasks = reportTasksForRange(workspace_.data.tasks, reportDateRange_->currentIndex(),
            reportFrom_->date(), reportTo_->date());
        QString html = field(employeeView ? QString::fromUtf8("Сотрудник") : QString::fromUtf8("Проект"), u(targetName));
        int matched = 0;
        for (const auto& task : reportTasks) {
            bool belongs = false;
            if (employeeView) {
                belongs = std::find(task.assignees.begin(), task.assignees.end(), targetKey) != task.assignees.end() ||
                    std::any_of(task.participants.begin(), task.participants.end(), [&](const auto& participant) {
                        return participant.profileId == targetKey;
                    });
            } else {
                const std::string projectKey = !task.projectId.empty() ? task.projectId :
                    (task.project.empty() ? "__no_project" : "name:" + task.project);
                belongs = projectKey == targetKey;
            }
            if (!belongs) continue;
            ++matched;
            int globalXp = 0, skillXp = 0;
            for (const auto& participant : task.participants) {
                if (employeeView && participant.profileId != targetKey) continue;
                globalXp += std::max(0, participant.globalXp);
                skillXp += std::max(0, participant.skillXp);
            }
            const QString taskTitle = QString::fromUtf8("%1 · %2")
                .arg(q(AppTaskDisplayTitle(task)), q(AppTaskStatusLabel(task.status)));
            html += field(taskTitle, u(timeText(task.createdAt))) + field(QString::fromUtf8("Срок"), u(timeText(task.deadlineAt)))
                + field(QString::fromUtf8("XP"), std::to_string(globalXp) + " / " + std::to_string(skillXp) + u8" (глобальный / навыки)");
        }
        if (!matched) html += QString::fromUtf8("<p>В выбранном периоде связанных задач нет.</p>");
        else html.prepend(QString::fromUtf8("<p>Задач в выбранном периоде: %1</p>").arg(matched));
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
        if (ShowProfessionEditor(this, workspace_, edit ? u(selectedId()) : std::string())) reload();
        return;
    }
    if (navigation_->currentRow() == Pipeline) {
        if (ShowPipelineEditor(this, workspace_, edit ? u(selectedId()) : std::string())) reload();
        return;
    }
    if (navigation_->currentRow() == Catalog) {
        if (ShowSkillEditor(this, workspace_, edit ? u(selectedId()) : std::string())) reload();
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
            if (!result.ok) { message(result.errorMessage); return; }
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
            if (!result.ok) { message(result.errorMessage); return; }
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) reload();
}

void QtWindow::pullCloud() {
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
        if (pending) {
            setEnabled(false);
            for (auto* timer : findChildren<QTimer*>()) timer->stop();
        }
        message(result.message);
        if (pending) QCoreApplication::exit(1);
        return;
    }
    profileSession_.lock();
    if (!reload()) return;
    statusBar()->showMessage(q(result.message), 15000);
    if (result.sync.storageConflict) {
        QMessageBox::warning(this, QString::fromUtf8("Конфликт storage.json"), q(result.message));
    }
}

void QtWindow::resolveCloudConflict() {
    if (!ShowCloudConflictResolver(this, workspace_.directory)) return;
    profileSession_.lock();
    if (reload()) statusBar()->showMessage(QString::fromUtf8("Локальная версия обновлена; облако не изменялось."), 15000);
}

void QtWindow::resolveStorageConflict() {
    if (!requireAdmin()) return;
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
            std::string text = error.what();
            if (prepared) {
                try { RecoverTaskCompletion(workspace_.directory); text += u8" Изменения полностью отменены."; }
                catch (const std::exception&) { text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
            }
            reload(); message(text); return;
        }
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
        int linkedProfiles = 0;
        std::vector<std::string> activeProfileIds;
        for (const auto& info : workspace_.profiles) {
            if (info.archived) {
                if (archivedProfileUsesProfession(workspace_.directory, info.id, id)) {
                    message(u8"Профессия назначена архивному профилю. Сначала восстановите профиль и снимите профессию."); return;
                }
                continue;
            }
            activeProfileIds.push_back(info.id);
            if (workspace_.storage->set_active_profile(info.id)) {
                const auto profile = workspace_.storage->load_profile();
                if (profile && profile->profession_id() == id) ++linkedProfiles;
            }
        }
        workspace_.storage->set_active_profile(u(profiles_->currentData().toString()));
        int linkedSkills = 0;
        for (const auto& skillId : workspace_.catalog.skills()) if (workspace_.catalog.has_profession(skillId, id)) ++linkedSkills;
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить профессию"),
            QString::fromUtf8("Удалить профессию «%1»?\nСвязи будут сняты: профили — %2, навыки — %3.")
                .arg(q(profession->name)).arg(linkedProfiles).arg(linkedSkills), QMessageBox::Yes | QMessageBox::No, this);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        bool prepared = false;
        AppProfessionMutationResult result;
        try {
            PrepareProfessionDeletionRecovery(workspace_.directory, activeProfileIds);
            prepared = true;
            result = AppDeleteProfessionEntry(workspace_.directory, workspace_.data.professions, *workspace_.storage,
                workspace_.profiles, workspace_.catalog, u(profiles_->currentData().toString()), id);
            if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось удалить профессию." : result.errorMessage);
            CommitQtRecoveryTransaction(workspace_.directory);
        } catch (const std::exception& error) {
            std::string text = error.what();
            if (prepared) {
                try { RecoverTaskCompletion(workspace_.directory); text += u8" Изменения полностью отменены."; }
                catch (const std::exception&) { text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
            }
            reload();
            message(text);
            return;
        }
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
        if (!result.ok) { message(result.errorMessage); return; }
        reload();
        statusBar()->showMessage(QString::fromUtf8("Задача удалена"), 4000);
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
        if (!result.ok) { message(result.errorMessage.empty() ? u8"Не удалось удалить этап." : result.errorMessage); return; }
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
    if (!result.ok) { message(result.errorMessage.empty() ? u8"Не удалось изменить порядок этапов." : result.errorMessage); return; }
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
        reload();
        message(result.errorMessage.empty() ? std::string(u8"Не удалось применить массовое изменение.") : result.errorMessage);
        return;
    }
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
        ShowTaskCompletionDialog(this, workspace_, q(id), profiles_->currentData().toString());
        reload();
        return;
    }
    const auto result = UpdateTaskStatusWithRecovery(workspace_.directory, workspace_.data.tasks,
        workspace_.data.taskAudit, id, next, "admin/qt");
    if (!result.ok) message(result.errorMessage);
    else reload();
}
