#include "QtLogActivityChart.h"

#include <QDateTime>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>
#include <algorithm>
#include <numeric>

QtLogActivityChart::QtLogActivityChart(QWidget* parent) : QWidget(parent) {
    setObjectName("logActivityChart");
    setFixedHeight(82);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QString::fromUtf8("Активность журнала Qt"));
    setToolTip(QString::fromUtf8("Число записей по 16 временным интервалам. График не зависит от фильтров."));
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
    for (const auto& entry : entries) {
        if (entry.timestamp <= 0) continue;
        if (firstTimestamp_ == 0 || entry.timestamp < firstTimestamp_) firstTimestamp_ = entry.timestamp;
        if (entry.timestamp > lastTimestamp_) lastTimestamp_ = entry.timestamp;
    }
    QStringList bins;
    for (const int count : values_) bins << QString::number(count);
    setAccessibleDescription(QString::fromUtf8("Распределение %1 записей по 16 временным интервалам: ").arg(entries.size()) + bins.join(", "));
    update();
}

void QtLogActivityChart::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto text = palette().color(QPalette::WindowText);
    const auto muted = palette().color(QPalette::Disabled, QPalette::Text);
    const auto track = palette().color(QPalette::AlternateBase);
    const auto accent = palette().color(QPalette::Highlight);

    auto titleFont = painter.font();
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(text);
    painter.drawText(QRect(0, 0, width(), 19), Qt::AlignLeft | Qt::AlignVCenter,
        QString::fromUtf8("Активность журнала"));

    auto captionFont = painter.font();
    captionFont.setWeight(QFont::Normal);
    captionFont.setPointSizeF(std::max(8.0, captionFont.pointSizeF() - 1.0));
    painter.setFont(captionFont);
    painter.setPen(muted);
    const int total = std::accumulate(values_.begin(), values_.end(), 0);
    painter.drawText(QRect(0, 18, width(), 15), Qt::AlignLeft | Qt::AlignVCenter,
        QString::fromUtf8("%1 интервалов · %2 записей · без фильтров").arg(values_.size()).arg(total));

    constexpr qreal gap = 4.0;
    constexpr qreal top = 37.0;
    constexpr qreal plotHeight = 29.0;
    const qreal barWidth = std::max<qreal>(1.0, (width() - gap * (values_.size() - 1)) / values_.size());
    const int maximum = *std::max_element(values_.begin(), values_.end());
    for (int index = 0; index < int(values_.size()); ++index) {
        const qreal left = index * (barWidth + gap);
        const QRectF background(left, top, barWidth, plotHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(background, 2.0, 2.0);
        if (maximum > 0 && values_[size_t(index)] > 0) {
            const qreal filledHeight = plotHeight * values_[size_t(index)] / maximum;
            painter.setBrush(accent);
            painter.drawRoundedRect(QRectF(left, top + plotHeight - filledHeight, barWidth, filledHeight), 2.0, 2.0);
        }
    }
    QString start = QString::fromUtf8("—");
    QString end = start;
    if (firstTimestamp_ > 0) start = QDateTime::fromSecsSinceEpoch(firstTimestamp_).toString("dd.MM HH:mm");
    if (lastTimestamp_ > 0) end = QDateTime::fromSecsSinceEpoch(lastTimestamp_).toString("dd.MM HH:mm");
    painter.setPen(muted);
    painter.drawText(QRect(0, 66, width() / 2, 15), Qt::AlignLeft | Qt::AlignVCenter, start);
    painter.drawText(QRect(width() / 2, 66, width() - width() / 2, 15), Qt::AlignRight | Qt::AlignVCenter, end);
}
