#include "QtCloudConflict.h"
#include "AppWorkspaceDataService.h"
#include "CloudSync.h"
#include <QtWidgets>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
QString q(const std::filesystem::path& path) { return QString::fromStdWString(path.wstring()); }
QString q(const std::string& text) { return QString::fromUtf8(text); }
bool supported(const std::string& path) {
    return path == "meta/tasks.json" || path == "meta/pipeline.json" || path == "meta/projects.json" ||
        path == "meta/banner.json" || path == "meta/gameplay.ini" || path == "meta/professions.txt";
}
QString objectName(const std::string& relative, const char* kind) {
    const auto stem = relative == "meta/tasks.json" ? QStringLiteral("tasks")
        : relative == "meta/pipeline.json" ? QStringLiteral("pipeline")
        : relative == "meta/projects.json" ? QStringLiteral("projects")
        : relative == "meta/banner.json" ? QStringLiteral("banner")
        : relative == "meta/gameplay.ini" ? QStringLiteral("gameplay") : QStringLiteral("professions");
    auto title = stem;
    title[0] = title[0].toUpper();
    const auto operation = QString::fromLatin1(kind);
    if (operation == QStringLiteral("ApplyCloud")) return QStringLiteral("applyCloud") + title;
    if (operation == QStringLiteral("PushCloud")) return QStringLiteral("pushCloud") + title;
    return stem + operation;
}
bool samePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code ec; auto a = std::filesystem::weakly_canonical(first, ec); if (ec) return false;
    auto b = std::filesystem::weakly_canonical(second, ec); if (ec) return false;
#ifdef _WIN32
    auto as = QString::fromStdWString(a.wstring()).toCaseFolded(); auto bs = QString::fromStdWString(b.wstring()).toCaseFolded(); return as == bs;
#else
    return a == b;
#endif
}
bool pathsOverlap(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code ec; auto a = std::filesystem::weakly_canonical(first, ec); if (ec) return true;
    auto b = std::filesystem::weakly_canonical(second, ec); if (ec) return true;
    auto as = QDir::fromNativeSeparators(q(a)); auto bs = QDir::fromNativeSeparators(q(b));
#ifdef _WIN32
    as = as.toCaseFolded(); bs = bs.toCaseFolded();
#endif
    if (!as.endsWith('/')) as += '/'; if (!bs.endsWith('/')) bs += '/';
    return as.startsWith(bs) || bs.startsWith(as);
}
bool safePath(const std::filesystem::path& path) {
    std::filesystem::path current;
    for (const auto& part : std::filesystem::absolute(path)) {
        current /= part;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#else
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(current))) return false;
#endif
    }
    return true;
}

QByteArray readFile(const std::filesystem::path& path, QString& error) {
    const QFileInfo info(q(path));
    if (!safePath(path) || info.isSymLink() || !info.exists() || !info.isFile()) { error = QString::fromUtf8("Источник отсутствует или является ссылкой."); return {}; }
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) { error = QString::fromUtf8("Не удалось прочитать источник."); return {}; }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { error = QString::fromUtf8("Ошибка чтения источника."); return {}; }
    return bytes;
}

bool validDocument(const QByteArray& bytes, QString& error) {
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject())) {
        error = QString::fromUtf8("Источник содержит повреждённый JSON."); return false;
    }
    return true;
}

bool decodeUtf8(const QByteArray& bytes, QString& text, QString& error, const QString& label) {
    text = QString::fromUtf8(bytes);
    auto encoded = text.toUtf8();
    auto comparable = bytes;
    if (bytes.startsWith("\xEF\xBB\xBF") && !encoded.startsWith("\xEF\xBB\xBF")) comparable = bytes.mid(3);
    if (encoded != comparable) {
        error = label + QString::fromUtf8(" содержит некорректный UTF-8.");
        return false;
    }
    return true;
}

