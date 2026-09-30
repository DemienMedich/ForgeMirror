#include "QtStorageConflict.h"
#include "AppUtils.h"
#include "AppWorkspaceStorageLock.h"
#include "CloudSync.h"
#include "QtScrollableDialog.h"
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

int decisionMetric(const QWidget* widget, int base) {
    const double savedBase = qApp->property("forgeBasePointSize").toDouble();
    const double current = widget ? widget->font().pointSizeF() : qApp->font().pointSizeF();
    const double scale = savedBase > 0.0 && current > 0.0 ? std::clamp(current / savedBase, 0.9, 2.0) : 1.0;
    return std::max(1, int(std::lround(base * scale)));
}
qreal layoutDecisionText(QTextLayout& text, qreal width) {
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    text.setTextOption(option); text.beginLayout();
    qreal height = 0;
    while (true) {
        auto line = text.createLine(); if (!line.isValid()) break;
        line.setLineWidth(std::max(qreal(1), width)); line.setPosition(QPointF(0, height)); height += line.height();
    }
    text.endLayout(); return std::ceil(height);
}
class StorageTextDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    int textHeight(const QString& text, int width, const QFont& font) const {
        QTextLayout layout(QString(text).replace(QLatin1Char('\n'), QChar::LineSeparator), font);
        return std::max(QFontMetrics(font).lineSpacing(), int(layoutDecisionText(layout, width)));
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto* table = qobject_cast<QTableWidget*>(parent());
        if (!table) return QStyledItemDelegate::sizeHint(option, index);
        QStyleOptionViewItem content(option); initStyleOption(&content, index);
        return QSize(table->columnWidth(index.column()), textHeight(content.text,
            std::max(1, table->columnWidth(index.column()) - decisionMetric(table, 16)), content.font) + decisionMetric(table, 8));
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem content(option); initStyleOption(&content, index);
        auto* style = content.widget ? content.widget->style() : QApplication::style();
        const int horizontalInset = decisionMetric(content.widget, 8), verticalInset = decisionMetric(content.widget, 4);
        const QRect textRect = option.rect.adjusted(horizontalInset, verticalInset, -horizontalInset, -verticalInset);
        QStyleOptionViewItem background(content); background.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, content.widget);
        QTextLayout text(QString(content.text).replace(QLatin1Char('\n'), QChar::LineSeparator), content.font);
        layoutDecisionText(text, std::max(1, textRect.width()));
        painter->save(); painter->setClipRect(option.rect);
        const auto group = content.state & QStyle::State_Enabled ? QPalette::Active : QPalette::Disabled;
        painter->setPen(content.palette.color(group, content.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        text.draw(painter, textRect.topLeft()); painter->restore();
    }
};

