#include "QtVaultEditor.h"
#include "AppMetaService.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>

namespace {
bool sameVaultSettings(const StorageVaultData& a, const StorageVaultData& b) {
    return a.currencyName == b.currencyName && a.currencyCode == b.currencyCode &&
        a.logLimit == b.logLimit && a.pomodoroStartMinutes == b.pomodoroStartMinutes &&
        a.pomodoroEndMinutes == b.pomodoroEndMinutes && a.pomodoroMinMinutes == b.pomodoroMinMinutes &&
        a.pomodoroCoinsPerCycle == b.pomodoroCoinsPerCycle && a.pomodoroDaysMask == b.pomodoroDaysMask &&
        std::abs(a.balance - b.balance) < 0.000001;
}
}

bool ShowVaultEditor(QWidget* parent, QtWorkspace& workspace) {
    QDialog dialog(parent); dialog.setObjectName("vaultEditor"); dialog.setWindowTitle(QString::fromUtf8("Настройки хранилища"));
    dialog.setMinimumWidth(520);
    auto* form = new QFormLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8("Баланс и журнал не редактируются. Здесь меняются только название валюты и правила наград Pomodoro."));
    hint->setWordWrap(true); form->addRow(hint);
    const auto current = workspace.data.vault;
    auto* name = new QLineEdit(QString::fromUtf8(current.currencyName)); name->setObjectName("vaultCurrencyName"); name->setMaxLength(48);
    auto* code = new QLineEdit(QString::fromUtf8(current.currencyCode)); code->setObjectName("vaultCurrencyCode"); code->setMaxLength(12);
    auto* limit = new QSpinBox; limit->setObjectName("vaultLogLimit"); limit->setRange(10, 50); limit->setValue(current.logLimit);
    auto* start = new QTimeEdit(QTime(current.pomodoroStartMinutes / 60, current.pomodoroStartMinutes % 60)); start->setObjectName("vaultPomodoroStart"); start->setDisplayFormat("HH:mm");
    auto* end = new QTimeEdit(QTime(current.pomodoroEndMinutes / 60, current.pomodoroEndMinutes % 60)); end->setObjectName("vaultPomodoroEnd"); end->setDisplayFormat("HH:mm");
    auto* minimum = new QSpinBox; minimum->setObjectName("vaultPomodoroMinimum"); minimum->setRange(1, 90); minimum->setSuffix(QString::fromUtf8(" мин")); minimum->setValue(current.pomodoroMinMinutes);
    auto* coins = new QSpinBox; coins->setObjectName("vaultPomodoroCoins"); coins->setRange(0, 5); coins->setValue(current.pomodoroCoinsPerCycle);
    form->addRow(QString::fromUtf8("Название валюты"), name); form->addRow(QString::fromUtf8("Код"), code);
    form->addRow(QString::fromUtf8("Записей в журнале"), limit); form->addRow(QString::fromUtf8("Начало наград"), start);
    form->addRow(QString::fromUtf8("Конец наград"), end); form->addRow(QString::fromUtf8("Минимальный фокус"), minimum);
    form->addRow(QString::fromUtf8("Монет за цикл"), coins);
    auto* days = new QWidget; auto* daysLayout = new QGridLayout(days); daysLayout->setContentsMargins(0, 0, 0, 0);
    daysLayout->setHorizontalSpacing(12); daysLayout->setVerticalSpacing(4);
    const QStringList labels = {QString::fromUtf8("Вс"), QString::fromUtf8("Пн"), QString::fromUtf8("Вт"), QString::fromUtf8("Ср"), QString::fromUtf8("Чт"), QString::fromUtf8("Пт"), QString::fromUtf8("Сб")};
    QVector<QCheckBox*> checks;
    for (int index = 0; index < 7; ++index) { auto* check = new QCheckBox(labels[index]); check->setObjectName(QString("vaultDay%1").arg(index)); check->setChecked(current.pomodoroDaysMask & (1 << index)); checks.push_back(check); daysLayout->addWidget(check, index / 4, index % 4); }
    daysLayout->setColumnStretch(4, 1); form->addRow(QString::fromUtf8("Дни недели"), days);
    auto* notice = new QLabel; notice->setObjectName("vaultNotice"); notice->setWordWrap(true); form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) { notice->setText(QString::fromUtf8("Сначала завершите восстановление данных.")); return; }
        const auto currencyName = name->text().trimmed(); const auto currencyCode = code->text().trimmed();
        if (currencyName.isEmpty() || currencyCode.isEmpty()) { notice->setText(QString::fromUtf8("Название и код валюты не должны быть пустыми.")); return; }
        int mask = 0; for (int index = 0; index < checks.size(); ++index) if (checks[index]->isChecked()) mask |= 1 << index;
        if (!mask) { notice->setText(QString::fromUtf8("Выберите хотя бы один день наград.")); return; }
        StorageVaultData draft = current;
        draft.currencyName = currencyName.toUtf8().toStdString(); draft.currencyCode = currencyCode.toUtf8().toStdString();
        draft.logLimit = limit->value(); draft.pomodoroStartMinutes = start->time().hour() * 60 + start->time().minute();
        draft.pomodoroEndMinutes = end->time().hour() * 60 + end->time().minute(); draft.pomodoroMinMinutes = minimum->value();
        draft.pomodoroCoinsPerCycle = coins->value(); draft.pomodoroDaysMask = mask;
        QTemporaryDir staging; if (!staging.isValid()) { notice->setText(QString::fromUtf8("Не удалось создать временный каталог.")); return; }
        const auto stagingDir = std::filesystem::u8path(staging.path().toUtf8().toStdString());
        if (!SaveStorageVault(stagingDir, draft)) { notice->setText(QString::fromUtf8("Не удалось сериализовать настройки.")); return; }
        const auto checked = LoadStorageVault(stagingDir);
        QFile source(staging.path() + "/meta/storage.json");
        if (!sameVaultSettings(checked, draft) || !source.open(QIODevice::ReadOnly)) { notice->setText(QString::fromUtf8("Проверка настроек не пройдена.")); return; }
        const auto bytes = source.readAll(); const auto target = QString::fromUtf8((workspace.directory / "meta/storage.json").u8string());
        if (QFileInfo(QFileInfo(target).absolutePath()).isSymLink() || QFileInfo(target).isSymLink()) { notice->setText(QString::fromUtf8("Символьная ссылка storage.json не поддерживается.")); return; }
        QDir().mkpath(QFileInfo(target).absolutePath()); QSaveFile output(target); output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) { notice->setText(QString::fromUtf8("Не удалось атомарно сохранить настройки.")); return; }
        const auto persisted = LoadStorageVault(workspace.directory);
        if (!sameVaultSettings(persisted, draft)) { notice->setText(QString::fromUtf8("Сохранённые настройки не прошли проверку.")); return; }
        workspace.data.vault = persisted; dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
