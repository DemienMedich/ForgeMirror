#include "QtCloudConflict.h"
#include "AppWorkspaceStorageLock.h"
#include "AppWorkspaceDataService.h"
#include "CloudSync.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
QString q(const std::filesystem::path& path) { return QString::fromStdWString(path.wstring()); }
QString q(const std::string& text) { return QString::fromUtf8(text); }
bool supported(const std::string& path) {
    return path == "meta/tasks.json" || path == "meta/pipeline.json" || path == "meta/projects.json" ||
        path == "meta/banner.json" || path == "meta/gameplay.ini" || path == "meta/professions.txt" || path == "skills.txt";
}
QString objectName(const std::string& relative, const char* kind) {
    const auto stem = relative == "meta/tasks.json" ? QStringLiteral("tasks")
        : relative == "meta/pipeline.json" ? QStringLiteral("pipeline")
        : relative == "meta/projects.json" ? QStringLiteral("projects")
        : relative == "meta/banner.json" ? QStringLiteral("banner")
        : relative == "meta/gameplay.ini" ? QStringLiteral("gameplay")
        : relative == "skills.txt" ? QStringLiteral("skills") : QStringLiteral("professions");
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

bool validSkillCatalog(const QByteArray& bytes, qsizetype* count, QString& error) {
    QString text;
    if (!decodeUtf8(bytes, text, error, QString::fromUtf8("Каталог навыков"))) return false;
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
        if (fields.size() >= 4 && fields[0].trimmed().isEmpty()) {
            error = QString::fromUtf8("Строка каталога навыков с ID должна содержать ID.");
            return false;
        }
        const auto name = (fields.size() >= 4 ? fields[1] : fields[0]).trimmed();
        if (name.isEmpty()) {
            error = QString::fromUtf8("Строка каталога навыков должна содержать название.");
            return false;
        }
        for (const auto& field : fields) {
            for (const auto ch : field) {
                if (ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) {
                    error = QString::fromUtf8("Каталог навыков содержит управляющий символ.");
                    return false;
                }
            }
        }
        ++entries;
    }
    if (entries == 0) {
        error = QString::fromUtf8("Каталог навыков не содержит записей.");
        return false;
    }
    if (count) *count = entries;
    return true;
}

