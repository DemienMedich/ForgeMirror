#include "QtWindow.h"
#include "QtAchievements.h"
#include "QtPomodoro.h"
#include "QtProfessionEditor.h"
#include "QtRulesEditor.h"
#include "QtVaultEditor.h"
#include "QtBannerEditor.h"
#include "QtReportExport.h"
#include "QtPipelineTransition.h"
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
#include <QtWidgets>
#include <algorithm>

namespace {
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
enum Page { ProfilePage, Tasks, Projects, Catalog, Pipeline, Professions, Statistics, Audit, Pomodoro, Rules, Vault, Shortcuts, Banner };
struct ProfileAuditRow { std::int64_t timestamp; std::string profile; std::string action; std::string details; };
std::vector<ProfileAuditRow> profileAudit(const std::filesystem::path& directory) {
    const auto path = q((directory / "meta/profile-audit.log").u8string());
    if (QFileInfo(path).isSymLink()) return {};
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    auto lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts); if (lines.size() > 500) lines = lines.mid(lines.size() - 500);
    std::vector<ProfileAuditRow> out;
    for (const auto& line : lines) {
        const auto fields = line.split('|'); bool ok = false; const auto timestamp = fields.value(0).toLongLong(&ok);
        if (ok && fields.size() >= 3) out.push_back({timestamp, u(fields[1]), u(fields[2]), u(fields.value(3))});
    }
    return out;
}
}

