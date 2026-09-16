#include "QtStorageConflict.h"
#include "AppUtils.h"
#include "CloudSync.h"
#include <QtWidgets>
#include <chrono>
#include <cmath>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
QString q(const fs::path& path) { return QString::fromStdWString(path.wstring()); }
QString q(const std::string& text) { return QString::fromUtf8(text); }
bool safePath(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path)) {
        current /= part;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#else
        if (fs::is_symlink(fs::symlink_status(current))) return false;
#endif
    }
    return true;
}
bool overlap(const fs::path& first, const fs::path& second) {
    std::error_code ec; auto a = fs::weakly_canonical(first, ec); if (ec) return true;
    auto b = fs::weakly_canonical(second, ec); if (ec) return true;
    auto as = QDir::fromNativeSeparators(q(a)); auto bs = QDir::fromNativeSeparators(q(b));
#ifdef _WIN32
    as = as.toCaseFolded(); bs = bs.toCaseFolded();
#endif
    if (!as.endsWith('/')) as += '/'; if (!bs.endsWith('/')) bs += '/';
    return as.startsWith(bs) || bs.startsWith(as);
}
QByteArray read(const fs::path& path, QString& error) {
    QFileInfo info(q(path));
    if (!safePath(path) || info.isSymLink() || !info.isFile()) { error = QString::fromUtf8("storage.json отсутствует или является ссылкой."); return {}; }
    QFile input(info.filePath());
    if (!input.open(QIODevice::ReadOnly)) { error = QString::fromUtf8("Не удалось прочитать storage.json."); return {}; }
    const auto bytes = input.readAll();
    if (input.error() != QFileDevice::NoError) error = QString::fromUtf8("Ошибка чтения storage.json.");
    return bytes;
}
bool validate(const QByteArray& bytes, QString& error) {
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) { error = QString::fromUtf8("storage.json содержит повреждённый JSON."); return false; }
    const auto object = document.object();
    const bool balance = object.value("balance_enc").isString() || object.value("balance").isDouble();
    if (!balance || !object.value("currency_name").isString() || !object.value("currency_code").isString() ||
        !object.value("rev").isDouble() || !object.value("updated_at").isDouble() ||
        !object.value("content_hash").isString() || object.value("content_hash").toString().isEmpty() ||
        !object.value("log").isArray()) {
        error = QString::fromUtf8("storage.json не содержит обязательные поля кошелька."); return false;
    }
    return true;
}
bool write(const fs::path& path, const QByteArray& bytes, QString& error) {
    if (!safePath(path.parent_path()) || QFileInfo(q(path)).isSymLink()) { error = QString::fromUtf8("Запись через ссылку запрещена."); return false; }
    QDir().mkpath(QFileInfo(q(path)).absolutePath());
    QSaveFile output(q(path)); output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = QString::fromUtf8("Не удалось атомарно заменить storage.json."); return false;
    }
    return true;
}
fs::path backup(const fs::path& workspace, const char* kind) {
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto dir = workspace / "meta/updates"; fs::path result;
    do result = dir / (std::string("storage.") + kind + "." + std::to_string(stamp++) + ".json"); while (fs::exists(result));
    return result;
}
QString summary(const StorageVaultData& vault) {
    return QString::fromUtf8("%1 %2 · рев. %3 · журнал %4 · обновлено %5")
        .arg(vault.balance, 0, 'f', 2).arg(q(vault.currencyCode)).arg(vault.revision).arg(vault.log.size())
        .arg(vault.updatedAt > 0 ? QDateTime::fromSecsSinceEpoch(vault.updatedAt).toString("yyyy-MM-dd HH:mm") : QString::fromUtf8("—"));
}
}

bool HasQtStorageConflict(const fs::path& workspace) {
    const auto config = LoadCloudSyncConfig(workspace); const auto root = ResolveCloudRootPath(config, workspace);
    if (!config.enabled || overlap(root, workspace)) return false;
    QString error; const auto local = read(workspace / "meta/storage.json", error); if (!error.isEmpty()) return false;
    error.clear(); const auto cloud = read(root / "meta/storage.json", error); return error.isEmpty() && local != cloud;
}

