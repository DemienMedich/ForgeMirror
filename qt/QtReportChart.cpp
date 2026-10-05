#include "QtReportChart.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QDateTime>
#include <QFontMetricsF>
#include <QResizeEvent>
#include <QStringList>
#include <QTextLayout>
#include <QtMath>
#include <algorithm>

namespace {
constexpr qreal textGap = 4;
constexpr qreal sectionGap = 8;
constexpr qreal statusBarHeight = 8;

// Measurement and painting share the same wrapping and line-height rules.
qreal arrangeText(QTextLayout& layout, qreal width, Qt::Alignment alignment, QTextOption::WrapMode wrapMode) {
    QTextOption option;
    option.setWrapMode(wrapMode);
    option.setAlignment(alignment);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max(qreal(1), width));
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return qCeil(height);
}

QtReportChart::TextRegion textRegion(const QString& role, const QString& text, const QFont& font,
                                    qreal left, qreal top, qreal width, Qt::Alignment alignment = Qt::AlignLeft) {
    QtReportChart::TextRegion result;
    result.role = role;
    result.text = text;
    result.font = font;
    result.alignment = alignment;
    QTextLayout layout(text, font);
    result.rect = QRectF(left, top, std::max(qreal(1), width), arrangeText(layout, width, alignment, result.wrapMode));
    return result;
}

void drawTextRegion(QPainter& painter, const QtReportChart::TextRegion& text) {
    if (text.text.isEmpty()) return;
    QTextLayout layout(text.text, text.font);
    arrangeText(layout, text.rect.width(), text.alignment, text.wrapMode);
    layout.draw(&painter, text.rect.topLeft());
}

qreal textWidth(const QFontMetricsF& metrics, const QString& text) {
    return qCeil(std::max(metrics.horizontalAdvance(text), metrics.boundingRect(text).width()));
}
}

QtReportChart::QtReportChart(QWidget* parent) : QWidget(parent) {
    setObjectName("statisticsStatusChart");
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    const auto today = QDate::currentDate();
    trendFirstMonth_ = QDate(today.year(), today.month(), 1).addMonths(-11);
    setAccessibleName(QString::fromUtf8("Распределение задач по текущим статусам"));
    setToolTip(QString::fromUtf8("Сверху — текущие статусы задач выбранного периода. Снизу — переходы в «Выполнена» по сохранённому аудиту за последние 12 месяцев."));
}

void QtReportChart::setCompletionTrend(const std::array<int, 12>& monthlyCompletions) {
    monthlyCompletions_ = monthlyCompletions;
    const auto today = QDate::currentDate();
    trendFirstMonth_ = QDate(today.year(), today.month(), 1).addMonths(-11);
    QStringList months;
    for (int index = 0; index < int(monthlyCompletions_.size()); ++index)
        months << QStringLiteral("%1: %2").arg(trendFirstMonth_.addMonths(index).toString("yyyy-MM"))
            .arg(monthlyCompletions_[size_t(index)]);
    completionTrendDescription_ = QString::fromUtf8("Завершения по месяцам (ГГГГ-ММ: число), последние 12 месяцев: ") +
        months.join(QStringLiteral("; ")) + QString::fromUtf8(". Учитываются все загруженные события task-audit.log за этот период.");
    updateAccessibleDescription();
    updateGeometry();
    update();
}

std::array<int, 12> QtReportChart::BuildMonthlyCompletionTrend(const std::vector<TaskAuditEntry>& audit,
                                                               const QDate& currentDate) {
    std::array<int, 12> result{};
    if (!currentDate.isValid()) return result;
    const auto firstMonth = QDate(currentDate.year(), currentDate.month(), 1).addMonths(-11);
    for (const auto& entry : audit) {
        if (entry.field != "status" || QString::fromUtf8(entry.newValue.data(), int(entry.newValue.size())) != QString::fromUtf8("Выполнена") || entry.timestamp <= 0)
            continue;
        const auto date = QDateTime::fromSecsSinceEpoch(entry.timestamp).date();
        const auto month = QDate(date.year(), date.month(), 1);
        const int index = (month.year() - firstMonth.year()) * 12 + month.month() - firstMonth.month();
        if (index >= 0 && index < int(result.size())) ++result[size_t(index)];
    }
    return result;
}

void QtReportChart::setValues(int newTasks, int inProgressTasks, int doneTasks, const QString& periodLabel) {
    values_[0] = std::max(0, newTasks);
    values_[1] = std::max(0, inProgressTasks);
    values_[2] = std::max(0, doneTasks);
    periodLabel_ = periodLabel;
    updateAccessibleDescription();
    updateGeometry();
    update();
}

