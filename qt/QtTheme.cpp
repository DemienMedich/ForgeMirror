#include "QtTheme.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>

static double clampMetric(double value, double maxValue, double fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0, maxValue) : fallback;
}

void ApplyQtLayoutMetrics(QApplication& app, int spacingPercent, int cornerRadius,
    double windowRounding, double frameRounding, double scrollbarRounding, double grabRounding,
    double framePaddingX, double framePaddingY, double itemSpacingX, double itemSpacingY) {
    const double factor = std::clamp(spacingPercent, 80, 120) / 100.0;
    const double basePointSize = app.property("forgeBasePointSize").toDouble();
    const double textScale = basePointSize > 0.0
        ? std::clamp(app.font().pointSizeF() / basePointSize, 0.9, 2.0) : 1.0;
    const int controlHeight = qRound(std::max(26.0, 18.0 + clampMetric(framePaddingY, 24.0, 0.0) * 2.0) * factor * textScale);
    const int horizontalPadding = qRound(clampMetric(framePaddingX, 24.0, 8.0) * factor * textScale);
    const int verticalPadding = qRound(clampMetric(framePaddingY, 24.0, 0.0) * factor * textScale);
    const int listPaddingX = qRound(clampMetric(itemSpacingX, 32.0, 8.0) * factor * textScale);
    const int listPaddingY = qRound(clampMetric(itemSpacingY, 32.0, 6.0) * factor * textScale);
    const int radius = std::clamp(cornerRadius, 0, 12);
    const int windowRadius = qRound(clampMetric(windowRounding, 24.0, double(radius)));
    const int frameRadius = qRound(clampMetric(frameRounding, 24.0, double(radius)));
    const int scrollbarRadius = qRound(clampMetric(scrollbarRounding, 24.0, 6.0));
    const int grabRadius = qRound(clampMetric(grabRounding, 24.0, 4.0));
    app.setStyleSheet(QStringLiteral(
        "QPushButton, QToolButton, QComboBox, QLineEdit, QDateEdit, QSpinBox, QDoubleSpinBox { min-height: %1px; padding: %2px %3px; }"
        "QPushButton, QToolButton { background: #33333b; color: #eeeeef; border: 1px solid #414149; border-radius: %4px; }"
        "QPushButton:hover, QToolButton:hover { background: #3b3b43; border-color: #53535d; }"
        "QPushButton:pressed, QToolButton:pressed { background: #2c2c32; border-color: #7554ad; }"
        "QPushButton:checked, QToolButton:checked { background: #2c2c32; border-color: #7554ad; }"
        "QPushButton:focus, QToolButton:focus { border-color: #eeeeef; }"
        "QPushButton:disabled, QToolButton:disabled { background: #26262c; color: #99999f; border-color: #33333b; }"
        "QPushButton#primary, QPushButton[primary=true] { background: #7554ad; color: white; border: 1px solid transparent; border-radius: %4px; }"
        "QPushButton#primary:hover, QPushButton[primary=true]:hover { background: #8764bf; } QLabel#title { font-weight: 600; }"
        "QPushButton#primary:pressed, QPushButton[primary=true]:pressed { background: #7554ad; border-color: #eeeeef; }"
        "QPushButton#primary:focus, QPushButton[primary=true]:focus { border-color: #eeeeef; }"
        "QPushButton#primary:disabled, QPushButton[primary=true]:disabled { background: #33333b; color: #99999f; border-color: #414149; }"
        "QFrame[metric=true] { background: #26262c; border-radius: %5px; } QLabel[metricValue=true] { font-weight: 600; }"
        "QLabel[timerValue=true] { font-size: 30px; font-weight: 600; } QProgressBar { min-height: 8px; max-height: 8px; }"
        "QProgressBar::chunk { background: #7554ad; }"
        "QListWidget#navigation { background: #202024; border: 0; outline: 0; padding: 6px 4px; }"
        "QListWidget#navigation::item { border: 1px solid transparent; border-radius: 6px; color: #b9b9c4; padding: 5px 7px; margin: 1px 0; }"
        "QListWidget#navigation::item:hover { background: #2c2c32; color: #eeeeef; }"
        "QListWidget#navigation::item:selected { background: #33333b; color: #ffffff; padding-left: 8px; }"
        "QListWidget#navigation::item:focus { border-color: #eeeeef; }"
        "QFrame#navigationActiveIndicator { background: #7554ad; border-radius: 1px; }"
        "QListWidget::item, QTableWidget::item { padding: %6px %7px; }"
        "QScrollBar::handle { border-radius: %8px; } QSlider::handle { border-radius: %9px; }")
        .arg(controlHeight).arg(verticalPadding).arg(horizontalPadding).arg(frameRadius).arg(windowRadius)
        .arg(listPaddingY).arg(listPaddingX).arg(scrollbarRadius).arg(grabRadius));
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
    ApplyQtLayoutMetrics(app, 100, 4, 4.0, 4.0, 6.0, 4.0, 8.0, 0.0, 8.0, 6.0);
}
