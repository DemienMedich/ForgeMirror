#include "QtPipelineMap.h"

#include <QtWidgets>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }

class PipelineNode final : public QGraphicsObject {
public:
    PipelineNode(const PipelineStep& step, bool missing, QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent), step_(step), missing_(missing) {
        setFlag(QGraphicsItem::ItemIsSelectable);
        setData(Qt::UserRole, q(step.id));
        setToolTip(missing ? QString::fromUtf8("Этап отсутствует: %1").arg(q(step.id))
                           : QString::fromUtf8("%1 · %2").arg(q(step.stageCode), q(step.title)));
    }
    QRectF boundingRect() const override { return {0, 0, 184, 76}; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        QColor fill = missing_ ? QColor("#4a3035") : QColor("#2c2c36");
        if (isSelected()) fill = QColor("#3a3150");
        painter->setBrush(fill);
        painter->setPen(QPen(isSelected() ? QColor("#a98adb") : missing_ ? QColor("#b86d6d") : QColor("#51466b"), isSelected() ? 2.0 : 1.0));
        painter->drawRoundedRect(boundingRect(), 7, 7);
        painter->setPen(missing_ ? QColor("#ffb2a8") : QColor("#eeeeef"));
        QFont titleFont = painter->font(); titleFont.setWeight(QFont::DemiBold); painter->setFont(titleFont);
        painter->drawText(QRectF(11, 8, 162, 32), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(titleFont).elidedText(missing_ ? q(step_.id) : q(step_.title), Qt::ElideRight, 162));
        QFont metaFont = painter->font(); metaFont.setPointSizeF(std::max(8.0, metaFont.pointSizeF() - 1)); painter->setFont(metaFont);
        painter->setPen(QColor("#b6b3c2"));
        const auto meta = missing_ ? QString::fromUtf8("Недоступная связь")
            : QString::fromUtf8("%1 · %2 → %3").arg(q(step_.stageCode).isEmpty() ? QString::fromUtf8("без кода") : q(step_.stageCode))
                .arg(q(step_.owner).isEmpty() ? QString::fromUtf8("без ответственного") : q(step_.owner)).arg(step_.nextIds.size());
        painter->drawText(QRectF(11, 43, 162, 22), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(metaFont).elidedText(meta, Qt::ElideRight, 162));
    }
    const PipelineStep& step() const { return step_; }
    bool missing() const { return missing_; }
private:
    PipelineStep step_;
    bool missing_;
};

class PipelineMapView final : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const qreal factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
            scale(factor, factor);
            event->accept();
            return;
        }
        QGraphicsView::wheelEvent(event);
    }
};

QString describe(const PipelineNode* node) {
    if (!node) return QString::fromUtf8("Выберите этап на карте, чтобы увидеть описание и допустимые переходы.");
    const auto& step = node->step();
    if (node->missing()) return QString::fromUtf8("<b>Связь на отсутствующий этап</b><br>%1").arg(q(step.id).toHtmlEscaped());
    QStringList targets;
    for (const auto& id : step.nextIds) targets << q(id).toHtmlEscaped();
    return QString::fromUtf8("<b>%1 · %2</b><br>Ветка: %3<br>Ответственный: %4<br><br>%5<br><br><b>Переходы:</b> %6")
        .arg(q(step.stageCode).toHtmlEscaped(), q(step.title).toHtmlEscaped(),
             q(step.branch).toHtmlEscaped().isEmpty() ? QString::fromUtf8("Общая") : q(step.branch).toHtmlEscaped(),
             q(step.owner).toHtmlEscaped().isEmpty() ? QString::fromUtf8("—") : q(step.owner).toHtmlEscaped(),
             q(step.description).toHtmlEscaped().replace('\n', "<br>"),
             targets.isEmpty() ? QString::fromUtf8("нет · конечный этап") : targets.join(QString::fromUtf8(", ")));
}

QPointF edgePoint(const QPointF& origin, const QPointF& target, qreal offset) {
    const qreal delta = target.x() - origin.x();
    return {origin.x() + delta * offset, origin.y()};
}
}

