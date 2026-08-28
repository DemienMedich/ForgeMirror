#include "QtAchievements.h"
#include <QtWidgets>
#include <QSaveFile>
#include <cmath>
#include <algorithm>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
QString date(std::int64_t value) { return value ? QDateTime::fromSecsSinceEpoch(value).toString("dd.MM.yyyy HH:mm") : QString::fromUtf8("Без срока"); }
QPixmap achievementIcon(const QtWorkspace& workspace, const QString& relative) {
    const QString prefix = "achievements/icons/";
    if (!relative.startsWith(prefix)) return {};
    const auto name = relative.mid(prefix.size());
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':') ||
        !name.endsWith(".png", Qt::CaseInsensitive)) return {};
    const auto root = q(workspace.directory.u8string());
    const QFileInfo file(root + "/" + relative);
    if (QFileInfo(root + "/achievements").isSymLink() ||
        QFileInfo(root + "/achievements/icons").isSymLink() || file.isSymLink() ||
        !file.isFile() || file.size() > 4 * 1024 * 1024) return {};
    QFile input(file.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly) || input.read(8) != QByteArray::fromHex("89504e470d0a1a0a")) return {};
    input.close();
    QImageReader reader(file.absoluteFilePath(), "png");
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 2048 || size.height() > 2048) return {};
    reader.setScaledSize(size.scaled(32, 32, Qt::KeepAspectRatio));
    return QPixmap::fromImage(reader.read());
}
QComboBox* iconSelector(const QtWorkspace& workspace, const QString& current = {}) {
    auto* combo = new QComboBox;
    combo->setObjectName("achievementIcon"); combo->setIconSize(QSize(24, 24));
    combo->setMaximumWidth(320);
    combo->addItem(QString::fromUtf8("Без иконки"), QString());
    const QDir directory(q(workspace.directory.u8string()) + "/achievements/icons");
    for (const auto& name : directory.entryList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        const auto relative = "achievements/icons/" + name;
        const auto pixmap = achievementIcon(workspace, relative);
        if (!pixmap.isNull()) combo->addItem(QIcon(pixmap), name, relative);
    }
    if (!current.isEmpty() && combo->findData(current) < 0)
        combo->addItem(QString::fromUtf8("Недоступна: ") + QFileInfo(current).fileName(), current);
    combo->setCurrentIndex(combo->findData(current));
    combo->setToolTip(QString::fromUtf8("PNG из achievements/icons. Недоступная старая ссылка сохраняется до явной замены."));
    return combo;
}
}
static QString MutateAchievement(QtWorkspace& workspace, const std::string& profileId,
    const QString& title, const std::string& skillId, double bonus, int days,
    int index = -1, const QByteArray* expectedFile = nullptr, bool changeDuration = true, bool revoke = false,
    std::optional<QString> icon = std::nullopt) {
    if (!revoke && (title.trimmed().isEmpty() || (!expectedFile && !workspace.catalog.contains_id(skillId)) ||
        !std::isfinite(bonus) || bonus < 0 || bonus > 10000 || days < 0 || days > 36500)
        ) return QString::fromUtf8("Укажите название, существующий навык, бонус 0–10000% и срок 0–36500 дней.");
    const auto id = q(profileId);
    if (id.isEmpty() || id == "." || id == ".." || id.contains('/') || id.contains('\\') || id.contains(':'))
        return QString::fromUtf8("Некорректный ID профиля.");
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return QString::fromUtf8("Сначала завершите восстановление данных.");
    const auto profiles = workspace.storage->list_profiles();
    if (std::none_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId && !p.archived; }) ||
        !workspace.storage->set_active_profile(profileId)) return QString::fromUtf8("Профиль недоступен.");
    const auto profile = workspace.storage->load_profile();
    if (!profile || profile->is_blocked()) return QString::fromUtf8("Профиль недоступен или заблокирован.");
    const auto directory = q((workspace.directory / "achievements").u8string());
    const auto path = directory + "/" + id + ".json";
    if (QFileInfo(directory).isSymLink() || QFileInfo(path).isSymLink()) return QString::fromUtf8("Ссылки в хранилище достижений не поддерживаются.");
    QJsonArray records;
    if (QFileInfo::exists(path)) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось прочитать достижения.");
        auto bytes = input.readAll();
        if (input.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения достижений.");
        if (expectedFile && bytes != *expectedFile) return QString::fromUtf8("Список достижений изменился. Закройте окно и откройте его снова.");
        if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !parsed.isArray())
            return QString::fromUtf8("Файл достижений повреждён. Выдача отменена; исходный файл сохранён.");
        records = parsed.array();
        for (const auto& record : records) if (!record.isObject()) return QString::fromUtf8("Неизвестный формат записи достижения.");
    }
    if (expectedFile && (index < 0 || index >= records.size() || records.size() != int(profile->achievements().size())))
        return QString::fromUtf8("Запись больше не существует или формат списка изменился.");
    const auto now = QDateTime::currentSecsSinceEpoch();
    QJsonObject record = expectedFile ? records[index].toObject() : QJsonObject{};
    if (!revoke && icon) {
        if (!icon->isEmpty() && *icon != record["icon"].toString() && achievementIcon(workspace, *icon).isNull())
            return QString::fromUtf8("Иконка недоступна. Выберите PNG из achievements/icons или вариант без иконки.");
        record["icon"] = *icon;
    }
    record["title"] = title.trimmed();
    record["bonus"] = bonus;
    if (record.contains("bonusPercent")) record["bonusPercent"] = bonus;
    if (!expectedFile) { record["skill"] = q(skillId); record["awarded"] = now; if (!icon) record["icon"] = ""; }
    if (changeDuration) {
        const auto awarded = expectedFile ? profile->achievements()[index].awardedAt : now;
        record["awarded"] = awarded;
        if (record.contains("awardedAt")) record["awardedAt"] = awarded;
        record["durationDays"] = days;
        record["expires"] = days ? awarded + qint64(days) * 86400 : 0;
        if (record.contains("expiresAt")) record["expiresAt"] = record["expires"];
    }
    if (!expectedFile) records.append(record);
    else if (revoke) records.removeAt(index);
    else records[index] = record;
    const auto bytes = QByteArray("\xEF\xBB\xBF") + QJsonDocument(records).toJson();
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!QDir().mkpath(directory) || !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return QString::fromUtf8("Не удалось сохранить достижение. Исходные данные не изменены.");
    return {};
}

