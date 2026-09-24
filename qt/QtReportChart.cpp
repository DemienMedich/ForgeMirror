#include "QtReportChart.h"

#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QDateTime>
#include <algorithm>

QtReportChart::QtReportChart(QWidget* parent) : QWidget(parent) {
    setObjectName("statisticsStatusChart");
    setFixedHeight(144);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QString::fromUtf8("Распределение задач по текущим статусам"));
    setToolTip(QString::fromUtf8("Сверху — текущие статусы задач выбранного периода. Снизу — переходы в «Выполнена» по дате из сохранённого аудита (до 200 событий)."));
}

void QtReportChart::setCompletionTrend(const std::array<int, 12>& monthlyCompletions) {
    monthlyCompletions_ = monthlyCompletions;
    QStringList values;
    for (const int value : monthlyCompletions_) values << QString::number(value);
    setAccessibleDescription(accessibleDescription() + QString::fromUtf8(" Завершения по месяцам, последние 12 месяцев: ") + values.join(", ") +
        QString::fromUtf8(". Учтены только переходы, оставшиеся в последних 200 событиях task-audit.log."));
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

    painter.setPen(mutedColor);
    painter.drawText(QRect(0, 70, width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
        QString::fromUtf8("Завершения по месяцу перехода · последние 12 месяцев · сохранённый аудит (до 200 событий)"));
    const QRectF plot(24, 91, std::max(0, width() - 36), 34);
    painter.setPen(QPen(trackColor, 1));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    const int trendMax = *std::max_element(monthlyCompletions_.begin(), monthlyCompletions_.end());
    if (trendMax == 0) {
        painter.setPen(mutedColor);
        painter.drawText(QRectF(plot.left(), plot.top(), plot.width(), plot.height()), Qt::AlignCenter,
            QString::fromUtf8("Нет завершений в сохранённой части аудита"));
    } else {
        QPolygonF line;
        for (int index = 0; index < int(monthlyCompletions_.size()); ++index) {
            const qreal x = plot.left() + (monthlyCompletions_.size() == 1 ? 0.0 : plot.width() * index / (monthlyCompletions_.size() - 1));
            const qreal y = plot.bottom() - plot.height() * monthlyCompletions_[size_t(index)] / trendMax;
            line << QPointF(x, y);
        }
        painter.setPen(QPen(accentColor, 2));
        painter.drawPolyline(line);
        painter.setBrush(accentColor);
        for (int index = 0; index < line.size(); ++index) {
            painter.drawEllipse(line[index], 2.5, 2.5);
            if (monthlyCompletions_[size_t(index)] > 0) {
                painter.setPen(textColor);
                painter.drawText(QRectF(line[index].x() - 14, line[index].y() - 15, 28, 13), Qt::AlignCenter,
                    QString::number(monthlyCompletions_[size_t(index)]));
                painter.setPen(QPen(accentColor, 2));
            }
        }
    }
    const auto firstMonth = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1).addMonths(-11);
    painter.setPen(mutedColor);
    const qreal step = monthlyCompletions_.size() > 1 ? plot.width() / (monthlyCompletions_.size() - 1) : plot.width();
    for (int index = 0; index < int(monthlyCompletions_.size()); index += 2) {
        const qreal center = plot.left() + step * index;
        painter.drawText(QRectF(center - step * 0.48, 127, step * 0.96, 15), Qt::AlignHCenter | Qt::AlignTop,
            firstMonth.addMonths(index).toString("MM/yy"));
    }
}