bool validGameplayConfig(const QByteArray& bytes, QString& error) {
    QString text;
    if (!decodeUtf8(bytes, text, error, QString::fromUtf8("Файл правил"))) return false;
    const QSet<QString> levelingKeys{"base", "linear", "quadratic"};
    const QSet<QString> categoryKeys{"e", "d", "c", "b", "a"};
    const QSet<QString> floatKeys{"focus_base", "focus_bonus", "repeat_factor", "recovery_factor"};
    const QRegularExpression integerPattern(QStringLiteral("^-?\\d+$"));
    const QRegularExpression floatPattern(QStringLiteral("^-?\\d+(?:[.,]\\d+)?$"));
    QString section;
    int recognized = 0;
    for (auto line : text.split('\n')) {
        line = line.trimmed();
        if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const auto separator = line.indexOf('=');
        if (separator < 0) continue;
        const auto key = line.left(separator).trimmed().toLower();
        const auto value = line.mid(separator + 1).trimmed();
        const bool integer = (section == QStringLiteral("leveling") && levelingKeys.contains(key)) ||
            (section == QStringLiteral("categories") && categoryKeys.contains(key)) ||
            (section == QStringLiteral("rewards") && key == QStringLiteral("recovery_tasks"));
        const bool decimal = section == QStringLiteral("rewards") && floatKeys.contains(key);
        if (!integer && !decimal) continue;
        if (integer ? !integerPattern.match(value).hasMatch() : !floatPattern.match(value).hasMatch()) {
            error = QString::fromUtf8("Файл правил содержит некорректное числовое значение.");
            return false;
        }
        bool converted = false;
        const auto numeric = integer ? value.toLongLong(&converted) : value.toDouble(&converted);
        if (!converted || (integer ? std::abs(double(numeric)) > 1000000000.0 : !std::isfinite(numeric) || std::abs(numeric) > 1000000000.0)) {
            error = QString::fromUtf8("Числовое значение в файле правил выходит за допустимые пределы.");
            return false;
        }
        ++recognized;
    }
    if (recognized == 0) {
        error = QString::fromUtf8("В файле не найдено ни одного распознанного параметра правил.");
        return false;
    }
    return true;
}

bool validProfessionCatalog(const QByteArray& bytes, qsizetype* count, QString& error) {
    QString text;
    if (!decodeUtf8(bytes, text, error, QString::fromUtf8("Каталог профессий"))) return false;
    qsizetype entries = 0;
    bool firstLine = true;
    for (auto line : text.split('\n')) {
        if (firstLine) {
            firstLine = false;
            if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
        }
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const auto fields = line.split('|');
        if (fields.size() < 2 || fields[0].trimmed().isEmpty() || fields[1].trimmed().isEmpty()) {
            error = QString::fromUtf8("Строка каталога профессий должна содержать ID и название через |.");
            return false;
        }
        for (const auto& field : fields) {
            for (const auto ch : field) {
                if (ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) {
                    error = QString::fromUtf8("Каталог профессий содержит управляющий символ.");
                    return false;
                }
            }
        }
        ++entries;
    }
    if (count) *count = entries;
    return true;
}

bool validSource(const QByteArray& bytes, const std::string& relative, QString& error) {
    if (relative == "meta/gameplay.ini") return validGameplayConfig(bytes, error);
    if (relative == "meta/professions.txt") return validProfessionCatalog(bytes, nullptr, error);
    return validDocument(bytes, error);
}

bool atomicWrite(const std::filesystem::path& path, const QByteArray& bytes, QString& error) {
    const QFileInfo info(q(path));
    if (!safePath(path.parent_path()) || info.isSymLink() || QFileInfo(info.absolutePath()).isSymLink()) { error = QString::fromUtf8("Запись через ссылку запрещена."); return false; }
    QDir().mkpath(info.absolutePath());
    QSaveFile output(info.filePath()); output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = QString::fromUtf8("Не удалось атомарно заменить файл."); return false;
    }
    return true;
}

std::filesystem::path backupPath(const std::filesystem::path& workspace, const std::string& relative,
                                 const std::string& kind) {
    const auto dir = workspace / "meta/updates";
    // Match CloudSync's public backup parser: punctuation is replaced before the extension is appended.
    const std::string stem = relative == "meta/tasks.json" ? "meta_tasks_json"
        : relative == "meta/pipeline.json" ? "meta_pipeline_json"
        : relative == "meta/projects.json" ? "meta_projects_json"
        : relative == "meta/banner.json" ? "meta_banner_json"
        : relative == "meta/gameplay.ini" ? "meta_gameplay_ini" : "meta_professions_txt";
    const auto extension = std::filesystem::u8path(relative).extension().string();
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::filesystem::path candidate;
    do candidate = dir / (stem + "." + kind + "." + std::to_string(stamp++) + extension);
    while (std::filesystem::exists(candidate));
    return candidate;
}