QString GrantQtAchievement(QtWorkspace& workspace, const std::string& profileId,
    const QString& title, const std::string& skillId, double bonus, int days, const QString& icon) {
    return MutateAchievement(workspace, profileId, title, skillId, bonus, days, -1, nullptr, true, false, icon);
}
QString UpdateQtAchievement(QtWorkspace& workspace, const std::string& profileId,
    int index, const QByteArray& expectedFile, const QString& title, double bonus,
    std::optional<int> durationDays, bool revoke, std::optional<QString> icon) {
    return MutateAchievement(workspace, profileId, title, {}, bonus, durationDays.value_or(0),
        index, &expectedFile, durationDays.has_value(), revoke, icon);
}

void ShowAchievements(QWidget* parent, QtWorkspace& workspace, const std::string& profileId, bool admin) {
    if (profileId.empty()) return;
    QDialog dialog(parent);
    dialog.setObjectName("achievements");
    dialog.setWindowTitle(QString::fromUtf8("Достижения профиля"));
    dialog.resize(760, 480);
    dialog.setMinimumSize(600, 400);
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8("Бонус действует на будущий XP указанного навыка. Уже начисленный XP не пересчитывается."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* grant = new QPushButton(QString::fromUtf8("Выдать достижение"));
    grant->setObjectName("grantAchievement");
    grant->setProperty("primary", true);
    grant->setVisible(admin);
    layout->addWidget(grant, 0, Qt::AlignRight);
    auto* table = new QTableWidget(0, 5);
    table->setObjectName("achievementRecords");
    table->setHorizontalHeaderLabels({QString::fromUtf8("Название"), QString::fromUtf8("Навык"), QString::fromUtf8("Бонус XP"), QString::fromUtf8("Действует до"), QString::fromUtf8("Состояние")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    layout->addWidget(table, 1);
    auto* notice = new QLabel;
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto* actions = new QHBoxLayout;
    auto* edit = new QPushButton(QString::fromUtf8("Редактировать")); edit->setObjectName("editAchievement");
    auto* revoke = new QPushButton(QString::fromUtf8("Отозвать")); revoke->setObjectName("revokeAchievement");
    edit->setVisible(admin); revoke->setVisible(admin);
    actions->addWidget(edit); actions->addWidget(revoke); actions->addStretch(); layout->addLayout(actions);
    QByteArray snapshot;
    std::vector<Achievement> displayed;
    auto refresh = [&] {
        table->setRowCount(0);
        displayed.clear(); snapshot.clear();
        QFile source(q((workspace.directory / "achievements" / (profileId + ".json")).u8string()));
        if (source.open(QIODevice::ReadOnly)) snapshot = source.readAll();
        if (!workspace.storage->set_active_profile(profileId)) return;
        const auto profile = workspace.storage->load_profile();
        if (!profile) { notice->setText(QString::fromUtf8("Профиль недоступен.")); return; }
        const auto now = QDateTime::currentSecsSinceEpoch();
        displayed = profile->achievements();
        for (const auto& item : profile->achievements()) {
            const auto row = table->rowCount(); table->insertRow(row);
            const QStringList values = {q(item.title), q(workspace.catalog.display_name(item.skill)),
                QString::number(item.bonusPercent) + "%", date(item.expiresAt),
                QString::fromUtf8(item.is_active(now) ? "Активно" : "Истекло")};
            for (int col = 0; col < values.size(); ++col) {
                auto* cell = new QTableWidgetItem(values[col]); cell->setToolTip(values[col]); table->setItem(row, col, cell);
            }
            table->item(row, 0)->setIcon(QIcon(achievementIcon(workspace, q(item.icon))));
        }
        table->resizeColumnsToContents();
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        notice->setText(QString::fromUtf8("Достижений: %1").arg(table->rowCount()));
    };
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, [&] {
        edit->setEnabled(table->currentRow() >= 0); revoke->setEnabled(table->currentRow() >= 0);
    });
    edit->setEnabled(false); revoke->setEnabled(false);
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        const auto index = table->currentRow();
        if (!admin || index < 0 || index >= int(displayed.size())) return;
        const auto original = displayed[index];
        if (!std::isfinite(original.bonusPercent) || original.bonusPercent < 0 || original.bonusPercent > 10000) {
            notice->setText(QString::fromUtf8("Бонус старой записи вне диапазона редактора. Запись не изменена.")); return;
        }
        const auto expected = snapshot;
        QDialog editor(&dialog); editor.setObjectName("achievementEdit");
        editor.setWindowTitle(QString::fromUtf8("Редактирование достижения")); editor.setMinimumWidth(460);
        auto* form = new QFormLayout(&editor);
        auto* icon = iconSelector(workspace, q(original.icon));
        form->addRow(QString::fromUtf8("Иконка"), icon);
        auto* title = new QLineEdit(q(original.title)); title->setObjectName("achievementTitle");
        auto* bonus = new QDoubleSpinBox; bonus->setObjectName("achievementBonus"); bonus->setRange(0, 10000); bonus->setDecimals(6); bonus->setValue(original.bonusPercent);
        const auto displayedBonus = bonus->value();
        auto* change = new QCheckBox(QString::fromUtf8("Изменить срок от даты выдачи")); change->setObjectName("changeAchievementDuration");
        auto* days = new QSpinBox; days->setObjectName("achievementDays"); days->setRange(0, 36500); days->setSpecialValueText(QString::fromUtf8("Без срока")); days->setEnabled(false);
        days->setValue(original.expiresAt ? int(std::clamp<std::int64_t>((original.expiresAt - original.awardedAt) / 86400, 0, 36500)) : 0);
        form->addRow(QString::fromUtf8("Название"), title); form->addRow(QString::fromUtf8("Бонус XP, %"), bonus);
        auto* hint = new QLabel(QString::fromUtf8("Выдано: ") + date(original.awardedAt) + QString::fromUtf8("\nТекущий срок: ") + date(original.expiresAt)); hint->setWordWrap(true); form->addRow(hint);
        form->addRow(change, days);
        QObject::connect(change, &QCheckBox::toggled, days, &QWidget::setEnabled);
        auto* error = new QLabel; error->setObjectName("achievementError"); error->setWordWrap(true); form->addRow(error);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
            const auto result = UpdateQtAchievement(workspace, profileId, index, expected, title->text(),
                bonus->value() == displayedBonus ? original.bonusPercent : bonus->value(),
                change->isChecked() ? std::optional<int>(days->value()) : std::nullopt, false, icon->currentData().toString());
            if (!result.isEmpty()) { error->setText(result); return; }
            editor.accept();
        });
        if (editor.exec() == QDialog::Accepted) refresh();
    });
    QObject::connect(revoke, &QPushButton::clicked, &dialog, [&] {
        const auto index = table->currentRow();
        if (!admin || index < 0 || index >= int(displayed.size())) return;
        const auto expected = snapshot;
        QMessageBox confirm(QMessageBox::Question, QString::fromUtf8("Отозвать достижение"),
            QString::fromUtf8("Отозвать «%1»? Бонус перестанет действовать. Уже начисленный XP не изменится.").arg(q(displayed[index].title)),
            QMessageBox::Yes | QMessageBox::No, &dialog);
        confirm.setTextFormat(Qt::PlainText); confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        const auto result = UpdateQtAchievement(workspace, profileId, index, expected, {}, 0, std::nullopt, true);
        if (!result.isEmpty()) { notice->setText(result); return; }
        refresh();
    });
    QObject::connect(grant, &QPushButton::clicked, &dialog, [&] {
        if (!admin) return;
        QDialog editor(&dialog);
        editor.setObjectName("achievementGrant");
        editor.setWindowTitle(QString::fromUtf8("Выдать достижение"));
        editor.setMinimumWidth(440);
        auto* form = new QFormLayout(&editor);
        auto* icon = iconSelector(workspace);
        form->addRow(QString::fromUtf8("Иконка"), icon);
        auto* title = new QLineEdit; title->setObjectName("achievementTitle");
        auto* skill = new QComboBox; skill->setObjectName("achievementSkill");
        for (const auto& id : workspace.catalog.skills()) skill->addItem(q(workspace.catalog.display_name(id)), q(id));
        auto* bonus = new QDoubleSpinBox; bonus->setObjectName("achievementBonus"); bonus->setRange(0, 10000); bonus->setSuffix(" %");
        auto* days = new QSpinBox; days->setObjectName("achievementDays"); days->setRange(0, 36500); days->setSpecialValueText(QString::fromUtf8("Без срока"));
        form->addRow(QString::fromUtf8("Название"), title);
        form->addRow(QString::fromUtf8("Навык"), skill);
        form->addRow(QString::fromUtf8("Бонус XP"), bonus);
        form->addRow(QString::fromUtf8("Срок, дней"), days);
        auto* error = new QLabel; error->setObjectName("achievementError"); error->setWordWrap(true); form->addRow(error);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Выдать"));
        buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
            const auto result = GrantQtAchievement(workspace, profileId, title->text(), skill->currentData().toString().toUtf8().toStdString(), bonus->value(), days->value(), icon->currentData().toString());
            if (!result.isEmpty()) { error->setText(result); return; }
            editor.accept();
        });
        if (editor.exec() == QDialog::Accepted) refresh();
    });
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    close->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(close);
    refresh();
    dialog.exec();
}
