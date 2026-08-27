#include "QtProfileDialogs.h"
#include "QtWorkspace.h"
#include "AppProfileMutationService.h"
#include <QtWidgets>
#include <algorithm>

namespace {
QString q(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string u(const QString& s) { return s.toUtf8().toStdString(); }
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
    for (auto* edit : {current, next, confirm}) edit->setEchoMode(QLineEdit::Password);
    current->setParent(&dialog);
    if (!adminReset) form->addRow(QString::fromUtf8("Текущий пароль"), current);
    else current->hide();
    form->addRow(QString::fromUtf8("Новый пароль"), next);
    form->addRow(QString::fromUtf8("Повторите пароль"), confirm);
    auto* error = notice(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!canWrite(workspace, error)) return;
        if (next->text().isEmpty() || next->text() != confirm->text()) {
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
        auto result = AppChangeProfilePassword(storage, u(activeId), u(profileId), u(current->text()), u(next->text()), !adminReset);
        if (!result.ok) { error->setText(q(result.errorMessage)); return; }
        dialog.accept();
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
    for (auto* button : {edit, archive, password}) actions->addWidget(button);
    actions->addStretch();
    layout->addLayout(actions);
    auto* status = notice(layout);
    auto* credentials = new QLineEdit;
    credentials->setObjectName("createdProfileCredentials");
    credentials->setReadOnly(true);
    credentials->setEchoMode(QLineEdit::Password);
    credentials->hide();
    layout->addWidget(credentials);
    auto* reveal = new QCheckBox(QString::fromUtf8("Показать реквизиты нового профиля"));
    reveal->hide();
    layout->addWidget(reveal);
    QObject::connect(reveal, &QCheckBox::toggled, &dialog, [=](bool show) { credentials->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password); });
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
        const auto result = AppCreateProfile(*workspace.storage, workspace.catalog, u(name));
        if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
        if (!result.ok) { status->setText(q(result.errorMessage)); return; }
        reveal->setChecked(false);
        credentials->setText(QString::fromUtf8("Логин: %1   Пароль: %2").arg(q(result.login), q(result.password)));
        credentials->show();
        reveal->show();
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
        const auto result = AppSetProfileArchived(*workspace.storage, info->id, !info->archived);
        status->setText(result.ok ? QString::fromUtf8("Состояние архива сохранено.") : q(result.errorMessage));
        refresh();
    });
    QObject::connect(password, &QPushButton::clicked, &dialog, [&] {
        const auto info = selected();
        if (info && canWrite(workspace, status)) ShowProfilePasswordDialog(&dialog, workspace, q(info->id), activeId, true);
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
        form->addRow(QString::fromUtf8("Профессия"), profession);
        form->addRow(QString::fromUtf8("Дух"), spirit);
        form->addRow(blocked);
        auto* error = notice(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
            if (!canWrite(workspace, error)) return;
            Profile draft = *loaded;
            draft.set_profession_id(u(profession->currentData().toString()));
            draft.set_spirit(ProfileSpirit(spirit->currentData().toInt()));
            draft.set_blocked(blocked->isChecked());
            const auto result = AppSaveProfileSnapshot(*workspace.storage, u(activeId), info->id, draft);
            if (!result.ok) { error->setText(q(result.errorMessage)); return; }
            editor.accept();
        });
        if (editor.exec() == QDialog::Accepted) {
            status->setText(QString::fromUtf8("Параметры сохранены; XP и история не изменены."));
            refresh();
        }
    });
    refresh();
    dialog.exec();
    if (!activeId.isEmpty()) workspace.storage->set_active_profile(u(activeId));
}
