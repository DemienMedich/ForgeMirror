#pragma once

#include <QtWidgets>

inline QString BuildQtGuiCommandHelpText(QString helpText, const QString& applicationName) {
    const auto usageMarker = QStringLiteral("Usage:");
    const auto optionsMarker = QStringLiteral(" [options]");
    const int options = helpText.indexOf(optionsMarker);
    const int firstLineEnd = helpText.indexOf(QLatin1Char('\n'));
    if (helpText.startsWith(usageMarker) && options >= usageMarker.size() &&
        (firstLineEnd < 0 || options < firstLineEnd))
        helpText.replace(usageMarker.size(), options - usageMarker.size(),
            QStringLiteral(" ") + applicationName);
    return helpText;
}

inline QDialog* CreateQtCommandHelpDialog(const QString& helpText, QWidget* parent = nullptr) {
    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("commandHelpDialog"));
    dialog->setWindowTitle(QString::fromUtf8("Справка ForgeMirror Qt"));
    dialog->setAccessibleName(QString::fromUtf8("Справка командной строки ForgeMirror Qt"));
    dialog->setModal(true);
    QSize available = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->availableGeometry().size() : QSize(800, 600);
    available = QSize(qMax(320, available.width() - 32), qMax(240, available.height() - 48));
    const QSize minimum(qMin(560, available.width()), qMin(360, available.height()));
    dialog->setMinimumSize(minimum);
    dialog->resize(qMin(760, available.width()), qMin(520, available.height()));

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* heading = new QLabel(QString::fromUtf8("Параметры запуска"));
    heading->setObjectName(QStringLiteral("commandHelpHeading"));
    QFont headingFont = heading->font();
    headingFont.setWeight(QFont::DemiBold);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto* text = new QPlainTextEdit(dialog);
    text->setObjectName(QStringLiteral("commandHelpText"));
    text->setAccessibleName(QString::fromUtf8("Список параметров командной строки"));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setPlainText(helpText);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setMinimumHeight(240);
    layout->addWidget(text, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName(QStringLiteral("commandHelpButtons"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->setFocusProxy(text);
    return dialog;
}
