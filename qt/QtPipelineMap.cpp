#include "QtPipelineMap.h"
#include "QtActionIcons.h"
#include "QtDisclosureButton.h"

#include <QtWidgets>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <set>
#include <cmath>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
QString stageLabel(const PipelineStep& step) {
    return (step.stageCode.empty() ? QString() : q(step.stageCode) + QString::fromUtf8(" · ")) +
        (step.title.empty() ? QString::fromUtf8("Без названия") : q(step.title));
}

qreal gridSize(qreal value) { return std::ceil(value / 8.0) * 8.0; }

QIcon zoomIcon(bool zoomIn, const QPalette& palette) {
    QIcon icon;
    for (const int size : {20, 40, 60}) {
        for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Selected}) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(size / 20.0, size / 20.0);
            const auto color = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::Text)
                : mode == QIcon::Selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text);
            painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(3, 3, 11, 11));
            painter.drawLine(QPointF(12.5, 12.5), QPointF(17, 17));
            painter.drawLine(QPointF(5.5, 8.5), QPointF(11.5, 8.5));
            if (zoomIn) painter.drawLine(QPointF(8.5, 5.5), QPointF(8.5, 11.5));
            icon.addPixmap(pixmap, mode, QIcon::Off);
            icon.addPixmap(pixmap, mode, QIcon::On);
            if (mode == QIcon::Normal) {
                icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
                icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
            }
        }
    }
    return icon;
}

// Keep the stage chooser useful on a narrow dialog; actions wrap independently
// when their measured widths no longer fit, without widening the modal window.
class PipelineMapActionLayout final : public QLayout {
public:
    explicit PipelineMapActionLayout(QWidget* parent) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); setSpacing(8); }
    ~PipelineMapActionLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.push_back(item); }
    int count() const override { return int(items_.size()); }
    QLayoutItem* itemAt(int index) const override { return index >= 0 && index < count() ? items_[size_t(index)] : nullptr; }
    QLayoutItem* takeAt(int index) override {
        if (index < 0 || index >= count()) return nullptr;
        auto* item = items_[size_t(index)]; items_.erase(items_.begin() + index); return item;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override {
        QSize size;
        int visible = 0;
        for (auto* item : items_) if (!item->isEmpty()) {
            const auto hint = item->sizeHint().expandedTo(item->minimumSize());
            size.rwidth() += hint.width(); size.setHeight(std::max(size.height(), hint.height())); ++visible;
        }
        size.rwidth() += std::max(0, visible - 1) * spacing();
        return size;
    }
    QSize minimumSize() const override {
        QSize size;
        for (auto* item : items_) if (!item->isEmpty()) size = size.expandedTo(item->minimumSize());
        return size;
    }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
    int arrange(const QRect& rect, bool place) const {
        std::vector<QLayoutItem*> row;
        int usedWidth = 0, rowHeight = 0, y = rect.y();
        const auto finishRow = [&] {
            int x = rect.x();
            for (auto* item : row) {
                const auto size = item->sizeHint().expandedTo(item->minimumSize());
                if (place) item->setGeometry(QRect(x, y + (rowHeight - size.height()) / 2, size.width(), size.height()));
                x += size.width() + spacing();
            }
            y += rowHeight;
            row.clear(); usedWidth = 0; rowHeight = 0;
        };
        for (auto* item : items_) if (!item->isEmpty()) {
            const auto size = item->sizeHint().expandedTo(item->minimumSize());
            if (!row.empty() && usedWidth + spacing() + size.width() > rect.width()) { finishRow(); y += spacing(); }
            if (!row.empty()) usedWidth += spacing();
            row.push_back(item); usedWidth += size.width(); rowHeight = std::max(rowHeight, size.height());
        }
        if (!row.empty()) finishRow();
        return y - rect.y();
    }
    std::vector<QLayoutItem*> items_;
};

