#include "QtTheme.h"
#include <QtWidgets>

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
    app.setStyleSheet("QPushButton, QToolButton, QComboBox, QLineEdit { min-height: 26px; }"
        "QPushButton { padding: 0 8px; } QListWidget::item { padding: 8px; }"
        "QPushButton#primary, QPushButton[primary=true] { background: #7554ad; color: white; border: 0; border-radius: 4px; }"
        "QPushButton#primary:hover, QPushButton[primary=true]:hover { background: #8764bf; } QLabel#title { font-weight: 600; }"
        "QFrame[metric=true] { background: #26262c; border-radius: 4px; } QLabel[metricValue=true] { font-weight: 600; }"
        "QLabel[timerValue=true] { font-size: 30px; font-weight: 600; } QProgressBar { min-height: 8px; max-height: 8px; }"
        "QProgressBar::chunk { background: #7554ad; }");
}