void QtReportChart::updateAccessibleDescription() {
    QString description = QString::fromUtf8("%1. Новых: %2; в работе: %3; выполнено: %4.")
        .arg(periodLabel_).arg(values_[0]).arg(values_[1]).arg(values_[2]);
    if (!completionTrendDescription_.isEmpty()) description += QLatin1Char(' ') + completionTrendDescription_;
    setAccessibleDescription(description);
}

QtReportChart::LayoutMetrics QtReportChart::layoutMetrics(int availableWidth) const {
    LayoutMetrics result;
    const qreal contentWidth = std::max(1, availableWidth);
    auto titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    auto captionFont = font();
    captionFont.setWeight(QFont::Normal);
    if (captionFont.pointSizeF() > 0)
        captionFont.setPointSizeF(std::max(8.0, captionFont.pointSizeF() - 1.0));
    const QFontMetricsF captionMetrics(captionFont);
    const QString labels[] = {QString::fromUtf8("Новые"), QString::fromUtf8("В работе"), QString::fromUtf8("Выполнены")};

    result.title = textRegion(QStringLiteral("title"), QString::fromUtf8("Задачи по текущему состоянию"),
        titleFont, 0, 0, contentWidth);
    result.period = textRegion(QStringLiteral("period"), periodLabel_ + QString::fromUtf8(" · данные на сейчас"),
        captionFont, 0, result.title.rect.bottom() + textGap, contentWidth);
    qreal top = result.period.rect.bottom() + textGap;
    const qreal columnWidth = std::max(qreal(1), (contentWidth - sectionGap * 2) / 3);
    for (int index = 0; index < 3; ++index) {
        if (std::max(textWidth(captionMetrics, labels[index]),
                textWidth(captionMetrics, QString::number(values_[index]))) > columnWidth) {
            result.statusColumns = 1;
            break;
        }
    }
    const qreal statusWidth = result.statusColumns == 3 ? columnWidth : contentWidth;
    qreal rowBottom = top;
    for (int index = 0; index < 3; ++index) {
        auto& status = result.statuses[size_t(index)];
        const qreal left = result.statusColumns == 3 ? index * (columnWidth + sectionGap) : 0;
        const QString count = QString::number(values_[index]);
        const qreal numberWidth = textWidth(captionMetrics, count);
        const bool inlineCount = textWidth(captionMetrics, labels[index]) + textGap + numberWidth <= statusWidth;
        status.label = textRegion(QStringLiteral("status-label-%1").arg(index), labels[index], captionFont,
            left, top, inlineCount ? statusWidth - numberWidth - textGap : statusWidth);
        status.value = textRegion(QStringLiteral("status-value-%1").arg(index), count, captionFont,
            inlineCount ? left + statusWidth - numberWidth : left,
            inlineCount ? top : status.label.rect.bottom() + textGap,
            inlineCount ? numberWidth : statusWidth, Qt::AlignRight);
        const qreal textBottom = std::max(status.label.rect.bottom(), status.value.rect.bottom());
        rowBottom = std::max(rowBottom, textBottom);
        if (result.statusColumns == 1) {
            status.track = QRectF(left, textBottom + textGap, statusWidth, statusBarHeight);
            top = status.track.bottom() + sectionGap;
        }
    }
    if (result.statusColumns == 3) {
        for (int index = 0; index < 3; ++index)
            result.statuses[size_t(index)].track = QRectF(index * (columnWidth + sectionGap),
                rowBottom + textGap, columnWidth, statusBarHeight);
        top = result.statuses.back().track.bottom() + sectionGap;
    }
    result.trendCaption = textRegion(QStringLiteral("trend-caption"),
        QString::fromUtf8("Завершения по месяцу перехода · последние 12 месяцев · весь доступный аудит"),
        captionFont, 0, top, contentWidth);
    top = result.trendCaption.rect.bottom() + textGap;
    const int trendMax = *std::max_element(monthlyCompletions_.begin(), monthlyCompletions_.end());
    const QString firstDate = trendFirstMonth_.toString("MM/yy");
    const QString lastDate = trendFirstMonth_.addMonths(11).toString("MM/yy");
    const qreal edgeWidth = std::max(textWidth(captionMetrics, firstDate), textWidth(captionMetrics, lastDate));
    const qreal plotInset = std::min(contentWidth / 4, std::max(textGap, edgeWidth / 2));
    // The populated trend is a readable chart, not a compressed sparkline.
    // Empty history remains compact and keeps its explanatory message.
    qreal plotHeight = trendMax > 0
        ? std::max(qreal(72), qreal(qCeil(captionMetrics.height() * 4.0)))
        : std::max(qreal(32), qreal(qCeil(captionMetrics.height() * 2.0)));
    if (trendMax == 0) {
        result.emptyMessage = textRegion(QStringLiteral("empty-trend"),
            QString::fromUtf8("Нет завершений в сохранённой части аудита за этот период"), captionFont,
            0, top, contentWidth, Qt::AlignHCenter);
        plotHeight = std::max(plotHeight, result.emptyMessage.rect.height());
        result.emptyMessage.rect.moveTop(top + (plotHeight - result.emptyMessage.rect.height()) / 2);
    } else {
        // Keep the peak's numeric annotation clear of the preceding caption.
        top += qCeil(captionMetrics.height()) + textGap;
    }
    result.plot = QRectF(plotInset, top, std::max(qreal(1), contentWidth - plotInset * 2), plotHeight);
    for (int index = 0; index < 12; ++index) {
        const qreal fraction = trendMax > 0 ? qreal(monthlyCompletions_[size_t(index)]) / trendMax : 0;
        result.trendPoints << QPointF(result.plot.left() + result.plot.width() * index / 11,
            result.plot.bottom() - result.plot.height() * fraction);
    }

    if (trendMax > 0) {
        // Prefer the peak and latest month; dense annotations must not overlap.
        const int peak = int(std::max_element(monthlyCompletions_.begin(), monthlyCompletions_.end()) - monthlyCompletions_.begin());
        std::vector<int> candidates{peak, 11};
        for (int index = 0; index < 12; ++index)
            if (index != peak && index != 11) candidates.push_back(index);
        for (const int index : candidates) {
            if (monthlyCompletions_[size_t(index)] <= 0 ||
                std::any_of(result.valueLabels.begin(), result.valueLabels.end(), [index](const auto& label) { return label.monthIndex == index; })) continue;
            const QString number = QString::number(monthlyCompletions_[size_t(index)]);
            const qreal numberWidth = std::min(contentWidth, textWidth(captionMetrics, number));
            const auto& point = result.trendPoints[index];
            auto region = textRegion(QStringLiteral("trend-value-%1").arg(index), number, captionFont,
                std::clamp(point.x() - numberWidth / 2, qreal(0), contentWidth - numberWidth),
                0, numberWidth, Qt::AlignHCenter);
            region.rect.moveTop(point.y() - region.rect.height() - textGap);
            const bool overlaps = std::any_of(result.valueLabels.begin(), result.valueLabels.end(),
                [&region](const auto& label) { return label.label.rect.adjusted(-textGap, -textGap, textGap, textGap).intersects(region.rect); });
            if (!overlaps) result.valueLabels.push_back({index, region});
        }
    }

    const qreal datesTop = result.plot.bottom() + textGap;
    auto dateRegion = [&](int index) {
        const QString date = trendFirstMonth_.addMonths(index).toString("MM/yy");
        const qreal dateWidth = std::min(contentWidth, textWidth(captionMetrics, date));
        return textRegion(QStringLiteral("trend-date-%1").arg(index), date, captionFont,
            std::clamp(result.trendPoints[index].x() - dateWidth / 2, qreal(0), contentWidth - dateWidth),
            datesTop, dateWidth, Qt::AlignHCenter);
    };
    result.dateLabels.push_back({0, dateRegion(0)});
    auto last = dateRegion(11);
    if (result.dateLabels.front().label.rect.adjusted(-textGap, 0, textGap, 0).intersects(last.rect))
        last.rect.moveTop(result.dateLabels.front().label.rect.bottom() + textGap);
    result.dateLabels.push_back({11, last});
    for (int index = 1; index < 11; ++index) {
        const auto candidate = dateRegion(index);
        const bool overlaps = std::any_of(result.dateLabels.begin(), result.dateLabels.end(),
            [&candidate](const auto& label) { return label.label.rect.adjusted(-textGap, 0, textGap, 0).intersects(candidate.rect); });
        if (!overlaps) result.dateLabels.push_back({index, candidate});
    }
    std::sort(result.dateLabels.begin(), result.dateLabels.end(), [](const auto& left, const auto& right) { return left.monthIndex < right.monthIndex; });
    std::sort(result.valueLabels.begin(), result.valueLabels.end(), [](const auto& left, const auto& right) { return left.monthIndex < right.monthIndex; });
    qreal bottom = result.plot.bottom();
    result.texts = {result.title, result.period, result.trendCaption};
    if (!result.emptyMessage.text.isEmpty()) result.texts.push_back(result.emptyMessage);
    for (const auto& status : result.statuses) {
        result.texts.push_back(status.label);
        result.texts.push_back(status.value);
    }
    for (const auto& label : result.valueLabels) result.texts.push_back(label.label);
    for (const auto& label : result.dateLabels) {
        result.texts.push_back(label.label);
        bottom = std::max(bottom, label.label.rect.bottom());
    }
    result.requiredHeight = qCeil(bottom + textGap);
    return result;
}

