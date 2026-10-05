#pragma once

#include "QtScrollableDialog.h"

// The first-run choice only describes the operation. The startup owner retains
// responsibility for locks, source validation, copying and workspace creation.
class QtWorkspaceImportDialog final : public QtScrollableDialog {
public:
    QtWorkspaceImportDialog(const QString& source, const QString& destination,
        QWidget* parent = nullptr)
        : QtScrollableDialog(parent, QSize(640, 520), QSize(420, 300)) {
        setObjectName("workspaceImportDialog");
        setWindowTitle(QString::fromUtf8("Начало работы · ForgeMirror Qt"));
        setAccessibleName(QString::fromUtf8("Выбор данных для первого запуска ForgeMirror Qt"));
        // The read-only context owns keyboard scrolling. Its surrounding
        // viewport is not a second, visually indistinguishable Tab stop.
        scrollArea()->setFocusPolicy(Qt::NoFocus);

        auto* context = new QTextBrowser(bodyWidget());
        context->setObjectName("workspaceImportContext");
        context->setAccessibleName(QString::fromUtf8("Папки и варианты начала работы"));
        context->setOpenLinks(false);
        context->setOpenExternalLinks(false);
        context->setTabChangesFocus(true);
        context->setFrameShape(QFrame::NoFrame);
        context->setMinimumHeight(context->fontMetrics().lineSpacing() * 3
            + qCeil(context->document()->documentMargin() * 2));
        auto option = context->document()->defaultTextOption();
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        context->document()->setDefaultTextOption(option);

        QTextCursor cursor(context->document());
        QTextCharFormat heading;
        heading.setFontWeight(QFont::DemiBold);
        auto paragraph = [&](const QString& text, bool emphasized = false) {
            if (!cursor.atStart()) cursor.insertBlock();
            QTextBlockFormat block;
            block.setBottomMargin(scaledMetric(8));
            cursor.setBlockFormat(block);
            cursor.insertText(text, emphasized ? heading : QTextCharFormat());
        };
        paragraph(QString::fromUtf8("Как начать работу?"), true);
        paragraph(QString::fromUtf8("Можно скопировать данные стабильной версии или начать с пустой папки Qt."));
        paragraph(QString::fromUtf8("Исходная папка"), true);
        paragraph(source);
        paragraph(QString::fromUtf8("Папка Qt"), true);
        paragraph(destination);
        paragraph(QString::fromUtf8("Копировать — создать отдельную копию данных. Исходная папка останется без изменений; изменения Qt не попадут обратно."));
        paragraph(QString::fromUtf8("Начать с нуля — создать пустую папку Qt без копирования. Отмена — завершить запуск."));
        context->setAccessibleDescription(context->toPlainText());
        context->moveCursor(QTextCursor::Start);
        formLayout()->addRow(context);

        auto* commands = new QtDialogFlowRow(this, scaledMetric(8), scaledMetric(4));
        commands->setObjectName("workspaceImportCommands");
        auto command = [&](const char* name, const QString& title, bool primary) {
            auto* button = new QPushButton(title, commands);
            button->setObjectName(name);
            button->setAccessibleName(title);
            button->setProperty("primary", primary);
            button->setAutoDefault(false);
            button->ensurePolished();
            button->setMinimumSize(button->sizeHint().expandedTo(
                QSize(0, scaledMetric(primary ? 32 : 26))));
            commands->addWidget(button);
            return button;
        };
        auto* copy = command("workspaceImportCopy", QString::fromUtf8("Копировать"), true);
        auto* empty = command("workspaceImportEmpty", QString::fromUtf8("Начать с нуля"), false);
        auto* cancel = command("workspaceImportCancel", QString::fromUtf8("Отмена"), false);
        cancel->setDefault(true);
        cancel->setFocus();
        copy->setToolTip(QString::fromUtf8("Создать отдельную копию данных в папке Qt"));
        empty->setToolTip(QString::fromUtf8("Создать пустое рабочее пространство без копирования"));
        QObject::connect(copy, &QPushButton::clicked, this, [this] {
            choice_ = QMessageBox::Yes;
            accept();
        });
        QObject::connect(empty, &QPushButton::clicked, this, [this] {
            choice_ = QMessageBox::No;
            accept();
        });
        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        footerLayout()->addWidget(commands);
        QWidget::setTabOrder(context, copy);
        QWidget::setTabOrder(copy, empty);
        QWidget::setTabOrder(empty, cancel);
        QWidget::setTabOrder(cancel, context);
    }

    QMessageBox::StandardButton choice() const {
        return result() == QDialog::Accepted ? choice_ : QMessageBox::Cancel;
    }

private:
    QMessageBox::StandardButton choice_ = QMessageBox::Cancel;
};
