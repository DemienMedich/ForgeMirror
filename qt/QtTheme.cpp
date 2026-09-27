#include "QtTheme.h"
#include <QtWidgets>
#include <algorithm>

void ApplyQtLayoutMetrics(QApplication& app, int spacingPercent, int cornerRadius) {
    const double factor = std::clamp(spacingPercent, 80, 120) / 100.0;
    const int controlHeight = qRound(26 * factor);
    const int horizontalPadding = qRound(8 * factor);
    const int listPadding = qRound(8 * factor);
    const int radius = std::clamp(cornerRadius, 0, 12);
    app.setStyleSheet(QStringLiteral(
        "QPushButton, QToolButton, QComboBox, QLineEdit { min-height: %1px; }"
        "QPushButton { padding: 0 %2px; } QListWidget::item { padding: %3px; }"
        "QPushButton#primary, QPushButton[primary=true] { background: #7554ad; color: white; border: 0; border-radius: %4px; }"
        "QPushButton#primary:hover, QPushButton[primary=true]:hover { background: #8764bf; } QLabel#title { font-weight: 600; }"
        "QFrame[metric=true] { background: #26262c; border-radius: %4px; } QLabel[metricValue=true] { font-weight: 600; }"
        "QLabel[timerValue=true] { font-size: 30px; font-weight: 600; } QProgressBar { min-height: 8px; max-height: 8px; }"
        "QProgressBar::chunk { background: #7554ad; }"
        "QListWidget#navigation { background: #202024; border: 0; outline: 0; padding: 6px 4px; }"
        "QListWidget#navigation::item { border: 0; border-radius: 6px; color: #b9b9c4; padding: 6px 8px; margin: 1px 0; }"
        "QListWidget#navigation::item:hover { background: #2c2c32; color: #eeeeef; }"
        "QListWidget#navigation::item:selected { background: #33333b; color: #ffffff; border-left: 3px solid #7554ad; padding-left: 5px; }")
        .arg(controlHeight).arg(horizontalPadding).arg(listPadding).arg(radius));
}

void ApplyQtTheme(QApplication& app) {
    QApplication::setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#202024"));
    palette.setColor(QPalette::WindowText, QColor("#eeeeef"));
    palette.setColor(QPalette::Base, QColor("#26262c"));
    palette.setColor(QPalette::AlternateBase, QColor("#2c2c32"));
    palette.setColor(QPalette::Text, QColor("#eeeeef"));
    palette.setColor(QPalette::Button, QColor("#33333b"));
    palette.setColor(QPalette::ButtonText, QColor("#eeeeef"));
    palette.setColor(QPalette::Highlight, QColor("#7554ad"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#99999f"));
    app.setPalette(palette);
    app.setFont(QFont("Segoe UI", 10));
    ApplyQtLayoutMetrics(app, 100, 4);
}