class PipelineMapControlsLayout final : public QLayout {
public:
    explicit PipelineMapControlsLayout(QWidget* parent) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); setSpacing(8); }
    ~PipelineMapControlsLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.push_back(item); }
    int count() const override { return int(items_.size()); }
    QLayoutItem* itemAt(int index) const override { return index >= 0 && index < count() ? items_[size_t(index)] : nullptr; }
    QLayoutItem* takeAt(int index) override {
        if (index < 0 || index >= count()) return nullptr;
        auto* item = items_[size_t(index)]; items_.erase(items_.begin() + index); return item;
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override {
        if (count() != 2) return {};
        const auto first = items_[0]->sizeHint(), second = items_[1]->sizeHint();
        return {chooserWidth() + spacing() + second.width(), std::max(first.height(), second.height())};
    }
    QSize minimumSize() const override {
        QSize size;
        for (auto* item : items_) size = size.expandedTo(item->minimumSize());
        return size;
    }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
    int chooserWidth() const {
        if (items_.empty()) return 0;
        const auto* widget = items_[0]->widget();
        return std::max(items_[0]->minimumSize().width(), widget->fontMetrics().horizontalAdvance(QStringLiteral("MMMMMMMMMMMMMM")) + 40);
    }
    int arrange(const QRect& rect, bool place) const {
        if (count() != 2) return 0;
        auto* choice = items_[0]; auto* actions = items_[1];
        const auto actionHint = actions->sizeHint();
        const bool oneRow = chooserWidth() + spacing() + actionHint.width() <= rect.width();
        const int choiceHeight = choice->sizeHint().height();
        const int actionWidth = oneRow ? actionHint.width() : rect.width();
        const int actionHeight = actions->hasHeightForWidth() ? actions->heightForWidth(actionWidth) : actionHint.height();
        const int height = oneRow ? std::max(choiceHeight, actionHeight) : choiceHeight + spacing() + actionHeight;
        if (place && oneRow) {
            const int choiceWidth = rect.width() - actionWidth - spacing();
            choice->setGeometry(QRect(rect.x(), rect.y() + (height - choiceHeight) / 2, choiceWidth, choiceHeight));
            actions->setGeometry(QRect(rect.x() + choiceWidth + spacing(), rect.y() + (height - actionHeight) / 2, actionWidth, actionHeight));
        } else if (place) {
            choice->setGeometry(QRect(rect.x(), rect.y(), rect.width(), choiceHeight));
            actions->setGeometry(QRect(rect.x(), rect.y() + choiceHeight + spacing(), rect.width(), actionHeight));
        }
        return height;
    }
    std::vector<QLayoutItem*> items_;
};

class PipelineMapDialog final : public QDialog {
public:
    using QDialog::QDialog;
    std::function<void()> appearanceChanged;
    std::function<void()> shown;
    std::function<void()> resized;
protected:
    void changeEvent(QEvent* event) override {
        QDialog::changeEvent(event);
        if (appearanceChanged && (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange ||
            event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::ApplicationPaletteChange))
            appearanceChanged();
    }
    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        if (shown) shown();
        queueResizeAdjustment();
    }
    void resizeEvent(QResizeEvent* event) override {
        QDialog::resizeEvent(event);
        queueResizeAdjustment();
    }
private:
    void queueResizeAdjustment() {
        if (!resized || resizeQueued_ || adjustingResize_) return;
        resizeQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            resizeQueued_ = false;
            if (!resized || !isVisible()) return;
            adjustingResize_ = true;
            resized();
            adjustingResize_ = false;
        });
    }
    bool resizeQueued_ = false;
    bool adjustingResize_ = false;
};