class StorageComparisonTable final : public QTableWidget {
public:
    explicit StorageComparisonTable(QWidget* parent = nullptr) : QTableWidget(2, 3, parent), text_(new StorageTextDelegate(this)) {
        setItemDelegate(text_); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setWordWrap(true); setTextElideMode(Qt::ElideNone);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        horizontalHeader()->setStretchLastSection(false); horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        verticalHeader()->setSectionResizeMode(QHeaderView::Fixed); setMinimumWidth(0);
    }
    void fitContents() {
        if (fitting_) return;
        fitting_ = true; ensurePolished();
        const QFontMetrics metrics(font()); const int padding = decisionMetric(this, 16), cellPadding = decisionMetric(this, 8);
        int desiredSide = 0, desiredSummary = 0;
        for (int row = 0; row < rowCount(); ++row) {
            if (auto* value = item(row, 0)) desiredSide = std::max(desiredSide,
                std::max(metrics.horizontalAdvance(value->text()), metrics.boundingRect(value->text()).width()) + padding);
            if (auto* value = item(row, 1)) for (const auto& word : value->text().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
                desiredSummary = std::max(desiredSummary, std::max(metrics.horizontalAdvance(word), metrics.boundingRect(word).width()) + padding);
        }
        const int summaryMinimum = headerWidth(1), pathMinimum = headerWidth(2);
        const int sideWidth = std::max(headerWidth(0), std::min(desiredSide, viewport()->width() - summaryMinimum - pathMinimum));
        const int spare = std::max(0, viewport()->width() - sideWidth - summaryMinimum - pathMinimum);
        const int summaryWidth = summaryMinimum + std::min(std::max(0, desiredSummary - summaryMinimum), spare / 2);
        const int pathWidth = std::max(pathMinimum, viewport()->width() - sideWidth - summaryWidth);
        setColumnWidth(0, sideWidth); setColumnWidth(1, summaryWidth); setColumnWidth(2, pathWidth);
        horizontalHeader()->setMinimumHeight(std::max(horizontalHeader()->sizeHint().height(), metrics.lineSpacing() + cellPadding));
        int height = std::max(horizontalHeader()->height(), horizontalHeader()->minimumHeight()) + frameWidth() * 2;
        for (int row = 0; row < rowCount(); ++row) {
            int rowHeight = metrics.lineSpacing() + cellPadding;
            for (int column = 0; column < columnCount(); ++column) if (auto* value = item(row, column))
                rowHeight = std::max(rowHeight, text_->textHeight(value->text(), std::max(1, columnWidth(column) - padding), font()) + cellPadding);
            setRowHeight(row, rowHeight); height += rowHeight;
        }
        if (sideWidth + summaryWidth + pathWidth > viewport()->width()) height += style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
        if (minimumHeight() != height) { setMinimumHeight(height); updateGeometry(); }
        fitting_ = false;
    }
    QSize sizeHint() const override { return QSize(480, minimumHeight()); }
    QSize minimumSizeHint() const override { return QSize(0, minimumHeight()); }
protected:
    bool viewportEvent(QEvent* event) override {
        const bool result = QTableWidget::viewportEvent(event);
        if (event->type() == QEvent::Resize && !queued_) {
            queued_ = true; QTimer::singleShot(0, this, [this] { queued_ = false; fitContents(); });
        }
        return result;
    }
    void resizeEvent(QResizeEvent* event) override { QTableWidget::resizeEvent(event); fitContents(); }
    void showEvent(QShowEvent* event) override { QTableWidget::showEvent(event); fitContents(); }
    void changeEvent(QEvent* event) override {
        QTableWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            QTimer::singleShot(0, this, [this] { fitContents(); });
    }
private:
    int headerWidth(int column) const {
        QStyleOptionHeader option; option.initFrom(horizontalHeader()); option.section = column;
        option.text = horizontalHeaderItem(column) ? horizontalHeaderItem(column)->text() : QString();
        option.fontMetrics = QFontMetrics(horizontalHeader()->font());
        const QSize text(std::max(option.fontMetrics.horizontalAdvance(option.text), option.fontMetrics.boundingRect(option.text).width()),
            option.fontMetrics.lineSpacing());
        return horizontalHeader()->style()->sizeFromContents(QStyle::CT_HeaderSection, &option, text, horizontalHeader()).width();
    }
    StorageTextDelegate* text_ = nullptr;
    bool fitting_ = false, queued_ = false;
};

class StorageConfirmation final : public QMessageBox {
public:
    StorageConfirmation(const QString& text, QWidget* parent)
        : QMessageBox(QMessageBox::Warning, QString::fromUtf8("Заменить storage.json"), text, QMessageBox::Yes | QMessageBox::Cancel, parent) {
        setTextFormat(Qt::PlainText); setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, false);
        auto* grid = qobject_cast<QGridLayout*>(layout()); auto* label = findChild<QLabel*>(QStringLiteral("qt_msgbox_label"));
        if (grid && label) {
            const int index = grid->indexOf(label); int row, column, rows, columns;
            grid->getItemPosition(index, &row, &column, &rows, &columns);
            grid->removeWidget(label); label->hide(); label->setMaximumSize(0, 0);
            auto* body = new QPlainTextEdit(this); body->setObjectName("conflictConfirmBody");
            body->setPlainText(text); body->setReadOnly(true); body->setTabChangesFocus(true);
            body->setLineWrapMode(QPlainTextEdit::WidgetWidth); body->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            body->setFrameShape(QFrame::NoFrame); body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
            body->setMinimumSize(0, 0); body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            body->setAccessibleName(QString::fromUtf8("Источник, заменяемый кошелёк и резервные копии")); body->setAccessibleDescription(text);
            grid->addWidget(body, row, column, rows, columns); grid->setRowStretch(row, 1);
            body->ensurePolished();
            const auto margins = body->contentsMargins();
            body->setMinimumHeight(body->fontMetrics().lineSpacing() * 3 + int(std::ceil(body->document()->documentMargin() * 2))
                + body->frameWidth() * 2 + margins.top() + margins.bottom());
        }
        if (layout()) layout()->setSizeConstraint(QLayout::SetNoConstraint);
    }
protected:
    void showEvent(QShowEvent* event) override {
        QMessageBox::showEvent(event);
        if (layout()) layout()->setSizeConstraint(QLayout::SetNoConstraint);
        const auto* currentScreen = screen() ? screen() : QGuiApplication::primaryScreen();
        if (!currentScreen) return;
        const auto available = currentScreen->availableGeometry();
        const QSize extra(std::max(0, frameGeometry().width() - width()), std::max(0, frameGeometry().height() - height()));
        const QSize limit(std::max(1, available.width() - extra.width() - 16), std::max(1, available.height() - extra.height() - 16));
        setMinimumSize(QSize(420, 260).boundedTo(limit)); setMaximumSize(limit); resize(QSize(640, 520).boundedTo(limit));
        const auto frame = frameGeometry();
        const int left = std::clamp(frame.left(), available.left(), std::max(available.left(), available.right() - frame.width() + 1));
        const int top = std::clamp(frame.top(), available.top(), std::max(available.top(), available.bottom() - frame.height() + 1));
        move(pos() + QPoint(left - frame.left(), top - frame.top()));
    }
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
            auto* button = qobject_cast<QPushButton*>(focusWidget());
            if (button && button->isEnabled() && button->isVisible()) { event->accept(); button->click(); return; }
        }
        QMessageBox::keyPressEvent(event);
    }
};
}

