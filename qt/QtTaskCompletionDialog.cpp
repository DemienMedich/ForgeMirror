#include "QtTaskCompletionDialog.h"
#include "QtWorkspace.h"
#include "AppTaskCompletionService.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>
#include <algorithm>

namespace {
QString q(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string u(const QString& s) { return s.toUtf8().toStdString(); }
void prepareForm(QtScrollableDialog& dialog) {
    auto* form = dialog.formLayout();
    form->setHorizontalSpacing(dialog.scaledMetric(8));
    form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFormAlignment(Qt::AlignTop);
    dialog.footerLayout()->setSpacing(dialog.scaledMetric(8));
    if (auto* outer = dialog.layout()) {
        const int margin = dialog.scaledMetric(16);
        outer->setContentsMargins(margin, margin, margin, margin);
        outer->setSpacing(dialog.scaledMetric(8));
    }
}
void addField(QFormLayout* form, const QString& caption, QWidget* field) {
    auto* label = new QLabel(caption);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setBuddy(field);
    form->addRow(label, field);
}
QLabel* contextLabel(const QString& text, const char* objectName) {
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAccessibleName(text);
    return label;
}
void secondaryCommand(QPushButton* button, const QString& description) {
    button->setAutoDefault(false);
    button->setAccessibleDescription(description);
    button->setToolTip(description);
}
void fullComboContext(QComboBox* combo) {
    combo->setToolTip(combo->currentText());
    QObject::connect(combo, &QComboBox::currentTextChanged, combo,
        [combo](const QString& text) { combo->setToolTip(text); });
}
void showSummary(QLabel* summary, const QString& text) {
    summary->setText(text);
    summary->setAccessibleDescription(text);
}
void prepareSpin(QSpinBox* spin) {
    spin->ensurePolished();
    spin->setMinimumSize(spin->sizeHint());
    spin->setToolTip(spin->accessibleName() + QStringLiteral("\n") + spin->accessibleDescription());
}
void setupTable(QTableWidget* table, const QStringList& headers, const QtScrollableDialog& dialog) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    QSpinBox sample(table);
    sample.setRange(0, 100);
    sample.ensurePolished();
    table->verticalHeader()->setDefaultSectionSize(std::max(dialog.scaledMetric(32),
        sample.sizeHint().height() + dialog.scaledMetric(4)));
    table->setWordWrap(false);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // Keep names useful when several numeric columns need their intrinsic width.
    // The table may scroll horizontally; the surrounding form never does.
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, dialog.scaledMetric(160));
    for (int column = 0; column < headers.size(); ++column)
        table->horizontalHeaderItem(column)->setToolTip(headers[column]);
    QObject::connect(table, &QTableWidget::itemChanged, table, [table](QTableWidgetItem* item) {
        QSignalBlocker blocker(table);
        item->setToolTip(item->text());
        item->setData(Qt::AccessibleTextRole, item->text());
    });
}
void fitAllocationTable(QTableWidget* table) {
    const int horizontalMargin = 2 * std::max(0,
        table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, table)) +
        (table->showGrid() ? 1 : 0);
    const int verticalMargin = 2 * std::max(0,
        table->style()->pixelMetric(QStyle::PM_FocusFrameVMargin, nullptr, table)) +
        (table->showGrid() ? 1 : 0);
    int rowHeight = table->verticalHeader()->defaultSectionSize();
    std::vector<int> widgetWidths(size_t(table->columnCount()), 0);
    for (int row = 0; row < table->rowCount(); ++row) {
        for (int column = 0; column < table->columnCount(); ++column) {
            if (auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(row, column))) {
                // Measure the actual child after it inherits the viewport font/style.
                // ResizeToContents alone does not reserve space for cell widgets.
                prepareSpin(spin);
                const auto hint = spin->sizeHint();
                widgetWidths[size_t(column)] = std::max(widgetWidths[size_t(column)],
                    hint.width() + horizontalMargin);
                rowHeight = std::max(rowHeight, hint.height() + verticalMargin);
            }
            if (auto* item = table->item(row, column)) {
                item->setToolTip(item->text());
                item->setData(Qt::AccessibleTextRole, item->text());
            }
        }
    }
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    for (int row = 0; row < table->rowCount(); ++row) table->setRowHeight(row, rowHeight);
    for (int column = 0; column < table->columnCount(); ++column) {
        if (!widgetWidths[size_t(column)]) continue;
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
        table->setColumnWidth(column, std::max(widgetWidths[size_t(column)],
            table->horizontalHeader()->sectionSizeHint(column)));
    }
    const int rows = std::clamp(table->rowCount(), 1, 3);
    const int height = table->horizontalHeader()->sizeHint().height() +
        rowHeight * rows + table->frameWidth() * 2 +
        table->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, table);
    table->setMinimumHeight(height);
    table->setMaximumHeight(height);
}
void prepareButtons(QtScrollableDialog& dialog, QDialogButtonBox* buttons,
                    const char* boxName, const char* cancelName) {
    buttons->setObjectName(boxName);
    auto* save = buttons->button(QDialogButtonBox::Save);
    save->setProperty("primary", true);
    save->setMinimumHeight(dialog.scaledMetric(32));
    save->setAutoDefault(true);
    save->setDefault(true);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setObjectName(cancelName);
    cancel->setText(QString::fromUtf8("Отмена"));
    cancel->setAccessibleName(QString::fromUtf8("Отменить начисление XP"));
    secondaryCommand(cancel, QString::fromUtf8("Закрывает форму без сохранения записи и без начисления XP."));
    dialog.footerLayout()->addWidget(buttons);
}
class CompletionSortItem final : public QTableWidgetItem {
public:
    explicit CompletionSortItem(const QString& text) : QTableWidgetItem(text) {}
    double numericKey = 0.0;
    bool numeric = false;
    bool operator<(const QTableWidgetItem& other) const override {
        const auto* comparable = dynamic_cast<const CompletionSortItem*>(&other);
        return comparable && numeric && comparable->numeric
            ? numericKey < comparable->numericKey
            : text().localeAwareCompare(other.text()) < 0;
    }
};
void reorderSkillRows(QTableWidget* table, const std::vector<std::string>& skillIds,
                      const std::vector<size_t>& order, std::vector<QSpinBox*>& ratings,
                      QObject* context, const std::function<void()>& changed) {
    struct RowData { std::vector<QTableWidgetItem*> items; int rating = 0; bool enabled = true; };
    std::vector<RowData> rows(skillIds.size());
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto id = table->item(row, 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
        const auto found = std::find(skillIds.begin(), skillIds.end(), id);
        if (found == skillIds.end()) continue;
        auto& saved = rows[size_t(found - skillIds.begin())];
        saved.items.resize(size_t(table->columnCount()));
        for (int column = 0; column < table->columnCount(); ++column)
            if (column != 1) saved.items[size_t(column)] = table->takeItem(row, column);
        if (auto* rating = qobject_cast<QSpinBox*>(table->cellWidget(row, 1))) {
            saved.rating = rating->value();
            saved.enabled = rating->isEnabled();
        }
    }
    table->setRowCount(0);
    for (const auto index : order) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto& saved = rows[index];
        for (int column = 0; column < table->columnCount(); ++column)
            if (column != 1 && saved.items[size_t(column)]) table->setItem(row, column, saved.items[size_t(column)]);
        auto* rating = new QSpinBox;
        rating->setRange(0, 5);
        rating->setValue(saved.rating);
        rating->setEnabled(saved.enabled);
        rating->setAccessibleName(QString::fromUtf8("Оценка навыка %1").arg(table->item(row, 0)->text()));
        rating->setAccessibleDescription(QString::fromUtf8("Оценка относительной доли опыта этого навыка от 0 до 5."));
        ratings[index] = rating;
        table->setCellWidget(row, 1, rating);
        QObject::connect(rating, &QSpinBox::valueChanged, context, [changed] { changed(); });
    }
    fitAllocationTable(table);
}
}

