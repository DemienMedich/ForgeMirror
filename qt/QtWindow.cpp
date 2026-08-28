#include "QtWindow.h"
#include "QtPipelineEditor.h"
#include "AppTaskCompletionService.h"
#include "QtSkillEditor.h"
#include "QtTaskCompletionDialog.h"
#include "QtProfileDialogs.h"
#include "AppTaskProjectService.h"
#include "AppTaskWorkflowService.h"
#include "AppTeamValueReportService.h"
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
enum Page { ProfilePage, Tasks, Projects, Catalog, Pipeline, Professions, Statistics, Audit };
}

QtWindow::QtWindow(QtWorkspace& workspace) : workspace_(workspace) {
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
    auto* passwordAction = menu->addAction(QString::fromUtf8("Сменить пароль выбранного профиля"), this, [this] {
        const auto id = profiles_->currentData().toString();
        if (id.isEmpty()) { message(u8"Сначала выберите профиль."); return; }
        ShowProfilePasswordDialog(this, workspace_, id, id, false);
    });
    passwordAction->setObjectName("changeOwnProfilePassword");
    menu->addAction(QString::fromUtf8("Открыть папку данных Qt"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(workspace_.directory.u8string())));
    });
    menu->addAction(QString::fromUtf8("О переносе"), this, [this] {
        QMessageBox::information(this, QString::fromUtf8("Перенос на Qt"), QString::fromUtf8(
            "Это первый этап переноса, не замена стабильной версии.\n"
            "Qt работает с отдельной копией данных. Облачная синхронизация отключена.\n"
            "Редакторы навыков/пайплайна, достижения, Pomodoro, 3D и настройки ещё не перенесены."));
    });
    menuButton->setMenu(menu);
    header->addWidget(menuButton);
    layout->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->addItems({QString::fromUtf8("Профиль  F1"), QString::fromUtf8("Задачи"),
        QString::fromUtf8("Проекты"), QString::fromUtf8("Навыки  F2"), QString::fromUtf8("Пайплайн  F3"),
        QString::fromUtf8("Профессии"), QString::fromUtf8("Статистика  F5"), QString::fromUtf8("Аудит  F6")});
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
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    content->addWidget(summary_);
    profileMetrics_ = new QWidget;
    profileMetrics_->setObjectName("profileMetrics");
    auto* metricsLayout = new QHBoxLayout(profileMetrics_);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(8);
    const QStringList metricNames = {QString::fromUtf8("Уровень"), QString::fromUtf8("Всего XP"),
        QString::fromUtf8("Выполнено задач"), QString::fromUtf8("XP до уровня")};
    for (int i = 0; i < 4; ++i) {
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
    content->addLayout(filters);
    table_ = new QTableWidget;
    table_->setObjectName("records");
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(28);
    table_->horizontalHeader()->setStretchLastSection(true);
    content->addWidget(table_, 1);
    auto* bottom = new QHBoxLayout;
    auto* detailsToggle = new QPushButton(QString::fromUtf8("Подробности"));
    detailsToggle->setCheckable(true);
    bottom->addWidget(detailsToggle);
    changeStatus_ = new QPushButton(QString::fromUtf8("Изменить статус"));
    changeStatus_->setObjectName("changeStatus");
    changeStatus_->setToolTip(QString::fromUtf8("Переходы проверяются ядром. Завершение открывает распределение XP."));
    bottom->addWidget(changeStatus_);
    editEntry_ = new QPushButton(QString::fromUtf8("Редактировать"));
    editEntry_->setObjectName("editEntry");
    bottom->addWidget(editEntry_);
    bottom->addStretch();
    content->addLayout(bottom);
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
    connect(profiles_, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { render(); });
    connect(statusFilter_, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { details(); });
    connect(detailsToggle, &QPushButton::toggled, details_, &QWidget::setVisible);
    connect(primary_, &QPushButton::clicked, this, [this] { createEntry(); });
    connect(editEntry_, &QPushButton::clicked, this, [this] { createEntry(true); });
    connect(changeStatus_, &QPushButton::clicked, this, [this] { changeStatus(); });
    for (const auto& shortcut : std::vector<std::pair<int, int>>{{Qt::Key_F1, ProfilePage},
             {Qt::Key_F2, Catalog}, {Qt::Key_F3, Pipeline}, {Qt::Key_F5, Statistics}, {Qt::Key_F6, Audit}}) {
        auto* action = new QShortcut(QKeySequence(shortcut.first), this);
        connect(action, &QShortcut::activated, this, [this, page = shortcut.second] {
            if (!navigation_->item(page)->isHidden()) navigation_->setCurrentRow(page);
        });
    }
    navigation_->setCurrentRow(ProfilePage);
    reload();
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
    render();
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
    navigation_->item(Tasks)->setHidden(!workspace_.modules.tasks);
    navigation_->item(Pipeline)->setHidden(!workspace_.modules.pipeline);
    navigation_->item(Professions)->setHidden(!workspace_.modules.professions || !admin_);
    for (int page : {Projects, Statistics, Audit}) navigation_->item(page)->setHidden(!admin_);
    int page = navigation_->currentRow();
    if (page < 0) return;
    if (navigation_->item(page)->isHidden()) {
        navigation_->setCurrentRow(ProfilePage);
        return;
    }
    const auto previous = selectedId();
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
    mode_->setText(admin_ ? QString::fromUtf8("Администратор · Qt") : QString::fromUtf8("Просмотр · Qt"));
    statusFilter_->setVisible(page == Tasks);
    primary_->setVisible((page == ProfilePage || page == Tasks || page == Projects || page == Catalog || page == Pipeline) && admin_);
    primary_->setText(page == ProfilePage ? QString::fromUtf8("Управление профилями") :
        (page == Projects ? QString::fromUtf8("Создать проект") :
         page == Catalog ? QString::fromUtf8("Создать навык") : page == Pipeline ? QString::fromUtf8("Создать этап") : QString::fromUtf8("Создать задачу")));
    editEntry_->setVisible(admin_ && (page == Projects || page == Catalog || page == Tasks || page == Pipeline));
    profileMetrics_->setVisible(page == ProfilePage);
    for (auto* value : profileValues_) value->setText(QString::fromUtf8("—"));
    changeStatus_->setVisible(page == Tasks && admin_);
    summary_->clear();

    if (page == ProfilePage) {
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
        headers({QString::fromUtf8("Навык"), QString::fromUtf8("Вес"), QString::fromUtf8("Описание")});
        for (const auto& id : workspace_.catalog.skills()) row(id, {q(workspace_.catalog.display_name(id)),
            QString::number(workspace_.catalog.weight(id)), q(workspace_.catalog.description(id))});
    } else if (page == Pipeline) {
        headers({QString::fromUtf8("Этап"), QString::fromUtf8("Название"), QString::fromUtf8("Ответственный"), QString::fromUtf8("Следующий шаг")});
        for (const auto& step : data.pipelineSteps) row(step.id, {q(step.stageCode), q(step.title), q(step.owner), q(step.nextStageLabel)});
    } else if (page == Professions) {
        headers({QString::fromUtf8("Профессия"), QString::fromUtf8("Описание")});
        for (const auto& item : data.professions) row(item.id, {q(item.name), q(item.description)});
    } else if (page == Statistics) {
        headers({QString::fromUtf8("Проект"), QString::fromUtf8("Активно"), QString::fromUtf8("Выполнено"), QString::fromUtf8("Просрочено"), QString::fromUtf8("Ждут XP")});
        const auto report = BuildTeamValueReport(data.tasks, data.projects, QDateTime::currentSecsSinceEpoch());
        for (const auto& item : report.projects) row(item.id, {q(item.name), QString::number(item.activeTasks),
            QString::number(item.doneTasks), QString::number(item.overdueTasks), QString::number(item.xpPendingTasks)});
        summary_->setText(QString::fromUtf8("Всего задач: %1  ·  всего проектов: %2  ·  выдано XP: %3")
            .arg(report.totalTasks).arg(report.totalProjects).arg(report.totalGlobalXp));
    } else if (page == Audit) {
        headers({QString::fromUtf8("Время"), QString::fromUtf8("Автор"), QString::fromUtf8("Задача"),
            QString::fromUtf8("Поле"), QString::fromUtf8("Было"), QString::fromUtf8("Стало")});
        for (const auto& entry : data.taskAudit) row(entry.taskId, {timeText(entry.timestamp), q(entry.actor),
            q(entry.taskId), q(entry.field), q(entry.oldValue), q(entry.newValue)});
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
    else if (page == Pipeline || page == Projects || page == Professions) stretchColumn = 1;
    table_->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
    table_->setSortingEnabled(true);
    for (int index = 0; index < table_->rowCount(); ++index) {
        if (table_->item(index, 0)->data(Qt::UserRole).toString() == previous) { table_->selectRow(index); break; }
    }
    details();
}

void QtWindow::details() {
    const auto id = u(selectedId());
    details_->clear();
    changeStatus_->setEnabled(!id.empty());
    editEntry_->setEnabled(!id.empty());
    const int page = navigation_->currentRow();
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
    if (!requireAdmin()) return;
    if (edit && selectedId().isEmpty()) return;
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
                : AppCreateTaskEntry(workspace_.directory, workspace_.data.tasks, task, "admin/qt", &workspace_.data.taskAudit);
            if (!result.ok) { message(result.errorMessage); return; }
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) reload();
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
    bool ok = false;
    const auto selected = QInputDialog::getItem(this, QString::fromUtf8("Статус задачи"),
        QString::fromUtf8("Новый статус:"), labels, 0, false, &ok);
    if (!ok || labels.indexOf(selected) < 0) return;
    const int next = states[size_t(labels.indexOf(selected))];
    if (next == 2 && found->participants.empty()) {
        ShowTaskCompletionDialog(this, workspace_, q(id), profiles_->currentData().toString());
        reload();
        return;
    }
    AppTaskWorkflowService workflow(workspace_.directory, workspace_.data.tasks, &workspace_.data.taskAudit);
    const auto result = workflow.UpdateStatus(id, next, "admin/qt");
    if (!result.ok) message(result.errorMessage);
    else reload();
}
