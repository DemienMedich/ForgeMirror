#include "QtLogActivityChart.h"

#include <QDateTime>
#include <QEvent>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
qreal textHeight(const QString& text, const QFont& font, qreal width, int alignment) {
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(alignment & Qt::TextWordWrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(Qt::Alignment(alignment & Qt::AlignHorizontal_Mask));
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max<qreal>(1, width));
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return std::ceil(std::max(height, QFontMetricsF(font).height()));
}

void drawText(QPainter& painter, const QtLogActivityChart::TextRegion& region) {
    QTextLayout layout(region.text, region.font);
    QTextOption option;
    option.setWrapMode(region.wrapMode);
    option.setAlignment(Qt::Alignment(region.alignment & Qt::AlignHorizontal_Mask));
    layout.setTextOption(option);
    layout.beginLayout();
    qreal y = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max<qreal>(1, region.rect.width()));
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout.endLayout();
    layout.draw(&painter, region.rect.topLeft());
}
}

QtLogActivityChart::QtLogActivityChart(QWidget* parent) : QWidget(parent) {
    setObjectName("logActivityChart");
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setAccessibleName(QString::fromUtf8("Активность журнала Qt"));
    setToolTip(QString::fromUtf8("Число записей по 16 временным интервалам. График не зависит от фильтров."));
    updateChartGeometry();
}

std::array<int, 16> QtLogActivityChart::BuildHistogram(const std::vector<AppLogEntry>& entries) {
    std::array<int, 16> result{};
    if (entries.empty()) return result;
    std::int64_t minTimestamp = 0;
    std::int64_t maxTimestamp = 0;
    for (const auto& entry : entries) {
        if (entry.timestamp <= 0) continue;
        if (minTimestamp == 0 || entry.timestamp < minTimestamp) minTimestamp = entry.timestamp;
        if (maxTimestamp == 0 || entry.timestamp > maxTimestamp) maxTimestamp = entry.timestamp;
    }
    if (minTimestamp > 0 && maxTimestamp > minTimestamp) {
        const double span = static_cast<double>(maxTimestamp - minTimestamp);
        int fallbackIndex = 0;
        for (const auto& entry : entries) {
            int bin = 0;
            if (entry.timestamp > 0) {
                const double position = static_cast<double>(entry.timestamp - minTimestamp) / span;
                bin = std::clamp(int(position * (result.size() - 1)), 0, int(result.size()) - 1);
            } else {
                bin = std::clamp(int((fallbackIndex * int(result.size())) / int(entries.size())), 0, int(result.size()) - 1);
                ++fallbackIndex;
            }
            ++result[size_t(bin)];
        }
    } else {
        int index = 0;
        for (const auto& entry : entries) {
            (void)entry;
            const int bin = std::clamp(int((index * int(result.size())) / int(entries.size())), 0, int(result.size()) - 1);
            ++result[size_t(bin)];
            ++index;
        }
    }
    return result;
}

void QtLogActivityChart::setEntries(const std::vector<AppLogEntry>& entries) {
    values_ = BuildHistogram(entries);
    firstTimestamp_ = 0;
    lastTimestamp_ = 0;
    bool hasMissingTimestamp = false;
    for (const auto& entry : entries) {
        if (entry.timestamp <= 0) { hasMissingTimestamp = true; continue; }
        if (firstTimestamp_ == 0 || entry.timestamp < firstTimestamp_) firstTimestamp_ = entry.timestamp;
        if (entry.timestamp > lastTimestamp_) lastTimestamp_ = entry.timestamp;
    }
    QStringList bins;
    for (int index = 0; index < int(values_.size()); ++index)
        bins << QString::fromUtf8("Интервал %1: %2").arg(index + 1).arg(values_[size_t(index)]);
    QString description = QString::fromUtf8("Распределение %1 записей журнала по 16 интервалам от самых ранних к поздним. График не зависит от фильтров. ")
        .arg(entries.size());
    if (firstTimestamp_ > 0) {
        description += QString::fromUtf8("Диапазон записей с временной меткой: %1 — %2. ")
            .arg(QDateTime::fromSecsSinceEpoch(firstTimestamp_).toString("yyyy-MM-dd HH:mm"),
                 QDateTime::fromSecsSinceEpoch(lastTimestamp_).toString("yyyy-MM-dd HH:mm"));
    }
    if (firstTimestamp_ == 0 || firstTimestamp_ == lastTimestamp_)
        description += QString::fromUtf8("Временной диапазон неразличим; интервалы сформированы по исходному порядку записей. ");
    else if (hasMissingTimestamp)
        description += QString::fromUtf8("Записи без временной метки распределены по исходному порядку. ");
    description += QString::fromUtf8("Число записей по интервалам: ") + bins.join(QStringLiteral("; ")) + QLatin1Char('.');
    setAccessibleDescription(description);
    updateChartGeometry();
}

