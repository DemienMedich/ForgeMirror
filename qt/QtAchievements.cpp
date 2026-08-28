#include "QtAchievements.h"
#include <QtWidgets>
#include <QSaveFile>
#include <cmath>
#include <algorithm>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
QString date(std::int64_t value) { return value ? QDateTime::fromSecsSinceEpoch(value).toString("dd.MM.yyyy HH:mm") : QString::fromUtf8("Без срока"); }
}
QString GrantQtAchievement(QtWorkspace& workspace, const std::string& profileId,
    const QString& title, const std::string& skillId, double bonus, int days) {
    if (title.trimmed().isEmpty() || !workspace.catalog.contains_id(skillId) ||
        !std::isfinite(bonus) || bonus < 0 || bonus > 10000 || days < 0 || days > 36500)
        return QString::fromUtf8("Укажите название, существующий навык, бонус 0–10000% и срок 0–36500 дней.");
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
        if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !parsed.isArray())
            return QString::fromUtf8("Файл достижений повреждён. Выдача отменена; исходный файл сохранён.");
        records = parsed.array();
        for (const auto& record : records) if (!record.isObject()) return QString::fromUtf8("Неизвестный формат записи достижения.");
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    QJsonObject record;
    record["title"] = title.trimmed();
    record["skill"] = q(skillId);
    record["bonus"] = bonus;
    record["awarded"] = now;
    record["durationDays"] = days;
    record["expires"] = days ? now + qint64(days) * 86400 : 0;
    record["icon"] = "";
    records.append(record);
    const auto bytes = QByteArray("\xEF\xBB\xBF") + QJsonDocument(records).toJson();
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!QDir().mkpath(directory) || !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return QString::fromUtf8("Не удалось сохранить достижение. Исходные данные не изменены.");
    return {};
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
    table->verticalHeader()->hide();
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    layout->addWidget(table, 1);
    auto* notice = new QLabel;
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto refresh = [&] {
        table->setRowCount(0);
        if (!workspace.storage->set_active_profile(profileId)) return;
        const auto profile = workspace.storage->load_profile();
        if (!profile) { notice->setText(QString::fromUtf8("Профиль недоступен.")); return; }
        const auto now = QDateTime::currentSecsSinceEpoch();
        for (const auto& item : profile->achievements()) {
            const auto row = table->rowCount(); table->insertRow(row);
            const QStringList values = {q(item.title), q(workspace.catalog.display_name(item.skill)),
                QString::number(item.bonusPercent) + "%", date(item.expiresAt),
                QString::fromUtf8(item.is_active(now) ? "Активно" : "Истекло")};
            for (int col = 0; col < values.size(); ++col) {
                auto* cell = new QTableWidgetItem(values[col]); cell->setToolTip(values[col]); table->setItem(row, col, cell);
            }
        }
        table->resizeColumnsToContents();
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        notice->setText(QString::fromUtf8("Достижений: %1").arg(table->rowCount()));
    };
    QObject::connect(grant, &QPushButton::clicked, &dialog, [&] {
        if (!admin) return;
        QDialog editor(&dialog);
        editor.setObjectName("achievementGrant");
        editor.setWindowTitle(QString::fromUtf8("Выдать достижение"));
        editor.setMinimumWidth(440);
        auto* form = new QFormLayout(&editor);
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
            const auto result = GrantQtAchievement(workspace, profileId, title->text(), skill->currentData().toString().toUtf8().toStdString(), bonus->value(), days->value());
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