QString preview(const std::filesystem::path& path, const std::string& relative) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return QString::fromUtf8("нет файла");
    try {
        if (relative == "meta/gameplay.ini") {
            QString error;
            const auto bytes = readFile(path, error);
            if (!error.isEmpty() || !validGameplayConfig(bytes, error))
                return QString::fromUtf8("некорректный файл · %1 байт").arg(std::filesystem::file_size(path, ec));
            return QString::fromUtf8("правила XP · %1 байт").arg(bytes.size());
        }
        if (relative == "meta/professions.txt") {
            QString error;
            qsizetype count = 0;
            const auto bytes = readFile(path, error);
            if (!error.isEmpty() || !validProfessionCatalog(bytes, &count, error))
                return QString::fromUtf8("некорректный каталог · %1 байт").arg(std::filesystem::file_size(path, ec));
            return QString::fromUtf8("%1 профессий · %2 байт").arg(count).arg(bytes.size());
        }
        qsizetype count = 0;
        QString unit;
        if (relative == "meta/tasks.json") { count = qsizetype(LoadTasksDataFromFile(path).size()); unit = QString::fromUtf8("задач"); }
        else if (relative == "meta/pipeline.json") { count = qsizetype(LoadPipelineDataFromFile(path).size()); unit = QString::fromUtf8("этапов"); }
        else {
            QJsonParseError parse;
            QString readError;
            const auto document = QJsonDocument::fromJson(readFile(path, readError), &parse);
            if (!readError.isEmpty()) throw std::runtime_error(readError.toUtf8().constData());
            if (parse.error != QJsonParseError::NoError) throw std::runtime_error("Malformed projects JSON.");
            if (relative == "meta/projects.json") {
                if (document.isArray()) count = document.array().size();
                else if (document.isObject() && document.object().value("projects").isArray()) count = document.object().value("projects").toArray().size();
                unit = QString::fromUtf8("проектов");
            } else {
                if (document.isObject() && document.object().value("items").isArray()) count = document.object().value("items").toArray().size();
                unit = QString::fromUtf8("фраз");
            }
        }
        return QString::fromUtf8("%1 %2 · %3 байт").arg(count).arg(unit).arg(std::filesystem::file_size(path, ec));
    } catch (...) { return QString::fromUtf8("не удалось разобрать · %1 байт").arg(std::filesystem::file_size(path, ec)); }
}

QString label(const std::string& relative) {
    return QString::fromUtf8(relative == "meta/tasks.json" ? "Задачи"
        : relative == "meta/pipeline.json" ? "Пайплайн"
        : relative == "meta/projects.json" ? "Проекты"
        : relative == "meta/banner.json" ? "Баннер"
        : relative == "meta/gameplay.ini" ? "Правила XP" : "Профессии");
}

bool confirm(QWidget* parent, const QString& title, const QString& source, const QString& target, const QString& action) {
    QMessageBox box(QMessageBox::Warning, title,
        QString::fromUtf8("Источник\n%1\n\nБудет заменено\n%2\n\nТекущая заменяемая версия сначала сохранится в локальном meta/updates.")
            .arg(source, target), QMessageBox::Yes | QMessageBox::Cancel, parent);
    box.setObjectName("cloudConflictConfirm");
    box.setDefaultButton(QMessageBox::Cancel);
    box.button(QMessageBox::Yes)->setText(action);
    box.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    box.button(QMessageBox::Yes)->setMinimumWidth(120); box.button(QMessageBox::Yes)->setStyleSheet("min-height: 40px; max-height: 40px;");
    box.button(QMessageBox::Cancel)->setMinimumWidth(120); box.button(QMessageBox::Cancel)->setStyleSheet("min-height: 40px; max-height: 40px;");
    return box.exec() == QMessageBox::Yes;
}
}

