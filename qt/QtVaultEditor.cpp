#include "QtVaultEditor.h"
#include "AppMetaService.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>

bool ShowVaultEditor(QWidget* parent, QtWorkspace& workspace) {
    QtScrollableDialog dialog(parent, QSize(640, 520));
    dialog.setObjectName("vaultEditor"); dialog.setWindowTitle(QString::fromUtf8("Настройки хранилища"));
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Поля настроек хранилища и наград Pomodoro"));
    auto* form = dialog.formLayout();
    form->setHorizontalSpacing(dialog.scaledMetric(8));
    form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFormAlignment(Qt::AlignTop);
    const auto outerMargins = dialog.layout()->contentsMargins();
    const int preferredFormWidth = dialog.width() - outerMargins.left() - outerMargins.right()
        - dialog.style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const auto addField = [form, preferredFormWidth](const QString& caption, const QString& details, QWidget* input) {
        auto* label = new QLabel(caption);
        label->setObjectName(input->objectName() + QStringLiteral("Label"));
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setBuddy(input);
        label->setToolTip(details);
        input->setAccessibleName(caption);
        input->setAccessibleDescription(details);
        input->setToolTip(details);
        form->addRow(label, input);
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(input)) {
            spin->ensurePolished();
            spin->setMinimumWidth(spin->sizeHint().width());
            if (label->sizeHint().width() + spin->minimumWidth() + form->horizontalSpacing() > preferredFormWidth)
                form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        }
    };
    const auto addHeading = [form](const QString& caption) {
        auto* heading = new QLabel(caption);
        heading->setTextFormat(Qt::PlainText);
        heading->setWordWrap(true);
        auto font = heading->font(); font.setWeight(QFont::DemiBold); heading->setFont(font);
        form->addRow(heading);
    };
    auto* hint = new QLabel(QString::fromUtf8("Баланс и журнал не редактируются. Здесь меняются только название валюты и правила наград Pomodoro."));
    hint->setObjectName("vaultEditorHint"); hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true); form->addRow(hint);
    const auto current = workspace.data.vault;
    auto* name = new QLineEdit(QString::fromUtf8(current.currencyName)); name->setObjectName("vaultCurrencyName"); name->setMaxLength(48);
    auto* code = new QLineEdit(QString::fromUtf8(current.currencyCode)); code->setObjectName("vaultCurrencyCode"); code->setMaxLength(12);
    auto* limit = new QSpinBox; limit->setObjectName("vaultLogLimit"); limit->setRange(10, 50); limit->setValue(current.logLimit);
    auto* start = new QTimeEdit(QTime(current.pomodoroStartMinutes / 60, current.pomodoroStartMinutes % 60)); start->setObjectName("vaultPomodoroStart"); start->setDisplayFormat("HH:mm");
    auto* end = new QTimeEdit(QTime(current.pomodoroEndMinutes / 60, current.pomodoroEndMinutes % 60)); end->setObjectName("vaultPomodoroEnd"); end->setDisplayFormat("HH:mm");
    auto* minimum = new QSpinBox; minimum->setObjectName("vaultPomodoroMinimum"); minimum->setRange(1, 90); minimum->setSuffix(QString::fromUtf8(" мин")); minimum->setValue(current.pomodoroMinMinutes);
    auto* coins = new QSpinBox; coins->setObjectName("vaultPomodoroCoins"); coins->setRange(0, 5); coins->setValue(current.pomodoroCoinsPerCycle);
    addHeading(QString::fromUtf8("Валюта и журнал"));
    addField(QString::fromUtf8("Название валюты"), QString::fromUtf8("Обязательное название валюты, до 48 символов."), name);
    addField(QString::fromUtf8("Код"), QString::fromUtf8("Обязательный код валюты, до 12 символов."), code);
    addField(QString::fromUtf8("Записей в журнале"), QString::fromUtf8("Число сохраняемых записей журнала: от 10 до 50."), limit);
    addHeading(QString::fromUtf8("Награды Pomodoro"));
    addField(QString::fromUtf8("Начало наград"), QString::fromUtf8("Начало времени выдачи наград Pomodoro."), start);
    addField(QString::fromUtf8("Конец наград"), QString::fromUtf8("Конец времени выдачи наград Pomodoro."), end);
    addField(QString::fromUtf8("Минимальный фокус"), QString::fromUtf8("Минимальная длительность фокуса для награды, от 1 до 90 минут."), minimum);
    addField(QString::fromUtf8("Монет за цикл"), QString::fromUtf8("Награда за подходящий цикл Pomodoro, от 0 до 5 монет."), coins);
    auto* days = new QtDialogFlowRow(dialog.bodyWidget(), dialog.scaledMetric(12), dialog.scaledMetric(4));
    days->setObjectName("vaultPomodoroDays");
    const QStringList labels = {QString::fromUtf8("Вс"), QString::fromUtf8("Пн"), QString::fromUtf8("Вт"), QString::fromUtf8("Ср"), QString::fromUtf8("Чт"), QString::fromUtf8("Пт"), QString::fromUtf8("Сб")};
    const QStringList dayNames = {QString::fromUtf8("Воскресенье"), QString::fromUtf8("Понедельник"), QString::fromUtf8("Вторник"),
        QString::fromUtf8("Среда"), QString::fromUtf8("Четверг"), QString::fromUtf8("Пятница"), QString::fromUtf8("Суббота")};
    QVector<QCheckBox*> checks;
    for (int index = 0; index < 7; ++index) {
        auto* check = new QCheckBox(labels[index]); check->setObjectName(QString("vaultDay%1").arg(index));
        check->setAccessibleName(dayNames[index]);
        check->setAccessibleDescription(QString::fromUtf8("Выдавать награды Pomodoro в этот день недели."));
        check->setToolTip(dayNames[index]);
        check->setChecked(current.pomodoroDaysMask & (1 << index)); checks.push_back(check); days->addWidget(check);
    }
    addField(QString::fromUtf8("Дни недели"), QString::fromUtf8("Нужно выбрать хотя бы один день наград Pomodoro."), days);
    auto* notice = new QLabel; notice->setObjectName("vaultNotice"); notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText); notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notice->setAccessibleName(QString::fromUtf8("Результат сохранения настроек хранилища"));
    dialog.footerLayout()->addWidget(notice); notice->hide();
    const auto showNotice = [notice](const QString& message) {
        notice->setText(message); notice->setAccessibleDescription(message); notice->show();
    };
    const auto focusRequired = [&](QWidget* field, const QString& message) {
        field->setAccessibleDescription(message);
        field->setFocus(Qt::OtherFocusReason);
        dialog.scrollArea()->ensureWidgetVisible(field, 0, dialog.scaledMetric(8));
    };
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("vaultEditorButtons");
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setObjectName("vaultSave"); cancel->setObjectName("vaultCancel");
    save->setText(QString::fromUtf8("Сохранить")); save->setProperty("primary", true);
    save->setAccessibleName(QString::fromUtf8("Сохранить настройки хранилища"));
    save->setMinimumHeight(dialog.scaledMetric(32)); save->setAutoDefault(true); save->setDefault(true);
    cancel->setText(QString::fromUtf8("Отмена")); cancel->setAccessibleName(QString::fromUtf8("Отменить настройки хранилища"));
    cancel->setAutoDefault(false);
    dialog.footerLayout()->addWidget(buttons);
    const auto clearNotice = [notice, name, code] {
        notice->clear(); notice->hide(); notice->setAccessibleDescription({});
        if (!name->text().trimmed().isEmpty()) name->setAccessibleDescription(name->toolTip());
        if (!code->text().trimmed().isEmpty()) code->setAccessibleDescription(code->toolTip());
    };
    QObject::connect(name, &QLineEdit::textChanged, &dialog, clearNotice);
    QObject::connect(code, &QLineEdit::textChanged, &dialog, clearNotice);
    for (auto* field : {limit, minimum, coins}) QObject::connect(field, &QSpinBox::valueChanged, &dialog, clearNotice);
    for (auto* field : {start, end}) QObject::connect(field, &QTimeEdit::timeChanged, &dialog, clearNotice);
    for (auto* check : checks) QObject::connect(check, &QCheckBox::toggled, &dialog, clearNotice);
    QList<QWidget*> tabFields{name, code, limit, start, end, minimum, coins};
    for (auto* check : checks) tabFields.append(check);
    tabFields << save << cancel;
    for (int index = 1; index < tabFields.size(); ++index) QWidget::setTabOrder(tabFields[index - 1], tabFields[index]);
    name->setFocus();
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) { showNotice(QString::fromUtf8("Сначала завершите восстановление данных.")); return; }
        const auto currencyName = name->text().trimmed(); const auto currencyCode = code->text().trimmed();
        if (currencyName.isEmpty() || currencyCode.isEmpty()) {
            showNotice(QString::fromUtf8("Название и код валюты не должны быть пустыми."));
            focusRequired(currencyName.isEmpty() ? name : code, notice->text()); return;
        }
        int mask = 0; for (int index = 0; index < checks.size(); ++index) if (checks[index]->isChecked()) mask |= 1 << index;
        if (!mask) {
            showNotice(QString::fromUtf8("Выберите хотя бы один день наград."));
            focusRequired(checks.front(), notice->text()); return;
        }
        const auto result = AppApplyVaultDraft(workspace.directory, workspace.data.vault,
            currencyName.toUtf8().toStdString(), currencyCode.toUtf8().toStdString(), limit->value(),
            start->time().hour() * 60 + start->time().minute(),
            end->time().hour() * 60 + end->time().minute(), minimum->value(), coins->value(), mask,
            workspace.metaEventLogger);
        if (!result.ok) {
            showNotice(QString::fromUtf8(result.errorMessage.c_str()));
            return;
        }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