bool ShowTaskCompletionDialog(QWidget* parent, QtWorkspace& workspace,
                              const QString& taskId, const QString& activeProfileId,
                              std::function<void(AppLogLevel, const std::string&)> eventLogger) {
    const auto it = std::find_if(workspace.data.tasks.begin(), workspace.data.tasks.end(),
        [&](const auto& task) { return task.id == u(taskId); });
    if (it == workspace.data.tasks.end() || !it->participants.empty()) return false;
    const auto task = *it;
    const auto activeProfile = workspace.storage->load_profile_snapshot(u(activeProfileId), false);
    AppContext context{workspace.directory, *workspace.storage, workspace.catalog};
    context.eventLogger = eventLogger;
    QtScrollableDialog dialog(parent, QSize(800, 640));
    dialog.setObjectName("taskCompletionDialog");
    dialog.setWindowTitle(QString::fromUtf8("Завершение задачи и XP"));
    prepareForm(dialog);
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Поля завершения задачи и распределения XP"));
    auto* form = dialog.formLayout();
    auto* title = contextLabel(q(task.title), "xpContext");
    form->addRow(title);
    auto* parameters = new QWidget;
    parameters->setObjectName("xpParameters");
    auto* top = new QFormLayout(parameters);
    top->setContentsMargins(0, 0, 0, 0);
    top->setHorizontalSpacing(dialog.scaledMetric(8));
    top->setVerticalSpacing(dialog.scaledMetric(8));
    top->setRowWrapPolicy(QFormLayout::WrapLongRows);
    top->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    top->setSizeConstraint(QLayout::SetMinimumSize);
    auto* category = new QComboBox;
    category->setObjectName("xpCategory");
    category->setAccessibleName(QString::fromUtf8("Категория завершённой задачи"));
    category->setAccessibleDescription(QString::fromUtf8("Определяет базовый пул опыта для начисления."));
    category->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    category->setMinimumContentsLength(8);
    category->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (auto* label : Profile::kCategoryLabels) category->addItem(label);
    category->setCurrentIndex(std::clamp(task.category, 0, 4));
    fullComboContext(category);
    auto* score = new QSpinBox;
    score->setObjectName("xpScore");
    score->setAccessibleName(QString::fromUtf8("Оценка выполнения задачи"));
    score->setAccessibleDescription(QString::fromUtf8("Оценка от 1 до 10 влияет на начисляемый опыт."));
    score->setRange(1, 10);
    score->setValue(10);
    addField(top, QString::fromUtf8("Категория"), category);
    addField(top, QString::fromUtf8("Оценка"), score);
    auto* penalty = contextLabel(QString::fromUtf8("Штраф задачи: %1%").arg(task.deadlinePenaltyPercent), "xpPenalty");
    penalty->setToolTip(QString::fromUtf8("Берётся заданный в задаче штраф, как в ImGui. Снижает общий пул до распределения."));
    top->addRow(penalty);
    form->addRow(parameters);
    auto* participantsHeader = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    participantsHeader->setObjectName("xpParticipantsCommands");
    participantsHeader->addWidget(contextLabel(QString::fromUtf8("Участники · всего 100% · 0% исключает участника"),
        "xpParticipantsHint"), 1);
    auto* even = new QPushButton(QString::fromUtf8("Поровну"));
    even->setObjectName("xpParticipantsEven");
    even->setAccessibleName(QString::fromUtf8("Равномерно распределить опыт между выбранными участниками"));
    even->setToolTip(QString::fromUtf8("Разделить 100% между участниками, у которых вклад больше нуля."));
    secondaryCommand(even, even->toolTip());
    participantsHeader->addWidget(even);
    auto* onlyActive = new QPushButton(QString::fromUtf8("Только активный"));
    onlyActive->setObjectName("xpOnlyActiveParticipant");
    onlyActive->setAccessibleName(QString::fromUtf8("Назначить 100% активному профилю"));
    secondaryCommand(onlyActive, QString::fromUtf8("Назначить 100% активному профилю и исключить остальных участников."));
    participantsHeader->addWidget(onlyActive);
    form->addRow(participantsHeader);
    auto* participants = new QTableWidget;
    participants->setObjectName("xpParticipants");
    participants->setAccessibleName(QString::fromUtf8("Распределение опыта между участниками"));
    participants->setAccessibleDescription(QString::fromUtf8("Укажите долю каждого участника. Ненулевые доли должны составлять 100 процентов."));
    setupTable(participants, {QString::fromUtf8("Профиль"), QString::fromUtf8("Вклад, %"), "XP", QString::fromUtf8("XP навыков"), QString::fromUtf8("Модификаторы")}, dialog);
    participants->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    participants->setColumnWidth(4, dialog.scaledMetric(160));
    form->addRow(participants);
    std::vector<std::string> profileIds;
    std::vector<QSpinBox*> shares;
    for (const auto& profile : workspace.profiles) if (!profile.archived) {
        int row = participants->rowCount();
        participants->insertRow(row);
        participants->setItem(row, 0, new QTableWidgetItem(q(profile.name)));
        auto* share = new QSpinBox;
        share->setRange(0, 100);
        share->setAccessibleName(QString::fromUtf8("Доля участника %1").arg(q(profile.name)));
        share->setAccessibleDescription(QString::fromUtf8("Процент общего опыта, назначаемый этому участнику."));
        const bool assigned = std::find(task.assignees.begin(), task.assignees.end(), profile.id) != task.assignees.end();
        share->setValue(assigned || (task.assignees.empty() && profile.id == u(activeProfileId)) ? 1 : 0);
        participants->setCellWidget(row, 1, share);
        for (int c = 2; c < 5; ++c) participants->setItem(row, c, new QTableWidgetItem);
        shares.push_back(share);
        profileIds.push_back(profile.id);
    }
    auto split = [&] {
        int count = int(std::count_if(shares.begin(), shares.end(), [](auto* spin) { return spin->value() > 0; }));
        if (!count) return;
        int remainder = 100 % count;
        for (auto* spin : shares) if (spin->value() > 0) {
            QSignalBlocker blocker(spin);
            spin->setValue(100 / count + (remainder-- > 0 ? 1 : 0));
        }
    };
    split();
    fitAllocationTable(participants);
    form->addRow(contextLabel(QString::fromUtf8("Навыки · оценки 0–5 распределяют 100%"), "xpSkillsHint"));
    auto* skillsHeader = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    skillsHeader->setObjectName("xpSkillCommands");
    auto* skillFilter = new QLineEdit;
    skillFilter->setObjectName("xpSkillFilter");
    skillFilter->setAccessibleName(QString::fromUtf8("Фильтр навыков начисления XP"));
    skillFilter->setClearButtonEnabled(true);
    if (auto* clearFilter = skillFilter->findChild<QAbstractButton*>())
        clearFilter->setAccessibleName(QString::fromUtf8("Очистить фильтр навыков начисления XP"));
    skillFilter->setPlaceholderText(QString::fromUtf8("Фильтр навыков"));
    skillsHeader->addWidget(skillFilter, 1);
    auto* skillSort = new QComboBox;
    skillSort->setObjectName("xpSkillSort");
    skillSort->setAccessibleName(QString::fromUtf8("Сортировка навыков начисления XP"));
    skillSort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    skillSort->setMinimumContentsLength(8);
    skillSort->addItems({QString::fromUtf8("По имени"), QString::fromUtf8("По доле"),
        QString::fromUtf8("По бонусу"), QString::fromUtf8("По XP")});
    fullComboContext(skillSort);
    skillsHeader->addWidget(skillSort);
    auto* evenSkills = new QPushButton(QString::fromUtf8("Навыки поровну"));
    evenSkills->setObjectName("xpSkillsEven");
    evenSkills->setAccessibleName(QString::fromUtf8("Равномерно распределить XP по навыкам"));
    evenSkills->setToolTip(QString::fromUtf8("Поставить оценку 1 всем навыкам."));
    secondaryCommand(evenSkills, evenSkills->toolTip());
    skillsHeader->addWidget(evenSkills);
    form->addRow(skillsHeader);
    auto* skills = new QTableWidget;
    skills->setObjectName("xpSkills");
    skills->setAccessibleName(QString::fromUtf8("Распределение опыта по навыкам"));
    skills->setAccessibleDescription(QString::fromUtf8("Оценки от 0 до 5 распределяют между выбранными навыками до 100 процентов."));
    setupTable(skills, {QString::fromUtf8("Навык"), QString::fromUtf8("Оценка"),
        QString::fromUtf8("Доля, %"), QString::fromUtf8("Бонус"), QString::fromUtf8("XP")}, dialog);
    form->addRow(skills);
    std::vector<std::string> skillIds = workspace.catalog.skills();
    std::vector<QSpinBox*> ratings;
    std::vector<bool> skillAllowed;
    for (const auto& id : skillIds) {
        int row = skills->rowCount();
        skills->insertRow(row);
        auto* nameItem = new CompletionSortItem(q(workspace.catalog.display_name(id)));
        nameItem->setData(Qt::UserRole, q(id));
        skills->setItem(row, 0, nameItem);
        auto* rating = new QSpinBox;
        rating->setRange(0, 5);
        rating->setAccessibleName(QString::fromUtf8("Оценка навыка %1").arg(q(workspace.catalog.display_name(id))));
        rating->setAccessibleDescription(QString::fromUtf8("Оценка относительной доли опыта этого навыка от 0 до 5."));
        const auto professions = workspace.catalog.professions(id);
        const bool allowed = !activeProfile || activeProfile->profession_id().empty() || professions.empty() ||
            std::find(professions.begin(), professions.end(), activeProfile->profession_id()) != professions.end();
        rating->setEnabled(allowed);
        rating->setValue(allowed && (task.skillIds.empty() || std::find(task.skillIds.begin(), task.skillIds.end(), id) != task.skillIds.end()) ? 1 : 0);
        skills->setCellWidget(row, 1, rating);
        for (int column = 2; column < 5; ++column) skills->setItem(row, column, new CompletionSortItem(QString::fromUtf8("—")));
        ratings.push_back(rating);
        skillAllowed.push_back(allowed);
    }
    auto* summary = new QLabel;
    summary->setObjectName("xpSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    summary->setToolTip(QString::fromUtf8("Пул = XP категории × множитель оценки × фокус × штраф задачи.\n"
        "Повтор и прогрев снижают общий XP. Бонусы достижений действуют на навыки; дух — на оба вида XP."));
    summary->setAccessibleName(QString::fromUtf8("Предпросмотр начисления XP или ошибка операции"));
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dialog.footerLayout()->addWidget(summary);
    fitAllocationTable(skills);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->button(QDialogButtonBox::Save);
    save->setText(QString::fromUtf8("Завершить"));
    save->setObjectName("completeXp");
    save->setAccessibleName(QString::fromUtf8("Завершить задачу и начислить XP"));
    save->setToolTip(QString::fromUtf8("Закрывает задачу и сохраняет начисление XP всем выбранным участникам. Повторное начисление запрещено."));
    prepareButtons(dialog, buttons, "xpButtons", "xpCancel");
    QWidget::setTabOrder(category, score);
    QWidget::setTabOrder(score, even); QWidget::setTabOrder(even, onlyActive);
    QWidget::setTabOrder(onlyActive, participants); QWidget::setTabOrder(participants, skillFilter);
    QWidget::setTabOrder(skillFilter, skillSort); QWidget::setTabOrder(skillSort, evenSkills);
    QWidget::setTabOrder(evenSkills, skills); QWidget::setTabOrder(skills, save);
    QWidget::setTabOrder(save, buttons->button(QDialogButtonBox::Cancel));
    auto input = [&] {
        TaskCompletionInput request;
        request.taskId = task.id;
        request.category = category->currentIndex();
        request.score = score->value();
        request.now = QDateTime::currentSecsSinceEpoch();
        request.restoreProfileId = u(activeProfileId);
        request.actor = "admin/qt";
        for (size_t i = 0; i < shares.size(); ++i) if (shares[i]->value() > 0)
            request.shares.push_back({profileIds[i], shares[i]->value()});
        for (size_t i = 0; i < ratings.size(); ++i) request.skills.push_back({skillIds[i], ratings[i]->value()});
        return request;
    };
    auto rowForSkill = [&](size_t skillIndex) {
        const auto id = q(skillIds[skillIndex]);
        for (int row = 0; row < skills->rowCount(); ++row)
            if (skills->item(row, 0)->data(Qt::UserRole).toString() == id) return row;
        return -1;
    };
    std::function<void()> refresh;
    refresh = [&] {
        skills->setSortingEnabled(false);
        const auto request = input();
        const auto preview = PreviewTaskCompletion(context, workspace.data.tasks, request);
        save->setEnabled(preview.ok);
        for (int r = 0; r < participants->rowCount(); ++r)
            for (int c = 2; c < 5; ++c) participants->item(r, c)->setText(QString::fromUtf8("—"));
        for (int r = 0; r < skills->rowCount(); ++r)
            for (int c = 2; c < 5; ++c) skills->item(r, c)->setText(QString::fromUtf8("—"));
        if (!preview.ok) { showSummary(summary, q(preview.errorMessage)); return; }
        for (size_t i = 0; i < preview.finalize.participants.size(); ++i) {
            const auto& p = preview.finalize.participants[i];
            const auto found = std::find(profileIds.begin(), profileIds.end(), p.profileId);
            const int row = int(found - profileIds.begin());
            participants->item(row, 2)->setText(QString::number(p.globalXp));
            participants->item(row, 3)->setText(QString::number(p.skillXp));
            participants->item(row, 4)->setText(q(preview.modifiers[i]));
        }
        for (size_t row = 0; row < skillIds.size(); ++row) {
            const int tableRow = rowForSkill(row);
            if (tableRow < 0) continue;
            int gained = 0;
            for (size_t i = 0; i < preview.updatedProfiles.size() && i < request.shares.size(); ++i) {
                const auto before = workspace.storage->load_profile_snapshot(request.shares[i].profileId, false);
                if (!before) continue;
                const auto beforeSkills = before->list_skills();
                const auto afterSkills = preview.updatedProfiles[i].list_skills();
                const auto findXp = [&](const auto& items) {
                    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& skill) { return skill.name == skillIds[row]; });
                    return found == items.end() ? 0 : found->xp;
                };
                gained += std::max(0, findXp(afterSkills) - findXp(beforeSkills));
            }
            const int share = preview.skillPercents[row];
            auto* shareItem = static_cast<CompletionSortItem*>(skills->item(tableRow, 2));
            shareItem->setText(QString::number(share)); shareItem->numeric = true; shareItem->numericKey = share;
            const double bonus = activeProfile ? (activeProfile->skill_bonus_multiplier(skillIds[row], request.now) - 1.0) * 100.0 : 0.0;
            auto* bonusItem = static_cast<CompletionSortItem*>(skills->item(tableRow, 3));
            bonusItem->setText(bonus > 0.01 ? QString::number(bonus, 'f', 1) + QLatin1Char('%') : QString::fromUtf8("—"));
            bonusItem->numeric = true; bonusItem->numericKey = bonus;
            auto* xpItem = static_cast<CompletionSortItem*>(skills->item(tableRow, 4));
            xpItem->setText(QString::number(gained)); xpItem->numeric = true; xpItem->numericKey = gained;
        }
        const int sortColumn = skillSort->currentIndex() == 1 ? 2 : skillSort->currentIndex() == 2 ? 3 : skillSort->currentIndex() == 3 ? 4 : 0;
        std::vector<size_t> order(skillIds.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
            const auto* a = skills->item(rowForSkill(left), sortColumn);
            const auto* b = skills->item(rowForSkill(right), sortColumn);
            if (sortColumn == 0) return a->text().localeAwareCompare(b->text()) < 0;
            const auto* numericA = static_cast<const CompletionSortItem*>(a);
            const auto* numericB = static_cast<const CompletionSortItem*>(b);
            if (numericA->numericKey != numericB->numericKey) return numericA->numericKey > numericB->numericKey;
            return skills->item(rowForSkill(left), 0)->text().localeAwareCompare(skills->item(rowForSkill(right), 0)->text()) < 0;
        });
        reorderSkillRows(skills, skillIds, order, ratings, &dialog, refresh);
        const auto filter = skillFilter->text().trimmed();
        for (int row = 0; row < skills->rowCount(); ++row) {
            const auto name = skills->item(row, 0)->text();
            const auto id = u(skills->item(row, 0)->data(Qt::UserRole).toString());
            const auto index = std::find(skillIds.begin(), skillIds.end(), id);
            const bool allowed = index != skillIds.end() && skillAllowed[size_t(index - skillIds.begin())];
            skills->setRowHidden(row, !allowed || (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)));
        }
        showSummary(summary, QString::fromUtf8("Пул: %1 XP · после штрафа: %2 XP · участников: %3")
            .arg(preview.rawPool).arg(preview.finalize.basePool).arg(preview.finalize.participants.size()));
    };
    for (auto* spin : shares) QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
    for (auto* spin : ratings) QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(score, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(category, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(skillFilter, &QLineEdit::textChanged, &dialog, refresh);
    QObject::connect(skillSort, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(even, &QPushButton::clicked, &dialog, [&] { split(); refresh(); });
    QObject::connect(onlyActive, &QPushButton::clicked, &dialog, [&] {
        for (size_t i = 0; i < shares.size(); ++i) shares[i]->setValue(profileIds[i] == u(activeProfileId) ? 100 : 0);
        refresh();
    });
    QObject::connect(evenSkills, &QPushButton::clicked, &dialog, [&] {
        for (auto* rating : ratings) { QSignalBlocker blocker(rating); rating->setValue(1); }
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        save->setEnabled(false);
        const auto result = CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input());
        if (!result.ok) {
            showSummary(summary, q(result.errorMessage));
            // A pending rollback must be resolved before any further mutations.
            save->setEnabled(!std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"));
            return;
        }
        dialog.accept();
    });
    refresh();
    return dialog.exec() == QDialog::Accepted;
}

bool ShowManualXpDialog(QWidget* parent, QtWorkspace& workspace, const QString& activeProfileId,
                        std::function<void(AppLogLevel, const std::string&)> eventLogger) {
    const auto activeId = u(activeProfileId);
    if (activeId.empty()) return false;
    const auto activeProfile = workspace.storage->load_profile_snapshot(activeId, false);
    if (!activeProfile) return false;

    TaskEntry draft;
    draft.status = 2;
    draft.createdAt = QDateTime::currentSecsSinceEpoch();
    for (size_t attempt = 0; ; ++attempt) {
        draft.id = AppGenerateTaskId(draft.createdAt, workspace.data.tasks.size() + attempt + 1);
        if (std::none_of(workspace.data.tasks.begin(), workspace.data.tasks.end(),
                [&](const auto& item) { return item.id == draft.id; })) break;
    }

    QtScrollableDialog dialog(parent, QSize(800, 640));
    dialog.setObjectName("manualXpDialog");
    dialog.setWindowTitle(QString::fromUtf8("Добавить опыт без задачи"));
    prepareForm(dialog);
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Поля ручной записи и распределения XP"));
    auto* details = dialog.formLayout();
    auto* description = contextLabel(QString::fromUtf8("Запишите выполненную работу. Запись истории и начисление XP сохраняются одной транзакцией."),
        "manualXpContext");
    details->addRow(description);
    auto* project = new QComboBox;
    project->setObjectName("manualXpProject");
    project->setAccessibleName(QString::fromUtf8("Проект ручной записи XP"));
    project->setAccessibleDescription(QString::fromUtf8("Обязательный проект или категория выполненной работы."));
    project->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    project->setMinimumContentsLength(8);
    project->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    project->setEditable(true);
    for (const auto& item : workspace.data.projects) project->addItem(q(item.name), q(item.id));
    project->setCurrentIndex(-1);
    project->setCurrentText(QString());
    project->setPlaceholderText(QString::fromUtf8("Проект или категория работы"));
    fullComboContext(project);
    auto* title = new QLineEdit;
    title->setObjectName("manualXpTitle");
    title->setAccessibleName(QString::fromUtf8("Название выполненной работы"));
    title->setAccessibleDescription(QString::fromUtf8("Обязательное краткое название ручной записи."));
    title->setPlaceholderText(QString::fromUtf8("Краткое название работы"));
    auto* note = new QPlainTextEdit;
    note->setObjectName("manualXpDescription");
    note->setAccessibleName(QString::fromUtf8("Описание выполненной работы"));
    note->setAccessibleDescription(QString::fromUtf8("Обязательное описание результата; переносы строк сохраняются."));
    note->setPlaceholderText(QString::fromUtf8("Краткое описание результата"));
    const int noteHeight = QFontMetrics(note->font()).lineSpacing() * 3 +
        qRound(note->document()->documentMargin() * 2) + note->frameWidth() * 2;
    note->setMinimumHeight(noteHeight);
    note->setMaximumHeight(noteHeight);
    auto* parameters = new QWidget;
    parameters->setObjectName("manualXpParameters");
    auto* params = new QFormLayout(parameters);
    params->setContentsMargins(0, 0, 0, 0);
    params->setHorizontalSpacing(dialog.scaledMetric(8));
    params->setVerticalSpacing(dialog.scaledMetric(8));
    params->setRowWrapPolicy(QFormLayout::WrapLongRows);
    params->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    params->setSizeConstraint(QLayout::SetMinimumSize);
    auto* category = new QComboBox;
    category->setObjectName("manualXpCategory");
    category->setAccessibleName(QString::fromUtf8("Категория ручного начисления XP"));
    category->setAccessibleDescription(QString::fromUtf8("Определяет базовый пул опыта для начисления."));
    category->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    category->setMinimumContentsLength(8);
    category->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (auto* label : Profile::kCategoryLabels) category->addItem(QString::fromUtf8(label));
    fullComboContext(category);
    auto* score = new QSpinBox;
    score->setObjectName("manualXpScore");
    score->setAccessibleName(QString::fromUtf8("Оценка ручной записи XP"));
    score->setAccessibleDescription(QString::fromUtf8("Оценка результата от 1 до 10 влияет на базовый пул XP."));
    score->setRange(1, 10);
    score->setValue(10);
    addField(params, QString::fromUtf8("Категория"), category);
    addField(params, QString::fromUtf8("Оценка"), score);
    addField(details, QString::fromUtf8("Проект"), project);
    addField(details, QString::fromUtf8("Работа"), title);
    addField(details, QString::fromUtf8("Описание"), note);
    details->addRow(parameters);

    auto* participantsTitle = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    participantsTitle->setObjectName("manualXpParticipantsCommands");
    participantsTitle->addWidget(contextLabel(QString::fromUtf8("Участники · всего 100%"), "manualXpParticipantsHint"), 1);
    auto* evenParticipants = new QPushButton(QString::fromUtf8("Равномерно"));
    evenParticipants->setObjectName("manualXpParticipantsEven");
    evenParticipants->setAccessibleName(QString::fromUtf8("Равномерно распределить XP между выбранными профилями"));
    secondaryCommand(evenParticipants, QString::fromUtf8("Разделить 100% между профилями, у которых доля больше нуля."));
    participantsTitle->addWidget(evenParticipants);
    auto* onlyActive = new QPushButton(QString::fromUtf8("Только активный"));
    onlyActive->setObjectName("manualXpOnlyActive");
    onlyActive->setAccessibleName(QString::fromUtf8("Оставить XP только активному профилю"));
    secondaryCommand(onlyActive, QString::fromUtf8("Назначить 100% активному профилю и исключить остальные профили."));
    participantsTitle->addWidget(onlyActive);
    details->addRow(participantsTitle);
    auto* participants = new QTableWidget;
    participants->setObjectName("manualXpParticipants");
    participants->setAccessibleName(QString::fromUtf8("Распределение опыта между профилями"));
    participants->setAccessibleDescription(QString::fromUtf8("Ненулевые доли профилей должны составлять 100 процентов."));
    setupTable(participants, {QString::fromUtf8("Профиль"), QString::fromUtf8("Доля, %"), QString::fromUtf8("XP")}, dialog);
    details->addRow(participants);
    std::vector<std::string> profileIds;
    std::vector<QSpinBox*> shares;
    for (const auto& info : workspace.profiles) if (!info.archived) {
        const int row = participants->rowCount();
        participants->insertRow(row);
        participants->setItem(row, 0, new QTableWidgetItem(q(info.name)));
        auto* share = new QSpinBox;
        share->setRange(0, 100);
        share->setAccessibleName(QString::fromUtf8("Доля профиля %1").arg(q(info.name)));
        share->setAccessibleDescription(QString::fromUtf8("Процент общего XP, назначаемый этому профилю."));
        share->setValue(info.id == activeId ? 100 : 0);
        participants->setCellWidget(row, 1, share);
        participants->setItem(row, 2, new QTableWidgetItem(QString::fromUtf8("—")));
        profileIds.push_back(info.id);
        shares.push_back(share);
    }

    fitAllocationTable(participants);
    details->addRow(contextLabel(QString::fromUtf8("Навыки · оценки 0–5 распределяют 100%"), "manualXpSkillsHint"));
    auto* skillTools = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    skillTools->setObjectName("manualXpSkillCommands");
    auto* skillFilter = new QLineEdit;
    skillFilter->setObjectName("manualXpSkillFilter");
    skillFilter->setAccessibleName(QString::fromUtf8("Фильтр навыков ручного начисления XP"));
    skillFilter->setClearButtonEnabled(true);
    if (auto* clearFilter = skillFilter->findChild<QAbstractButton*>())
        clearFilter->setAccessibleName(QString::fromUtf8("Очистить фильтр навыков ручного начисления XP"));
    skillFilter->setPlaceholderText(QString::fromUtf8("Фильтр навыков"));
    auto* skillSort = new QComboBox;
    skillSort->setObjectName("manualXpSkillSort");
    skillSort->setAccessibleName(QString::fromUtf8("Сортировка навыков ручного начисления XP"));
    skillSort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    skillSort->setMinimumContentsLength(8);
    skillSort->addItems({QString::fromUtf8("По имени"), QString::fromUtf8("По доле"),
        QString::fromUtf8("По бонусу"), QString::fromUtf8("По XP")});
    fullComboContext(skillSort);
    auto* evenSkills = new QPushButton(QString::fromUtf8("Навыки равномерно"));
    evenSkills->setObjectName("manualXpSkillsEven");
    evenSkills->setAccessibleName(QString::fromUtf8("Равномерно распределить XP по доступным навыкам"));
    secondaryCommand(evenSkills, QString::fromUtf8("Поставить оценку 1 доступным навыкам; недоступные оставить с оценкой 0."));
    skillTools->addWidget(skillFilter, 1);
    skillTools->addWidget(skillSort);
    skillTools->addWidget(evenSkills);
    details->addRow(skillTools);
    auto* skills = new QTableWidget;
    skills->setObjectName("manualXpSkills");
    skills->setAccessibleName(QString::fromUtf8("Распределение опыта по навыкам"));
    skills->setAccessibleDescription(QString::fromUtf8("Оценки от 0 до 5 автоматически определяют долю XP каждого навыка."));
    setupTable(skills, {QString::fromUtf8("Навык"), QString::fromUtf8("Оценка"),
        QString::fromUtf8("Доля, %"), QString::fromUtf8("Бонус"), QString::fromUtf8("XP")}, dialog);
    details->addRow(skills);
    std::vector<std::string> skillIds = workspace.catalog.skills();
    std::vector<QSpinBox*> ratings;
    std::vector<bool> allowed;
    for (const auto& id : skillIds) {
        const int row = skills->rowCount();
        skills->insertRow(row);
        auto* nameItem = new CompletionSortItem(q(workspace.catalog.display_name(id)));
        nameItem->setData(Qt::UserRole, q(id));
        skills->setItem(row, 0, nameItem);
        auto* rating = new QSpinBox;
        rating->setRange(0, 5);
        rating->setAccessibleName(QString::fromUtf8("Оценка навыка %1").arg(q(workspace.catalog.display_name(id))));
        rating->setAccessibleDescription(QString::fromUtf8("Оценка относительной доли опыта этого навыка от 0 до 5."));
        const auto professions = workspace.catalog.professions(id);
        const bool skillAllowed = activeProfile->profession_id().empty() || professions.empty() ||
            std::find(professions.begin(), professions.end(), activeProfile->profession_id()) != professions.end();
        rating->setEnabled(skillAllowed);
        rating->setValue(skillAllowed ? 1 : 0);
        skills->setCellWidget(row, 1, rating);
        for (int column = 2; column < 5; ++column) skills->setItem(row, column, new CompletionSortItem(QString::fromUtf8("—")));
        ratings.push_back(rating);
        allowed.push_back(skillAllowed);
    }
    auto* summary = new QLabel;
    summary->setObjectName("manualXpSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    summary->setAccessibleName(QString::fromUtf8("Предпросмотр ручного начисления XP или ошибка операции"));
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setToolTip(QString::fromUtf8("Пул зависит от категории, оценки и текущих модификаторов. Доли участников должны давать 100%; оценки навыков распределяют XP автоматически."));
    dialog.footerLayout()->addWidget(summary);
    fitAllocationTable(skills);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->button(QDialogButtonBox::Save);
    save->setText(QString::fromUtf8("Начислить опыт"));
    save->setObjectName("manualXpSave");
    save->setAccessibleName(QString::fromUtf8("Создать ручную запись и начислить XP"));
    save->setToolTip(QString::fromUtf8("Создаёт завершённую запись работы и начисляет XP выбранным участникам одной транзакцией."));
    prepareButtons(dialog, buttons, "manualXpButtons", "manualXpCancel");
    QWidget::setTabOrder(project, title); QWidget::setTabOrder(title, note);
    QWidget::setTabOrder(note, category); QWidget::setTabOrder(category, score);
    QWidget::setTabOrder(score, evenParticipants); QWidget::setTabOrder(evenParticipants, onlyActive);
    QWidget::setTabOrder(onlyActive, participants); QWidget::setTabOrder(participants, skillFilter);
    QWidget::setTabOrder(skillFilter, skillSort); QWidget::setTabOrder(skillSort, evenSkills);
    QWidget::setTabOrder(evenSkills, skills); QWidget::setTabOrder(skills, save);
    QWidget::setTabOrder(save, buttons->button(QDialogButtonBox::Cancel));

    auto buildDraft = [&] {
        TaskEntry item = draft;
        item.project = u(project->currentText().trimmed());
        item.title = u(title->text().trimmed());
        item.description = u(note->toPlainText().trimmed());
        item.category = category->currentIndex();
        item.score = score->value();
        item.status = 2;
        item.deadlinePenaltyPercent = 0;
        const auto found = std::find_if(workspace.data.projects.begin(), workspace.data.projects.end(),
            [&](const auto& value) { return value.name == item.project; });
        if (found != workspace.data.projects.end()) item.projectId = found->id;
        return item;
    };
    auto buildInput = [&] {
        TaskCompletionInput input;
        input.taskId = draft.id;
        input.category = category->currentIndex();
        input.score = score->value();
        input.now = QDateTime::currentSecsSinceEpoch();
        input.restoreProfileId = activeId;
        input.actor = "admin/qt";
        for (size_t i = 0; i < shares.size(); ++i)
            if (shares[i]->value() > 0) input.shares.push_back({profileIds[i], shares[i]->value()});
        for (size_t i = 0; i < ratings.size(); ++i) input.skills.push_back({skillIds[i], ratings[i]->value()});
        return input;
    };
    auto rowForSkill = [&](size_t skillIndex) {
        const auto id = q(skillIds[skillIndex]);
        for (int row = 0; row < skills->rowCount(); ++row)
            if (skills->item(row, 0)->data(Qt::UserRole).toString() == id) return row;
        return -1;
    };
    AppContext context{workspace.directory, *workspace.storage, workspace.catalog};
    context.eventLogger = eventLogger;
    std::function<void()> refresh;
    refresh = [&] {
        skills->setSortingEnabled(false);
        const auto input = buildInput();
        auto previewTasks = workspace.data.tasks;
        previewTasks.push_back(buildDraft());
        const auto preview = PreviewTaskCompletion(context, previewTasks, input);
        for (int row = 0; row < participants->rowCount(); ++row) participants->item(row, 2)->setText(QString::fromUtf8("—"));
        for (int row = 0; row < skills->rowCount(); ++row)
            for (int column = 2; column < 5; ++column) skills->item(row, column)->setText(QString::fromUtf8("—"));
        if (!preview.ok) {
            showSummary(summary, q(preview.errorMessage));
            save->setEnabled(false);
        } else {
            for (size_t i = 0; i < preview.finalize.participants.size(); ++i) {
                const auto found = std::find(profileIds.begin(), profileIds.end(), preview.finalize.participants[i].profileId);
                if (found != profileIds.end()) participants->item(int(found - profileIds.begin()), 2)->setText(QString::number(preview.finalize.participants[i].globalXp));
            }
            const auto baseProfiles = input.shares;
            for (size_t row = 0; row < skillIds.size(); ++row) {
                const int tableRow = rowForSkill(row);
                if (tableRow < 0) continue;
                int gained = 0;
                for (size_t i = 0; i < preview.updatedProfiles.size() && i < baseProfiles.size(); ++i) {
                    const auto before = workspace.storage->load_profile_snapshot(baseProfiles[i].profileId, false);
                    if (!before) continue;
                    const auto beforeSkills = before->list_skills();
                    const auto afterSkills = preview.updatedProfiles[i].list_skills();
                    const auto findXp = [&](const auto& items) {
                        const auto it = std::find_if(items.begin(), items.end(), [&](const auto& skill) { return skill.name == skillIds[row]; });
                        return it == items.end() ? 0 : it->xp;
                    };
                    gained += std::max(0, findXp(afterSkills) - findXp(beforeSkills));
                }
                auto* shareItem = static_cast<CompletionSortItem*>(skills->item(tableRow, 2));
                shareItem->setText(QString::number(preview.skillPercents[row]));
                shareItem->numeric = true; shareItem->numericKey = preview.skillPercents[row];
                auto* bonusItem = static_cast<CompletionSortItem*>(skills->item(tableRow, 3));
                const double bonus = (activeProfile->skill_bonus_multiplier(skillIds[row], input.now) - 1.0) * 100.0;
                bonusItem->setText(bonus > 0.01 ? QString::number(bonus, 'f', 1) + QLatin1Char('%') : QString::fromUtf8("—"));
                bonusItem->numeric = true; bonusItem->numericKey = bonus;
                auto* xpItem = static_cast<CompletionSortItem*>(skills->item(tableRow, 4));
                xpItem->setText(QString::number(gained)); xpItem->numeric = true; xpItem->numericKey = gained;
            }
            showSummary(summary, QString::fromUtf8("Пул: %1 XP · после штрафа: %2 XP · участников: %3")
                .arg(preview.rawPool).arg(preview.finalize.basePool).arg(preview.finalize.participants.size()));
            save->setEnabled(!title->text().trimmed().isEmpty() && !project->currentText().trimmed().isEmpty() &&
                !note->toPlainText().trimmed().isEmpty());
        }
        const int sortColumn = skillSort->currentIndex() == 1 ? 2 : skillSort->currentIndex() == 2 ? 3 : skillSort->currentIndex() == 3 ? 4 : 0;
        std::vector<size_t> order(skillIds.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
            const auto* a = skills->item(rowForSkill(left), sortColumn);
            const auto* b = skills->item(rowForSkill(right), sortColumn);
            if (sortColumn == 0) return a->text().localeAwareCompare(b->text()) < 0;
            const auto* numericA = static_cast<const CompletionSortItem*>(a);
            const auto* numericB = static_cast<const CompletionSortItem*>(b);
            if (numericA->numericKey != numericB->numericKey) return numericA->numericKey > numericB->numericKey;
            return skills->item(rowForSkill(left), 0)->text().localeAwareCompare(skills->item(rowForSkill(right), 0)->text()) < 0;
        });
        reorderSkillRows(skills, skillIds, order, ratings, &dialog, refresh);
        const auto filter = skillFilter->text().trimmed();
        for (int row = 0; row < skills->rowCount(); ++row) {
            const auto name = skills->item(row, 0)->text();
            const auto id = u(skills->item(row, 0)->data(Qt::UserRole).toString());
            const auto skillIndex = std::find(skillIds.begin(), skillIds.end(), id);
            const bool skillEnabled = skillIndex != skillIds.end() && allowed[size_t(skillIndex - skillIds.begin())];
            skills->setRowHidden(row, !skillEnabled || (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)));
        }
    };
    QObject::connect(evenParticipants, &QPushButton::clicked, &dialog, [&] {
        const int count = int(std::count_if(shares.begin(), shares.end(), [](auto* spin) { return spin->value() > 0; }));
        if (count <= 0) return;
        int remainder = 100 % count;
        for (auto* spin : shares) if (spin->value() > 0) {
            QSignalBlocker blocker(spin);
            spin->setValue(100 / count + (remainder-- > 0 ? 1 : 0));
        }
        refresh();
    });
    QObject::connect(onlyActive, &QPushButton::clicked, &dialog, [&] {
        for (size_t i = 0; i < shares.size(); ++i) {
            QSignalBlocker blocker(shares[i]);
            shares[i]->setValue(profileIds[i] == activeId ? 100 : 0);
        }
        refresh();
    });
    QObject::connect(evenSkills, &QPushButton::clicked, &dialog, [&] {
        for (size_t i = 0; i < ratings.size(); ++i) {
            QSignalBlocker blocker(ratings[i]);
            ratings[i]->setValue(allowed[i] ? 1 : 0);
        }
        refresh();
    });
    for (auto* spin : shares) QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
    for (auto* spin : ratings) QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(project, &QComboBox::currentTextChanged, &dialog, refresh);
    QObject::connect(title, &QLineEdit::textChanged, &dialog, refresh);
    QObject::connect(note, &QPlainTextEdit::textChanged, &dialog, refresh);
    QObject::connect(category, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(score, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(skillFilter, &QLineEdit::textChanged, &dialog, refresh);
    QObject::connect(skillSort, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        save->setEnabled(false);
        const auto input = buildInput();
        const auto result = CreateManualTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, buildDraft(), input);
        if (!result.ok) {
            showSummary(summary, q(result.errorMessage));
            save->setEnabled(!std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"));
            return;
        }
        dialog.accept();
    });
    refresh();
    return dialog.exec() == QDialog::Accepted;
}