QSize QtReportChart::sizeHint() const { return QSize(480, heightForWidth(480)); }
QSize QtReportChart::minimumSizeHint() const { return QSize(0, heightForWidth(std::max(1, width()))); }
bool QtReportChart::hasHeightForWidth() const { return true; }
int QtReportChart::heightForWidth(int width) const { return layoutMetrics(width).requiredHeight; }

void QtReportChart::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (event->oldSize().width() != event->size().width()) updateGeometry();
}

void QtReportChart::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        updateGeometry();
        update();
    }
}

void QtReportChart::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto textColor = palette().color(QPalette::WindowText);
    const auto mutedColor = palette().color(QPalette::Disabled, QPalette::Text);
    const auto trackColor = palette().color(QPalette::AlternateBase);
    const auto accentColor = palette().color(QPalette::Highlight);

    const auto layout = layoutMetrics(width());
    painter.setPen(textColor);
    drawTextRegion(painter, layout.title);
    painter.setPen(mutedColor);
    drawTextRegion(painter, layout.period);
    const int maxValue = std::max({values_[0], values_[1], values_[2]});
    for (int index = 0; index < 3; ++index) {
        const auto& status = layout.statuses[size_t(index)];
        painter.setPen(textColor);
        drawTextRegion(painter, status.label);
        drawTextRegion(painter, status.value);
        painter.setPen(Qt::NoPen);
        painter.setBrush(trackColor);
        painter.drawRoundedRect(status.track, 3.5, 3.5);
        if (maxValue > 0 && values_[index] > 0 && status.track.width() > 0) {
            const qreal filledWidth = status.track.width() * double(values_[index]) / double(maxValue);
            QLinearGradient fill(status.track.topLeft(), status.track.topRight());
            fill.setColorAt(0, accentColor.lighter(135));
            fill.setColorAt(1, accentColor);
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(status.track.left(), status.track.top(), filledWidth,
                status.track.height()), 3.5, 3.5);
        }
    }

    painter.setPen(mutedColor);
    drawTextRegion(painter, layout.trendCaption);
    painter.setPen(QPen(trackColor, 1));
    painter.drawLine(QPointF(layout.plot.left(), layout.plot.bottom()), QPointF(layout.plot.right(), layout.plot.bottom()));
    const int trendMax = *std::max_element(monthlyCompletions_.begin(), monthlyCompletions_.end());
    if (trendMax == 0) {
        painter.setPen(mutedColor);
        drawTextRegion(painter, layout.emptyMessage);
    } else {
        QPainterPath area;
        area.moveTo(layout.trendPoints.front());
        for (int index = 1; index < layout.trendPoints.size(); ++index)
            area.lineTo(layout.trendPoints[index]);
        area.lineTo(layout.plot.bottomRight());
        area.lineTo(layout.plot.bottomLeft());
        area.closeSubpath();
        QLinearGradient fill(layout.plot.topLeft(), layout.plot.bottomLeft());
        auto fillTop = accentColor; fillTop.setAlpha(90);
        auto fillBottom = accentColor; fillBottom.setAlpha(0);
        fill.setColorAt(0, fillTop); fill.setColorAt(1, fillBottom);
        painter.fillPath(area, fill);
        const auto lineColor = accentColor.lighter(150);
        painter.setPen(QPen(lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(layout.trendPoints);
        painter.setBrush(palette().color(QPalette::Window));
        for (const auto& point : layout.trendPoints) painter.drawEllipse(point, 2.5, 2.5);
        painter.setPen(textColor);
        for (const auto& label : layout.valueLabels) drawTextRegion(painter, label.label);
    }
    painter.setPen(mutedColor);
    for (const auto& label : layout.dateLabels) drawTextRegion(painter, label.label);
}
