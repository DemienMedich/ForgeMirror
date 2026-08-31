#include "QtCloudSettings.h"
#include "CloudSync.h"
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
    QDialog dialog(parent); dialog.setObjectName("cloudSettings"); dialog.setWindowTitle(QString::fromUtf8("Настройки облака")); dialog.setMinimumWidth(580);
    auto* form = new QFormLayout(&dialog);
    auto* warning = new QLabel(QString::fromUtf8("На этом этапе Qt только сохраняет конфигурацию и проверяет готовность. Копирование, push, pull и разрешение конфликтов отключены."));
    warning->setWordWrap(true); warning->setProperty("warning", true); form->addRow(warning);
    auto* enabled = new QCheckBox(QString::fromUtf8("Включить конфигурацию облака")); enabled->setObjectName("cloudEnabled"); enabled->setChecked(current.enabled);
    auto* root = new QLineEdit(QString::fromUtf8(current.root.u8string())); root->setObjectName("cloudRoot");
    auto* browse = new QPushButton(QString::fromUtf8("Выбрать папку…")); browse->setObjectName("cloudBrowse");
    auto* autoPull = new QCheckBox(QString::fromUtf8("Разрешить автоматический pull в стабильной версии")); autoPull->setObjectName("cloudAutoPull"); autoPull->setChecked(current.autoPull);
    auto* autoPush = new QCheckBox(QString::fromUtf8("Разрешить автоматический push в стабильной версии")); autoPush->setObjectName("cloudAutoPush"); autoPush->setChecked(current.autoPush);
    auto* includeAdmin = new QCheckBox(QString::fromUtf8("Включать профили администраторов")); includeAdmin->setObjectName("cloudIncludeAdmin"); includeAdmin->setChecked(current.includeAdminProfiles);
    auto* autoSync = new QCheckBox(QString::fromUtf8("Автосинхронизация в стабильной версии")); autoSync->setObjectName("cloudAutoSync"); autoSync->setChecked(current.autoSyncEnabled);
    auto* minutes = new QSpinBox; minutes->setObjectName("cloudMinutes"); minutes->setRange(1, 120); minutes->setValue(std::clamp(current.autoSyncMinutes, 1, 120)); minutes->setSuffix(QString::fromUtf8(" мин"));
    form->addRow(enabled); form->addRow(QString::fromUtf8("Корневая папка"), root); form->addRow(browse);
    form->addRow(autoPull); form->addRow(autoPush); form->addRow(includeAdmin); form->addRow(autoSync); form->addRow(QString::fromUtf8("Интервал"), minutes);
    auto* notice = new QLabel; notice->setObjectName("cloudNotice"); notice->setWordWrap(true); form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto selected = QFileDialog::getExistingDirectory(&dialog, QString::fromUtf8("Папка синхронизации"), root->text(), QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
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
    return dialog.exec() == QDialog::Accepted;
}
