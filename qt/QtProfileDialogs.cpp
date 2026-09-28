#include "QtProfileDialogs.h"
#include "QtWorkspace.h"
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
    layout->addWidget(label);
    return label;
}
bool canWrite(QtWorkspace& workspace, QLabel* error) {
    if (!std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) return true;
    error->setText(QString::fromUtf8("Сначала восстановите незавершённую XP-транзакцию перезапуском Qt."));
    return false;
}
}

bool ShowProfilePasswordDialog(QWidget* parent, QtWorkspace& workspace, const QString& profileId,
                               const QString& activeId, bool adminReset) {
    QDialog dialog(parent);
    dialog.setObjectName("profilePasswordDialog");
    dialog.setWindowTitle(adminReset ? QString::fromUtf8("Сброс пароля профиля") : QString::fromUtf8("Смена пароля профиля"));
    dialog.setMinimumWidth(400);
    auto* form = new QFormLayout(&dialog);
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
        auto* loginRow = new QWidget(&dialog);
        auto* loginLayout = new QHBoxLayout(loginRow);
        loginLayout->setContentsMargins(0, 0, 0, 0);
        resetLogin = new QLineEdit(profile->login().empty() ? profileId : q(profile->login()));
        resetLogin->setObjectName("resetProfileLogin");
        resetLogin->setReadOnly(true);
        labelForAccessibility(resetLogin, QString::fromUtf8("Логин профиля"),
            QString::fromUtf8("Логин профиля, пароль которого будет сброшен."));
        copyResetLogin = new QPushButton(QString::fromUtf8("Копировать логин"));
        copyResetLogin->setObjectName("copyResetProfileLogin");
        labelForAccessibility(copyResetLogin, QString::fromUtf8("Копировать логин профиля"),
            QString::fromUtf8("Копирует логин профиля в буфер обмена."));
        loginLayout->addWidget(resetLogin, 1);
        loginLayout->addWidget(copyResetLogin);
        form->addRow(QString::fromUtf8("Логин"), loginRow);

        auto* passwordRow = new QWidget(&dialog);
        auto* passwordLayout = new QHBoxLayout(passwordRow);
        passwordLayout->setContentsMargins(0, 0, 0, 0);
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
        passwordLayout->addWidget(resetPassword, 1);
        passwordLayout->addWidget(revealResetPassword);
        passwordLayout->addWidget(copyResetPassword);
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
        form->addRow(resetConfirmed);
    } else {
        form->addRow(QString::fromUtf8("Текущий пароль"), current);
        form->addRow(QString::fromUtf8("Новый пароль"), next);
        form->addRow(QString::fromUtf8("Повторите пароль"), confirm);
    }
    auto* error = notice(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* saveButton = buttons->button(QDialogButtonBox::Save);
    saveButton->setText(adminReset ? QString::fromUtf8("Сбросить") : QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool resetSaved = false;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (resetSaved) { dialog.accept(); return; }
        if (!canWrite(workspace, error)) return;
        if (adminReset && !resetConfirmed->isChecked()) {
            error->setText(QString::fromUtf8("Подтвердите сброс пароля перед сохранением."));
            return;
        }
        if (!adminReset && (next->text().isEmpty() || next->text() != confirm->text())) {
            error->setText(QString::fromUtf8("Новый пароль пуст или подтверждение не совпадает."));
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
        if (!result.ok) { error->setText(q(result.errorMessage)); return; }
        if (!adminReset) { dialog.accept(); return; }
        resetSaved = true;
        error->setText(QString::fromUtf8("Пароль сброшен. Сохраните логин и новый пароль до закрытия окна."));
        saveButton->setText(QString::fromUtf8("Готово"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Закрыть"));
    });
    return dialog.exec() == QDialog::Accepted;
}

void ShowProfileManager(QWidget* parent, QtWorkspace& workspace, const QString& activeId) {
    QDialog dialog(parent);
    dialog.setObjectName("profileManager");
    dialog.setWindowTitle(QString::fromUtf8("Управление профилями"));
    dialog.resize(760, 520);
    dialog.setMinimumSize(640, 440);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    auto* toolbar = new QHBoxLayout;
    auto* search = new QLineEdit;
    search->setPlaceholderText(QString::fromUtf8("Поиск по имени или ID"));
    search->setClearButtonEnabled(true);
    search->setObjectName("profileSearch");
    search->setAccessibleName(QString::fromUtf8("Поиск профилей"));
    search->setAccessibleDescription(QString::fromUtf8("Фильтрует профили по имени или идентификатору."));
    auto* archived = new QCheckBox(QString::fromUtf8("Показать архив"));
    archived->setObjectName("showArchivedProfiles");
    auto* create = new QPushButton(QString::fromUtf8("Создать профиль"));
    create->setObjectName("createProfile");
    create->setProperty("primary", true);
    create->setFixedHeight(32);
    toolbar->addWidget(search, 1);
    toolbar->addWidget(archived);
    toolbar->addWidget(create);
    layout->addLayout(toolbar);
    auto* table = new QTableWidget;
    table->setObjectName("profileRecords");
    table->setAccessibleName(QString::fromUtf8("Список профилей"));
    table->setAccessibleDescription(QString::fromUtf8("Таблица профилей с идентификатором и состоянием; выберите строку для доступных действий."));
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QString::fromUtf8("Профиль"), "ID", QString::fromUtf8("Состояние")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(28);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table, 1);
    auto* actions = new QHBoxLayout;
    auto* edit = new QPushButton(QString::fromUtf8("Редактировать"));
    edit->setObjectName("editProfile");
    auto* archive = new QPushButton(QString::fromUtf8("В архив"));
    archive->setObjectName("archiveProfile");
    auto* password = new QPushButton(QString::fromUtf8("Сбросить пароль"));
    password->setObjectName("resetProfilePassword");
    auto* remove = new QPushButton(QString::fromUtf8("Удалить навсегда"));
    remove->setObjectName("deleteArchivedProfile");
    remove->setToolTip(QString::fromUtf8("Только пустой архивный профиль без задач и прогресса"));
    archive->setMinimumWidth(104);
    remove->setMinimumWidth(128);
    for (auto* button : {edit, archive, password, remove}) actions->addWidget(button);
    actions->addStretch();
    layout->addLayout(actions);
    auto* status = notice(layout);
    auto* credentials = new QLineEdit;
    credentials->setObjectName("createdProfileCredentials");
    credentials->setReadOnly(true);
    credentials->setEchoMode(QLineEdit::Password);
    credentials->hide();
    layout->addWidget(credentials);
    QString createdLoginValue;
    QString createdPasswordValue;
    auto* credentialActions = new QHBoxLayout;
    auto* copyCreatedLogin = new QPushButton(QString::fromUtf8("Копировать логин"));
    copyCreatedLogin->setObjectName("copyCreatedProfileLogin");
    labelForAccessibility(copyCreatedLogin, QString::fromUtf8("Копировать логин нового профиля"),
        QString::fromUtf8("Копирует логин созданного профиля в буфер обмена."));
    auto* copyCreatedPassword = new QPushButton(QString::fromUtf8("Копировать пароль"));
    copyCreatedPassword->setObjectName("copyCreatedProfilePassword");
    labelForAccessibility(copyCreatedPassword, QString::fromUtf8("Копировать пароль нового профиля"),
        QString::fromUtf8("Становится доступно после явного показа реквизитов."));
    copyCreatedPassword->setEnabled(false);
    for (auto* button : {copyCreatedLogin, copyCreatedPassword}) credentialActions->addWidget(button);
    credentialActions->addStretch();
    auto* credentialActionsWidget = new QWidget;
    credentialActionsWidget->setLayout(credentialActions);
    credentialActionsWidget->hide();
    layout->addWidget(credentialActionsWidget);
    auto* reveal = new QCheckBox(QString::fromUtf8("Показать реквизиты нового профиля"));
    reveal->setObjectName("showCreatedProfileCredentials");
    reveal->setAccessibleName(QString::fromUtf8("Показать реквизиты нового профиля"));
    reveal->setAccessibleDescription(QString::fromUtf8("Показывает сгенерированные логин и пароль профиля."));
    reveal->hide();
    layout->addWidget(reveal);
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
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    close->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto selected = [&]() -> std::optional<IJobStorage::ProfileInfo> {
        auto* item = table->item(table->currentRow(), 0);
        if (!item) return {};
        const auto id = u(item->data(Qt::UserRole).toString());
        for (const auto& p : workspace.profiles) if (p.id == id) return p;
        return {};
    };
    auto selection = [&] {
        const auto info = selected();
        edit->setEnabled(info && !info->archived);
        password->setEnabled(info && !info->archived);
        archive->setEnabled(bool(info));
        remove->setEnabled(info && info->archived);
        archive->setText(info && info->archived ? QString::fromUtf8("Восстановить") : QString::fromUtf8("В архив"));
    };
    auto refresh = [&] {
        const auto previous = selected();
        workspace.profiles = workspace.storage->list_profiles();
        std::sort(workspace.profiles.begin(), workspace.profiles.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        QSignalBlocker blocker(table);
        table->setRowCount(0);
        for (const auto& p : workspace.profiles) {
            if (p.archived && !archived->isChecked()) continue;
            if (!(q(p.name) + " " + q(p.id)).contains(search->text(), Qt::CaseInsensitive)) continue;
            int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(q(p.name)));
            table->item(row, 0)->setToolTip(q(p.name));
            table->item(row, 0)->setData(Qt::UserRole, q(p.id));
            table->setItem(row, 1, new QTableWidgetItem(q(p.id)));
            QString state = QString::fromUtf8("Архив");
            if (!p.archived) {
                std::optional<Profile> profile;
                if (workspace.storage->set_active_profile(p.id)) profile = workspace.storage->load_profile();
                state = !profile ? QString::fromUtf8("Ошибка чтения") :
                    (profile->is_blocked() ? QString::fromUtf8("Заблокирован") : QString::fromUtf8("Доступен"));
            }
            table->setItem(row, 2, new QTableWidgetItem(state));
            if (previous && previous->id == p.id) table->selectRow(row);
        }
        if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
        selection();
    };
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, selection);
    QObject::connect(search, &QLineEdit::textChanged, &dialog, refresh);
    QObject::connect(archived, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(create, &QPushButton::clicked, &dialog, [&] {
        if (!canWrite(workspace, status)) return;
        bool ok = false;
        const auto name = QInputDialog::getText(&dialog, QString::fromUtf8("Создание профиля"), QString::fromUtf8("Имя:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok) return;
        // Profile storage is INI; line breaks and control characters must not create new fields.
        if (name.isEmpty() || std::any_of(name.begin(), name.end(), [](QChar c) { return c.category() == QChar::Other_Control; })) {
            status->setText(QString::fromUtf8("Введите непустое имя без управляющих символов.")); return;
        }
        const auto result = AppCreateProfile(*workspace.storage, workspace.catalog, u(name),
            workspace.profileEventLogger);
        if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
        if (!result.ok) { status->setText(q(result.errorMessage)); return; }
        createdLoginValue = q(result.login);
        createdPasswordValue = q(result.password);
        reveal->setChecked(false);
        credentials->setText(QString::fromUtf8("Логин: %1   Пароль: %2").arg(createdLoginValue, createdPasswordValue));
        credentials->show();
        reveal->show();
        credentialActionsWidget->show();
        status->setText(QString::fromUtf8("Профиль создан. Сохраните реквизиты перед закрытием окна."));
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
        try {
            PrepareProfileDeletionRecovery(workspace.directory, info->id);
            prepared = true;
            const auto result = AppDeleteEmptyArchivedProfile(*workspace.storage, workspace.profiles, workspace.data.tasks, info->id);
            if (!result.ok) throw std::runtime_error(result.errorMessage);
            CommitQtRecoveryTransaction(workspace.directory);
            status->setText(QString::fromUtf8("Пустой архивный профиль удалён."));
        } catch (const std::exception& error) {
            std::string text = error.what();
            if (prepared) {
                try { text += RecoverTaskCompletionWithNotice(workspace.directory); }
                catch (const std::exception&) { text += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
            }
            status->setText(q(text));
        }
        refresh();
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (!info || !canWrite(workspace, status) || !workspace.storage->set_active_profile(info->id)) return;
        auto loaded = workspace.storage->load_profile();
        if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
        if (!loaded) { status->setText(QString::fromUtf8("Не удалось загрузить профиль.")); return; }
        QDialog editor(&dialog);
        editor.setObjectName("profileEditor");
        editor.setWindowTitle(QString::fromUtf8("Параметры профиля · ") + q(info->name));
        editor.setMinimumWidth(440);
        auto* form = new QFormLayout(&editor);
        auto* profileName = new QLineEdit(q(loaded->name()));
        profileName->setObjectName("profileName");
        auto* profession = new QComboBox;
        profession->setObjectName("profileProfession");
        profession->addItem(QString::fromUtf8("Без профессии"), "");
        for (const auto& p : workspace.data.professions) profession->addItem(q(p.name), q(p.id));
        if (profession->findData(q(loaded->profession_id())) < 0) profession->addItem(q(loaded->profession_id()), q(loaded->profession_id()));
        profession->setCurrentIndex(profession->findData(q(loaded->profession_id())));
        auto* spirit = new QComboBox;
        spirit->setObjectName("profileSpirit");
        for (auto s : {ProfileSpirit::None, ProfileSpirit::Good, ProfileSpirit::Evil}) spirit->addItem(q(ProfileSpiritLabel(s)), int(s));
        spirit->setCurrentIndex(spirit->findData(int(loaded->spirit())));
        spirit->setToolTip(QString::fromUtf8("Добрый дух: +1% XP; злой: −1% XP. Действует на общий XP и навыки."));
        auto* blocked = new QCheckBox(QString::fromUtf8("Заблокировать профиль"));
        blocked->setObjectName("profileBlocked");
        blocked->setChecked(loaded->is_blocked());
        form->addRow(QString::fromUtf8("Имя"), profileName);
        form->addRow(QString::fromUtf8("Профессия"), profession);
        form->addRow(QString::fromUtf8("Дух"), spirit);
        form->addRow(blocked);
        auto* error = notice(form);
        QObject::connect(profileName, &QLineEdit::textChanged, error, [=] { error->clear(); });
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
            if (!canWrite(workspace, error)) return;
            const auto candidateName = profileName->text().trimmed();
            if (candidateName.isEmpty() || std::any_of(candidateName.begin(), candidateName.end(), [](QChar c) { return c.category() == QChar::Other_Control; })) {
                error->setText(QString::fromUtf8("Введите непустое имя без управляющих символов.")); return;
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
    refresh();
    dialog.exec();
    if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
}