QtCloudConflictResult ApplyQtCloudWorkspaceFile(const std::filesystem::path& workspace,
                                                const std::filesystem::path& source,
                                                const std::string& relative,
                                                const std::string& sourceKind) {
    QtCloudConflictResult result;
    if (!supported(relative) || (sourceKind != "cloud" && sourceKind != "restore")) {
        result.message = u8"Неподдерживаемый источник разрешения конфликта."; return result;
    }
    if (sourceKind == "cloud") {
        const auto config = LoadCloudSyncConfig(workspace);
        if (!config.enabled || !samePath(source, ResolveCloudRootPath(config, workspace) / std::filesystem::u8path(relative))) {
            result.message = u8"Облачный источник не соответствует настроенному корню."; return result;
        }
    } else {
        const auto backups = ListCloudWorkspaceBackups(workspace, relative);
        if (std::none_of(backups.begin(), backups.end(), [&](const auto& item) { return samePath(item.path, source); })) {
            result.message = u8"Снимок не принадлежит списку резервных копий."; return result;
        }
    }
    QString error;
    const auto sourceBytes = readFile(source, error);
    if (!error.isEmpty() || !validSource(sourceBytes, relative, error)) { result.message = error.toUtf8().toStdString(); return result; }
    const auto target = workspace / std::filesystem::u8path(relative);
    QByteArray targetBytes;
    if (std::filesystem::exists(target)) {
        targetBytes = readFile(target, error);
        if (!error.isEmpty()) { result.message = error.toUtf8().toStdString(); return result; }
        if (targetBytes == sourceBytes) { result.ok = true; result.message = u8"Версии уже совпадают."; return result; }
        result.backupPath = backupPath(workspace, relative, sourceKind == "cloud" ? "local" : "restore");
        if (!atomicWrite(result.backupPath, targetBytes, error)) { result.message = error.toUtf8().toStdString(); result.backupPath.clear(); return result; }
    }
    if (sourceKind == "cloud") {
        const auto cloudBackup = backupPath(workspace, relative, "cloud");
        if (!atomicWrite(cloudBackup, sourceBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    }
    // Reject stale previews: the caller must still be applying the bytes it displayed.
    const auto checkedSource = readFile(source, error);
    if (!error.isEmpty() || checkedSource != sourceBytes) { result.message = u8"Источник изменился после проверки. Откройте сравнение заново."; return result; }
    if (!atomicWrite(target, sourceBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    result.ok = true; result.changed = true;
    result.message = sourceKind == "cloud" ? u8"Применена облачная версия." : u8"Восстановлена выбранная резервная копия.";
    return result;
}

QtCloudConflictResult PushQtCloudWorkspaceFile(const std::filesystem::path& workspace,
                                               const std::string& relative) {
    QtCloudConflictResult result;
    if (!supported(relative)) { result.message = u8"Неподдерживаемый файл для отправки."; return result; }
    const auto config = LoadCloudSyncConfig(workspace);
    const auto root = ResolveCloudRootPath(config, workspace);
    if (!config.enabled || !std::filesystem::is_directory(root) || pathsOverlap(root, workspace)) {
        result.message = u8"Облачный корень недоступен или пересекается с рабочей папкой."; return result;
    }
    const auto source = workspace / std::filesystem::u8path(relative);
    const auto target = root / std::filesystem::u8path(relative);
    QString error;
    const auto sourceBytes = readFile(source, error);
    if (!error.isEmpty() || !validSource(sourceBytes, relative, error)) { result.message = error.toUtf8().toStdString(); return result; }
    QByteArray targetBytes; const bool targetExisted = std::filesystem::exists(target);
    if (targetExisted) {
        targetBytes = readFile(target, error);
        if (!error.isEmpty()) { result.message = error.toUtf8().toStdString(); return result; }
        if (targetBytes == sourceBytes) { result.ok = true; result.message = u8"Локальная и облачная версии уже совпадают."; return result; }
    } else if (!safePath(target.parent_path())) {
        result.message = u8"Запись через ссылку запрещена."; return result;
    }
    const auto localBackup = backupPath(workspace, relative, "local");
    if (!atomicWrite(localBackup, sourceBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    if (targetExisted) {
        result.backupPath = backupPath(workspace, relative, "cloud");
        if (!atomicWrite(result.backupPath, targetBytes, error)) { result.message = error.toUtf8().toStdString(); result.backupPath.clear(); return result; }
    }
    // Both ends must still match the preview immediately before the atomic replacement.
    const auto checkedSource = readFile(source, error);
    if (!error.isEmpty() || checkedSource != sourceBytes) { result.message = u8"Локальная версия изменилась после проверки. Откройте сравнение заново."; return result; }
    if (targetExisted) {
        const auto checkedTarget = readFile(target, error);
        if (!error.isEmpty() || checkedTarget != targetBytes) { result.message = u8"Облачная версия изменилась после проверки. Откройте сравнение заново."; return result; }
    } else if (std::filesystem::exists(target)) {
        result.message = u8"Облачный файл появился после проверки. Откройте сравнение заново."; return result;
    }
    if (!atomicWrite(target, sourceBytes, error)) { result.message = error.toUtf8().toStdString(); return result; }
    result.ok = true; result.changed = true; result.message = u8"Локальная версия отправлена в облако.";
    return result;
}

bool ShowCloudConflictResolver(QWidget* parent, const std::filesystem::path& workspace) {
    const auto config = LoadCloudSyncConfig(workspace);
    const auto root = ResolveCloudRootPath(config, workspace);
    QDialog dialog(parent); dialog.setObjectName("cloudConflictResolver");
    dialog.setWindowTitle(QString::fromUtf8("Сравнение облачных версий")); dialog.resize(820, 560); dialog.setMinimumSize(720, 480);
    auto* layout = new QVBoxLayout(&dialog); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(12);
    auto* intro = new QLabel(QString::fromUtf8("Выберите направление для отдельного файла. Любая замена требует подтверждения и резервной копии."));
    intro->setWordWrap(true); intro->setProperty("warning", true); layout->addWidget(intro);
    auto* tabs = new QTabWidget; tabs->setObjectName("cloudConflictTabs"); layout->addWidget(tabs, 1);
    bool changed = false;
    auto addFileTab = [&](const std::string& relative) {
        auto* page = new QWidget; auto* box = new QVBoxLayout(page); box->setContentsMargins(12, 12, 12, 12); box->setSpacing(10);
        const auto local = workspace / std::filesystem::u8path(relative); const auto cloud = root / std::filesystem::u8path(relative);
        auto* comparison = new QTableWidget(2, 3); comparison->setObjectName(objectName(relative, "Comparison"));
        comparison->setHorizontalHeaderLabels({QString::fromUtf8("Версия"), QString::fromUtf8("Сводка"), QString::fromUtf8("Путь")});
        comparison->verticalHeader()->hide(); comparison->setEditTriggers(QAbstractItemView::NoEditTriggers);
        comparison->setSelectionMode(QAbstractItemView::NoSelection); comparison->setShowGrid(false); comparison->setAlternatingRowColors(true);
        comparison->setItem(0, 0, new QTableWidgetItem(QString::fromUtf8("Локальная")));
        comparison->setItem(0, 1, new QTableWidgetItem(preview(local, relative)));
        comparison->setItem(0, 2, new QTableWidgetItem(q(local)));
        comparison->setItem(1, 0, new QTableWidgetItem(QString::fromUtf8("Облачная")));
        comparison->setItem(1, 1, new QTableWidgetItem(preview(cloud, relative)));
        comparison->setItem(1, 2, new QTableWidgetItem(q(cloud)));
        comparison->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed); comparison->setColumnWidth(0, 105);
        comparison->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed); comparison->setColumnWidth(1, 160);
        comparison->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        comparison->setTextElideMode(Qt::ElideMiddle); comparison->setMaximumHeight(118); box->addWidget(comparison);
        auto* actions = new QHBoxLayout; actions->setSpacing(8);
        auto* apply = new QPushButton(QString::fromUtf8("Принять из облака")); apply->setObjectName(objectName(relative, "ApplyCloud"));
        apply->setProperty("primary", true); apply->setStyleSheet("min-height: 40px; max-height: 40px;"); apply->setEnabled(std::filesystem::is_regular_file(cloud)); actions->addWidget(apply);
        auto* push = new QPushButton(QString::fromUtf8("Отправить локальную")); push->setObjectName(objectName(relative, "PushCloud"));
        push->setStyleSheet("min-height: 40px; max-height: 40px;"); push->setEnabled(config.enabled && std::filesystem::is_regular_file(local)); actions->addWidget(push); actions->addStretch(); box->addLayout(actions);
        auto* backups = new QTableWidget; backups->setObjectName(objectName(relative, "Backups"));
        backups->setColumnCount(4); backups->setHorizontalHeaderLabels({QString::fromUtf8("Дата"), QString::fromUtf8("Источник"), QString::fromUtf8("Сводка"), QString::fromUtf8("Действие")});
        backups->verticalHeader()->hide(); backups->setEditTriggers(QAbstractItemView::NoEditTriggers); backups->setSelectionMode(QAbstractItemView::NoSelection); backups->setShowGrid(false);
        const auto snapshots = ListCloudWorkspaceBackups(workspace, relative); const int shown = int(std::min<size_t>(5, snapshots.size())); backups->setRowCount(shown);
        for (int i = 0; i < shown; ++i) {
            const auto snapshot = snapshots[size_t(i)];
            backups->setItem(i, 0, new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(snapshot.createdAt).toString("yyyy-MM-dd HH:mm")));
            backups->setItem(i, 1, new QTableWidgetItem(q(snapshot.sourceKind)));
            backups->setItem(i, 2, new QTableWidgetItem(preview(snapshot.path, relative)));
            auto* restore = new QPushButton(QString::fromUtf8("Восстановить")); restore->setStyleSheet("min-height: 40px; max-height: 40px;"); backups->setRowHeight(i, 44); backups->setCellWidget(i, 3, restore);
            QObject::connect(restore, &QPushButton::clicked, &dialog, [&, snapshot, relative, local] {
                if (!confirm(&dialog, QString::fromUtf8("Восстановить снимок"), preview(snapshot.path, relative) + "\n" + q(snapshot.path), preview(local, relative) + "\n" + q(local), QString::fromUtf8("Восстановить"))) return;
                const auto result = ApplyQtCloudWorkspaceFile(workspace, snapshot.path, relative, "restore");
                if (!result.ok) QMessageBox::warning(&dialog, QString::fromUtf8("Восстановление"), q(result.message));
                else { changed = changed || result.changed; dialog.accept(); }
            });
        }
        backups->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed); backups->setColumnWidth(0, 140);
        backups->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed); backups->setColumnWidth(1, 82);
        backups->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        backups->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed); backups->setColumnWidth(3, 150);
        box->addWidget(new QLabel(QString::fromUtf8("Последние локальные снимки"))); box->addWidget(backups, 1);
        QObject::connect(apply, &QPushButton::clicked, &dialog, [&, relative, local, cloud] {
            if (!confirm(&dialog, QString::fromUtf8("Применить облачную версию"), preview(cloud, relative) + "\n" + q(cloud), preview(local, relative) + "\n" + q(local), QString::fromUtf8("Применить"))) return;
            const auto result = ApplyQtCloudWorkspaceFile(workspace, cloud, relative, "cloud");
            if (!result.ok) QMessageBox::warning(&dialog, QString::fromUtf8("Облачная версия"), q(result.message));
            else { changed = changed || result.changed; dialog.accept(); }
        });
        QObject::connect(push, &QPushButton::clicked, &dialog, [&, relative, local, cloud] {
            if (!confirm(&dialog, QString::fromUtf8("Отправить локальную версию"), preview(local, relative) + "\n" + q(local), preview(cloud, relative) + "\n" + q(cloud), QString::fromUtf8("Отправить"))) return;
            const auto result = PushQtCloudWorkspaceFile(workspace, relative);
            if (!result.ok) QMessageBox::warning(&dialog, QString::fromUtf8("Отправка в облако"), q(result.message));
            else { changed = changed || result.changed; dialog.accept(); }
        });
        tabs->addTab(page, label(relative));
    };
    addFileTab("meta/tasks.json"); addFileTab("meta/pipeline.json");
    addFileTab("meta/projects.json"); addFileTab("meta/banner.json");
    addFileTab("meta/gameplay.ini"); addFileTab("meta/professions.txt");
    auto* close = new QPushButton(QString::fromUtf8("Закрыть")); close->setMinimumWidth(120); close->setStyleSheet("min-height: 40px; max-height: 40px;"); layout->addWidget(close, 0, Qt::AlignRight);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec(); return changed;
}