QtLogActivityChart::LayoutMetrics QtLogActivityChart::layoutMetrics(int availableWidth) const {
    LayoutMetrics result;
    const qreal width = std::max(1, availableWidth);
    auto titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    auto captionFont = font();
    captionFont.setWeight(QFont::Normal);
    if (captionFont.pointSizeF() > 0) captionFont.setPointSizeF(std::max(8.0, captionFont.pointSizeF() - 1.0));
    else if (captionFont.pixelSize() > 0) captionFont.setPixelSize(std::max(8, captionFont.pixelSize() - 1));
    qreal y = 0;
    auto append = [&](const QString& role, const QString& text, const QFont& textFont,
                      qreal left, qreal textWidth, int alignment = Qt::AlignLeft | Qt::TextWordWrap) {
        TextRegion region{text, QRectF(left, y, textWidth,
            textHeight(text, textFont, textWidth, alignment)), textFont, alignment, role};
        region.wrapMode = alignment & Qt::TextWordWrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap;
        result.texts.push_back(region);
        return region.rect.height();
    };
    y += append(QStringLiteral("title"), QString::fromUtf8("Активность журнала"), titleFont, 0, width);
    y += append(QStringLiteral("caption"), QString::fromUtf8("%1 интервалов · %2 записей · без фильтров")
        .arg(values_.size()).arg(std::accumulate(values_.begin(), values_.end(), std::int64_t(0))), captionFont, 0, width);
    y += 4;
    const qreal plotHeight = std::ceil(std::max<qreal>(24, QFontMetricsF(captionFont).height() * 2));
    result.plot = QRectF(0, y, width, plotHeight);
    const qreal gap = std::min<qreal>(4, width / (values_.size() * 2));
    const qreal barWidth = std::max<qreal>(0, (width - gap * (values_.size() - 1)) / values_.size());
    for (int index = 0; index < int(values_.size()); ++index)
        result.bars[size_t(index)] = QRectF(index * (barWidth + gap), y, barWidth, plotHeight);
    if (std::all_of(values_.begin(), values_.end(), [](int value) { return value == 0; })) {
        const auto empty = QString::fromUtf8("Нет записей журнала");
        const qreal emptyHeight = textHeight(empty, captionFont, width, Qt::AlignHCenter | Qt::TextWordWrap);
        result.plot.setHeight(std::max(plotHeight, emptyHeight));
        result.texts.push_back({empty, QRectF(0, y + (result.plot.height() - emptyHeight) / 2,
            width, emptyHeight), captionFont, Qt::AlignHCenter | Qt::TextWordWrap, QStringLiteral("emptyMessage")});
    }
    y = result.plot.bottom() + 4;
    QString start = QString::fromUtf8("—"), end = start;
    if (firstTimestamp_ > 0) start = QDateTime::fromSecsSinceEpoch(firstTimestamp_).toString("dd.MM HH:mm");
    if (lastTimestamp_ > 0) end = QDateTime::fromSecsSinceEpoch(lastTimestamp_).toString("dd.MM HH:mm");
    const QFontMetricsF captionMetrics(captionFont);
    const qreal dateWidth = std::max(captionMetrics.horizontalAdvance(start), captionMetrics.horizontalAdvance(end)) + 4;
    result.datesStacked = dateWidth * 2 + 4 > width;
    if (result.datesStacked) {
        y += append(QStringLiteral("dateStart"), start, captionFont, 0, width, Qt::AlignLeft);
        y += append(QStringLiteral("dateEnd"), end, captionFont, 0, width, Qt::AlignRight);
    } else {
        const qreal dateHeight = append(QStringLiteral("dateStart"), start, captionFont, 0, (width - 4) / 2, Qt::AlignLeft);
        const qreal endHeight = append(QStringLiteral("dateEnd"), end, captionFont, (width + 4) / 2,
            (width - 4) / 2, Qt::AlignRight);
        y += std::max(dateHeight, endHeight);
    }
    result.requiredHeight = int(std::ceil(y));
    return result;
}

bool QtLogActivityChart::hasHeightForWidth() const { return true; }
int QtLogActivityChart::heightForWidth(int availableWidth) const { return layoutMetrics(availableWidth).requiredHeight; }
QSize QtLogActivityChart::sizeHint() const { return QSize(640, heightForWidth(640)); }
QSize QtLogActivityChart::minimumSizeHint() const { return QSize(0, 0); }

void QtLogActivityChart::updateChartGeometry() {
    const int required = heightForWidth(std::max(1, width()));
    if (minimumHeight() != required) setMinimumHeight(required);
    updateGeometry();
    update();
}

void QtLogActivityChart::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateChartGeometry();
}

void QtLogActivityChart::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (event->oldSize().width() != event->size().width()) updateChartGeometry();
}

void QtLogActivityChart::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    const auto geometry = layoutMetrics(width());
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto text = palette().color(QPalette::WindowText);
    const auto muted = palette().color(QPalette::Disabled, QPalette::Text);
    const auto track = palette().color(QPalette::AlternateBase);
    const auto accent = palette().color(QPalette::Highlight);
    const int maximum = *std::max_element(values_.begin(), values_.end());
    for (int index = 0; maximum > 0 && index < int(values_.size()); ++index) {
        const QRectF background = geometry.bars[size_t(index)];
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(background, 2.0, 2.0);
        if (maximum > 0 && values_[size_t(index)] > 0) {
            const qreal filledHeight = background.height() * values_[size_t(index)] / maximum;
            painter.setBrush(accent);
            painter.drawRoundedRect(QRectF(background.left(), background.bottom() - filledHeight,
                background.width(), filledHeight), 2.0, 2.0);
        }
    }
    for (const auto& region : geometry.texts) {
        painter.setPen(region.role == QStringLiteral("title") ? text : muted);
        drawText(painter, region);
    }
}