QtStorageConflictResult ResolveQtStorageConflict(const fs::path& workspace, bool preferCloud) {
    QtStorageConflictResult result; const auto config = LoadCloudSyncConfig(workspace); const auto root = ResolveCloudRootPath(config, workspace);
    if (!config.enabled || !fs::is_directory(root) || overlap(root, workspace)) { result.message = u8"Облачный корень недоступен или пересекается с рабочей папкой."; return result; }
    const auto local = workspace / "meta/storage.json"; const auto cloud = root / "meta/storage.json";
    QString error; const auto localBytes = read(local, error); if (!error.isEmpty() || !validate(localBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    error.clear(); const auto cloudBytes = read(cloud, error); if (!error.isEmpty() || !validate(cloudBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    if (!ValidateStorageVaultFile(workspace) || !ValidateStorageVaultFile(root)) { result.message = u8"content_hash storage.json не соответствует содержимому."; return result; }
    if (localBytes == cloudBytes) { result.ok = true; result.message = u8"Версии storage.json уже совпадают."; return result; }
    const auto localBackup = backup(workspace, "local"); const auto cloudBackup = backup(workspace, "cloud");
    if (!write(localBackup, localBytes, error) || !write(cloudBackup, cloudBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    const auto source = preferCloud ? cloud : local; const auto target = preferCloud ? local : cloud;
    const auto sourceBytes = preferCloud ? cloudBytes : localBytes; const auto targetBytes = preferCloud ? localBytes : cloudBytes;
    error.clear(); const auto checkedSource = read(source, error);
    if (!error.isEmpty() || checkedSource != sourceBytes) { result.message = u8"Источник изменился после проверки. Откройте сравнение заново."; return result; }
    error.clear(); const auto checkedTarget = read(target, error);
    if (!error.isEmpty() || checkedTarget != targetBytes) { result.message = u8"Цель изменилась после проверки. Откройте сравнение заново."; return result; }
    if (!write(target, sourceBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    result.ok = true; result.changed = true;
    result.message = preferCloud ? u8"Облачная версия storage.json принята локально." : u8"Локальная версия storage.json сохранена в облаке.";
    return result;
}

bool ShowQtStorageConflictResolver(QWidget* parent, const fs::path& workspace, bool* localChanged) {
    if (localChanged) *localChanged = false;
    const auto config = LoadCloudSyncConfig(workspace); const auto root = ResolveCloudRootPath(config, workspace);
    const auto localPath = workspace / "meta/storage.json"; const auto cloudPath = root / "meta/storage.json";
    const auto local = LoadStorageVault(workspace); const auto cloud = LoadStorageVault(root);
    QDialog dialog(parent); dialog.setObjectName("storageConflictResolver"); dialog.setWindowTitle(QString::fromUtf8("Конфликт storage.json")); dialog.setMinimumSize(760, 390);
    auto* layout = new QVBoxLayout(&dialog); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(12);
    auto* warning = new QLabel(QString::fromUtf8("Выберите целую версию кошелька. Баланс и журнал не объединяются. Обе исходные версии будут сохранены локально."));
    warning->setWordWrap(true); warning->setProperty("warning", true); layout->addWidget(warning);
    auto* table = new QTableWidget(2, 3); table->setObjectName("storageComparison"); table->setHorizontalHeaderLabels({QString::fromUtf8("Версия"), QString::fromUtf8("Сводка"), QString::fromUtf8("Путь")});
    table->verticalHeader()->hide(); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionMode(QAbstractItemView::NoSelection); table->setShowGrid(false); table->setAlternatingRowColors(true);
    table->setItem(0, 0, new QTableWidgetItem(QString::fromUtf8("Локальная"))); table->setItem(0, 1, new QTableWidgetItem(summary(local))); table->setItem(0, 2, new QTableWidgetItem(q(localPath)));
    table->setItem(1, 0, new QTableWidgetItem(QString::fromUtf8("Облачная"))); table->setItem(1, 1, new QTableWidgetItem(summary(cloud))); table->setItem(1, 2, new QTableWidgetItem(q(cloudPath)));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed); table->setColumnWidth(0, 105); table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed); table->setColumnWidth(1, 310); table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch); table->setTextElideMode(Qt::ElideMiddle); table->setMaximumHeight(118); layout->addWidget(table);
    auto* actions = new QHBoxLayout; actions->setSpacing(8);
    auto* acceptCloud = new QPushButton(QString::fromUtf8("Принять облачную")); acceptCloud->setObjectName("acceptCloudStorage"); acceptCloud->setProperty("primary", true); acceptCloud->setStyleSheet("min-height: 40px; max-height: 40px;"); actions->addWidget(acceptCloud);
    auto* keepLocal = new QPushButton(QString::fromUtf8("Оставить локальную в облаке")); keepLocal->setObjectName("keepLocalStorage"); keepLocal->setStyleSheet("min-height: 40px; max-height: 40px;"); actions->addWidget(keepLocal); actions->addStretch(); layout->addLayout(actions);
    auto* close = new QPushButton(QString::fromUtf8("Закрыть")); close->setMinimumWidth(120); close->setStyleSheet("min-height: 40px; max-height: 40px;"); layout->addStretch(); layout->addWidget(close, 0, Qt::AlignRight);
    bool changed = false;
    auto run = [&](bool preferCloud) {
        QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Заменить storage.json"),
            QString::fromUtf8("Будет полностью заменён баланс, журнал и правила наград выбранной стороны. Продолжить?"), QMessageBox::Yes | QMessageBox::Cancel, &dialog);
        confirm.setObjectName("storageConflictConfirm"); confirm.setDefaultButton(QMessageBox::Cancel); confirm.button(QMessageBox::Yes)->setText(preferCloud ? QString::fromUtf8("Принять") : QString::fromUtf8("Отправить")); confirm.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
        confirm.button(QMessageBox::Yes)->setMinimumWidth(120); confirm.button(QMessageBox::Yes)->setStyleSheet("min-height: 40px; max-height: 40px;");
        confirm.button(QMessageBox::Cancel)->setMinimumWidth(120); confirm.button(QMessageBox::Cancel)->setStyleSheet("min-height: 40px; max-height: 40px;");
        if (confirm.exec() != QMessageBox::Yes) return;
        const auto result = ResolveQtStorageConflict(workspace, preferCloud);
        if (!result.ok) { QMessageBox::warning(&dialog, QString::fromUtf8("Конфликт storage.json"), q(result.message)); return; }
        changed = result.changed; if (localChanged) *localChanged = preferCloud && result.changed; dialog.accept();
    };
    QObject::connect(acceptCloud, &QPushButton::clicked, &dialog, [&] { run(true); }); QObject::connect(keepLocal, &QPushButton::clicked, &dialog, [&] { run(false); }); QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec(); return changed;
}
