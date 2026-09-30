#include "QtProfileDialogs.h"
#include "QtWorkspace.h"
#include "QtScrollableDialog.h"
#include "QtDisclosureButton.h"
#include "QtDisplaySettings.h"
#include "AppProfileMutationService.h"
#include "AppProfileDeletionService.h"
#include "AppTaskCompletionService.h"
#include "AppUtils.h"
#include <QtWidgets>
#include <algorithm>

namespace {
QString q(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string u(const QString& s) { return s.toUtf8().toStdString(); }
void labelForAccessibility(QWidget* widget, const QString& name, const QString& description) {
    widget->setAccessibleName(name);
    widget->setAccessibleDescription(description);
}
QLabel* notice(QLayout* layout) {
    auto* label = new QLabel;
    label->setObjectName("profileNotice");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAccessibleName(QString::fromUtf8("Результат операции с профилем"));
    layout->addWidget(label);
    return label;
}
void prepareForm(QtScrollableDialog& dialog, QFormLayout* form) {
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(dialog.scaledMetric(12));
    form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFormAlignment(Qt::AlignTop);
}
void secondaryCommand(QPushButton* button, const QString& name, const QString& description) {
    button->setAutoDefault(false);
    labelForAccessibility(button, name, description);
    button->setToolTip(description);
}
bool validProfileName(const QString& name) {
    return !name.trimmed().isEmpty() && !std::any_of(name.begin(), name.end(),
        [](QChar c) { return c.category() == QChar::Other_Control; });
}
// The same row-wrap contract as QtWindow's flow layout, kept local because
// these four commands must advertise their height before the dialog lays out.
class ProfileCommandFlowLayout final : public QLayout {
public:
    explicit ProfileCommandFlowLayout(QWidget* parent, int spacing)
        : QLayout(parent), spacing_(spacing) { setContentsMargins(0, 0, 0, 0); }
    ~ProfileCommandFlowLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.append(item); }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index, nullptr); }
    QLayoutItem* takeAt(int index) override {
        return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize result;
        for (const auto* item : items_) if (!item->isEmpty()) result = result.expandedTo(item->minimumSize());
        return result;
    }
    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect); doLayout(rect, true);
    }
private:
    int doLayout(const QRect& area, bool place) const {
        int x = area.x(), y = area.y(), rowHeight = 0;
        for (auto* item : items_) {
            if (item->isEmpty()) continue;
            const QSize wanted = item->sizeHint().expandedTo(item->minimumSize());
            if (rowHeight > 0 && x + wanted.width() > area.x() + area.width()) {
                x = area.x(); y += rowHeight + spacing_; rowHeight = 0;
            }
            if (place) item->setGeometry(QRect(QPoint(x, y), wanted));
            x += wanted.width() + spacing_;
            rowHeight = std::max(rowHeight, wanted.height());
        }
        return y - area.y() + rowHeight;
    }
    QList<QLayoutItem*> items_;
    int spacing_;
};
// Retain QInputDialog compatibility while keeping an invalid draft open.
class ProfileNameInputDialog final : public QInputDialog {
public:
    explicit ProfileNameInputDialog(QtScrollableDialog* parent) : QInputDialog(parent) {
        setObjectName("profileCreateDialog");
        setWindowTitle(QString::fromUtf8("Создание профиля"));
        setInputMode(QInputDialog::TextInput);
        setLabelText(QString::fromUtf8("Имя профиля"));
        setOkButtonText(QString::fromUtf8("Создать"));
        setCancelButtonText(QString::fromUtf8("Отмена"));
        input_ = findChild<QLineEdit*>();
        if (input_) {
            input_->setObjectName("profileCreateName");
            labelForAccessibility(input_, QString::fromUtf8("Имя нового профиля"),
                QString::fromUtf8("Обязательное имя без управляющих символов."));
        }
        error_ = new QLabel(this);
        error_->setObjectName("profileNameValidationNotice");
        error_->setTextFormat(Qt::PlainText); error_->setWordWrap(true);
        error_->setAccessibleName(QString::fromUtf8("Ошибка имени нового профиля"));
        if (auto* column = qobject_cast<QVBoxLayout*>(layout())) column->insertWidget(column->count() - 1, error_);
        else if (layout()) layout()->addWidget(error_);
        error_->hide();
        // QInputDialog normally fixes its maximum height to the initial form.
        // A later validation row needs an intrinsic minimum, not that old cap.
        if (layout()) layout()->setSizeConstraint(QLayout::SetMinimumSize);
        setMaximumHeight(QWIDGETSIZE_MAX);
        QObject::connect(this, &QInputDialog::textValueChanged, this, [this](const QString& text) {
            if (validProfileName(text.trimmed())) {
                error_->clear(); error_->hide();
                if (input_) input_->setAccessibleDescription(QString::fromUtf8("Обязательное имя без управляющих символов."));
            }
        });
        if (auto* box = findChild<QDialogButtonBox*>()) {
            if (auto* save = box->button(QDialogButtonBox::Ok)) {
                save->setObjectName("profileCreateSave");
                save->setProperty("primary", true); save->setDefault(true);
                save->setAutoDefault(true);
                // QInputDialog creates and polishes these buttons before our
                // dynamic primary property exists; refresh the shared style.
                save->style()->unpolish(save); save->style()->polish(save); save->update();
                save->setMinimumHeight(parent->scaledMetric(32));
                labelForAccessibility(save, QString::fromUtf8("Создать новый профиль"),
                    QString::fromUtf8("Создаёт профиль после проверки имени."));
            }
            if (auto* cancel = box->button(QDialogButtonBox::Cancel)) {
                cancel->setObjectName("profileCreateCancel");
                cancel->setAutoDefault(false);
                labelForAccessibility(cancel, QString::fromUtf8("Отменить создание профиля"),
                    QString::fromUtf8("Закрывает форму без создания профиля."));
            }
        }
        resize(560, 220);
    }
    void accept() override {
        // Validate the trimmed name just as the existing mutation path does.
        if (!validProfileName(textValue().trimmed())) {
            error_->setText(QString::fromUtf8("Введите непустое имя без управляющих символов."));
            error_->show();
            setMaximumHeight(QWIDGETSIZE_MAX);
            if (layout()) {
                layout()->invalidate(); layout()->activate();
                const int neededHeight = layout()->hasHeightForWidth() ? layout()->heightForWidth(width()) : sizeHint().height();
                resize(width(), std::max(height(), std::max(minimumSizeHint().height(), neededHeight)));
            }
            if (input_) { input_->setAccessibleDescription(error_->text()); input_->setFocus(Qt::OtherFocusReason); }
            return;
        }
        QInputDialog::accept();
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
            auto* button = qobject_cast<QPushButton*>(focusWidget());
            if (button && button->isVisible() && button->isEnabled()) {
                event->accept(); button->click(); return;
            }
        }
        QInputDialog::keyPressEvent(event);
    }
