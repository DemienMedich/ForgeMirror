#pragma once

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>

enum class QtActionIcon {
    Edit,
    Delete,
    Reset,
    MoveUp,
    MoveDown,
    Chart,
    Tasks,
    Focus,
    AddXp,
    Details,
    ChevronRight,
    ChevronDown
};

// Shared compact action symbols; colors follow the caller's palette and icon state.
inline QIcon CreateQtActionIcon(QtActionIcon action, const QPalette& palette) {
    const auto render = [action](const QColor& color, int size) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(size / 20.0, size / 20.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;

        switch (action) {
        case QtActionIcon::Edit:
            path.moveTo(4.5, 12.5);
            path.lineTo(13.0, 4.0);
            path.lineTo(16.0, 7.0);
            path.lineTo(7.5, 15.5);
            path.lineTo(3.5, 16.5);
            path.closeSubpath();
            painter.drawPath(path);
            painter.drawLine(QPointF(11.5, 5.5), QPointF(14.5, 8.5));
            break;
        case QtActionIcon::Delete:
            painter.drawLine(QPointF(4.0, 5.5), QPointF(16.0, 5.5));
            path.moveTo(7.0, 5.5);
            path.lineTo(7.0, 3.5);
            path.lineTo(13.0, 3.5);
            path.lineTo(13.0, 5.5);
            path.moveTo(5.0, 7.0);
            path.lineTo(6.0, 16.5);
            path.lineTo(14.0, 16.5);
            path.lineTo(15.0, 7.0);
            painter.drawPath(path);
            painter.drawLine(QPointF(8.0, 9.0), QPointF(8.0, 14.0));
            painter.drawLine(QPointF(12.0, 9.0), QPointF(12.0, 14.0));
            break;
        case QtActionIcon::Reset:
            path.moveTo(4.5, 7.5);
            path.cubicTo(5.4, 5.0, 7.3, 3.5, 10.0, 3.5);
            path.cubicTo(13.6, 3.5, 16.5, 6.4, 16.5, 10.0);
            path.cubicTo(16.5, 13.6, 13.6, 16.5, 10.0, 16.5);
            path.cubicTo(7.0, 16.5, 4.6, 14.5, 3.7, 11.7);
            path.moveTo(4.5, 3.5);
            path.lineTo(4.5, 7.5);
            path.lineTo(8.5, 7.5);
            painter.drawPath(path);
            break;
        case QtActionIcon::MoveUp:
            painter.drawLine(QPointF(10.0, 16.0), QPointF(10.0, 4.0));
            path.moveTo(5.5, 8.5);
            path.lineTo(10.0, 4.0);
            path.lineTo(14.5, 8.5);
            painter.drawPath(path);
            break;
        case QtActionIcon::MoveDown:
            painter.drawLine(QPointF(10.0, 4.0), QPointF(10.0, 16.0));
            path.moveTo(5.5, 11.5);
            path.lineTo(10.0, 16.0);
            path.lineTo(14.5, 11.5);
            painter.drawPath(path);
            break;
        case QtActionIcon::Chart:
            path.moveTo(3.5, 3.5);
            path.lineTo(3.5, 16.5);
            path.lineTo(16.5, 16.5);
            path.moveTo(6.0, 12.0);
            path.lineTo(9.0, 9.0);
            path.lineTo(12.0, 11.0);
            path.lineTo(16.0, 5.5);
            painter.drawPath(path);
            break;
        case QtActionIcon::Tasks:
            path.moveTo(7.0, 4.5);
            path.lineTo(5.5, 4.5);
            path.quadTo(4.5, 4.5, 4.5, 5.5);
            path.lineTo(4.5, 15.5);
            path.quadTo(4.5, 16.5, 5.5, 16.5);
            path.lineTo(14.5, 16.5);
            path.quadTo(15.5, 16.5, 15.5, 15.5);
            path.lineTo(15.5, 5.5);
            path.quadTo(15.5, 4.5, 14.5, 4.5);
            path.lineTo(13.0, 4.5);
            painter.drawPath(path);
            painter.drawRoundedRect(QRectF(7.0, 3.0, 6.0, 3.0), 1.0, 1.0);
            path = QPainterPath();
            path.moveTo(6.7, 9.3);
            path.lineTo(7.7, 10.3);
            path.lineTo(9.3, 8.5);
            painter.drawPath(path);
            painter.drawLine(QPointF(11.0, 9.5), QPointF(13.0, 9.5));
            painter.drawLine(QPointF(7.0, 13.0), QPointF(13.0, 13.0));
            break;
        case QtActionIcon::Focus:
            painter.drawEllipse(QRectF(5.0, 5.0, 10.0, 10.0));
            painter.drawEllipse(QRectF(8.8, 8.8, 2.4, 2.4));
            painter.drawLine(QPointF(10.0, 2.5), QPointF(10.0, 6.0));
            painter.drawLine(QPointF(10.0, 14.0), QPointF(10.0, 17.5));
            painter.drawLine(QPointF(2.5, 10.0), QPointF(6.0, 10.0));
            painter.drawLine(QPointF(14.0, 10.0), QPointF(17.5, 10.0));
            break;
        case QtActionIcon::AddXp:
            path.moveTo(8.0, 4.5);
            path.lineTo(9.3, 8.2);
            path.lineTo(13.0, 9.5);
            path.lineTo(9.3, 10.8);
            path.lineTo(8.0, 14.5);
            path.lineTo(6.7, 10.8);
            path.lineTo(3.0, 9.5);
            path.lineTo(6.7, 8.2);
            path.closeSubpath();
            painter.drawPath(path);
            painter.drawLine(QPointF(13.0, 4.5), QPointF(17.0, 4.5));
            painter.drawLine(QPointF(15.0, 2.5), QPointF(15.0, 6.5));
            break;
        case QtActionIcon::Details:
            painter.drawEllipse(QRectF(3.5, 3.5, 13.0, 13.0));
            painter.drawPoint(QPointF(10.0, 6.5));
            painter.drawLine(QPointF(10.0, 9.0), QPointF(10.0, 13.5));
            break;
        case QtActionIcon::ChevronRight:
            path.moveTo(7.0, 4.5);
            path.lineTo(12.5, 10.0);
            path.lineTo(7.0, 15.5);
            painter.drawPath(path);
            break;
        case QtActionIcon::ChevronDown:
            path.moveTo(4.5, 7.0);
            path.lineTo(10.0, 12.5);
            path.lineTo(15.5, 7.0);
            painter.drawPath(path);
            break;
        }
        painter.end();
        return pixmap;
    };

    QIcon icon;
    for (int size = 20; size <= 60; size += 20) {
        const QPixmap normal = render(palette.color(QPalette::Active, QPalette::Text), size);
        const QPixmap disabled = render(palette.color(QPalette::Disabled, QPalette::Text), size);
        const QPixmap selected = render(palette.color(QPalette::Active, QPalette::HighlightedText), size);
        icon.addPixmap(normal, QIcon::Normal, QIcon::Off);
        icon.addPixmap(normal, QIcon::Normal, QIcon::On);
        icon.addPixmap(normal, QIcon::Active, QIcon::Off);
        icon.addPixmap(normal, QIcon::Active, QIcon::On);
        icon.addPixmap(disabled, QIcon::Disabled, QIcon::Off);
        icon.addPixmap(disabled, QIcon::Disabled, QIcon::On);
        icon.addPixmap(selected, QIcon::Selected, QIcon::Off);
        icon.addPixmap(selected, QIcon::Selected, QIcon::On);
    }
    return icon;
}