bool HasQtStorageConflict(const fs::path& workspace) {
    const auto config = LoadCloudSyncConfig(workspace); const auto root = ResolveCloudRootPath(config, workspace);
    if (!config.enabled || overlap(root, workspace)) return false;
    QString error; const auto local = read(workspace / "meta/storage.json", error); if (!error.isEmpty()) return false;
    error.clear(); const auto cloud = read(root / "meta/storage.json", error); return error.isEmpty() && local != cloud;
}

QtStorageConflictResult ResolveQtStorageConflict(const fs::path& workspace, bool preferCloud) {
    QtStorageConflictResult result;
    AppWorkspaceStorageWriteLock writeLock(workspace);
    if (!writeLock.acquired()) { result.message = u8"Рабочая папка занята другой операцией записи."; return result; }
    const auto config = LoadCloudSyncConfig(workspace); const auto root = ResolveCloudRootPath(config, workspace);
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
    QtScrollableDialog dialog(parent, QSize(640, 520)); dialog.setObjectName("storageConflictResolver");
    dialog.setWindowTitle(QString::fromUtf8("Конфликт storage.json"));
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Сравнение локального и облачного кошелька"));
    auto* form = dialog.formLayout(); form->setFormAlignment(Qt::AlignTop); form->setVerticalSpacing(dialog.scaledMetric(8));
    auto* warning = new QLabel(QString::fromUtf8("Выберите целую версию кошелька: принять облачную локально или отправить локальную в облако. Баланс и журнал не объединяются. Обе исходные версии будут сохранены локально."));
    warning->setTextFormat(Qt::PlainText); warning->setWordWrap(true); warning->setProperty("warning", true); form->addRow(warning);
    auto* table = new StorageComparisonTable; table->setObjectName("storageComparison"); table->setHorizontalHeaderLabels({QString::fromUtf8("Версия"), QString::fromUtf8("Сводка"), QString::fromUtf8("Путь")});
    table->setAccessibleName(QString::fromUtf8("Сравнение локального и облачного кошелька"));
    table->setAccessibleDescription(QString::fromUtf8("Показывает сторону, сводку баланса и журнала и путь. Выберите целую версию кнопками ниже."));
    table->verticalHeader()->hide(); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionMode(QAbstractItemView::NoSelection); table->setShowGrid(false); table->setAlternatingRowColors(true);
    table->setItem(0, 0, new QTableWidgetItem(QString::fromUtf8("Локальная"))); table->setItem(0, 1, new QTableWidgetItem(summary(local))); table->setItem(0, 2, new QTableWidgetItem(q(localPath)));
    table->setItem(1, 0, new QTableWidgetItem(QString::fromUtf8("Облачная"))); table->setItem(1, 1, new QTableWidgetItem(summary(cloud))); table->setItem(1, 2, new QTableWidgetItem(q(cloudPath)));
    for (int row = 0; row < table->rowCount(); ++row) for (int column = 0; column < table->columnCount(); ++column) {
        auto* item = table->item(row, column); item->setToolTip(Qt::convertFromPlainText(item->text())); item->setData(Qt::AccessibleTextRole, item->text());
    }
    form->addRow(table); table->fitContents();
    auto* actions = new QtDialogFlowRow(dialog.bodyWidget(), dialog.scaledMetric(8), dialog.scaledMetric(4));
    actions->setObjectName("storageConflictActions");
    auto* acceptCloud = new QPushButton(QString::fromUtf8("Принять облачную")); acceptCloud->setObjectName("acceptCloudStorage");
    acceptCloud->setProperty("primary", true); acceptCloud->setAutoDefault(false);
    acceptCloud->setAccessibleName(QString::fromUtf8("Заменить локальный кошелёк облачной версией"));
    acceptCloud->setToolTip(QString::fromUtf8("Баланс, журнал и правила наград локальной стороны будут заменены целиком после подтверждения.")); actions->addWidget(acceptCloud);
    auto* keepLocal = new QPushButton(QString::fromUtf8("Отправить локальную")); keepLocal->setObjectName("keepLocalStorage");
    keepLocal->setAutoDefault(false);
    keepLocal->setAccessibleName(QString::fromUtf8("Заменить облачный кошелёк локальной версией"));
    keepLocal->setToolTip(QString::fromUtf8("Баланс, журнал и правила наград облачной стороны будут заменены целиком после подтверждения."));
    actions->addWidget(keepLocal); form->addRow(actions);
    for (auto* button : {acceptCloud, keepLocal}) {
        button->ensurePolished(); button->setMinimumHeight(std::max(dialog.scaledMetric(40), button->sizeHint().height()));
    }
    auto* notice = new QLabel(QString::fromUtf8("Баланс и журнал не объединяются. Закрытие не изменяет файлы."));
    notice->setObjectName("storageConflictNotice"); notice->setTextFormat(Qt::PlainText); notice->setWordWrap(true);
    notice->setAccessibleName(QString::fromUtf8("Последствия решения и безопасное закрытие")); dialog.footerLayout()->addWidget(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); buttons->setObjectName("storageConflictButtons");
    auto* close = buttons->button(QDialogButtonBox::Close); close->setObjectName("storageConflictClose");
    close->setText(QString::fromUtf8("Закрыть")); close->setAccessibleName(QString::fromUtf8("Закрыть конфликт кошелька без изменений"));
    close->setAutoDefault(true); close->setDefault(true); dialog.footerLayout()->addWidget(buttons);
    close->ensurePolished(); close->setMinimumHeight(std::max(dialog.scaledMetric(40), close->sizeHint().height()));
    QWidget::setTabOrder(table, acceptCloud); QWidget::setTabOrder(acceptCloud, keepLocal); QWidget::setTabOrder(keepLocal, close); close->setFocus();
    bool changed = false;
    auto run = [&](bool preferCloud) {
        const auto source = preferCloud ? cloudPath : localPath; const auto target = preferCloud ? localPath : cloudPath;
        const auto sourceSummary = preferCloud ? summary(cloud) : summary(local); const auto targetSummary = preferCloud ? summary(local) : summary(cloud);
        StorageConfirmation confirm(QString::fromUtf8("Будет полностью заменён баланс, журнал и правила наград выбранной стороны. Продолжить?\n\nИсточник\n%1\n%2\n\nБудет заменено\n%3\n%4\n\nОбе исходные версии будут сохранены локально в meta/updates.")
            .arg(sourceSummary, q(source), targetSummary, q(target)), &dialog);
        confirm.setObjectName("storageConflictConfirm"); confirm.setDefaultButton(QMessageBox::Cancel); confirm.button(QMessageBox::Yes)->setText(preferCloud ? QString::fromUtf8("Принять") : QString::fromUtf8("Отправить")); confirm.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
        for (const auto role : {QMessageBox::Yes, QMessageBox::Cancel}) {
            auto* button = qobject_cast<QPushButton*>(confirm.button(role));
            button->setAutoDefault(role == QMessageBox::Cancel); button->setAccessibleName(button->text());
            button->ensurePolished(); button->setMinimumHeight(std::max(dialog.scaledMetric(40), button->sizeHint().height()));
        }
        if (confirm.exec() != QMessageBox::Yes) return;
        const auto result = ResolveQtStorageConflict(workspace, preferCloud);
        if (!result.ok) { QMessageBox::warning(&dialog, QString::fromUtf8("Конфликт storage.json"), q(result.message)); return; }
        changed = result.changed; if (localChanged) *localChanged = preferCloud && result.changed; dialog.accept();
    };
    QObject::connect(acceptCloud, &QPushButton::clicked, &dialog, [&] { run(true); }); QObject::connect(keepLocal, &QPushButton::clicked, &dialog, [&] { run(false); }); QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec(); return changed;
}