private:
    QLineEdit* input_ = nullptr;
    QLabel* error_ = nullptr;
};
bool canWrite(QtWorkspace& workspace, QLabel* error) {
    if (!std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return true;
    error->setText(QString::fromUtf8("Сначала восстановите незавершённую XP-транзакцию перезапуском Qt."));
    return false;
}
}

bool ShowProfilePasswordDialog(QWidget* parent, QtWorkspace& workspace, const QString& profileId,
                               const QString& activeId, bool adminReset) {
    QtScrollableDialog dialog(parent, QSize(560, 380));
    dialog.setObjectName("profilePasswordDialog");
    dialog.setWindowTitle(adminReset ? QString::fromUtf8("Сброс пароля профиля") : QString::fromUtf8("Смена пароля профиля"));
    auto* form = dialog.formLayout();
    prepareForm(dialog, form);
    auto* current = new QLineEdit;
    auto* next = new QLineEdit;
    auto* confirm = new QLineEdit;
    current->setObjectName("currentPassword");
    next->setObjectName("newPassword");
    confirm->setObjectName("confirmPassword");
    labelForAccessibility(current, QString::fromUtf8("Текущий пароль профиля"),
        QString::fromUtf8("Введите действующий пароль профиля для смены пароля."));
    labelForAccessibility(next, QString::fromUtf8("Новый пароль профиля"),
        QString::fromUtf8("Пароль должен совпадать с подтверждением."));
    labelForAccessibility(confirm, QString::fromUtf8("Подтверждение нового пароля профиля"),
        QString::fromUtf8("Повторите новый пароль."));
    for (auto* edit : {current, next, confirm}) edit->setEchoMode(QLineEdit::Password);
    for (auto* edit : {current, next, confirm}) edit->setParent(&dialog);
    QString generatedPassword;
    QCheckBox* resetConfirmed = nullptr;
    QLineEdit* resetLogin = nullptr;
    QLineEdit* resetPassword = nullptr;
    QCheckBox* revealResetPassword = nullptr;
    QPushButton* copyResetLogin = nullptr;
    QPushButton* copyResetPassword = nullptr;
    if (adminReset) {
        current->hide();
        next->hide();
        confirm->hide();
        auto& storage = *workspace.storage;
        if (!storage.set_active_profile(u(profileId))) return false;
        const auto profile = storage.load_profile();
        if (!activeId.isEmpty()) storage.set_active_profile(u(activeId));
        if (!profile) return false;
        generatedPassword = q(GenerateRandomPassword());
        auto* loginRow = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
        loginRow->setObjectName("resetProfileLoginRow");
        resetLogin = new QLineEdit(profile->login().empty() ? profileId : q(profile->login()));
        resetLogin->setObjectName("resetProfileLogin");
        resetLogin->setReadOnly(true);
        labelForAccessibility(resetLogin, QString::fromUtf8("Логин профиля"),
            QString::fromUtf8("Логин профиля, пароль которого будет сброшен."));
        copyResetLogin = new QPushButton(QString::fromUtf8("Копировать логин"));
        copyResetLogin->setObjectName("copyResetProfileLogin");
        labelForAccessibility(copyResetLogin, QString::fromUtf8("Копировать логин профиля"),
            QString::fromUtf8("Копирует логин профиля в буфер обмена."));
        secondaryCommand(copyResetLogin, copyResetLogin->accessibleName(), copyResetLogin->accessibleDescription());
        loginRow->addWidget(resetLogin, 1);
        loginRow->addWidget(copyResetLogin);
        form->addRow(QString::fromUtf8("Логин"), loginRow);

        auto* passwordRow = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
        passwordRow->setObjectName("resetProfilePasswordRow");
        resetPassword = new QLineEdit(generatedPassword, passwordRow);
        resetPassword->setObjectName("resetProfileGeneratedPassword");
        resetPassword->setReadOnly(true);
        resetPassword->setEchoMode(QLineEdit::Password);
        labelForAccessibility(resetPassword, QString::fromUtf8("Сгенерированный пароль профиля"),
            QString::fromUtf8("Новый пароль, который будет сохранён после подтверждения сброса."));
        revealResetPassword = new QCheckBox(QString::fromUtf8("Показать"), passwordRow);
        revealResetPassword->setObjectName("revealResetProfilePassword");
        labelForAccessibility(revealResetPassword, QString::fromUtf8("Показать новый пароль"),
            QString::fromUtf8("Показывает сгенерированный пароль перед его сохранением."));
        copyResetPassword = new QPushButton(QString::fromUtf8("Копировать пароль"), passwordRow);
        copyResetPassword->setObjectName("copyResetProfilePassword");
        copyResetPassword->setEnabled(false);
        labelForAccessibility(copyResetPassword, QString::fromUtf8("Копировать новый пароль"),
            QString::fromUtf8("Становится доступно после явного показа нового пароля."));
        secondaryCommand(copyResetPassword, copyResetPassword->accessibleName(), copyResetPassword->accessibleDescription());
        passwordRow->addWidget(resetPassword, 1);
        passwordRow->addWidget(revealResetPassword);
        passwordRow->addWidget(copyResetPassword);
        form->addRow(QString::fromUtf8("Новый пароль"), passwordRow);
        QObject::connect(revealResetPassword, &QCheckBox::toggled, &dialog, [=](bool show) {
            resetPassword->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
            copyResetPassword->setEnabled(show);
        });
        QObject::connect(copyResetLogin, &QPushButton::clicked, &dialog, [=] {
            QApplication::clipboard()->setText(resetLogin->text());
        });
        QObject::connect(copyResetPassword, &QPushButton::clicked, &dialog, [=] {
            if (revealResetPassword->isChecked()) QApplication::clipboard()->setText(generatedPassword);
        });
        resetConfirmed = new QCheckBox(QString::fromUtf8("Подтверждаю сброс пароля"));
        resetConfirmed->setObjectName("confirmProfilePasswordReset");
        labelForAccessibility(resetConfirmed, resetConfirmed->text(),
            QString::fromUtf8("Явное подтверждение перед сохранением сгенерированного пароля."));
        resetConfirmed->setToolTip(resetConfirmed->accessibleDescription());
        form->addRow(resetConfirmed);
    } else {
        form->addRow(QString::fromUtf8("Текущий пароль"), current);
        form->addRow(QString::fromUtf8("Новый пароль"), next);
        form->addRow(QString::fromUtf8("Повторите пароль"), confirm);
    }
    auto* error = notice(dialog.footerLayout());
    for (auto* input : {current, next, confirm}) {
        QObject::connect(input, &QLineEdit::textChanged, error, [error] { error->clear(); });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("profilePasswordButtons");
    auto* saveButton = buttons->button(QDialogButtonBox::Save);
    auto* cancelButton = buttons->button(QDialogButtonBox::Cancel);
    saveButton->setText(adminReset ? QString::fromUtf8("Сбросить") : QString::fromUtf8("Сохранить"));
    saveButton->setObjectName("profilePasswordSave");
    saveButton->setProperty("primary", true);
    saveButton->setMinimumHeight(dialog.scaledMetric(32));
    saveButton->setDefault(true);
    saveButton->setAutoDefault(true);
    labelForAccessibility(saveButton, adminReset ? QString::fromUtf8("Подтвердить сброс пароля")
        : QString::fromUtf8("Сохранить новый пароль"), QString::fromUtf8("Сохраняет пароль только после проверки полей."));
    cancelButton->setText(QString::fromUtf8("Отмена"));
    cancelButton->setObjectName("profilePasswordCancel");
    secondaryCommand(cancelButton, QString::fromUtf8("Отменить изменение пароля"),
        QString::fromUtf8("Закрывает форму без сохранения нового пароля."));
    dialog.footerLayout()->addWidget(buttons);
    QWidget* previous = adminReset ? static_cast<QWidget*>(resetLogin) : current;
    const auto appendTab = [&previous](QWidget* control) {
        if (!control) return;
        QWidget::setTabOrder(previous, control); previous = control;
    };
    if (adminReset) {
        for (QWidget* control : std::initializer_list<QWidget*>{copyResetLogin, resetPassword,
            revealResetPassword, copyResetPassword, resetConfirmed}) appendTab(control);
    } else { appendTab(next); appendTab(confirm); }
    appendTab(saveButton); appendTab(cancelButton);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool resetSaved = false;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (resetSaved) { dialog.accept(); return; }
        if (!canWrite(workspace, error)) return;
        if (adminReset && !resetConfirmed->isChecked()) {
            error->setText(QString::fromUtf8("Подтвердите сброс пароля перед сохранением."));
            dialog.scrollArea()->ensureWidgetVisible(resetConfirmed);
            resetConfirmed->setFocus(Qt::OtherFocusReason);
            return;
        }
        if (!adminReset && (next->text().isEmpty() || next->text() != confirm->text())) {
            error->setText(QString::fromUtf8("Новый пароль пуст или подтверждение не совпадает."));
            auto* invalid = next->text().isEmpty() ? next : confirm;
            dialog.scrollArea()->ensureWidgetVisible(invalid);
            invalid->setFocus(Qt::OtherFocusReason);
            return;
        }
        auto& storage = *workspace.storage;
        if (!storage.set_active_profile(u(profileId))) { error->setText(QString::fromUtf8("Профиль недоступен.")); return; }
        auto profile = storage.load_profile();
        if (!activeId.isEmpty()) storage.set_active_profile(u(activeId));
        // The legacy core permits setting an empty-password profile without verification.
        // In Qt, that recovery path is reserved for an administrator.
        if (!profile || (!adminReset && (profile->is_blocked() || profile->password_encoded().empty()))) {
            error->setText(QString::fromUtf8("Профиль заблокирован или не имеет пароля. Обратитесь к администратору."));
            return;
        }
        AppContext context{workspace.directory, storage, workspace.catalog, workspace.profileEventLogger};
        auto result = ChangeProfilePasswordWithAuditRecovery(context, u(activeId), u(profileId),
            adminReset ? std::string{} : u(current->text()), adminReset ? u(generatedPassword) : u(next->text()),
            !adminReset, adminReset ? "password_reset" : "password_change");
        if (!result.ok) {
            error->setText(q(result.errorMessage));
            if (!adminReset) {
                dialog.scrollArea()->ensureWidgetVisible(current);
                current->setFocus(Qt::OtherFocusReason);
            }
            return;
        }
        if (!adminReset) { dialog.accept(); return; }
        resetSaved = true;
        error->setText(QString::fromUtf8("Пароль сброшен. Сохраните логин и новый пароль до закрытия окна."));
        saveButton->setText(QString::fromUtf8("Готово"));
        saveButton->setAccessibleName(QString::fromUtf8("Закрыть подтверждённый сброс пароля"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Закрыть"));
        cancelButton->setAccessibleName(QString::fromUtf8("Закрыть окно после сброса пароля"));
        cancelButton->setAccessibleDescription(QString::fromUtf8("Пароль уже сохранён. Закрывает окно реквизитов."));
        cancelButton->setToolTip(QString::fromUtf8("Пароль уже сохранён. Закрывает окно реквизитов."));
    });
    return dialog.exec() == QDialog::Accepted;
}

QString ShowProfileManager(QWidget* parent, QtWorkspace& workspace, const QString& activeId) {
    QtScrollableDialog dialog(parent, QSize(760, 520));
    dialog.setObjectName("profileManager");
    dialog.setWindowTitle(QString::fromUtf8("Управление профилями"));
    auto* form = dialog.formLayout();
    prepareForm(dialog, form);
    const auto displaySettings = LoadQtDisplaySettings(workspace.directory);
    const auto motionAllowed = [displaySettings] { return IsQtMotionAllowed(displaySettings); };
    auto* search = new QLineEdit;
    search->setPlaceholderText(QString::fromUtf8("Имя, ID, логин или профессия"));
    search->setClearButtonEnabled(true);
    search->setObjectName("profileSearch");
    search->setAccessibleName(QString::fromUtf8("Поиск профилей"));
    search->setAccessibleDescription(QString::fromUtf8("Фильтрует по имени, идентификатору, логину или профессии."));
    auto* archiveFilter = new QComboBox;
    archiveFilter->setObjectName("profileArchiveFilter");
    archiveFilter->addItems({QString::fromUtf8("Все профили"), QString::fromUtf8("Активные"), QString::fromUtf8("Архив")});
    archiveFilter->setAccessibleName(QString::fromUtf8("Состояние профилей"));
    auto* professionFilter = new QComboBox;
    professionFilter->setObjectName("profileProfessionFilter");
    professionFilter->addItem(QString::fromUtf8("Все профессии"), QString());
    professionFilter->addItem(QString::fromUtf8("Без профессии"), QStringLiteral("__none__"));
    for (const auto& profession : workspace.data.professions) {
        if (profession.id.empty()) continue;
        professionFilter->addItem(q(profession.name), q(profession.id));
    }
    professionFilter->setAccessibleName(QString::fromUtf8("Фильтр по профессии"));
    auto* sort = new QComboBox;
    sort->setObjectName("profileSort");
    sort->addItems({QString::fromUtf8("Сортировка: ID"), QString::fromUtf8("Сортировка: имя")});
    sort->setAccessibleName(QString::fromUtf8("Сортировка профилей"));
    auto* create = new QPushButton(QString::fromUtf8("Создать профиль"));
    create->setObjectName("createProfile");
    secondaryCommand(create, QString::fromUtf8("Создать профиль"),
        QString::fromUtf8("Создаёт профиль и показывает его сгенерированные реквизиты."));
    auto* refreshProfiles = new QPushButton(QString::fromUtf8("Обновить список"));
    refreshProfiles->setObjectName("refreshProfiles");
    secondaryCommand(refreshProfiles, QString::fromUtf8("Обновить список профилей"),
        QString::fromUtf8("Повторно читает профили, сохраняя текущий поиск и фильтры."));
    refreshProfiles->setIcon(CreateQtActionIcon(QtActionIcon::Reset, refreshProfiles->palette()));
    for (auto* box : {archiveFilter, professionFilter, sort}) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(8);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QObject::connect(box, &QComboBox::currentTextChanged, box,
            [box](const QString& text) { box->setToolTip(text); });
        box->setToolTip(box->currentText());
    }
    auto* searchRow = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    searchRow->setObjectName("profileSearchRow");
    searchRow->addWidget(search, 1); searchRow->addWidget(create);
    form->addRow(searchRow);
    auto* profileCount = new QLabel;
    profileCount->setObjectName("profileListSummary");
    profileCount->setAccessibleName(QString::fromUtf8("Количество отображаемых профилей"));
    profileCount->setAccessibleDescription(QString::fromUtf8("Число профилей, прошедших поиск и фильтры, из общего числа профилей."));
    profileCount->setWordWrap(true);
    auto* filterToggle = new QtDisclosureButton(nullptr, motionAllowed);
    filterToggle->setObjectName("profileFiltersToggle");
    filterToggle->setText(QString::fromUtf8("Фильтры"));
    labelForAccessibility(filterToggle, QString::fromUtf8("Фильтры и сортировка профилей"),
        QString::fromUtf8("Свёрнуто. Состояние, профессия и порядок списка."));
    filterToggle->setToolTip(QString::fromUtf8("Состояние, профессия, сортировка и обновление списка"));
    auto* summaryRow = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    summaryRow->setObjectName("profileSummaryRow");
    summaryRow->addWidget(profileCount, 1); summaryRow->addWidget(filterToggle);
    form->addRow(summaryRow);
    auto* filtersPanel = new QWidget;
    filtersPanel->setObjectName("profileFiltersPanel");
    auto* filters = new QFormLayout(filtersPanel);
    prepareForm(dialog, filters);
    filters->setSizeConstraint(QLayout::SetMinimumSize);
    filters->addRow(QString::fromUtf8("Состояние"), archiveFilter);
    filters->addRow(QString::fromUtf8("Профессия"), professionFilter);
    filters->addRow(QString::fromUtf8("Порядок"), sort);
    filters->addRow(refreshProfiles);
    form->addRow(filtersPanel);
    filtersPanel->hide();
    QObject::connect(filterToggle, &QToolButton::toggled, filtersPanel,
        [filterToggle, filtersPanel](bool expanded) {
            filtersPanel->setVisible(expanded);
            filterToggle->setAccessibleDescription(expanded ? QString::fromUtf8("Развёрнуто") : QString::fromUtf8("Свёрнуто"));
        });
    auto* table = new QTableWidget;
    table->setObjectName("profileRecords");
    table->setAccessibleName(QString::fromUtf8("Список профилей"));
    table->setAccessibleDescription(QString::fromUtf8("Таблица профилей с ID, логином, профессией и состоянием; выберите строку для доступных действий."));
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QString::fromUtf8("Профиль"), "ID", QString::fromUtf8("Логин"),
        QString::fromUtf8("Профессия"), QString::fromUtf8("Состояние"), QString::fromUtf8("Баланс")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setMinimumSectionSize(dialog.scaledMetric(32));
    for (int column = 0; column < table->columnCount(); ++column)
        table->horizontalHeaderItem(column)->setToolTip(table->horizontalHeaderItem(column)->text());
    table->verticalHeader()->hide();
    const int rowHeight = std::max(dialog.scaledMetric(28), QFontMetrics(table->font()).lineSpacing() + dialog.scaledMetric(8));
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    table->setMinimumHeight(table->horizontalHeader()->sizeHint().height() + rowHeight * 3 + table->frameWidth() * 2);
    table->setWordWrap(false);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // The list is a working surface, not a field in the controls' scroll body.
    // Expanding filters or credentials must never push its rows off screen.
    auto* outer = qobject_cast<QVBoxLayout*>(dialog.layout());
    dialog.scrollArea()->setMinimumHeight(0);
    if (outer) {
        outer->setStretch(0, 0);
        outer->insertWidget(1, table, 1);
    }
    auto* openProfile = new QPushButton(QString::fromUtf8("Открыть"), &dialog);
    openProfile->setObjectName("openManagedProfile");
    labelForAccessibility(openProfile, QString::fromUtf8("Открыть выбранный профиль"),
        QString::fromUtf8("Переключает рабочую страницу на выбранный активный профиль без изменения его данных."));
    openProfile->setToolTip(openProfile->accessibleDescription());
    openProfile->setProperty("primary", true);
    openProfile->setMinimumHeight(dialog.scaledMetric(32));
    openProfile->setAutoDefault(true); openProfile->setDefault(true);
    auto* edit = new QPushButton(QString::fromUtf8("Изменить"));
    edit->setObjectName("editProfile");
    secondaryCommand(edit, QString::fromUtf8("Редактировать выбранный профиль"),
        QString::fromUtf8("Открывает имя, профессию, дух и блокировку выбранного профиля."));
    edit->setIcon(CreateQtActionIcon(QtActionIcon::Edit, edit->palette()));
    auto* archive = new QPushButton(QString::fromUtf8("В архив"), &dialog);
    archive->setObjectName("archiveProfile");
    auto* password = new QPushButton(QString::fromUtf8("Сбросить пароль"), &dialog);
    password->setObjectName("resetProfilePassword");
    auto* remove = new QPushButton(QString::fromUtf8("Удалить навсегда"), &dialog);
    remove->setObjectName("deleteArchivedProfile");
    remove->setToolTip(QString::fromUtf8("Только пустой архивный профиль без задач и прогресса"));
    // Keep the original command objects and handlers. Visible menu actions
    // invoke them without forcing five wide buttons above the working list.
    auto* menu = new QMenu(&dialog);
    menu->setObjectName("profileActionsMenu");
    auto* archiveAction = menu->addAction(QString::fromUtf8("Архивировать профиль"));
    archiveAction->setObjectName("archiveProfileAction");
    archiveAction->setToolTip(QString::fromUtf8("Сохраняет профиль и историю; архивный профиль можно восстановить."));
    auto* passwordAction = menu->addAction(CreateQtActionIcon(QtActionIcon::Reset, menu->palette()), QString::fromUtf8("Сбросить пароль"));
    passwordAction->setObjectName("resetProfilePasswordAction");
    passwordAction->setToolTip(QString::fromUtf8("Показывает сгенерированный пароль и требует отдельного подтверждения."));
    auto* removeAction = menu->addAction(CreateQtActionIcon(QtActionIcon::Delete, menu->palette()), QString::fromUtf8("Удалить навсегда"));
    removeAction->setObjectName("deleteArchivedProfileAction");
    removeAction->setToolTip(remove->toolTip());
    menu->setToolTipsVisible(true);
    for (auto* button : {archive, password, remove}) { button->setAutoDefault(false); button->hide(); }
    QObject::connect(archiveAction, &QAction::triggered, archive, &QPushButton::click);
    QObject::connect(passwordAction, &QAction::triggered, password, &QPushButton::click);
    QObject::connect(removeAction, &QAction::triggered, remove, &QPushButton::click);
    auto* more = new QPushButton(QString::fromUtf8("Действия"));
    more->setObjectName("profileMoreActions");
    secondaryCommand(more, QString::fromUtf8("Дополнительные действия с профилем"),
        QString::fromUtf8("Архив, сброс пароля и подтверждаемое удаление пустого архивного профиля."));
    more->setMenu(menu);
    auto* status = notice(form);
    auto* credentials = new QLineEdit;
    credentials->setObjectName("createdProfileCredentials");
    credentials->setReadOnly(true);
    credentials->setEchoMode(QLineEdit::Password);
    credentials->hide();
    labelForAccessibility(credentials, QString::fromUtf8("Реквизиты нового профиля"),
        QString::fromUtf8("Сгенерированные реквизиты скрыты до явного показа; содержимое не записывается в журнал."));
    QString createdLoginValue;
    QString createdPasswordValue;
    auto* credentialActionsWidget = new QtDialogAdaptiveRow(nullptr, dialog.scaledMetric(8));
    credentialActionsWidget->setObjectName("profileCredentialsActions");
    auto* copyCreatedLogin = new QPushButton(QString::fromUtf8("Копировать логин"));
    copyCreatedLogin->setObjectName("copyCreatedProfileLogin");
    labelForAccessibility(copyCreatedLogin, QString::fromUtf8("Копировать логин нового профиля"),
        QString::fromUtf8("Копирует логин созданного профиля в буфер обмена."));
    auto* copyCreatedPassword = new QPushButton(QString::fromUtf8("Копировать пароль"));
    copyCreatedPassword->setObjectName("copyCreatedProfilePassword");
    labelForAccessibility(copyCreatedPassword, QString::fromUtf8("Копировать пароль нового профиля"),
        QString::fromUtf8("Становится доступно после явного показа реквизитов."));
    copyCreatedPassword->setEnabled(false);
    for (auto* button : {copyCreatedLogin, copyCreatedPassword}) {
        secondaryCommand(button, button->accessibleName(), button->accessibleDescription());
        credentialActionsWidget->addWidget(button);
    }
    credentialActionsWidget->hide();
    auto* reveal = new QCheckBox(QString::fromUtf8("Показать реквизиты"));
    reveal->setObjectName("showCreatedProfileCredentials");
    reveal->setAccessibleName(QString::fromUtf8("Показать реквизиты нового профиля"));
    reveal->setAccessibleDescription(QString::fromUtf8("Показывает сгенерированные логин и пароль профиля."));
    reveal->hide();
    auto* credentialsToggle = new QtDisclosureButton(nullptr, motionAllowed);
    credentialsToggle->setObjectName("profileCredentialsToggle");
    credentialsToggle->setText(QString::fromUtf8("Реквизиты нового профиля"));
    labelForAccessibility(credentialsToggle, QString::fromUtf8("Реквизиты нового профиля"), QString::fromUtf8("Свёрнуто"));
    credentialsToggle->setToolTip(QString::fromUtf8("Сохраните логин и пароль созданного профиля перед закрытием окна."));
    form->addRow(credentialsToggle);
    credentialsToggle->hide();
    auto* credentialsPanel = new QWidget;
    credentialsPanel->setObjectName("profileCredentialsPanel");
    auto* credentialForm = new QFormLayout(credentialsPanel);
    prepareForm(dialog, credentialForm);
    credentialForm->setSizeConstraint(QLayout::SetMinimumSize);
    credentialForm->addRow(credentials);
    credentialForm->addRow(reveal);
    credentialForm->addRow(credentialActionsWidget);
    form->addRow(credentialsPanel);
    credentialsPanel->hide();
    QObject::connect(credentialsToggle, &QToolButton::toggled, credentialsPanel,
        [credentialsToggle, credentialsPanel](bool expanded) {
            credentialsPanel->setVisible(expanded);
            credentialsToggle->setAccessibleDescription(expanded ? QString::fromUtf8("Развёрнуто") : QString::fromUtf8("Свёрнуто"));
        });
    QString openedProfileId;
    QObject::connect(reveal, &QCheckBox::toggled, &dialog, [=](bool show) {
        credentials->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        copyCreatedPassword->setEnabled(show);
    });
    QObject::connect(copyCreatedLogin, &QPushButton::clicked, &dialog, [&] {
        if (!createdLoginValue.isEmpty()) QApplication::clipboard()->setText(createdLoginValue);
    });
    QObject::connect(copyCreatedPassword, &QPushButton::clicked, &dialog, [&] {
        if (reveal->isChecked() && !createdPasswordValue.isEmpty())
            QApplication::clipboard()->setText(createdPasswordValue);
    });
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    close->setObjectName("profileManagerButtons");
    close->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    secondaryCommand(close->button(QDialogButtonBox::Close), QString::fromUtf8("Закрыть управление профилями"),
        QString::fromUtf8("Закрывает список, не открывая выбранный профиль."));
    auto* commands = new QWidget;
    commands->setObjectName("profileManagerCommands");
    auto* commandFlow = new ProfileCommandFlowLayout(commands, dialog.scaledMetric(8));
    commandFlow->addWidget(openProfile); commandFlow->addWidget(edit);
    commandFlow->addWidget(more); commandFlow->addWidget(close);
    dialog.footerLayout()->addWidget(commands);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto selected = [&]() -> std::optional<IJobStorage::ProfileInfo> {
        const int currentRow = table->currentRow();
        if (currentRow < 0) return {};
        auto* item = table->item(currentRow, 0);
        if (!item) return {};
        const auto id = u(item->data(Qt::UserRole).toString());
        for (const auto& p : workspace.profiles) if (p.id == id) return p;
        return {};
    };
    auto selection = [&] {
        const auto info = selected();
        openProfile->setEnabled(info && !info->archived);
        edit->setEnabled(info && !info->archived);
        password->setEnabled(info && !info->archived);
        archive->setEnabled(bool(info));
        remove->setEnabled(info && info->archived);
        archive->setText(info && info->archived ? QString::fromUtf8("Восстановить") : QString::fromUtf8("В архив"));
        archiveAction->setText(info && info->archived ? QString::fromUtf8("Восстановить профиль") : QString::fromUtf8("Архивировать профиль"));
        archiveAction->setEnabled(archive->isEnabled());
        passwordAction->setEnabled(password->isEnabled());
        removeAction->setEnabled(remove->isEnabled());
        more->setEnabled(bool(info));
    };
    QObject::connect(openProfile, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (!info || info->archived) return;
        openedProfileId = q(info->id);
        dialog.accept();
    });
    auto refresh = [&] {
        const auto previous = selected();
        workspace.profiles = workspace.storage->list_profiles();
        const bool sortByName = sort->currentIndex() == 1;
        std::sort(workspace.profiles.begin(), workspace.profiles.end(), [sortByName](const auto& a, const auto& b) {
            if (sortByName) {
                const auto left = QString::fromUtf8(a.name.data(), int(a.name.size()));
                const auto right = QString::fromUtf8(b.name.data(), int(b.name.size()));
                const int compare = QString::compare(left, right, Qt::CaseInsensitive);
                if (compare != 0) return compare < 0;
            }
            return a.id < b.id;
        });
        QSignalBlocker blocker(table);
        table->setRowCount(0);
        for (const auto& p : workspace.profiles) {
            const int archiveMode = archiveFilter->currentIndex();
            if (archiveMode == 1 && p.archived) continue;
            if (archiveMode == 2 && !p.archived) continue;
            const auto snapshot = workspace.storage->load_profile_snapshot(p.id, true);
            const std::string professionId = snapshot ? snapshot->profession_id() : std::string{};
            const QString selectedProfession = professionFilter->currentData().toString();
            if (selectedProfession == QStringLiteral("__none__") && !professionId.empty()) continue;
            if (!selectedProfession.isEmpty() && selectedProfession != QStringLiteral("__none__") &&
                selectedProfession != q(professionId)) continue;
            QString professionName;
            for (const auto& profession : workspace.data.professions) {
                if (profession.id == professionId) { professionName = q(profession.name); break; }
            }
            if (professionName.isEmpty() && !professionId.empty()) professionName = q(professionId);
            const QString login = snapshot && !snapshot->login().empty() ? q(snapshot->login()) : q(p.id);
            const QString profileName = q(p.name);
            const QString query = search->text().trimmed();
            if (!query.isEmpty() && !(profileName + " " + q(p.id) + " " + login + " " + professionName)
                    .contains(query, Qt::CaseInsensitive)) continue;
            int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(profileName));
            table->item(row, 0)->setToolTip(profileName);
            table->item(row, 0)->setData(Qt::UserRole, q(p.id));
            table->setItem(row, 1, new QTableWidgetItem(q(p.id)));
            table->setItem(row, 2, new QTableWidgetItem(login));
            QString state = QString::fromUtf8("Архив");
            if (!p.archived) {
                state = !snapshot ? QString::fromUtf8("Ошибка чтения") :
                    (snapshot->is_blocked() ? QString::fromUtf8("Заблокирован") : QString::fromUtf8("Доступен"));
            }
            table->setItem(row, 3, new QTableWidgetItem(professionName.isEmpty() ? QString::fromUtf8("—") : professionName));
            table->setItem(row, 4, new QTableWidgetItem(state));
            std::string currencyCode = workspace.data.vault.currencyCode.empty()
                ? workspace.data.vault.currencyName : workspace.data.vault.currencyCode;
            if (currencyCode.empty()) currencyCode = "KUK";
            const QString currency = q(currencyCode);
            auto* balance = new QTableWidgetItem(snapshot
                ? QString::number(snapshot->wallet_balance(), 'f', 0) + " " + currency
                : QString::fromUtf8("—"));
            balance->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 5, balance);
            for (int column = 0; column < table->columnCount(); ++column) {
                auto* cell = table->item(row, column);
                cell->setToolTip(cell->text());
                cell->setData(Qt::AccessibleTextRole, cell->text());
            }
            if (previous && previous->id == p.id) table->selectRow(row);
        }
        profileCount->setText(QString::fromUtf8("Показано: %1 из %2")
            .arg(table->rowCount()).arg(workspace.profiles.size()));
        selection();
    };
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, selection);
    QObject::connect(search, &QLineEdit::textChanged, &dialog, refresh);
    QObject::connect(archiveFilter, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(professionFilter, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(sort, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(refreshProfiles, &QPushButton::clicked, &dialog, refresh);
    QObject::connect(create, &QPushButton::clicked, &dialog, [&] {
        if (!canWrite(workspace, status)) return;
        ProfileNameInputDialog input(&dialog);
        if (input.exec() != QDialog::Accepted) return;
        const auto name = input.textValue().trimmed();
        // Profile storage is INI; line breaks and control characters must not create new fields.
        if (name.isEmpty() || std::any_of(name.begin(), name.end(), [](QChar c) { return c.category() == QChar::Other_Control; })) {
            status->setText(QString::fromUtf8("Введите непустое имя без управляющих символов.")); return;
        }
        const auto result = AppCreateProfile(*workspace.storage, workspace.catalog, u(name),
            workspace.profileEventLogger);
        if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
        if (!result.ok) { status->setText(q(result.errorMessage)); return; }
        const bool profileAuditSaved = AppendProfileAudit(workspace.directory, result.profileId,
            "create", "login=" + result.login);
        createdLoginValue = q(result.login);
        createdPasswordValue = q(result.password);
        reveal->setChecked(false);
        credentials->setText(QString::fromUtf8("Логин: %1   Пароль: %2").arg(createdLoginValue, createdPasswordValue));
        credentials->show();
        reveal->show();
        credentialActionsWidget->show();
        credentialsToggle->show();
        status->setText(profileAuditSaved
            ? QString::fromUtf8("Профиль создан. Сохраните реквизиты перед закрытием окна.")
            : QString::fromUtf8("Профиль создан, но событие не записано в историю профиля. Сохраните реквизиты."));
        refresh();
    });
    QObject::connect(archive, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (!info || !canWrite(workspace, status)) return;
        if (!info->archived) {
            const int activeTasks = int(std::count_if(workspace.data.tasks.begin(), workspace.data.tasks.end(), [&](const auto& t) {
                return t.status != 2 && std::find(t.assignees.begin(), t.assignees.end(), info->id) != t.assignees.end();
            }));
            if (QMessageBox::question(&dialog, QString::fromUtf8("Архивировать профиль?"),
                QString::fromUtf8("Активных задач: %1. Профиль и история сохранятся, но начисление XP будет недоступно до восстановления.").arg(activeTasks),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        }
        const auto result = ArchiveProfileWithAuditRecovery(*workspace.storage, workspace.directory,
            u(activeId), info->id, !info->archived, workspace.profileEventLogger);
        status->setText(result.ok ? QString::fromUtf8("Состояние архива сохранено.") : q(result.errorMessage));
        refresh();
    });
    QObject::connect(password, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (info && canWrite(workspace, status)) ShowProfilePasswordDialog(&dialog, workspace, q(info->id), activeId, true);
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (!info || !info->archived || !canWrite(workspace, status)) return;
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Удалить профиль навсегда?"),
            QString::fromUtf8("Удалить пустой архивный профиль «%1»?\nПрофиль с задачами или прогрессом будет защищён проверкой.").arg(q(info->name)),
            QMessageBox::Yes | QMessageBox::No, &dialog);
        confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Удалить навсегда"));
        confirm.button(QMessageBox::No)->setText(QString::fromUtf8("Отмена"));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        bool prepared = false;
        bool committed = false;
        bool recoveryPending = false;
        try {
            PrepareProfileDeletionRecovery(workspace.directory, info->id);
            prepared = true;
            const auto result = AppDeleteEmptyArchivedProfile(*workspace.storage, workspace.profiles, workspace.data.tasks, info->id);
            if (!result.ok) throw std::runtime_error(result.errorMessage);
            CommitQtRecoveryTransaction(workspace.directory);
            committed = true;
            status->setText(QString::fromUtf8("Пустой архивный профиль удалён."));
        } catch (const std::exception& error) {
            std::string text = error.what();
            if (prepared && !committed) {
                try { text += RecoverTaskCompletionWithNotice(workspace.directory); }
                catch (const std::exception&) {
                    recoveryPending = true;
                    text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt.";
                }
            }
            status->setText(q(text));
        }
        if (workspace.profileEventLogger) {
            const auto level = committed ? AppLogLevel::Info : recoveryPending ? AppLogLevel::Error : AppLogLevel::Warning;
            const char* event = committed ? "Profile deletion committed" :
                recoveryPending ? "Profile deletion recovery remains pending" :
                "Profile deletion failed or was rolled back";
            try { workspace.profileEventLogger(level, event); } catch (...) {}
        }
        refresh();
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (!info || !canWrite(workspace, status) || !workspace.storage->set_active_profile(info->id)) return;
        auto loaded = workspace.storage->load_profile();
        if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
        if (!loaded) { status->setText(QString::fromUtf8("Не удалось загрузить профиль.")); return; }
        QtScrollableDialog editor(&dialog, QSize(560, 380));
        editor.setObjectName("profileEditor");
        editor.setWindowTitle(QString::fromUtf8("Параметры профиля · ") + q(info->name));
        auto* form = editor.formLayout();
        prepareForm(editor, form);
        auto* profileName = new QLineEdit(q(loaded->name()));
        profileName->setObjectName("profileName");
        labelForAccessibility(profileName, QString::fromUtf8("Имя профиля"),
            QString::fromUtf8("Обязательное имя без управляющих символов."));
        auto* profession = new QComboBox;
        profession->setObjectName("profileProfession");
        labelForAccessibility(profession, QString::fromUtf8("Профессия профиля"),
            QString::fromUtf8("Профессия определяет связанные навыки; XP не изменяется."));
        profession->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        profession->setMinimumContentsLength(8);
        profession->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        profession->addItem(QString::fromUtf8("Без профессии"), "");
        for (const auto& p : workspace.data.professions) profession->addItem(q(p.name), q(p.id));
        if (profession->findData(q(loaded->profession_id())) < 0) profession->addItem(q(loaded->profession_id()), q(loaded->profession_id()));
        profession->setCurrentIndex(profession->findData(q(loaded->profession_id())));
        profession->setToolTip(profession->currentText());
        QObject::connect(profession, &QComboBox::currentTextChanged, profession,
            [profession](const QString& text) { profession->setToolTip(text); });
        auto* spirit = new QComboBox;
        spirit->setObjectName("profileSpirit");
        labelForAccessibility(spirit, QString::fromUtf8("Дух профиля"),
            QString::fromUtf8("Добрый дух: +1% XP; злой: −1% XP. Действует на общий XP и навыки."));
        spirit->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        spirit->setMinimumContentsLength(8);
        spirit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        for (auto s : {ProfileSpirit::None, ProfileSpirit::Good, ProfileSpirit::Evil}) spirit->addItem(q(ProfileSpiritLabel(s)), int(s));
        spirit->setCurrentIndex(spirit->findData(int(loaded->spirit())));
        spirit->setToolTip(QString::fromUtf8("Добрый дух: +1% XP; злой: −1% XP. Действует на общий XP и навыки."));
        auto* blocked = new QCheckBox(QString::fromUtf8("Заблокировать профиль"));
        blocked->setObjectName("profileBlocked");
        blocked->setChecked(loaded->is_blocked());
        labelForAccessibility(blocked, blocked->text(),
            QString::fromUtf8("Сохраняет выбранное состояние блокировки профиля; XP не изменяется."));
        blocked->setToolTip(blocked->accessibleDescription());
        form->addRow(QString::fromUtf8("Имя"), profileName);
        form->addRow(QString::fromUtf8("Профессия"), profession);
        form->addRow(QString::fromUtf8("Дух"), spirit);
        form->addRow(blocked);
        auto* error = notice(editor.footerLayout());
        QObject::connect(profileName, &QLineEdit::textChanged, error, [=] {
            error->clear(); profileName->setAccessibleDescription(QString::fromUtf8("Обязательное имя без управляющих символов."));
        });
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
        buttons->setObjectName("profileEditorButtons");
        buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
        auto* save = buttons->button(QDialogButtonBox::Save);
        auto* cancel = buttons->button(QDialogButtonBox::Cancel);
        save->setObjectName("profileEditorSave");
        save->setProperty("primary", true);
        save->setMinimumHeight(editor.scaledMetric(32));
        save->setAutoDefault(true); save->setDefault(true);
        labelForAccessibility(save, QString::fromUtf8("Сохранить параметры профиля"),
            QString::fromUtf8("Сохраняет изменения имени, профессии, духа и блокировки с записью аудита."));
        cancel->setObjectName("profileEditorCancel");
        secondaryCommand(cancel, QString::fromUtf8("Отменить редактирование профиля"),
            QString::fromUtf8("Закрывает форму, сохраняя исходные параметры профиля."));
        editor.footerLayout()->addWidget(buttons);
        QWidget::setTabOrder(profileName, profession); QWidget::setTabOrder(profession, spirit);
        QWidget::setTabOrder(spirit, blocked); QWidget::setTabOrder(blocked, save); QWidget::setTabOrder(save, cancel);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
            if (!canWrite(workspace, error)) return;
            const auto candidateName = profileName->text().trimmed();
            if (candidateName.isEmpty() || std::any_of(candidateName.begin(), candidateName.end(), [](QChar c) { return c.category() == QChar::Other_Control; })) {
                error->setText(QString::fromUtf8("Введите непустое имя без управляющих символов."));
                profileName->setAccessibleDescription(error->text());
                editor.scrollArea()->ensureWidgetVisible(profileName);
                profileName->setFocus(Qt::OtherFocusReason);
                return;
            }
            Profile draft = *loaded;
            draft.set_name(u(candidateName));
            draft.set_profession_id(u(profession->currentData().toString()));
            draft.set_spirit(ProfileSpirit(spirit->currentData().toInt()));
            draft.set_blocked(blocked->isChecked());
            QStringList changedFields;
            if (draft.name() != loaded->name()) changedFields << "name";
            if (draft.profession_id() != loaded->profession_id()) changedFields << "profession";
            if (draft.spirit() != loaded->spirit()) changedFields << "spirit";
            if (draft.is_blocked() != loaded->is_blocked()) changedFields << "blocked";
            AppContext context{workspace.directory, *workspace.storage, workspace.catalog, workspace.profileEventLogger};
            const auto result = SaveProfileSnapshotWithAuditRecovery(context, u(activeId), info->id, draft,
                "profile_edit", u(changedFields.join(',')));
            if (!result.ok) {
                error->setText(q(result.errorMessage)); return;
            }
            editor.accept();
        });
        if (editor.exec() == QDialog::Accepted) {
            status->setText(QString::fromUtf8("Параметры сохранены и записаны в аудит; XP не изменён."));
            refresh();
        }
    });
    QWidget* previous = search;
    for (QWidget* control : std::initializer_list<QWidget*>{create, filterToggle, archiveFilter,
        professionFilter, sort, refreshProfiles, table, credentialsToggle, credentials, reveal,
        copyCreatedLogin, copyCreatedPassword, openProfile, edit, more,
        close->button(QDialogButtonBox::Close)}) {
        QWidget::setTabOrder(previous, control); previous = control;
    }
    refresh();
    dialog.exec();
    if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
    return openedProfileId;
}