QtWindow::QtWindow(QtWorkspace& workspace) : workspace_(workspace), profileSession_(workspace.directory), displaySettings_(LoadQtDisplaySettings(workspace.directory)) {
    ApplyQtDisplaySettings(*qApp, displaySettings_);
    setWindowTitle(QString::fromUtf8("ForgeMirror · Qt migration · ") + APP_VERSION);
    resize(1120, 720);
    setMinimumSize(800, 520);
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
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
        ApplyQtDisplaySettings(*qApp, displaySettings_); render();
    });
    displaySettings->setObjectName("qtDisplaySettingsAction");
    auto* shortcutHelp = menu->addAction(QString::fromUtf8("Горячие клавиши"), this, [this] { showShortcutHelp(); });
    shortcutHelp->setObjectName("shortcutHelpAction");
    menu->addAction(QString::fromUtf8("О переносе"), this, [this] {
        QMessageBox::information(this, QString::fromUtf8("Перенос на Qt"), QString::fromUtf8(
            "Перенос ещё не завершён; это не замена стабильной версии.\n"
            "Qt работает с отдельной копией данных. Облачная синхронизация отключена.\n"
            "3D и оставшиеся настройки ещё не перенесены."));
    });
    menuButton->setMenu(menu);
    header->addWidget(menuButton);
    layout->addLayout(header);
    banner_ = new QLabel;
    banner_->setObjectName("bannerStrip"); banner_->setAlignment(Qt::AlignCenter); banner_->setFixedHeight(28);
    banner_->setProperty("banner", true); layout->addWidget(banner_);
    auto* bannerTimer = new QTimer(this); bannerTimer->setInterval(60000);
    connect(bannerTimer, &QTimer::timeout, this, [this] { ++bannerIndex_; updateBanner(); }); bannerTimer->start();

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->addItems({QString::fromUtf8("Профиль  F1"), QString::fromUtf8("Задачи"),
        QString::fromUtf8("Проекты"), QString::fromUtf8("Навыки  F2"), QString::fromUtf8("Пайплайн  F3"),
        QString::fromUtf8("Профессии"), QString::fromUtf8("Статистика  F5"), QString::fromUtf8("Аудит  F6"),
        QString::fromUtf8("Pomodoro"), QString::fromUtf8("Правила  F4"), QString::fromUtf8("Хранилище"), QString::fromUtf8("Ярлыки"), QString::fromUtf8("Баннер")});
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
        reload();
        return QString::fromUtf8("Начислено Кукоинов: +%1").arg(amount);
    });
    content->addWidget(pomodoro_, 1);
    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setObjectName("search");
    search_->setPlaceholderText(QString::fromUtf8("Поиск по текущему разделу…"));
    search_->setClearButtonEnabled(true);
    filters->addWidget(search_);
    statusFilter_ = new QComboBox;
    statusFilter_->setObjectName("statusFilter");
    statusFilter_->addItems({QString::fromUtf8("Все статусы"), QString::fromUtf8("Новая"),
                            QString::fromUtf8("В работе"), QString::fromUtf8("Выполнена")});
    filters->addWidget(statusFilter_);
    reportView_ = new QComboBox;
    reportView_->setObjectName("reportView");
    reportView_->addItems({QString::fromUtf8("По проектам"), QString::fromUtf8("По сотрудникам")});
    filters->addWidget(reportView_);
    content->addLayout(filters);
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
    reapplyRules_ = new QPushButton(QString::fromUtf8("Пересчитать профили"));
    reapplyRules_->setObjectName("reapplyRules");
    reapplyRules_->setToolTip(QString::fromUtf8("Сохранить общий XP и пересчитать уровни всех активных и архивных профилей по текущим правилам"));
    bottom->addWidget(reapplyRules_);
    directXp_ = new QPushButton(QString::fromUtf8("Добавить XP"));
    directXp_->setObjectName("directXp");
    directXp_->setToolTip(QString::fromUtf8("Вручную начислить XP одному навыку выбранного активного профиля"));
    bottom->addWidget(directXp_);
    openShortcut_ = new QPushButton(QString::fromUtf8("Открыть"));
    openShortcut_->setObjectName("openShortcut");
    openShortcut_->setToolTip(QString::fromUtf8("Открыть выбранный локальный файл через Windows"));
    bottom->addWidget(openShortcut_);
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

    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(navigation_, &QListWidget::currentRowChanged, this, [this] {
        QSignalBlocker blocker(search_);
        search_->clear();
        render();
    });
    connect(profiles_, &QComboBox::currentIndexChanged, this, [this] { profileSession_.lock(); render(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { render(); });
    connect(statusFilter_, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(reportView_, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { details(); });
    connect(detailsToggle_, &QPushButton::toggled, details_, &QWidget::setVisible);
    connect(primary_, &QPushButton::clicked, this, [this] { createEntry(); });
    connect(editEntry_, &QPushButton::clicked, this, [this] { createEntry(true); });
    connect(deleteEntry_, &QPushButton::clicked, this, [this] { deleteEntry(); });
    connect(moveUp_, &QPushButton::clicked, this, [this] { movePipeline(-1); });
    connect(moveDown_, &QPushButton::clicked, this, [this] { movePipeline(1); });
    connect(openShortcut_, &QPushButton::clicked, this, [this] {
        if (navigation_->currentRow() != Shortcuts) return;
        const auto found = std::find_if(workspace_.data.shortcuts.begin(), workspace_.data.shortcuts.end(),
            [this](const auto& entry) { return entry.id == u(selectedId()); });
        std::error_code ec;
        const bool exists = found != workspace_.data.shortcuts.end() &&
            std::filesystem::exists(std::filesystem::u8path(found->path), ec) && !ec;
        if (!exists || !QDesktopServices::openUrl(QUrl::fromLocalFile(q(found->path)))) message(u8"Не удалось открыть ярлык.");
    });
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
        reload();
    });
    connect(advanceStage_, &QPushButton::clicked, this, [this] {
        if (!requireAdmin() || !workspace_.modules.pipeline || navigation_->currentRow() != Tasks) return;
        if (ShowPipelineTransition(this, workspace_, u(selectedId()))) reload();
    });
    connect(changeStatus_, &QPushButton::clicked, this, [this] { changeStatus(); });
    connect(exportReport_, &QPushButton::clicked, this, [this] { exportReport(); });
    connect(reapplyRules_, &QPushButton::clicked, this, [this] { reapplyRules(); });
    connect(directXp_, &QPushButton::clicked, this, [this] { grantDirectXp(); });
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
    navigation_->setCurrentRow(ProfilePage);
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
    auto* table = new QTableWidget(12, 2, &dialog);
    table->setObjectName("shortcutHelpTable");
    table->setHorizontalHeaderLabels({QString::fromUtf8("Клавиша"), QString::fromUtf8("Действие")});
    const std::vector<std::pair<QString, QString>> rows = {
        {"F1", QString::fromUtf8("Профиль")}, {"F2", QString::fromUtf8("Навыки")},
        {"F3", QString::fromUtf8("Пайплайн")}, {"F4", QString::fromUtf8("Правила")},
        {"F5", QString::fromUtf8("Статистика")}, {"F6", QString::fromUtf8("Аудит")},
        {"Ctrl+N", QString::fromUtf8("Создать запись")}, {"Ctrl+E", QString::fromUtf8("Редактировать выбранную запись")},
        {"Delete", QString::fromUtf8("Удалить выбранный проект или этап")}, {"Ctrl+R", QString::fromUtf8("Перечитать локальные данные")},
        {"Ctrl+I", QString::fromUtf8("Показать или скрыть подробности")}, {"Ctrl+/", QString::fromUtf8("Открыть эту памятку")}
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
    QMessageBox::warning(this, QString::fromUtf8("ForgeMirror"), q(error));
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

void QtWindow::reload() {
    const auto previous = profiles_->currentData().toString();
    try {
        workspace_.reload();
    } catch (const std::exception& error) {
        message(error.what());
        return;
    }
    {
        QSignalBlocker blocker(profiles_);
        profiles_->clear();
        for (const auto& profile : workspace_.profiles) {
            if (!profile.archived) profiles_->addItem(q(profile.name), q(profile.id));
        }
        const int index = profiles_->findData(previous);
        if (index >= 0) profiles_->setCurrentIndex(index);
    }
    updateBanner(); render();
    if (!workspace_.data.recoveryWarnings.empty()) {
        QStringList warnings;
        for (const auto& warning : workspace_.data.recoveryWarnings) warnings << q(warning);
        message(u(warnings.join('\n')));
    }
}

QString QtWindow::selectedId() const {
    const auto* item = table_->item(table_->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void QtWindow::render() {
    const auto profileId = u(profiles_->currentData().toString());
    const bool unlocked = profileSession_.isUnlocked(*workspace_.storage, profileId);
    profileAccessAction_->setText(QString::fromUtf8(unlocked ? "Выйти из профиля" : "Войти в выбранный профиль"));
    profileAccessAction_->setEnabled(!profileId.empty());
    ownPasswordAction_->setEnabled(unlocked);
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
    summary_->setVisible(!timerPage);
    search_->setVisible(!timerPage);
    table_->setVisible(!timerPage);
    bottomActions_->setVisible(!timerPage);
    details_->setVisible(!timerPage && details_->isVisible());
    pomodoro_->setVisible(timerPage);
    static_cast<QtPomodoro*>(pomodoro_)->setAdministrator(admin_);
    statusFilter_->setVisible(page == Tasks);
    reportView_->setVisible(page == Statistics);
    primary_->setVisible(page == Shortcuts || ((page == ProfilePage || page == Tasks || page == Projects || page == Catalog || page == Pipeline || page == Professions || page == Rules || page == Vault || page == Banner) && admin_));
    primary_->setText(page == ProfilePage ? QString::fromUtf8("Управление профилями") :
        (page == Projects ? QString::fromUtf8("Создать проект") :
         page == Catalog ? QString::fromUtf8("Создать навык") : page == Pipeline ? QString::fromUtf8("Создать этап") : page == Professions ? QString::fromUtf8("Создать профессию") : page == Rules ? QString::fromUtf8("Изменить правила") : page == Vault ? QString::fromUtf8("Настройки хранилища") : page == Shortcuts ? QString::fromUtf8("Добавить ярлык") : page == Banner ? QString::fromUtf8("Добавить фразу") : QString::fromUtf8("Создать задачу")));
    editEntry_->setVisible(admin_ && (page == Projects || page == Catalog || page == Tasks || page == Pipeline || page == Professions || page == Banner));
    deleteEntry_->setVisible(page == Shortcuts || (admin_ && (page == Tasks || page == Projects || page == Catalog || page == Pipeline || page == Professions || page == Banner)));
    moveUp_->setVisible(page == Shortcuts || (admin_ && page == Pipeline));
    moveDown_->setVisible(page == Shortcuts || (admin_ && page == Pipeline));
    openShortcut_->setVisible(page == Shortcuts);
    profileMetrics_->setVisible(page == ProfilePage);
    achievements_->setVisible(page == ProfilePage);
    achievements_->setEnabled(!profiles_->currentData().toString().isEmpty());
    removeSpirit_->setVisible(page == ProfilePage && unlocked);
    exportReport_->setVisible(admin_ && page == Statistics);
    reapplyRules_->setVisible(admin_ && page == Rules);
    directXp_->setVisible(admin_ && page == ProfilePage);
    directXp_->setEnabled(!profiles_->currentData().toString().isEmpty());
    removeSpirit_->setEnabled(false);
    for (auto* value : profileValues_) value->setText(QString::fromUtf8("—"));
    changeStatus_->setVisible(page == Tasks && admin_);
    advanceStage_->setVisible(page == Tasks && admin_ && workspace_.modules.pipeline);
    summary_->clear();

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
        for (const auto& task : data.tasks) {
            if (statusFilter_->currentIndex() && task.status != statusFilter_->currentIndex() - 1) continue;
            const auto project = std::find_if(data.projects.begin(), data.projects.end(),
                [&](const auto& entry) { return !task.projectId.empty() && entry.id == task.projectId; });
            const auto stage = std::find_if(data.pipelineSteps.begin(), data.pipelineSteps.end(),
                [&](const auto& entry) { return !task.pipelineStepId.empty() && entry.id == task.pipelineStepId; });
            row(task.id, {q(AppTaskDisplayTitle(task)), q(project == data.projects.end() ? task.project : project->name), q(AppTaskStatusLabel(task.status)),
                q(AppTaskPriorityLabel(task.priority)), timeText(task.deadlineAt), q(stage == data.pipelineSteps.end() ? task.pipelineStep : stage->title)});
        }
        const auto report = BuildTeamValueReport(data.tasks, data.projects, QDateTime::currentSecsSinceEpoch());
        summary_->setText(QString::fromUtf8("Активных: %1  ·  просрочено: %2  ·  ждут XP: %3  ·  показано: %4")
            .arg(report.activeTasks).arg(report.overdueTasks).arg(report.xpPendingTasks).arg(table_->rowCount()));
    } else if (page == Projects) {
        headers({QString::fromUtf8("Проект"), QString::fromUtf8("Описание"), QString::fromUtf8("Создан")});
        for (const auto& project : data.projects) row(project.id, {q(project.name), q(project.description), timeText(project.createdAt)});
    } else if (page == Catalog) {
        headers({QString::fromUtf8("Навык"), QString::fromUtf8("Вес"), QString::fromUtf8("Описание"), QString::fromUtf8("Профессии")});
        for (const auto& id : workspace_.catalog.skills()) {
            QStringList names;
            for (const auto& binding : workspace_.catalog.professions(id)) {
                const auto found = std::find_if(data.professions.begin(), data.professions.end(), [&](const auto& p) { return p.id == binding; });
                names << q(found == data.professions.end() ? binding : found->name);
            }
            row(id, {q(workspace_.catalog.display_name(id)), QString::number(workspace_.catalog.weight(id)),
                q(workspace_.catalog.description(id)), names.join(", ")});
        }
    } else if (page == Pipeline) {
        headers({QString::fromUtf8("Этап"), QString::fromUtf8("Название"), QString::fromUtf8("Ответственный"), QString::fromUtf8("Следующий шаг")});
        for (const auto& step : data.pipelineSteps) row(step.id, {q(step.stageCode), q(step.title), q(step.owner), q(step.nextStageLabel)});
    } else if (page == Professions) {
        headers({QString::fromUtf8("Профессия"), QString::fromUtf8("Описание")});
        for (const auto& item : data.professions) row(item.id, {q(item.name), q(item.description)});
    } else if (page == Statistics) {
        const auto report = BuildTeamValueReport(data.tasks, data.projects, QDateTime::currentSecsSinceEpoch());
        if (reportView_->currentIndex() == 0) {
            headers({QString::fromUtf8("Проект"), QString::fromUtf8("Активно"), QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP")});
            for (const auto& item : report.projects) row(item.id, {q(item.name), QString::number(item.activeTasks),
                QString::number(item.doneTasks), QString::number(item.overdueTasks), QString::number(item.xpPendingTasks)});
            summary_->setText(QString::fromUtf8("Всего задач: %1  ·  всего проектов: %2  ·  выдано XP: %3")
                .arg(report.totalTasks).arg(report.totalProjects).arg(report.totalGlobalXp));
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
            summary_->setText(QString::fromUtf8("Сотрудников в задачах: %1  ·  без исполнителя: %2  ·  выдано XP: %3")
                .arg(int(report.assignees.size())).arg(report.unassignedTasks).arg(report.totalGlobalXp));
        }
    } else if (page == Audit) {
        headers({QString::fromUtf8("Источник"), QString::fromUtf8("Время"), QString::fromUtf8("Автор"), QString::fromUtf8("Объект"),
            QString::fromUtf8("Поле"), QString::fromUtf8("Было"), QString::fromUtf8("Стало")});
        for (const auto& entry : data.taskAudit) row(entry.taskId, {QString::fromUtf8("Задача"), timeText(entry.timestamp), q(entry.actor),
            q(entry.taskId), q(entry.field), q(entry.oldValue), q(entry.newValue)});
        for (const auto& entry : profileAudit(workspace_.directory)) row(entry.profile, {QString::fromUtf8("Профиль"), timeText(entry.timestamp),
            QString::fromUtf8("локально"), q(entry.profile), q(entry.action), QString(), q(entry.details)});
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

void QtWindow::exportReport() {
    if (!requireAdmin() || navigation_->currentRow() != Statistics) return;
    const auto suggested = QString::fromUtf8("ForgeMirror-report-%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFileDialog dialog(this, QString::fromUtf8("Экспорт управленческого отчёта"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QString::fromUtf8("CSV-файлы (*.csv)"));
    dialog.setDefaultSuffix("csv");
    dialog.selectFile(suggested);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    auto path = dialog.selectedFiles().front();
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    const auto report = BuildTeamValueReport(workspace_.data.tasks, workspace_.data.projects, QDateTime::currentSecsSinceEpoch());
    QString error;
    if (!ExportTeamValueReportCsv(path, report, &error)) { message(error.toUtf8().toStdString()); return; }
    statusBar()->showMessage(QString::fromUtf8("Отчёт сохранён: %1").arg(QDir::toNativeSeparators(path)), 6000);
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