class PipelineNode final : public QGraphicsObject {
public:
    PipelineNode(const PipelineStep& step, bool missing, const QFont& font, const QPalette& palette, QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent), step_(step), missing_(missing) {
        setFlag(QGraphicsItem::ItemIsSelectable);
        setFlag(QGraphicsItem::ItemIsFocusable);
        setAcceptHoverEvents(true);
        setData(Qt::UserRole, q(step.id));
        setToolTip(missing ? QString::fromUtf8("Этап отсутствует: %1").arg(q(step.id))
                           : stageLabel(step) + QString::fromUtf8("\nОтветственный: %1\nПереходов: %2")
                                .arg(step.owner.empty() ? QString::fromUtf8("Не задан") : q(step.owner)).arg(step.nextIds.size()));
        setAppearance(font, palette);
    }
    void setAppearance(const QFont& font, const QPalette& palette) {
        prepareGeometryChange();
        palette_ = palette;
        titleFont_ = font;
        titleFont_.setWeight(QFont::DemiBold);
        metaFont_ = font;
        const QFontMetricsF titleMetrics(titleFont_), metaMetrics(metaFont_);
        const qreal width = gridSize(std::max<qreal>(184, titleMetrics.horizontalAdvance(QStringLiteral("MMMMMMMMMMMMMMMM")) + 16));
        titleRect_ = QRectF(8, 8, width - 16, std::ceil(titleMetrics.height()));
        metaRect_ = QRectF(8, titleRect_.bottom() + 4, width - 16, std::ceil(metaMetrics.height()));
        bounds_ = QRectF(0, 0, width, gridSize(metaRect_.bottom() + 8));
        update();
    }
    QRectF boundingRect() const override { return bounds_; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        QColor border = palette_.color(QPalette::Disabled, QPalette::Text);
        border.setAlphaF(0.5);
        if (isSelected() || option->state.testFlag(QStyle::State_MouseOver)) border = palette_.color(QPalette::Link);
        if (isSelected()) {
            QLinearGradient surface(bounds_.topLeft(), bounds_.bottomRight());
            surface.setColorAt(0.0, palette_.color(QPalette::Highlight).darker(170));
            surface.setColorAt(1.0, palette_.color(QPalette::Button));
            painter->setBrush(surface);
        } else {
            painter->setBrush(palette_.color(QPalette::AlternateBase));
        }
        painter->setPen(QPen(border, isSelected() ? 2.0 : 1.0, missing_ ? Qt::DashLine : Qt::SolidLine));
        painter->drawRoundedRect(bounds_.adjusted(1, 1, -1, -1), 8, 8);
        painter->setPen(palette_.color(QPalette::Text));
        painter->setFont(titleFont_);
        painter->drawText(titleRect_, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(titleFont_).elidedText(missing_ ? q(step_.id) :
                step_.title.empty() ? QString::fromUtf8("Без названия") : q(step_.title), Qt::ElideRight, int(titleRect_.width())));
        painter->setFont(metaFont_);
        painter->setPen(isSelected() ? palette_.color(QPalette::Text) : palette_.color(QPalette::Disabled, QPalette::Text));
        const QFontMetrics metrics(metaFont_);
        QString meta = QString::fromUtf8("Недоступная связь");
        if (!missing_) {
            const QString code = metrics.elidedText(step_.stageCode.empty() ? QString::fromUtf8("без кода") : q(step_.stageCode),
                Qt::ElideRight, int(metaRect_.width() * 0.35));
            const QString prefix = code + QString::fromUtf8(" · ");
            const QString suffix = QString::fromUtf8(" → %1").arg(step_.nextIds.size());
            const int ownerWidth = std::max(0, int(metaRect_.width()) - metrics.horizontalAdvance(prefix + suffix));
            meta = prefix + metrics.elidedText(step_.owner.empty() ? QString::fromUtf8("без ответственного") : q(step_.owner),
                Qt::ElideRight, ownerWidth) + suffix;
        }
        painter->drawText(metaRect_, Qt::AlignLeft | Qt::AlignVCenter,
            missing_ ? metrics.elidedText(meta, Qt::ElideRight, int(metaRect_.width())) : meta);
    }
    const PipelineStep& step() const { return step_; }
    bool missing() const { return missing_; }
private:
    PipelineStep step_;
    bool missing_;
    QPalette palette_;
    QFont titleFont_, metaFont_;
    QRectF bounds_, titleRect_, metaRect_;
};

class PipelineMapView final : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
    std::function<void(qreal)> zoomRequested;
    std::function<void()> resetRequested;
    std::function<void(int)> selectionRequested;
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const qreal factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
            if (zoomRequested) zoomRequested(transform().m11() * factor);
            event->accept();
            return;
        }
        QGraphicsView::wheelEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (selectionRequested && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            selectionRequested(event->key() == Qt::Key_Up ? -1 : 1);
            event->accept();
            return;
        }
        if (zoomRequested && event->modifiers().testFlag(Qt::ControlModifier) &&
            (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal || event->key() == Qt::Key_Minus || event->key() == Qt::Key_0)) {
            if (event->key() == Qt::Key_0 && resetRequested) resetRequested();
            else zoomRequested(transform().m11() * (event->key() == Qt::Key_Minus ? 1.0 / 1.2 : 1.2));
            event->accept();
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }
};