bool validSource(const QByteArray& bytes, const std::string& relative, QString& error) {
    if (relative == "meta/gameplay.ini") return validGameplayConfig(bytes, error);
    if (relative == "meta/professions.txt") return validProfessionCatalog(bytes, nullptr, error);
    if (relative == "skills.txt") return validSkillCatalog(bytes, nullptr, error);
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
        : relative == "meta/gameplay.ini" ? "meta_gameplay_ini"
        : relative == "skills.txt" ? "skills_txt" : "meta_professions_txt";
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
        if (relative == "skills.txt") {
            QString error;
            qsizetype count = 0;
            const auto bytes = readFile(path, error);
            if (!error.isEmpty() || !validSkillCatalog(bytes, &count, error))
                return QString::fromUtf8("некорректный каталог · %1 байт").arg(std::filesystem::file_size(path, ec));
            return QString::fromUtf8("%1 навыков · %2 байт").arg(count).arg(bytes.size());
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
        : relative == "meta/gameplay.ini" ? "Правила XP"
        : relative == "skills.txt" ? "Навыки" : "Профессии");
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

// The same line layout measures and paints cells, including unbroken paths.
class DecisionTextDelegate final : public QStyledItemDelegate {
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
        const int padding = decisionMetric(table, 16);
        return QSize(table->columnWidth(index.column()), textHeight(content.text,
            std::max(1, table->columnWidth(index.column()) - padding), content.font) + decisionMetric(table, 8));
    }
    QSize commandCellSize(QWidget* command, const QModelIndex& index) const {
        const auto* table = qobject_cast<QTableWidget*>(parent());
        const QSize wanted = command->sizeHint().expandedTo(command->minimumSize());
        QStyleOptionViewItem option; option.initFrom(table); option.widget = table;
        option.font = table->font(); option.fontMetrics = table->fontMetrics(); option.rect = QRect(QPoint(), wanted);
        const QRect contents = editorContents(option, index, command);
        // Index widgets are positioned in SE_ItemViewItemText, not the full
        // cell. Reserve its actual QSS/style insets, not a guessed cell gap.
        return wanted + QSize(std::max(0, option.rect.width() - contents.width()),
            std::max(0, option.rect.height() - contents.height()));
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto* table = qobject_cast<QTableWidget*>(parent());
        if (!table || table->cellWidget(index.row(), index.column()) != editor) {
            QStyledItemDelegate::updateEditorGeometry(editor, option, index); return;
        }
        const QRect contents = editorContents(option, index, editor);
        const int wantedHeight = editor->sizeHint().expandedTo(editor->minimumSize()).height();
        const int height = std::min(contents.height(), wantedHeight);
        editor->setGeometry(QRect(contents.left(), contents.top() + (contents.height() - height) / 2,
            contents.width(), height));
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
private:
    QRect editorContents(const QStyleOptionViewItem& option, const QModelIndex& index, const QWidget* editor) const {
        QStyleOptionViewItem content(option); initStyleOption(&content, index);
        content.showDecorationSelected = editor->style()->styleHint(QStyle::SH_ItemView_ShowDecorationSelected, nullptr, editor);
        auto* style = content.widget ? content.widget->style() : QApplication::style();
        return style->subElementRect(QStyle::SE_ItemViewItemText, &content, content.widget);
    }
};

class DecisionTable final : public QTableWidget {
public:
    explicit DecisionTable(bool backups, QWidget* parent = nullptr)
        : QTableWidget(parent), backups_(backups), text_(new DecisionTextDelegate(this)) {
        setItemDelegate(text_); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setWordWrap(true); setTextElideMode(Qt::ElideNone);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        horizontalHeader()->setStretchLastSection(false);
        horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        setMinimumWidth(0);
    }
    void fitContents() {
        if (fitting_ || !columnCount()) return;
        fitting_ = true; ensurePolished();
        const QFontMetrics metrics(font()); const int padding = decisionMetric(this, 16);
        const int cellPadding = decisionMetric(this, 8);
        const auto fullWidth = [&](int column) {
            int wanted = 0;
            for (int row = 0; row < rowCount(); ++row) if (auto* value = item(row, column))
                for (const auto& line : value->text().split(QLatin1Char('\n')))
                    wanted = std::max(wanted, std::max(metrics.horizontalAdvance(line), metrics.boundingRect(line).width()));
            return std::max(headerWidth(column), wanted + padding);
        };
        const auto wordWidth = [&](int column) {
            int wanted = 0;
            for (int row = 0; row < rowCount(); ++row) if (auto* value = item(row, column))
                for (const auto& word : value->text().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
                    wanted = std::max(wanted, std::max(metrics.horizontalAdvance(word), metrics.boundingRect(word).width()));
            return std::max(headerWidth(column), wanted + padding);
        };
        std::vector<int> widths(static_cast<size_t>(columnCount()), 0);
        for (int column = 0; column < columnCount(); ++column) widths[size_t(column)] = headerWidth(column);
        if (backups_) {
            for (int row = 0; row < rowCount(); ++row) if (auto* button = cellWidget(row, 3)) {
                button->ensurePolished(); widths[3] = std::max(widths[3], text_->commandCellSize(button, model()->index(row, 3)).width());
            }
            // Headers and actual commands are the only hard minima. Prefer the
            // complete date/source when spare space permits; summary wraps.
            int spare = std::max(0, viewport()->width() - widths[0] - widths[1] - widths[2] - widths[3]);
            for (int column : {0, 1}) {
                const int extra = std::min(spare, std::max(0, fullWidth(column) - widths[size_t(column)]));
                widths[size_t(column)] += extra; spare -= extra;
            }
            widths[2] += spare;
        } else {
            const int available = viewport()->width();
            widths[0] = std::max(widths[0], std::min(fullWidth(0), available - widths[1] - widths[2]));
            const int spare = std::max(0, available - widths[0] - widths[1] - widths[2]);
            // Short real summary words get useful space, while at least half
            // the flexible width remains available for the full wrapped path.
            widths[1] += std::min(std::max(0, wordWidth(1) - widths[1]), spare / 2);
            widths[2] = std::max(widths[2], available - widths[0] - widths[1]);
        }
        int totalWidth = 0;
        for (int column = 0; column < columnCount(); ++column) { setColumnWidth(column, widths[size_t(column)]); totalWidth += widths[size_t(column)]; }
        horizontalHeader()->setMinimumHeight(std::max(horizontalHeader()->sizeHint().height(), metrics.lineSpacing() + cellPadding));
        int height = std::max(horizontalHeader()->height(), horizontalHeader()->minimumHeight()) + frameWidth() * 2;
        for (int row = 0; row < rowCount(); ++row) {
            int rowHeight = metrics.lineSpacing() + cellPadding;
            for (int column = 0; column < columnCount(); ++column) {
                if (auto* value = item(row, column)) rowHeight = std::max(rowHeight,
                    text_->textHeight(value->text(), std::max(1, columnWidth(column) - padding), font()) + cellPadding);
                if (auto* widget = cellWidget(row, column)) rowHeight = std::max(rowHeight,
                    text_->commandCellSize(widget, model()->index(row, column)).height());
            }
            setRowHeight(row, rowHeight); height += rowHeight;
        }
        if (totalWidth > viewport()->width()) height += style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
        if (minimumHeight() != height) { setMinimumHeight(height); updateGeometry(); }
        fitting_ = false;
    }
    QSize sizeHint() const override { return QSize(480, minimumHeight()); }
    QSize minimumSizeHint() const override { return QSize(0, minimumHeight()); }
protected:
    bool viewportEvent(QEvent* event) override {
        const bool result = QTableWidget::viewportEvent(event);
        if (event->type() == QEvent::Resize && !queued_) {
            queued_ = true;
            QTimer::singleShot(0, this, [this] { queued_ = false; fitContents(); });
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
    bool backups_ = false;
    bool fitting_ = false;
    bool queued_ = false;
    DecisionTextDelegate* text_ = nullptr;
};

void describeDecisionItems(QTableWidget* table) {
    for (int row = 0; row < table->rowCount(); ++row) for (int column = 0; column < table->columnCount(); ++column)
        if (auto* item = table->item(row, column)) {
            item->setToolTip(Qt::convertFromPlainText(item->text())); item->setData(Qt::AccessibleTextRole, item->text());
        }
}

// Keep QMessageBox identity and its result/default contract. Only its long
// source/target label becomes a scrollable plain-text body, not hidden details.
class DecisionConfirmation final : public QMessageBox {
public:
    DecisionConfirmation(Icon icon, const QString& title, const QString& text, StandardButtons buttons, QWidget* parent)
        : QMessageBox(icon, title, text, buttons, parent) {
        setTextFormat(Qt::PlainText);
        setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, false);
        auto* grid = qobject_cast<QGridLayout*>(layout());
        auto* label = findChild<QLabel*>(QStringLiteral("qt_msgbox_label"));
        if (grid && label) {
            const int index = grid->indexOf(label); int row, column, rows, columns;
            grid->getItemPosition(index, &row, &column, &rows, &columns);
            grid->removeWidget(label); label->hide(); label->setMaximumSize(0, 0);
            auto* body = new QPlainTextEdit(this); body->setObjectName("conflictConfirmBody");
            body->setPlainText(text); body->setReadOnly(true); body->setTabChangesFocus(true);
            body->setLineWrapMode(QPlainTextEdit::WidgetWidth); body->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            body->setFrameShape(QFrame::NoFrame); body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
            body->setMinimumSize(0, 0); body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            body->setAccessibleName(QString::fromUtf8("Источник, заменяемая версия и резервная копия"));
            body->setAccessibleDescription(text);
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

bool confirm(QWidget* parent, const QString& title, const QString& source, const QString& target, const QString& action) {
    DecisionConfirmation box(QMessageBox::Warning, title,
        QString::fromUtf8("Источник\n%1\n\nБудет заменено\n%2\n\nТекущая заменяемая версия сначала сохранится в локальном meta/updates.")
            .arg(source, target), QMessageBox::Yes | QMessageBox::Cancel, parent);
    box.setObjectName("cloudConflictConfirm");
    box.setDefaultButton(QMessageBox::Cancel);
    box.button(QMessageBox::Yes)->setText(action);
    box.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    for (const auto role : {QMessageBox::Yes, QMessageBox::Cancel}) {
        auto* button = qobject_cast<QPushButton*>(box.button(role));
        button->setAutoDefault(role == QMessageBox::Cancel);
        button->setAccessibleName(button->text());
        button->ensurePolished();
        button->setMinimumHeight(std::max(decisionMetric(&box, 40), button->sizeHint().height()));
    }
    return box.exec() == QMessageBox::Yes;
}
}

QtCloudConflictResult ApplyQtCloudWorkspaceFile(const std::filesystem::path& workspace,
                                                const std::filesystem::path& source,
                                                const std::string& relative,
                                                const std::string& sourceKind) {
    QtCloudConflictResult result;
    AppWorkspaceStorageWriteLock writeLock(workspace);
    if (!writeLock.acquired()) {
        result.message = u8"Рабочая папка занята другой операцией записи.";
        return result;
    }
    if (sourceKind == "restore" && (relative == "skills.txt" || relative == "meta/professions.txt")) {
        result.message = u8"Профессии и навыки можно восстановить только парой.";
        return result;
    }
    if (sourceKind == "cloud" && (relative == "skills.txt" || relative == "meta/professions.txt")) {
        const auto config = LoadCloudSyncConfig(workspace);
        const auto cloudRoot = ResolveCloudRootPath(config, workspace);
        const auto expected = cloudRoot / std::filesystem::u8path(relative);
        if (!config.enabled || !samePath(source, expected)) {
            result.message = u8"Облачный источник не соответствует настроенному корню."; return result;
        }
        return ApplyQtCloudCatalogPair(workspace, cloudRoot);
    }
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
    AppWorkspaceStorageWriteLock writeLock(workspace);
    if (!writeLock.acquired()) {
        QtCloudConflictResult result;
        result.message = u8"Рабочая папка занята другой операцией записи.";
        return result;
    }
    if (relative == "skills.txt" || relative == "meta/professions.txt")
        return PushQtCloudCatalogPair(workspace);
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

namespace {
struct CatalogPairFile {
    std::string relative;
    std::filesystem::path local;
    std::filesystem::path cloud;
    QByteArray localBytes;
    QByteArray cloudBytes;
    bool cloudExists = false;
    bool localExists = false;
};

bool prepareCatalogPair(const std::filesystem::path& workspace, const std::filesystem::path& root,
                        std::array<CatalogPairFile, 2>& files, QString& error) {
    const std::array<std::string, 2> relatives{"skills.txt", "meta/professions.txt"};
    for (size_t i = 0; i < files.size(); ++i) {
        auto& file = files[i];
        file.relative = relatives[i];
        file.local = workspace / std::filesystem::u8path(file.relative);
        file.cloud = root / std::filesystem::u8path(file.relative);
        file.localBytes = readFile(file.local, error);
        if (!error.isEmpty() || !validSource(file.localBytes, file.relative, error)) return false;
        file.localExists = std::filesystem::exists(file.local);
        file.cloudExists = std::filesystem::exists(file.cloud);
        if (file.cloudExists) {
            file.cloudBytes = readFile(file.cloud, error);
            if (!error.isEmpty() || !validSource(file.cloudBytes, file.relative, error)) return false;
        } else if (!safePath(file.cloud.parent_path())) {
            error = QString::fromUtf8("Запись через ссылку запрещена.");
            return false;
        }
    }
    return true;
}

void addPairBackup(QtCloudConflictResult& result, const std::filesystem::path& path) {
    result.backupPaths.push_back(path);
    if (result.backupPath.empty()) result.backupPath = path;
}

bool rollbackPairWrite(const CatalogPairFile& file, bool toCloud, QString& error) {
    const auto& target = toCloud ? file.cloud : file.local;
    const bool existed = toCloud ? file.cloudExists : file.localExists;
    const auto& bytes = toCloud ? file.cloudBytes : file.localBytes;
    if (existed) return atomicWrite(target, bytes, error);
    std::error_code ec;
    std::filesystem::remove(target, ec);
    if (ec) {
        error = QString::fromUtf8("Не удалось удалить частично созданный файл при откате.");
        return false;
    }
    return true;
}

QtCloudConflictResult transferCatalogPair(const std::filesystem::path& workspace,
                                          const std::filesystem::path& root, bool toCloud) {
    QtCloudConflictResult result;
    if (!std::filesystem::is_directory(root) || pathsOverlap(root, workspace)) {
        result.message = u8"Облачный корень недоступен или пересекается с рабочей папкой."; return result;
    }
    std::array<CatalogPairFile, 2> files;
    QString error;
    if (!prepareCatalogPair(workspace, root, files, error)) {
        result.message = error.toUtf8().toStdString(); return result;
    }
    bool differs = false;
    for (const auto& file : files) {
        const bool targetExists = toCloud ? file.cloudExists : file.localExists;
        const auto& targetBytes = toCloud ? file.cloudBytes : file.localBytes;
        const auto& sourceBytes = toCloud ? file.localBytes : file.cloudBytes;
        if (!targetExists || targetBytes != sourceBytes) differs = true;
    }
    if (!differs) {
        result.ok = true;
        result.message = u8"Каталоги профессий и навыков уже совпадают.";
        return result;
    }

    // Save both source sets and both replaced targets before changing either catalog.
    auto pairStamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto backupDir = workspace / "meta/updates";
    const auto hasPairCollision = [&](std::int64_t stamp) {
        for (const auto& file : files) {
            const auto stem = file.relative == "skills.txt" ? std::string("skills_txt") : std::string("meta_professions_txt");
            const auto ext = std::filesystem::u8path(file.relative).extension().string();
            for (const auto* kind : {toCloud ? "local" : "cloud", toCloud ? "cloud" : "local"}) {
                if ((toCloud ? file.cloudExists : file.localExists) || std::string(kind) == (toCloud ? "local" : "cloud")) {
                    if (std::filesystem::exists(backupDir / (stem + "." + kind + "." + std::to_string(stamp) + ext))) return true;
                }
            }
        }
        return false;
    };
    while (hasPairCollision(pairStamp)) ++pairStamp;
    for (const auto& file : files) {
        const auto& sourceBytes = toCloud ? file.localBytes : file.cloudBytes;
        const auto sourceKind = toCloud ? "local" : "cloud";
        const std::string stem = file.relative == "skills.txt" ? "skills_txt" : "meta_professions_txt";
        const auto sourceBackup = backupDir / (stem + "." + sourceKind + "." + std::to_string(pairStamp) + std::filesystem::u8path(file.relative).extension().string());
        if (!atomicWrite(sourceBackup, sourceBytes, error)) {
            result.message = error.toUtf8().toStdString(); return result;
        }
        addPairBackup(result, sourceBackup);
        const bool targetExists = toCloud ? file.cloudExists : file.localExists;
        if (targetExists) {
            const auto& targetBytes = toCloud ? file.cloudBytes : file.localBytes;
            const std::string replacedKind = toCloud ? "cloud" : "local";
            const auto replacedBackup = backupDir / (stem + "." + replacedKind + "." + std::to_string(pairStamp) + std::filesystem::u8path(file.relative).extension().string());
            if (!atomicWrite(replacedBackup, targetBytes, error)) {
                result.message = error.toUtf8().toStdString(); return result;
            }
            addPairBackup(result, replacedBackup);
            result.backupPath = replacedBackup;
        }
    }

    // Reject a stale comparison across either half before beginning the pair write.
    for (const auto& file : files) {
        const auto& source = toCloud ? file.local : file.cloud;
        const auto& expected = toCloud ? file.localBytes : file.cloudBytes;
        if (std::filesystem::exists(source)) {
            const auto checked = readFile(source, error);
            if (!error.isEmpty() || checked != expected) {
                result.message = u8"Один из каталогов изменился после сравнения. Откройте его заново."; return result;
            }
        } else {
            result.message = u8"Один из каталогов изменился после сравнения. Откройте его заново."; return result;
        }
        const auto& target = toCloud ? file.cloud : file.local;
        const bool existed = toCloud ? file.cloudExists : file.localExists;
        if (std::filesystem::exists(target) != existed) {
            result.message = u8"Один из целевых каталогов изменился после сравнения. Откройте его заново."; return result;
        }
        if (existed) {
            const auto checked = readFile(target, error);
            const auto& expectedTarget = toCloud ? file.cloudBytes : file.localBytes;
            if (!error.isEmpty() || checked != expectedTarget) {
                result.message = u8"Один из целевых каталогов изменился после сравнения. Откройте его заново."; return result;
            }
        }
    }

    std::vector<size_t> written;
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        const auto& target = toCloud ? file.cloud : file.local;
        const auto& bytes = toCloud ? file.localBytes : file.cloudBytes;
        const bool existed = toCloud ? file.cloudExists : file.localExists;
        const auto& previous = toCloud ? file.cloudBytes : file.localBytes;
        if (existed && previous == bytes) continue;
        if (!atomicWrite(target, bytes, error)) break;
        written.push_back(i);
    }
    if (written.size() != size_t(std::count_if(files.begin(), files.end(), [&](const auto& file) {
            const bool existed = toCloud ? file.cloudExists : file.localExists;
            const auto& previous = toCloud ? file.cloudBytes : file.localBytes;
            const auto& replacement = toCloud ? file.localBytes : file.cloudBytes;
            return !existed || previous != replacement;
        }))) {
        QString rollbackError;
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            const auto index = *it;
            const auto& file = files[index];
            QString oneError;
            if (!rollbackPairWrite(file, toCloud, oneError) && rollbackError.isEmpty()) rollbackError = oneError;
        }
        result.message = error.toUtf8().toStdString();
        if (!rollbackError.isEmpty()) result.message += std::string(" ") + rollbackError.toUtf8().toStdString();
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.message = toCloud ? u8"Оба каталога отправлены в облако." : u8"Оба облачных каталога применены.";
    return result;
}
}

QtCloudConflictResult ApplyQtCloudCatalogPair(const std::filesystem::path& workspace,
                                              const std::filesystem::path& cloudRoot) {
    AppWorkspaceStorageWriteLock writeLock(workspace);
    if (!writeLock.acquired()) {
        QtCloudConflictResult result;
        result.message = u8"Рабочая папка занята другой операцией записи.";
        return result;
    }
    const auto config = LoadCloudSyncConfig(workspace);
    if (!config.enabled || !samePath(cloudRoot, ResolveCloudRootPath(config, workspace))) {
        QtCloudConflictResult result;
        result.message = u8"Облачный корень не соответствует настройкам.";
        return result;
    }
    return transferCatalogPair(workspace, cloudRoot, false);
}

QtCloudConflictResult PushQtCloudCatalogPair(const std::filesystem::path& workspace) {
    AppWorkspaceStorageWriteLock writeLock(workspace);
    if (!writeLock.acquired()) {
        QtCloudConflictResult result;
        result.message = u8"Рабочая папка занята другой операцией записи.";
        return result;
    }
    const auto config = LoadCloudSyncConfig(workspace);
    if (!config.enabled) {
        QtCloudConflictResult result;
        result.message = u8"Облачная папка не настроена.";
        return result;
    }
    return transferCatalogPair(workspace, ResolveCloudRootPath(config, workspace), true);
}

QtCloudConflictResult RestoreQtCloudCatalogPair(const std::filesystem::path& workspace,
                                                const std::filesystem::path& skillsBackup,
                                                const std::filesystem::path& professionsBackup) {
    QtCloudConflictResult result;
    AppWorkspaceStorageWriteLock writeLock(workspace);
    if (!writeLock.acquired()) {
        result.message = u8"Рабочая папка занята другой операцией записи.";
        return result;
    }
    const auto skills = ListCloudWorkspaceBackups(workspace, "skills.txt");
    const auto professions = ListCloudWorkspaceBackups(workspace, "meta/professions.txt");
    if (std::none_of(skills.begin(), skills.end(), [&](const auto& item) { return samePath(item.path, skillsBackup); }) ||
        std::none_of(professions.begin(), professions.end(), [&](const auto& item) { return samePath(item.path, professionsBackup); })) {
        result.message = u8"Оба снимка должны принадлежать спискам резервных копий."; return result;
    }
    QString error;
    std::array<CatalogPairFile, 2> files;
    const auto skillBytes = readFile(skillsBackup, error);
    if (!error.isEmpty() || !validSkillCatalog(skillBytes, nullptr, error)) { result.message = error.toUtf8().toStdString(); return result; }
    const auto professionBytes = readFile(professionsBackup, error);
    if (!error.isEmpty() || !validProfessionCatalog(professionBytes, nullptr, error)) { result.message = error.toUtf8().toStdString(); return result; }
    files[0].relative = "skills.txt"; files[0].local = workspace / "skills.txt"; files[0].localExists = std::filesystem::exists(files[0].local);
    files[0].localBytes = files[0].localExists ? readFile(files[0].local, error) : QByteArray();
    files[1].relative = "meta/professions.txt"; files[1].local = workspace / "meta/professions.txt"; files[1].localExists = std::filesystem::exists(files[1].local);
    files[1].localBytes = files[1].localExists ? readFile(files[1].local, error) : QByteArray();
    if (!error.isEmpty()) { result.message = error.toUtf8().toStdString(); return result; }
    const std::array<QByteArray, 2> sources{skillBytes, professionBytes};
    std::array<bool, 2> needsWrite{};
    for (size_t i = 0; i < files.size(); ++i) {
        auto& file = files[i];
        needsWrite[i] = !file.localExists || file.localBytes != sources[i];
        if (needsWrite[i]) {
            const auto currentBackup = backupPath(workspace, file.relative, "restore");
            if (file.localExists && !atomicWrite(currentBackup, file.localBytes, error)) {
                result.message = error.toUtf8().toStdString(); return result;
            }
            if (file.localExists) addPairBackup(result, currentBackup);
        }
    }
    if (!needsWrite[0] && !needsWrite[1]) {
        result.ok = true; result.message = u8"Каталоги уже совпадают с выбранной резервной парой."; return result;
    }
    std::vector<size_t> written;
    for (size_t i = 0; i < files.size(); ++i) {
        if (!needsWrite[i]) continue;
        if (!atomicWrite(files[i].local, sources[i], error)) break;
        written.push_back(i);
    }
    if (written.size() != size_t(std::count(needsWrite.begin(), needsWrite.end(), true))) {
        QString rollbackError;
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            const auto index = *it;
            QString oneError;
            if (!rollbackPairWrite(files[index], false, oneError) && rollbackError.isEmpty()) rollbackError = oneError;
        }
        result.message = error.toUtf8().toStdString();
        if (!rollbackError.isEmpty()) result.message += std::string(" ") + rollbackError.toUtf8().toStdString();
        return result;
    }
    result.ok = true; result.changed = true; result.message = u8"Пара каталогов восстановлена.";
    return result;
}

bool ShowCloudConflictResolver(QWidget* parent, const std::filesystem::path& workspace,
                               std::function<void(const std::string&)> openModule,
                               bool tasksModuleEnabled, bool pipelineModuleEnabled) {
    const auto config = LoadCloudSyncConfig(workspace);
    const auto root = ResolveCloudRootPath(config, workspace);
    QtScrollableDialog dialog(parent, QSize(640, 520)); dialog.setObjectName("cloudConflictResolver");
    dialog.setWindowTitle(QString::fromUtf8("Сравнение облачных версий"));
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Сравнение файлов и локальные резервные копии"));
    auto* form = dialog.formLayout(); form->setFormAlignment(Qt::AlignTop);
    form->setVerticalSpacing(dialog.scaledMetric(8));
    auto* intro = new QLabel(QString::fromUtf8("Выберите направление для отдельного файла. Любая замена требует подтверждения и резервной копии."));
    intro->setTextFormat(Qt::PlainText); intro->setWordWrap(true); intro->setProperty("warning", true); form->addRow(intro);
    auto* tabs = new QTabWidget; tabs->setObjectName("cloudConflictTabs");
    tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); tabs->setUsesScrollButtons(true);
    tabs->setAccessibleName(QString::fromUtf8("Файл для сравнения"));
    form->addRow(tabs);
    bool changed = false;
    std::string requestedModule;
    auto addFileTab = [&](const std::string& relative) {
        auto* page = new QWidget; page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* box = new QVBoxLayout(page); box->setContentsMargins(dialog.scaledMetric(8), dialog.scaledMetric(8), dialog.scaledMetric(8), dialog.scaledMetric(8));
        box->setSpacing(dialog.scaledMetric(8)); box->setAlignment(Qt::AlignTop);
        const bool catalogPair = relative == "skills.txt" || relative == "meta/professions.txt";
        const auto local = workspace / std::filesystem::u8path(relative); const auto cloud = root / std::filesystem::u8path(relative);
        auto* comparison = new DecisionTable(false); comparison->setRowCount(2); comparison->setColumnCount(3);
        comparison->setObjectName(objectName(relative, "Comparison"));
        comparison->setAccessibleName(QString::fromUtf8("Сравнение локальной и облачной версий файла"));
        comparison->setAccessibleDescription(QString::fromUtf8("Столбцы показывают сторону, сводку содержимого и путь. Выберите направление кнопками ниже."));
        comparison->setHorizontalHeaderLabels({QString::fromUtf8("Версия"), QString::fromUtf8("Сводка"), QString::fromUtf8("Путь")});
        comparison->verticalHeader()->hide(); comparison->setEditTriggers(QAbstractItemView::NoEditTriggers);
        comparison->setSelectionMode(QAbstractItemView::NoSelection); comparison->setShowGrid(false); comparison->setAlternatingRowColors(true);
        comparison->setItem(0, 0, new QTableWidgetItem(QString::fromUtf8("Локальная")));
        comparison->setItem(0, 1, new QTableWidgetItem(preview(local, relative)));
        comparison->setItem(0, 2, new QTableWidgetItem(q(local)));
        comparison->setItem(1, 0, new QTableWidgetItem(QString::fromUtf8("Облачная")));
        comparison->setItem(1, 1, new QTableWidgetItem(preview(cloud, relative)));
        comparison->setItem(1, 2, new QTableWidgetItem(q(cloud)));
        describeDecisionItems(comparison); box->addWidget(comparison); comparison->fitContents();
        auto* actions = new QtDialogFlowRow(page, dialog.scaledMetric(8), dialog.scaledMetric(4));
        actions->setObjectName(objectName(relative, "Actions"));
        const bool canOpenModule = (relative == "meta/tasks.json" && tasksModuleEnabled) ||
            (relative == "meta/pipeline.json" && pipelineModuleEnabled);
        if (canOpenModule) {
            auto* openModuleButton = new QPushButton(QString::fromUtf8("Открыть модуль"));
            openModuleButton->setObjectName(relative == "meta/tasks.json" ? "openTasksModule" : "openPipelineModule");
            openModuleButton->setAccessibleName(relative == "meta/tasks.json"
                ? QString::fromUtf8("Открыть модуль задач") : QString::fromUtf8("Открыть модуль пайплайна"));
            openModuleButton->setToolTip(QString::fromUtf8("Перейти к модулю, не изменяя локальные и облачные файлы."));
            openModuleButton->setAutoDefault(false);
            actions->addWidget(openModuleButton);
            QObject::connect(openModuleButton, &QPushButton::clicked, &dialog, [&, relative] {
                requestedModule = relative;
                dialog.reject();
            });
        }
        auto* apply = new QPushButton(QString::fromUtf8("Принять из облака")); apply->setObjectName(objectName(relative, "ApplyCloud"));
        apply->setProperty("primary", true); apply->setAutoDefault(false);
        apply->setAccessibleName(QString::fromUtf8("Принять из облака: ") + label(relative));
        apply->setToolTip(QString::fromUtf8("Заменить локальную версию после подтверждения и резервного копирования."));
        apply->setEnabled(std::filesystem::is_regular_file(cloud)); actions->addWidget(apply);
        auto* push = new QPushButton(QString::fromUtf8("Отправить локальную")); push->setObjectName(objectName(relative, "PushCloud"));
        push->setAutoDefault(false);
        push->setAccessibleName(QString::fromUtf8("Отправить локальную версию: ") + label(relative));
        push->setToolTip(QString::fromUtf8("Заменить облачную версию после подтверждения и резервного копирования."));
        push->setEnabled(config.enabled && std::filesystem::is_regular_file(local)); actions->addWidget(push); box->addWidget(actions);
        auto* backups = new DecisionTable(true); backups->setObjectName(objectName(relative, "Backups"));
        backups->setAccessibleName(QString::fromUtf8("Резервные копии файла"));
        backups->setAccessibleDescription(QString::fromUtf8("Список локальных снимков с датой, источником и сводкой. Восстановление доступно в последнем столбце."));
        backups->setColumnCount(4); backups->setHorizontalHeaderLabels({QString::fromUtf8("Дата"), QString::fromUtf8("Источник"), QString::fromUtf8("Сводка"), QString::fromUtf8("Действие")});
        backups->verticalHeader()->hide(); backups->setEditTriggers(QAbstractItemView::NoEditTriggers); backups->setSelectionMode(QAbstractItemView::NoSelection); backups->setShowGrid(false);
        const auto snapshots = ListCloudWorkspaceBackups(workspace, relative); const int shown = int(std::min<size_t>(5, snapshots.size())); backups->setRowCount(shown);
        for (int i = 0; i < shown; ++i) {
            const auto snapshot = snapshots[size_t(i)];
            const auto timestamp = QDateTime::fromSecsSinceEpoch(snapshot.createdAt);
            auto* date = new QTableWidgetItem(timestamp.toString("yyyy-MM-dd\nHH:mm"));
            date->setData(Qt::AccessibleDescriptionRole, timestamp.toString(Qt::ISODate)); backups->setItem(i, 0, date);
            backups->setItem(i, 1, new QTableWidgetItem(q(snapshot.sourceKind)));
            backups->setItem(i, 2, new QTableWidgetItem(preview(snapshot.path, relative)));
            auto* restore = new QPushButton(QString::fromUtf8("Восстановить"));
            restore->setObjectName(objectName(relative, "Restore") + QString::number(i));
            restore->setAutoDefault(false);
            const auto context = label(relative) + QStringLiteral(" · ") + timestamp.toString("yyyy-MM-dd HH:mm") +
                QStringLiteral(" · ") + q(snapshot.sourceKind) + QStringLiteral("\n") + q(snapshot.path);
            restore->setAccessibleName(QString::fromUtf8("Восстановить снимок: ") + context);
            restore->setAccessibleDescription(QString::fromUtf8("Замена локального файла только после подтверждения; текущая версия сохраняется."));
            restore->setToolTip(Qt::convertFromPlainText(context)); backups->setCellWidget(i, 3, restore);
            QObject::connect(restore, &QPushButton::clicked, &dialog, [&, snapshot, relative, local, catalogPair] {
                auto sourceText = preview(snapshot.path, relative) + "\n" + q(snapshot.path);
                auto targetText = preview(local, relative) + "\n" + q(local);
                if (catalogPair) {
                    const auto other = relative == "skills.txt" ? std::string("meta/professions.txt") : std::string("skills.txt");
                    sourceText += QString::fromUtf8("\n\nБудет восстановлена соответствующая резервная пара (%1) также для %2.")
                        .arg(q(snapshot.sourceKind), QString::fromStdString(other));
                    targetText += QString::fromUtf8("\n\nВторая часть текущей пары: %1").arg(preview(workspace / std::filesystem::u8path(other), other));
                }
                if (!confirm(&dialog, QString::fromUtf8("Восстановить снимок"), sourceText, targetText, QString::fromUtf8("Восстановить"))) return;
                QtCloudConflictResult result;
                if (!catalogPair) {
                    result = ApplyQtCloudWorkspaceFile(workspace, snapshot.path, relative, "restore");
                } else {
                    const auto skills = ListCloudWorkspaceBackups(workspace, "skills.txt");
                    const auto professions = ListCloudWorkspaceBackups(workspace, "meta/professions.txt");
                    const auto findMate = [&](const auto& snapshots, const std::string& desiredKind) -> std::filesystem::path {
                        for (const auto& candidate : snapshots) {
                            if (candidate.sourceKind != desiredKind || candidate.createdAt != snapshot.createdAt) continue;
                            return candidate.path;
                        }
                        return {};
                    };
                    const auto skillBackup = relative == "skills.txt" ? snapshot.path : findMate(skills, snapshot.sourceKind);
                    const auto professionBackup = relative == "meta/professions.txt" ? snapshot.path : findMate(professions, snapshot.sourceKind);
                    result = skillBackup.empty() || professionBackup.empty() ? QtCloudConflictResult{false, false, u8"Не удалось найти снимок второго каталога из этой пары."}
                        : RestoreQtCloudCatalogPair(workspace, skillBackup, professionBackup);
                }
                if (!result.ok) QMessageBox::warning(&dialog, QString::fromUtf8("Восстановление"), q(result.message));
                else { changed = changed || result.changed; dialog.accept(); }
            });
        }
        describeDecisionItems(backups);
        auto* backupTitle = new QLabel(QString::fromUtf8("Последние локальные снимки"));
        backupTitle->setTextFormat(Qt::PlainText); backupTitle->setWordWrap(true); box->addWidget(backupTitle);
        box->addWidget(backups); backups->fitContents();
        auto* empty = new QLabel(QString::fromUtf8("Локальных снимков этого файла пока нет."));
        empty->setObjectName(objectName(relative, "EmptyBackups")); empty->setTextFormat(Qt::PlainText); empty->setWordWrap(true);
        box->addWidget(empty); empty->setVisible(shown == 0); backups->setVisible(shown != 0);
        QObject::connect(apply, &QPushButton::clicked, &dialog, [&, relative, local, cloud, catalogPair] {
            auto sourceText = preview(cloud, relative) + "\n" + q(cloud);
            auto targetText = preview(local, relative) + "\n" + q(local);
            if (catalogPair) {
                const auto other = relative == "skills.txt" ? std::string("meta/professions.txt") : std::string("skills.txt");
                const auto otherLocal = workspace / std::filesystem::u8path(other);
                const auto otherCloud = root / std::filesystem::u8path(other);
                sourceText += QString::fromUtf8("\n\nПрофессии и навыки будут применены как единая пара:\n%1 · %2\n%3 · %4")
                    .arg(QString::fromStdString(relative), preview(cloud, relative), QString::fromStdString(other), preview(otherCloud, other));
                targetText += QString::fromUtf8("\n\nЛокальная пара:\n%1 · %2").arg(QString::fromStdString(other), preview(otherLocal, other));
            }
            if (!confirm(&dialog, QString::fromUtf8("Применить облачную версию"), sourceText, targetText, QString::fromUtf8("Применить"))) return;
            const auto result = ApplyQtCloudWorkspaceFile(workspace, cloud, relative, "cloud");
            if (!result.ok) QMessageBox::warning(&dialog, QString::fromUtf8("Облачная версия"), q(result.message));
            else { changed = changed || result.changed; dialog.accept(); }
        });
        QObject::connect(push, &QPushButton::clicked, &dialog, [&, relative, local, cloud, catalogPair] {
            auto sourceText = preview(local, relative) + "\n" + q(local);
            auto targetText = preview(cloud, relative) + "\n" + q(cloud);
            if (catalogPair) {
                const auto other = relative == "skills.txt" ? std::string("meta/professions.txt") : std::string("skills.txt");
                const auto otherLocal = workspace / std::filesystem::u8path(other);
                const auto otherCloud = root / std::filesystem::u8path(other);
                sourceText += QString::fromUtf8("\n\nБудет также отправлена вторая часть пары:\n%1 · %2")
                    .arg(QString::fromStdString(other), preview(otherLocal, other));
                targetText += QString::fromUtf8("\n\nОблачный каталог:\n%1 · %2").arg(QString::fromStdString(other), preview(otherCloud, other));
            }
            if (!confirm(&dialog, QString::fromUtf8("Отправить локальную версию"), sourceText, targetText, QString::fromUtf8("Отправить"))) return;
            const auto result = PushQtCloudWorkspaceFile(workspace, relative);
            if (!result.ok) QMessageBox::warning(&dialog, QString::fromUtf8("Отправка в облако"), q(result.message));
            else { changed = changed || result.changed; dialog.accept(); }
        });
        tabs->addTab(page, label(relative));
        // QSS can reset QWidget minima when a previously parentless page is
        // polished/reparented. Measure commands only after their final ancestry.
        for (auto* button : page->findChildren<QPushButton*>()) {
            button->ensurePolished();
            button->setMinimumHeight(std::max(dialog.scaledMetric(40), button->sizeHint().height()));
        }
        comparison->fitContents(); backups->fitContents();
    };
    addFileTab("meta/tasks.json"); addFileTab("meta/pipeline.json");
    addFileTab("meta/projects.json"); addFileTab("meta/banner.json");
    addFileTab("meta/gameplay.ini"); addFileTab("meta/professions.txt"); addFileTab("skills.txt");
    auto* notice = new QLabel;
    notice->setObjectName("cloudConflictNotice"); notice->setTextFormat(Qt::PlainText); notice->setWordWrap(true);
    notice->setAccessibleName(QString::fromUtf8("Контекст выбранного файла и безопасное закрытие"));
    dialog.footerLayout()->addWidget(notice);
    const auto updateContext = [tabs, notice] {
        notice->setText(QString::fromUtf8("%1. Замена — после подтверждения и резервной копии. Закрытие не изменяет файлы.").arg(tabs->tabText(tabs->currentIndex())));
    };
    QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, updateContext); updateContext();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); buttons->setObjectName("cloudConflictButtons");
    auto* close = buttons->button(QDialogButtonBox::Close); close->setObjectName("cloudConflictClose");
    close->setText(QString::fromUtf8("Закрыть")); close->setAccessibleName(QString::fromUtf8("Закрыть сравнение без изменений"));
    close->setAutoDefault(true); close->setDefault(true);
    dialog.footerLayout()->addWidget(buttons);
    close->ensurePolished(); close->setMinimumHeight(std::max(dialog.scaledMetric(40), close->sizeHint().height()));
    QWidget::setTabOrder(tabs, close); close->setFocus();
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
    if (!requestedModule.empty() && openModule) openModule(requestedModule);
    return changed;
}
