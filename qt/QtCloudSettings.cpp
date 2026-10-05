#include "QtCloudSettings.h"
#include "CloudSync.h"
#include "QtScrollableDialog.h"
#include "QtExportPicker.h"
#include <QtWidgets>
#include <algorithm>
#include <cctype>

namespace {
bool overlaps(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    const auto ca = std::filesystem::weakly_canonical(a, ec); if (ec) return true;
    const auto cb = std::filesystem::weakly_canonical(b, ec); if (ec) return true;
    auto sa = ca.generic_u8string(), sb = cb.generic_u8string();
#ifdef _WIN32
    std::transform(sa.begin(), sa.end(), sa.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(sb.begin(), sb.end(), sb.begin(), [](unsigned char c) { return char(std::tolower(c)); });
#endif
    if (!sa.empty() && sa.back() != '/') sa.push_back('/'); if (!sb.empty() && sb.back() != '/') sb.push_back('/');
    return sa.rfind(sb, 0) == 0 || sb.rfind(sa, 0) == 0;
}
}

bool ShowCloudSettings(QWidget* parent, const std::filesystem::path& workspaceDirectory) {
    const auto current = LoadCloudSyncConfig(workspaceDirectory);
    QtScrollableDialog dialog(parent, QSize(640, 520), QSize(420, 300));
    dialog.setObjectName("cloudSettings");
    dialog.setWindowTitle(QString::fromUtf8("Настройки облака"));
    auto* form = dialog.formLayout();
    form->setHorizontalSpacing(dialog.scaledMetric(8));
    form->setVerticalSpacing(dialog.scaledMetric(8));
    dialog.footerLayout()->setSpacing(dialog.scaledMetric(8));
    auto* enabled = new QCheckBox(QString::fromUtf8("Использовать облако"));
    enabled->setObjectName("cloudEnabled");
    enabled->setChecked(current.enabled);
    enabled->setAccessibleName(QString::fromUtf8("Включить конфигурацию облака"));
    enabled->setToolTip(enabled->accessibleName());
    auto* root = new QLineEdit(QString::fromUtf8(current.root.u8string()));
    root->setObjectName("cloudRoot");
    root->setMinimumWidth(0);
    root->setMinimumHeight(dialog.scaledMetric(32));
    root->setAccessibleName(QString::fromUtf8("Корневая папка синхронизации"));
    const auto rootDetails = QString::fromUtf8("Внешняя папка облака. Не должна совпадать с рабочей папкой или пересекаться с ней.");
    root->setAccessibleDescription(rootDetails);
    auto* browse = new QPushButton(QString::fromUtf8("Выбрать папку…"));
    browse->setObjectName("cloudBrowse");
    browse->setAccessibleName(QString::fromUtf8("Выбрать папку синхронизации"));
    browse->setToolTip(browse->accessibleName());
    browse->setMinimumHeight(dialog.scaledMetric(32));
    browse->setAutoDefault(false);
    auto* pathRow = new QtDialogAdaptiveRow(dialog.bodyWidget(), dialog.scaledMetric(8));
    pathRow->setObjectName("cloudRootRow");
    // QLineEdit already expands horizontally. An explicit stretch would turn
    // into vertical expansion when this adaptive row stacks its controls.
    pathRow->addWidget(root);
    pathRow->addWidget(browse);
    auto* rootLabel = new QLabel(QString::fromUtf8("Корневая папка"));
    rootLabel->setObjectName("cloudRootLabel");
    rootLabel->setWordWrap(true);
    rootLabel->setBuddy(root);
    form->addRow(enabled);
    form->addRow(rootLabel, pathRow);

    auto* warning = new QLabel(QString::fromUtf8("Автосинхронизация выполняет выбранные pull/push действия через заданный интервал без отдельного подтверждения каждого запуска. Pull может заменить локальные sync-файлы, push может удалить облачные файлы, которых нет локально. Перед применением создаётся полная резервная копия с восстановлением при ошибке. Push доступен только администратору. Ручной pull всегда запрашивает подтверждение."));
    warning->setObjectName("cloudSyncWarning");
    warning->setTextFormat(Qt::PlainText);
    warning->setWordWrap(true);
    warning->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    warning->setProperty("warning", true);
    warning->setAccessibleName(QString::fromUtf8("Последствия автоматической синхронизации"));
    warning->setAccessibleDescription(warning->text());
    warning->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(warning);
    auto* automationHeading = new QLabel(QString::fromUtf8("Автоматические действия"));
    automationHeading->setObjectName("cloudAutomationHeading");
    automationHeading->setWordWrap(true);
    auto headingFont = automationHeading->font();
    headingFont.setWeight(QFont::DemiBold);
    automationHeading->setFont(headingFont);
    form->addRow(automationHeading);
    auto* autoPull = new QCheckBox(QString::fromUtf8("Получать из облака")); autoPull->setObjectName("cloudAutoPull"); autoPull->setChecked(current.autoPull);
    autoPull->setAccessibleName(QString::fromUtf8("Автоматически получать данные из облака"));
    autoPull->setToolTip(autoPull->accessibleName());
    auto* autoPush = new QCheckBox(QString::fromUtf8("Выгружать в облако")); autoPush->setObjectName("cloudAutoPush"); autoPush->setChecked(current.autoPush);
    autoPush->setAccessibleName(QString::fromUtf8("Разрешить автоматическую выгрузку администраторам"));
    autoPush->setToolTip(autoPush->accessibleName());
    auto* includeAdmin = new QCheckBox(QString::fromUtf8("Администраторы")); includeAdmin->setObjectName("cloudIncludeAdmin"); includeAdmin->setChecked(current.includeAdminProfiles);
    includeAdmin->setAccessibleName(QString::fromUtf8("Включать профили администраторов в синхронизацию"));
    includeAdmin->setToolTip(includeAdmin->accessibleName());
    auto* adminHint = new QLabel(QString::fromUtf8("При включении профили администраторов тоже участвуют в синхронизации."));
    adminHint->setObjectName("cloudAdminHint");
    adminHint->setTextFormat(Qt::PlainText);
    adminHint->setWordWrap(true);
    adminHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* autoSync = new QCheckBox(QString::fromUtf8("По интервалу")); autoSync->setObjectName("cloudAutoSync"); autoSync->setChecked(current.autoSyncEnabled);
    autoSync->setAccessibleName(QString::fromUtf8("Включить автосинхронизацию по интервалу ниже"));
    autoSync->setToolTip(autoSync->accessibleName());
    auto* intervalHint = new QLabel(QString::fromUtf8("Повторять выбранные действия автоматически через заданный интервал."));
    intervalHint->setObjectName("cloudIntervalHint");
    intervalHint->setTextFormat(Qt::PlainText);
    intervalHint->setWordWrap(true);
    intervalHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* minutes = new QSpinBox; minutes->setObjectName("cloudMinutes"); minutes->setRange(1, 120); minutes->setValue(std::clamp(current.autoSyncMinutes, 1, 120)); minutes->setSuffix(QString::fromUtf8(" мин"));
    minutes->setAccessibleName(QString::fromUtf8("Интервал автосинхронизации в минутах"));
    minutes->setToolTip(QString::fromUtf8("От 1 до 120 минут. Используется при включённой автосинхронизации."));
    form->addRow(autoPull); form->addRow(autoPush); form->addRow(includeAdmin); form->addRow(adminHint);
    form->addRow(autoSync); form->addRow(intervalHint);
    auto* intervalLabel = new QLabel(QString::fromUtf8("Интервал"));
    intervalLabel->setWordWrap(true);
    intervalLabel->setBuddy(minutes);
    form->addRow(intervalLabel, minutes);
    auto* notice = new QLabel;
    notice->setObjectName("cloudNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notice->setAccessibleName(QString::fromUtf8("Результат сохранения облачных настроек"));
    dialog.footerLayout()->addWidget(notice);
    notice->hide();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("cloudButtons");
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setObjectName("cloudSave");
    cancel->setObjectName("cloudCancel");
    save->setText(QString::fromUtf8("Сохранить"));
    cancel->setText(QString::fromUtf8("Отмена"));
    save->setAccessibleName(QString::fromUtf8("Сохранить настройки облака"));
    cancel->setAccessibleName(QString::fromUtf8("Отменить изменения облачных настроек"));
    save->setProperty("primary", true);
    save->setMinimumHeight(dialog.scaledMetric(32));
    save->setAutoDefault(true);
    save->setDefault(true);
    cancel->setAutoDefault(false);
    dialog.footerLayout()->addWidget(buttons);
    const auto updateRootContext = [root, rootDetails] {
        root->setToolTip(Qt::convertFromPlainText(root->text()));
        root->setAccessibleDescription(rootDetails + QString::fromUtf8(" Путь: ") + root->text());
    };
    updateRootContext();
    const auto clearNotice = [notice, updateRootContext] {
        notice->clear(); notice->hide();
        notice->setAccessibleName(QString::fromUtf8("Результат сохранения облачных настроек"));
        notice->setAccessibleDescription({});
        updateRootContext();
    };
    QObject::connect(root, &QLineEdit::textChanged, &dialog, clearNotice);
    for (auto* check : {enabled, autoPull, autoPush, includeAdmin, autoSync})
        QObject::connect(check, &QCheckBox::toggled, &dialog, clearNotice);
    QObject::connect(minutes, qOverload<int>(&QSpinBox::valueChanged), &dialog, clearNotice);
    QWidget::setTabOrder(enabled, root);
    QWidget::setTabOrder(root, browse);
    QWidget::setTabOrder(browse, autoPull);
    QWidget::setTabOrder(autoPull, autoPush);
    QWidget::setTabOrder(autoPush, includeAdmin);
    QWidget::setTabOrder(includeAdmin, autoSync);
    QWidget::setTabOrder(autoSync, minutes);
    QWidget::setTabOrder(minutes, save);
    QWidget::setTabOrder(save, cancel);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        QFileDialog picker(&dialog, QString::fromUtf8("Папка синхронизации"), root->text());
        picker.setFileMode(QFileDialog::Directory);
        picker.setOptions(QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
        PrepareQtFilePicker(picker);
        if (auto* folder = picker.findChild<QComboBox*>(QStringLiteral("lookInCombo"))) {
            folder->setAccessibleName(QString::fromUtf8("Папка синхронизации"));
            folder->setAccessibleDescription(rootDetails);
        }
        if (auto* type = picker.findChild<QComboBox*>(QStringLiteral("fileTypeCombo"))) {
            type->setAccessibleName(QString::fromUtf8("Тип выбираемого объекта"));
            type->setAccessibleDescription(QString::fromUtf8("Выбор внешней папки, не файла."));
        }
        const auto selected = picker.exec() == QDialog::Accepted && !picker.selectedFiles().isEmpty()
            ? picker.selectedFiles().front() : QString();
        if (!selected.isEmpty()) root->setText(QDir::toNativeSeparators(selected));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto raw = QDir::fromNativeSeparators(root->text().trimmed());
        if (raw.isEmpty()) { notice->setText(QString::fromUtf8("Укажите внешнюю папку синхронизации.")); return; }
        const auto cloudRoot = std::filesystem::u8path(raw.toUtf8().toStdString());
        const auto resolved = cloudRoot.is_absolute() ? cloudRoot : workspaceDirectory / cloudRoot;
        if (overlaps(resolved, workspaceDirectory)) { notice->setText(QString::fromUtf8("Папка облака не должна совпадать с рабочей папкой или находиться внутри неё.")); return; }
        const auto target = workspaceDirectory / "meta/cloud.ini";
        std::error_code ec;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(target, ec)) ||
            std::filesystem::is_symlink(std::filesystem::symlink_status(target.parent_path(), ec))) {
            notice->setText(QString::fromUtf8("Символьная ссылка cloud.ini не поддерживается.")); return;
        }
        CloudSyncConfig draft = current; draft.enabled = enabled->isChecked(); draft.root = cloudRoot;
        draft.autoPull = autoPull->isChecked(); draft.autoPush = autoPush->isChecked(); draft.includeAdminProfiles = includeAdmin->isChecked();
        draft.autoSyncEnabled = autoSync->isChecked(); draft.autoSyncMinutes = minutes->value();
        if (!SaveCloudSyncConfig(workspaceDirectory, draft)) { notice->setText(QString::fromUtf8("Не удалось атомарно сохранить cloud.ini.")); return; }
        const auto checked = LoadCloudSyncConfig(workspaceDirectory);
        if (checked.enabled != draft.enabled || checked.root != draft.root || checked.autoPull != draft.autoPull || checked.autoPush != draft.autoPush ||
            checked.includeAdminProfiles != draft.includeAdminProfiles || checked.autoSyncEnabled != draft.autoSyncEnabled || checked.autoSyncMinutes != draft.autoSyncMinutes) {
            notice->setText(QString::fromUtf8("Сохранённая конфигурация не прошла проверку.")); return;
        }
        dialog.accept();
    });
    // Presentation follows the unchanged persistence/security callback. Failed
    // validation keeps every field and check state; only its address is exposed.
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (dialog.result() == QDialog::Accepted || notice->text().isEmpty()) return;
        notice->show();
        notice->setAccessibleName(notice->text());
        notice->setAccessibleDescription(QString::fromUtf8("Ошибка сохранения облачных настроек."));
        const bool rootError = notice->text() == QString::fromUtf8("Укажите внешнюю папку синхронизации.") ||
            notice->text() == QString::fromUtf8("Папка облака не должна совпадать с рабочей папкой или находиться внутри неё.");
        if (rootError) {
            root->setAccessibleDescription(notice->text() + QLatin1Char(' ') + rootDetails);
            dialog.layout()->activate();
            dialog.formLayout()->activate();
            dialog.scrollArea()->ensureWidgetVisible(root, 0, dialog.scaledMetric(8));
            root->setFocus(Qt::OtherFocusReason);
        }
    });
    return dialog.exec() == QDialog::Accepted;
}