QString describe(const PipelineNode* node, const std::vector<PipelineStep>& steps) {
    if (!node) return QString::fromUtf8("Выберите этап на карте, чтобы увидеть описание и допустимые переходы.");
    const auto& step = node->step();
    if (node->missing()) return QString::fromUtf8("<b>Связь на отсутствующий этап</b><br>%1").arg(q(step.id).toHtmlEscaped());
    auto field = [](const QString& label, const std::string& value) {
        return QStringLiteral("<p><b>%1</b><br>%2</p>").arg(label.toHtmlEscaped(), q(value).toHtmlEscaped().replace('\n', "<br>"));
    };
    auto stageLabel = [](const PipelineStep& value) {
        const QString code = q(value.stageCode).isEmpty() ? QString::fromUtf8("без кода") : q(value.stageCode);
        return code + QString::fromUtf8(" · ") + q(value.title);
    };
    const QString title = stageLabel(step).toHtmlEscaped();
    QString html = QStringLiteral("<h3>%1</h3>").arg(title);
    html += field(QString::fromUtf8("Ветка"), step.branch.empty() ? std::string(u8"Общая") : step.branch);
    html += field(QString::fromUtf8("Ответственный"), step.owner);
    html += field(QString::fromUtf8("Вход"), step.input);
    html += field(QString::fromUtf8("Выход"), step.output);
    html += field(QString::fromUtf8("Критерий готовности"), step.doneCriteria);
    html += field(QString::fromUtf8("Следующий этап"), step.nextStageLabel);
    html += field(QString::fromUtf8("Описание"), step.description);
    html += QStringLiteral("<h4>Подсказки</h4>");
    if (step.hints.empty()) html += QString::fromUtf8("<p>Подсказки не заданы.</p>");
    else {
        html += QStringLiteral("<ul>");
        for (const auto& hint : step.hints)
            html += QStringLiteral("<li>%1</li>").arg(q(hint).toHtmlEscaped().replace('\n', "<br>"));
        html += QStringLiteral("</ul>");
    }
    html += field(QString::fromUtf8("Проверка в движке"), step.engineCheck);
    html += field(QString::fromUtf8("Риски и возвраты"), step.risk);
    html += field(QString::fromUtf8("Практика Forge Mirror"), step.legacyNotes);
    html += QStringLiteral("<h4>Путь</h4>");
    const auto stageLink = [&stageLabel](const PipelineStep& value) {
        return QStringLiteral("<a href=\"pipeline:%1\">%2</a>")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(q(value.id))), stageLabel(value).toHtmlEscaped());
    };
    QStringList incoming;
    for (const auto& source : steps)
        if (std::find(source.nextIds.begin(), source.nextIds.end(), step.id) != source.nextIds.end())
            incoming.push_back(stageLink(source));
    html += QString::fromUtf8("<p><b>Приходит из</b><br>") + (incoming.isEmpty()
        ? QString::fromUtf8("Стартовая точка пайплайна.") : incoming.join(QStringLiteral(", "))) + QStringLiteral("</p>");
    QStringList targets;
    for (const auto& id : step.nextIds) {
        const auto target = std::find_if(steps.begin(), steps.end(), [&](const auto& value) { return value.id == id; });
        targets.push_back(target == steps.end() ? QString::fromUtf8("Не найден: %1").arg(q(id).toHtmlEscaped())
                                                : stageLink(*target));
    }
    html += QString::fromUtf8("<p><b>Переходы</b><br>") + (targets.isEmpty()
        ? QString::fromUtf8("Нет · конечный этап") : targets.join(QStringLiteral(", "))) + QStringLiteral("</p>");
    return html;
}

}