bool ShowQtPipelineMap(QWidget* parent, const std::vector<PipelineStep>& steps) {
    QDialog dialog(parent);
    dialog.setObjectName("pipelineMapDialog");
    dialog.setWindowTitle(QString::fromUtf8("Карта ветвлений пайплайна"));
    dialog.resize(1040, 740);
    dialog.setMinimumSize(720, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel;
    summary->setObjectName("pipelineMapSummary");
    summary->setTextFormat(Qt::PlainText);
    layout->addWidget(summary);
    auto* view = new PipelineMapView;
    view->setObjectName("pipelineMapView");
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::ScrollHandDrag);
    view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setToolTip(QString::fromUtf8("Ctrl + колесо — масштаб; перетаскивание — перемещение карты"));
    auto* scene = new QGraphicsScene(view);
    scene->setBackgroundBrush(dialog.palette().color(QPalette::Base));
    view->setScene(scene);
    layout->addWidget(view, 1);
    auto* details = new QTextBrowser;
    details->setObjectName("pipelineMapDetails");
    details->setMaximumHeight(105);
    details->setOpenExternalLinks(false);
    layout->addWidget(details);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8("Закрыть"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::map<QString, std::vector<const PipelineStep*>> lanes;
    std::unordered_map<std::string, const PipelineStep*> byId;
    std::unordered_map<std::string, size_t> columns;
    std::unordered_set<std::string> missingIds;
    int transitionCount = 0;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        byId.emplace(step.id, &step);
        columns.emplace(step.id, i);
        lanes[q(step.branch).trimmed().isEmpty() ? QString::fromUtf8("Общая") : q(step.branch)].push_back(&step);
    }
    for (const auto& step : steps) {
        transitionCount += int(step.nextIds.size());
        for (const auto& target : step.nextIds) if (byId.find(target) == byId.end()) missingIds.insert(target);
    }
    std::vector<QString> laneOrder;
    for (const auto& lane : lanes) laneOrder.push_back(lane.first);
    if (!missingIds.empty()) laneOrder.push_back(QString::fromUtf8("Отсутствующие этапы"));

    std::unordered_map<std::string, PipelineNode*> nodes;
    std::unordered_map<std::string, QPointF> positions;
    const qreal xStep = 204.0, yStep = 118.0, left = 160.0, top = 52.0;
    qreal maxX = 760.0;
    for (size_t laneIndex = 0; laneIndex < laneOrder.size(); ++laneIndex) {
        const auto& laneName = laneOrder[laneIndex];
        const qreal y = top + qreal(laneIndex) * yStep;
        scene->addText(laneName)->setPos(16, y + 26);
        if (laneName == QString::fromUtf8("Отсутствующие этапы")) {
            size_t column = 0;
            for (const auto& missing : missingIds) {
                PipelineStep placeholder; placeholder.id = missing;
                auto* node = new PipelineNode(placeholder, true);
                const QPointF pos(left + qreal(steps.size() + column++) * xStep, y);
                scene->addItem(node); node->setPos(pos); nodes[missing] = node; positions[missing] = pos;
                maxX = std::max(maxX, pos.x() + 220.0);
            }
            continue;
        }
        size_t column = 0;
        for (const auto* step : lanes[laneName]) {
            auto* node = new PipelineNode(*step, false);
            const auto position = columns.find(step->id);
            const auto stageColumn = position == columns.end() ? column++ : position->second;
            const QPointF pos(left + qreal(stageColumn) * xStep, y);
            scene->addItem(node); node->setPos(pos); nodes[step->id] = node; positions[step->id] = pos;
            maxX = std::max(maxX, pos.x() + 220.0);
        }
    }
    for (const auto& step : steps) {
        const auto source = positions.find(step.id);
        if (source == positions.end()) continue;
        for (const auto& targetId : step.nextIds) {
            const auto target = positions.find(targetId);
            if (target == positions.end()) continue;
            QPainterPath path;
            const QPointF start(source->second.x() + 184, source->second.y() + 38);
            const QPointF finish(target->second.x(), target->second.y() + 38);
            if (step.id == targetId) {
                const QPointF loopStart(source->second.x() + 148, source->second.y());
                const QPointF loopEnd(source->second.x() + 36, source->second.y());
                path.moveTo(loopStart);
                path.cubicTo(loopStart + QPointF(44, -58), loopEnd + QPointF(-44, -58), loopEnd);
            } else {
                path.moveTo(start);
                const qreal bend = std::copysign(std::max<qreal>(36, std::abs(finish.x() - start.x()) * 0.42), finish.x() - start.x());
                path.cubicTo(start + QPointF(bend, 0), finish - QPointF(bend, 0), finish);
            }
            auto* edge = scene->addPath(path, QPen(QColor("#8c76b0"), 1.6));
            edge->setZValue(-1);
            const QPointF tangent = path.pointAtPercent(0.98);
            const QPointF tip = path.pointAtPercent(1.0);
            QLineF direction(tangent, tip);
            const QPointF unit(std::cos(qDegreesToRadians(direction.angle())), -std::sin(qDegreesToRadians(direction.angle())));
            const QPointF normal(-unit.y(), unit.x());
            QPolygonF arrow{tip, tip - unit * 10 + normal * 4.5, tip - unit * 10 - normal * 4.5};
            auto* arrowItem = scene->addPolygon(arrow, Qt::NoPen, QBrush(QColor("#8c76b0")));
            arrowItem->setZValue(-1);
        }
    }
    const qreal sceneHeight = laneOrder.empty() ? 240 : top + qreal(laneOrder.size() - 1) * yStep + 100;
    scene->setSceneRect(0, 0, maxX, std::max<qreal>(240, sceneHeight));
    summary->setText(QString::fromUtf8("Этапов: %1 · переходов: %2 · недоступных связей: %3 · масштаб: Ctrl + колесо")
        .arg(steps.size()).arg(transitionCount).arg(missingIds.size()));
    QObject::connect(scene, &QGraphicsScene::selectionChanged, &dialog, [scene, details] {
        const auto selected = scene->selectedItems();
        details->setHtml(selected.isEmpty() ? describe(nullptr) : describe(dynamic_cast<PipelineNode*>(selected.front())));
    });
    if (!steps.empty()) {
        for (const auto& step : steps) {
            const auto node = nodes.find(step.id);
            if (node != nodes.end()) { node->second->setSelected(true); break; }
        }
    }
    details->setHtml(steps.empty() ? QString::fromUtf8("Пайплайн пуст.") : describe(dynamic_cast<PipelineNode*>(scene->selectedItems().value(0))));
    dialog.exec();
    return true;
}
