#include "QtReportChart.h"

#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>
#include <algorithm>

QtReportChart::QtReportChart(QWidget* parent) : QWidget(parent) {
    setObjectName("statisticsStatusChart");
    setFixedHeight(76);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QString::fromUtf8("Распределение задач по текущим статусам"));
    setToolTip(QString::fromUtf8("Распределение задач выбранного периода по их текущим статусам; график не показывает историческую динамику."));
}

void QtReportChart::setValues(int newTasks, int inProgressTasks, int doneTasks, const QString& periodLabel) {
    values_[0] = std::max(0, newTasks);
    values_[1] = std::max(0, inProgressTasks);
    values_[2] = std::max(0, doneTasks);
    periodLabel_ = periodLabel;
    setAccessibleDescription(QString::fromUtf8("%1. Новых: %2; в работе: %3; выполнено: %4.")
        .arg(periodLabel_).arg(values_[0]).arg(values_[1]).arg(values_[2]));
    update();
}

void QtReportChart::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto textColor = palette().color(QPalette::WindowText);
    const auto mutedColor = palette().color(QPalette::Disabled, QPalette::Text);
    const auto trackColor = palette().color(QPalette::AlternateBase);
    const auto accentColor = palette().color(QPalette::Highlight);

    auto titleFont = painter.font();
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(textColor);
    painter.drawText(QRect(0, 0, width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
        QString::fromUtf8("Задачи по текущему состоянию"));

    auto captionFont = painter.font();
    captionFont.setWeight(QFont::Normal);
    captionFont.setPointSizeF(std::max(8.0, captionFont.pointSizeF() - 1.0));
    painter.setFont(captionFont);
    painter.setPen(mutedColor);
    painter.drawText(QRect(0, 19, width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
        periodLabel_ + QString::fromUtf8(" · данные на сейчас"));

    const QString labels[] = {QString::fromUtf8("Новые"), QString::fromUtf8("В работе"), QString::fromUtf8("Выполнены")};
    const int maxValue = std::max({values_[0], values_[1], values_[2]});
    const int columnWidth = width() / 3;
    const int barHeight = 7;
    for (int index = 0; index < 3; ++index) {
        const int left = index * columnWidth;
        const int right = index == 2 ? width() : left + columnWidth;
        const QRect labelRect(left + 1, 36, right - left - 8, 18);
        painter.setPen(textColor);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, labels[index]);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(values_[index]));

        const QRectF track(left + 1, 57, std::max(0, right - left - 10), barHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(trackColor);
        painter.drawRoundedRect(track, 3.5, 3.5);
        if (maxValue > 0 && values_[index] > 0 && track.width() > 0) {
            const qreal filledWidth = track.width() * double(values_[index]) / double(maxValue);
            painter.setBrush(accentColor);
            painter.drawRoundedRect(QRectF(track.left(), track.top(), filledWidth, track.height()), 3.5, 3.5);
        }
    }
}