bool ShowQtPipelineMap(QWidget* parent, const std::vector<PipelineStep>& steps,
                       const std::string& selectedId, std::function<bool()> motionAllowed) {
    PipelineMapDialog dialog(parent);
    dialog.setObjectName("pipelineMapDialog");
    dialog.setWindowTitle(QString::fromUtf8("Карта ветвлений пайплайна"));
    dialog.resize(1040, 740);
    dialog.setMinimumSize(720, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel;
    summary->setObjectName("pipelineMapSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* controlsWidget = new QWidget;
    controlsWidget->setObjectName("pipelineMapControls");
    controlsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* controls = new PipelineMapControlsLayout(controlsWidget);
    auto* stageChoice = new QComboBox;
    stageChoice->setObjectName("pipelineMapStageChoice");
    stageChoice->setAccessibleName(QString::fromUtf8("Выбранный этап карты пайплайна"));
    stageChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    stageChoice->setMinimumContentsLength(1);
    stageChoice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    controls->addWidget(stageChoice);
    auto* actionsWidget = new QWidget;
    actionsWidget->setObjectName("pipelineMapActions");
    actionsWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* actions = new PipelineMapActionLayout(actionsWidget);
    controls->addWidget(actionsWidget);
    const auto tool = [&](const char* name, const QString& text) {
        auto* button = new QToolButton;
        button->setObjectName(name);
        button->setAccessibleName(text);
        button->setToolTip(text);
        actions->addWidget(button);
        return button;
    };
    auto* zoomOut = tool("pipelineMapZoomOut", QString::fromUtf8("Уменьшить масштаб · Ctrl + минус"));
    auto* zoomValue = new QLabel;
    zoomValue->setObjectName("pipelineMapZoom");
    zoomValue->setAccessibleName(QString::fromUtf8("Масштаб карты"));
    zoomValue->setAlignment(Qt::AlignCenter);
    actions->addWidget(zoomValue);
    auto* zoomIn = tool("pipelineMapZoomIn", QString::fromUtf8("Увеличить масштаб · Ctrl + плюс"));
    auto* fit = tool("pipelineMapFit", QString::fromUtf8("Обзор всей карты в доступном масштабе"));
    fit->setText(QString::fromUtf8("Вписать"));
    auto* reset = tool("pipelineMapZoomReset", QString::fromUtf8("Вернуть масштаб 100% и показать выбранный этап · Ctrl + 0"));
    auto* detailsToggle = new QtDisclosureButton(nullptr, std::move(motionAllowed));
    detailsToggle->setObjectName("pipelineMapDetailsToggle");
    detailsToggle->setText(QString::fromUtf8("Подробности"));
    detailsToggle->setAccessibleName(QString::fromUtf8("Подробности выбранного этапа"));
    detailsToggle->setToolTip(QString::fromUtf8("Показать вход, выход, критерии готовности, подсказки и переходы этапа"));
    actions->addWidget(detailsToggle);
    layout->addWidget(controlsWidget);
    auto* view = new PipelineMapView;
    view->setObjectName("pipelineMapView");
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::ScrollHandDrag);
    view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setFrameShape(QFrame::NoFrame);
    view->setFocusPolicy(Qt::StrongFocus);
    view->viewport()->setFocusPolicy(Qt::StrongFocus);
    view->setAccessibleName(QString::fromUtf8("Карта переходов пайплайна"));
    view->setToolTip(QString::fromUtf8("Ctrl + колесо — масштаб; перетаскивание — перемещение; вверх/вниз — выбор этапа"));
    auto* scene = new QGraphicsScene(view);
    scene->setBackgroundBrush(dialog.palette().color(QPalette::Base));
    view->setScene(scene);
    layout->addWidget(view, 1);
    auto* details = new QTextBrowser;
    details->setObjectName("pipelineMapDetails");
    details->setAccessibleName(QString::fromUtf8("Подробности выбранного этапа карты пайплайна"));
    details->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    details->setOpenExternalLinks(false);
    details->setOpenLinks(false);
    layout->addWidget(details);
    details->hide();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::map<QString, std::vector<const PipelineStep*>> lanes;
    std::unordered_map<std::string, const PipelineStep*> byId;
    std::unordered_map<std::string, size_t> columns;
    std::unordered_map<std::string, size_t> indexes;
    std::set<std::string> missingIds;
    int transitionCount = 0;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        byId.emplace(step.id, &step);
        indexes.emplace(step.id, i);
        lanes[q(step.branch).trimmed().isEmpty() ? QString::fromUtf8("Общая") : q(step.branch)].push_back(&step);
    }
    // This changes scene placement only. Canonical order within a lane is kept;
    // forward links advance layers, while return links and loops remain arrows.
    std::map<QString, size_t> previousColumn;
    for (size_t index = 0; index < steps.size(); ++index) {
        const auto& step = steps[index];
        const QString lane = q(step.branch).trimmed().isEmpty() ? QString::fromUtf8("Общая") : q(step.branch);
        auto& column = columns[step.id];
        if (const auto previous = previousColumn.find(lane); previous != previousColumn.end())
            column = std::max(column, previous->second + 1);
        previousColumn[lane] = column;
        for (const auto& target : step.nextIds) {
            const auto found = indexes.find(target);
            if (found == indexes.end() || found->second > index)
                columns[target] = std::max(columns[target], column + 1);
        }
    }
    for (const auto& step : steps) {
        transitionCount += int(step.nextIds.size());
        for (const auto& target : step.nextIds) if (byId.find(target) == byId.end()) missingIds.insert(target);
    }
    std::vector<QString> laneOrder;
    for (const auto& lane : lanes) laneOrder.push_back(lane.first);
    if (!missingIds.empty()) laneOrder.push_back(QString::fromUtf8("Отсутствующие этапы"));

    std::unordered_map<std::string, PipelineNode*> nodes;
    std::vector<PipelineNode*> allNodes;
    std::vector<QGraphicsSimpleTextItem*> laneLabels;
    for (size_t laneIndex = 0; laneIndex < laneOrder.size(); ++laneIndex) {
        const auto& laneName = laneOrder[laneIndex];
        auto* label = scene->addSimpleText(laneName, dialog.font());
        label->setToolTip(laneName);
        laneLabels.push_back(label);
        if (!missingIds.empty() && laneIndex == lanes.size()) {
            size_t previous = 0;
            bool hasPrevious = false;
            for (const auto& missing : missingIds) {
                PipelineStep placeholder; placeholder.id = missing;
                auto* node = new PipelineNode(placeholder, true, dialog.font(), dialog.palette());
                scene->addItem(node); nodes[missing] = node; allNodes.push_back(node);
                if (hasPrevious) columns[missing] = std::max(columns[missing], previous + 1);
                previous = columns[missing]; hasPrevious = true;
            }
            continue;
        }
        for (const auto* step : lanes[laneName]) {
            auto* node = new PipelineNode(*step, false, dialog.font(), dialog.palette());
            scene->addItem(node); nodes[step->id] = node; allNodes.push_back(node);
        }
    }
    struct Edge { std::string source, target; QGraphicsPathItem* path; QGraphicsPolygonItem* arrow; };
    std::vector<Edge> edges;
    for (const auto& step : steps) {
        if (nodes.find(step.id) == nodes.end()) continue;
        for (const auto& targetId : step.nextIds) {
            if (nodes.find(targetId) == nodes.end()) continue;
            auto* edge = scene->addPath(QPainterPath());
            auto* arrow = scene->addPolygon(QPolygonF());
            edge->setZValue(-1); arrow->setZValue(-1);
            edges.push_back({step.id, targetId, edge, arrow});
        }
    }
    for (const auto& step : steps) {
        stageChoice->addItem(stageLabel(step), q(step.id));
        const int index = stageChoice->count() - 1;
        stageChoice->setItemData(index, q(step.branch), Qt::ToolTipRole);
        stageChoice->setItemData(index, stageLabel(step), Qt::AccessibleTextRole);
    }
    for (const auto& missing : missingIds)
        stageChoice->addItem(QString::fromUtf8("Недоступный этап · %1").arg(q(missing)), q(missing));

    const auto selectedNode = [&]() -> PipelineNode* {
        for (auto* item : scene->selectedItems()) if (auto* node = dynamic_cast<PipelineNode*>(item)) return node;
        return nullptr;
    };
    const auto keepSelectedVisible = [&] {
        const auto* node = selectedNode();
        if (!node) return;
        const auto bounds = node->sceneBoundingRect();
        const auto scaled = view->transform().mapRect(bounds);
        const auto available = view->viewport()->rect().adjusted(8, 8, -8, -8);
        if (scaled.width() <= available.width() && scaled.height() <= available.height())
            view->ensureVisible(bounds, 8, 8);
        else view->centerOn(node);
    };
    const auto updateEdges = [&] {
        const auto* selected = selectedNode();
        for (const auto& edge : edges) {
            const auto* source = nodes.at(edge.source);
            const auto* target = nodes.at(edge.target);
            const auto sourceRect = source->sceneBoundingRect(), targetRect = target->sceneBoundingRect();
            const bool forward = targetRect.center().x() >= sourceRect.center().x();
            const QPointF start(forward ? sourceRect.right() : sourceRect.left(), sourceRect.center().y());
            const QPointF finish(forward ? targetRect.left() : targetRect.right(), targetRect.center().y());
            QPainterPath path;
            if (edge.source == edge.target) {
                const QPointF loopStart(sourceRect.left() + sourceRect.width() * 0.75, sourceRect.top());
                const QPointF loopEnd(sourceRect.left() + sourceRect.width() * 0.25, sourceRect.top());
                const qreal rise = sourceRect.height() * 0.6;
                path.moveTo(loopStart);
                path.cubicTo(loopStart + QPointF(rise, -rise), loopEnd + QPointF(-rise, -rise), loopEnd);
            } else {
                path.moveTo(start);
                const qreal bend = std::copysign(std::max<qreal>(36, std::abs(finish.x() - start.x()) * 0.42), finish.x() - start.x());
                path.cubicTo(start + QPointF(bend, 0), finish - QPointF(bend, 0), finish);
            }
            const bool highlighted = selected && (selected->step().id == edge.source || selected->step().id == edge.target);
            QColor color = dialog.palette().color(QPalette::Disabled, QPalette::Text);
            if (highlighted) color = dialog.palette().color(QPalette::Highlight);
            else color.setAlphaF(0.7);
            edge.path->setPath(path);
            edge.path->setPen(QPen(color, highlighted ? 1.6 : 1.2, target->missing() ? Qt::DashLine : Qt::SolidLine));
            const QPointF tangent = path.pointAtPercent(0.98);
            const QPointF tip = path.pointAtPercent(1.0);
            QLineF direction(tangent, tip);
            const QPointF unit(std::cos(qDegreesToRadians(direction.angle())), -std::sin(qDegreesToRadians(direction.angle())));
            const QPointF normal(-unit.y(), unit.x());
            const qreal arrowSize = std::max<qreal>(8, QFontMetricsF(dialog.font()).height() * 0.5);
            edge.arrow->setPolygon(QPolygonF{tip, tip - unit * arrowSize + normal * (arrowSize * 0.45),
                tip - unit * arrowSize - normal * (arrowSize * 0.45)});
            edge.arrow->setPen(Qt::NoPen);
            edge.arrow->setBrush(color);
        }
    };
    const auto relayout = [&] {
        scene->setBackgroundBrush(dialog.palette().color(QPalette::Base));
        qreal nodeWidth = 184, nodeHeight = 56;
        for (auto* node : allNodes) {
            node->setAppearance(dialog.font(), dialog.palette());
            nodeWidth = std::max(nodeWidth, node->boundingRect().width());
            nodeHeight = std::max(nodeHeight, node->boundingRect().height());
        }
        const QFontMetrics metrics(dialog.font());
        const qreal labelWidth = gridSize(std::max<qreal>(128, metrics.horizontalAdvance(QStringLiteral("MMMMMMMMMMMMMM"))));
        const qreal left = labelWidth + 24, xStep = nodeWidth + 24;
        const qreal yStep = nodeHeight + gridSize(std::max<qreal>(32, metrics.height() * 2 + 16));
        for (size_t index = 0; index < laneOrder.size(); ++index) {
            const auto& lane = laneOrder[index];
            const qreal y = 16 + qreal(index) * yStep;
            auto* label = laneLabels.at(index);
            label->setFont(dialog.font());
            label->setText(metrics.elidedText(lane, Qt::ElideRight, int(labelWidth)));
            label->setBrush(dialog.palette().color(QPalette::Text));
            label->setPos(8, y + (nodeHeight - label->boundingRect().height()) * 0.5);
            if (!missingIds.empty() && index == lanes.size()) {
                for (const auto& id : missingIds) nodes.at(id)->setPos(left + qreal(columns.at(id)) * xStep, y);
            } else for (const auto* step : lanes.at(lane))
                nodes.at(step->id)->setPos(left + qreal(columns.at(step->id)) * xStep, y);
        }
        updateEdges();
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-16, -16, 16, 16));
        const double basePointSize = qApp->property("forgeBasePointSize").toDouble();
        const double textScale = basePointSize > 0.0
            ? std::clamp(dialog.font().pointSizeF() / basePointSize, 0.9, 2.0) : 1.0;
        const int extent = std::max(qRound(26 * textScale), metrics.height() + 8);
        const int iconSide = qRound(18 * textScale);
        const QSize iconSize(iconSide, iconSide);
        for (auto* button : {zoomOut, zoomIn, reset}) {
            // Global text-button padding otherwise leaves no room for a scaled icon.
            button->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));
            button->setFixedSize(extent, extent);
            button->setIconSize(iconSize);
        }
        fit->setMinimumHeight(extent);
        detailsToggle->setMinimumHeight(extent);
        detailsToggle->setIconSize(iconSize);
        zoomOut->setIcon(zoomIcon(false, dialog.palette()));
        zoomIn->setIcon(zoomIcon(true, dialog.palette()));
        reset->setIcon(CreateQtActionIcon(QtActionIcon::Reset, dialog.palette()));
        zoomValue->setFixedWidth(metrics.horizontalAdvance(QStringLiteral("300%")) + 8);
        details->setMaximumHeight(int(gridSize(std::max(120, metrics.height() * 4))));
    };
    summary->setText(QString::fromUtf8("Этапов: %1 · переходов: %2 · недоступных связей: %3")
        .arg(steps.size()).arg(transitionCount).arg(missingIds.size()));
    const auto refreshSelection = [&] {
        const auto* node = selectedNode();
        details->setHtml(steps.empty() ? QString::fromUtf8("Пайплайн пуст.") : describe(node, steps));
        if (node) {
            const QSignalBlocker blocker(stageChoice);
            stageChoice->setCurrentIndex(stageChoice->findData(q(node->step().id)));
            stageChoice->setToolTip(node->toolTip());
            stageChoice->setAccessibleDescription(node->toolTip());
            view->ensureVisible(node->sceneBoundingRect(), 16, 16);
        }
        updateEdges();
        view->setAccessibleDescription(QString::fromUtf8("%1. Выбор этапа: вверх/вниз или список этапов. Ctrl + колесо — масштаб.")
            .arg(node ? stageChoice->currentText() : QString::fromUtf8("Этап не выбран")));
    };
    const auto selectStage = [&](const QString& id, bool focusView) {
        const auto found = nodes.find(id.toUtf8().toStdString());
        if (found == nodes.end()) return;
        {
            const QSignalBlocker blocker(scene);
            scene->clearSelection();
            found->second->setSelected(true);
        }
        refreshSelection();
        if (focusView) {
            view->setFocus(Qt::OtherFocusReason);
            found->second->setFocus(Qt::OtherFocusReason);
        }
    };
    constexpr qreal minimumZoom = 0.05, maximumZoom = 3.0;
    const auto setZoom = [&](qreal value, bool keepSelected) {
        value = std::isfinite(value) ? std::clamp(value, minimumZoom, maximumZoom) : 1.0;
        view->setTransform(QTransform::fromScale(value, value));
        view->setProperty("pipelineMapZoomPercent", qRound(value * 100));
        zoomValue->setText(QStringLiteral("%1%").arg(qRound(value * 100)));
        zoomValue->setAccessibleDescription(zoomValue->text());
        zoomOut->setEnabled(!nodes.empty() && value > minimumZoom + 0.0001);
        zoomIn->setEnabled(!nodes.empty() && value < maximumZoom - 0.0001);
        if (keepSelected) if (const auto* node = selectedNode()) view->centerOn(node);
    };
    QObject::connect(scene, &QGraphicsScene::selectionChanged, &dialog, refreshSelection);
    QObject::connect(stageChoice, &QComboBox::currentIndexChanged, &dialog, [&](int) {
        selectStage(stageChoice->currentData().toString(), false);
    });
    QObject::connect(details, &QTextBrowser::anchorClicked, &dialog, [&](const QUrl& url) {
        if (url.scheme() == QStringLiteral("pipeline"))
            selectStage(QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).toUtf8()), false);
    });
    QObject::connect(detailsToggle, &QToolButton::toggled, &dialog, [&](bool expanded) {
        details->setVisible(expanded);
        detailsToggle->setAccessibleDescription(expanded ? QString::fromUtf8("Подробности раскрыты") : QString::fromUtf8("Подробности свёрнуты"));
        layout->activate();
        if (const auto* node = selectedNode()) view->ensureVisible(node->sceneBoundingRect(), 16, 16);
    });
    QObject::connect(zoomOut, &QToolButton::clicked, &dialog, [&] { setZoom(view->transform().m11() / 1.2, true); });
    QObject::connect(zoomIn, &QToolButton::clicked, &dialog, [&] { setZoom(view->transform().m11() * 1.2, true); });
    QObject::connect(reset, &QToolButton::clicked, &dialog, [&] { setZoom(1.0, true); });
    QObject::connect(fit, &QToolButton::clicked, &dialog, [&] {
        const auto rect = scene->sceneRect();
        if (rect.isEmpty()) return;
        setZoom(std::min((view->viewport()->width() - 32) / rect.width(), (view->viewport()->height() - 32) / rect.height()), false);
        view->centerOn(rect.center());
    });
    view->zoomRequested = [&](qreal zoom) { setZoom(zoom, false); };
    view->resetRequested = [&] { setZoom(1.0, true); };
    view->selectionRequested = [&](int delta) {
        if (stageChoice->count())
            selectStage(stageChoice->itemData(std::clamp(stageChoice->currentIndex() + delta, 0, stageChoice->count() - 1)).toString(), true);
    };
    for (auto* widget : std::initializer_list<QWidget*>{stageChoice, zoomOut, zoomIn, fit, reset, detailsToggle, view, details, buttons->button(QDialogButtonBox::Close)})
        widget->setFocusPolicy(Qt::StrongFocus);
    QWidget::setTabOrder(stageChoice, zoomOut);
    QWidget::setTabOrder(zoomOut, zoomIn);
    QWidget::setTabOrder(zoomIn, fit);
    QWidget::setTabOrder(fit, reset);
    QWidget::setTabOrder(reset, detailsToggle);
    QWidget::setTabOrder(detailsToggle, view);
    QWidget::setTabOrder(view, details);
    QWidget::setTabOrder(details, buttons->button(QDialogButtonBox::Close));
    stageChoice->setEnabled(!nodes.empty());
    detailsToggle->setEnabled(!nodes.empty());
    detailsToggle->setAccessibleDescription(QString::fromUtf8("Подробности свёрнуты"));
    fit->setEnabled(!nodes.empty());
    reset->setEnabled(!nodes.empty());
    relayout();
    setZoom(1.0, false);
    if (!nodes.empty()) selectStage(nodes.find(selectedId) != nodes.end() ? q(selectedId) : stageChoice->itemData(0).toString(), false);
    else refreshSelection();
    dialog.appearanceChanged = [&] {
        relayout();
        refreshSelection();
    };
    dialog.shown = [&] { if (const auto* node = selectedNode()) view->centerOn(node); };
    // Correct only a real modal resize, after its HFW toolbar has settled.
    // Scrollbar changes from manual pan/zoom do not trigger this callback.
    dialog.resized = [&] {
        layout->activate();
        keepSelectedVisible();
    };
    dialog.exec();
    dialog.appearanceChanged = {};
    dialog.shown = {};
    dialog.resized = {};
    view->zoomRequested = {};
    view->resetRequested = {};
    view->selectionRequested = {};
    // The modal owns the widgets; disconnect captures before local graph data dies.
    for (auto* sender : std::initializer_list<QObject*>{scene, stageChoice, details, detailsToggle, zoomOut, zoomIn, reset, fit})
        QObject::disconnect(sender, nullptr, &dialog, nullptr);
    return true;
}
