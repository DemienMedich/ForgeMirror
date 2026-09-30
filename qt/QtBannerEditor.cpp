#include "QtBannerEditor.h"
#include "AppMetaService.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>

bool ShowBannerEditor(QWidget* parent, QtWorkspace& workspace, int editIndex) {
    if (editIndex < -1 || editIndex >= int(workspace.data.bannerTexts.size())) return false;
    QtScrollableDialog dialog(parent, QSize(640, 520));
    dialog.setObjectName("bannerEditor");
    dialog.setWindowTitle(QString::fromUtf8(editIndex >= 0 ? "Редактировать фразу" : "Добавить фразу"));
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Текст и пояснения редактора фразы"));
    auto* form = dialog.formLayout();
    form->setHorizontalSpacing(dialog.scaledMetric(8));
    form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFormAlignment(Qt::AlignTop);
    auto* hint = new QLabel(QString::fromUtf8("Фразы показываются в верхней панели и хранятся в локальном meta/banner.json."));
    hint->setObjectName("bannerEditorHint");
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* text = new QPlainTextEdit;
    text->setObjectName("bannerText");
    text->setAccessibleName(QString::fromUtf8("Текст фразы"));
    const QString textDetails = QString::fromUtf8("Обязательная фраза. Enter добавляет новую строку; сохранение — отдельной кнопкой.");
    text->setAccessibleDescription(textDetails);
    text->setToolTip(textDetails);
    text->setTabChangesFocus(true);
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    text->setMinimumHeight(std::max(dialog.scaledMetric(96),
        text->fontMetrics().lineSpacing() * 4 + dialog.scaledMetric(8)));
    text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (editIndex >= 0) text->setPlainText(QString::fromUtf8(workspace.data.bannerTexts[size_t(editIndex)]));
    auto* textLabel = new QLabel(QString::fromUtf8("Текст"));
    textLabel->setObjectName("bannerTextLabel");
    textLabel->setTextFormat(Qt::PlainText);
    textLabel->setWordWrap(true);
    textLabel->setBuddy(text);
    form->addRow(textLabel, text);
    auto* notice = new QLabel;
    notice->setObjectName("bannerNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notice->setAccessibleName(QString::fromUtf8("Результат сохранения фразы"));
    dialog.footerLayout()->addWidget(notice);
    notice->hide();
    const auto showNotice = [notice](const QString& message) {
        notice->setText(message);
        notice->setAccessibleDescription(message);
        notice->show();
    };
    QObject::connect(text, &QPlainTextEdit::textChanged, &dialog, [notice, text, textDetails] {
        notice->clear(); notice->hide();
        notice->setAccessibleDescription({});
        if (!text->toPlainText().trimmed().isEmpty()) text->setAccessibleDescription(textDetails);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("bannerEditorButtons");
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setObjectName("bannerSave");
    cancel->setObjectName("bannerCancel");
    save->setText(QString::fromUtf8(editIndex >= 0 ? "Сохранить" : "Добавить"));
    save->setAccessibleName(QString::fromUtf8(editIndex >= 0 ? "Сохранить фразу" : "Добавить фразу"));
    save->setProperty("primary", true);
    save->setMinimumHeight(dialog.scaledMetric(32));
    save->setAutoDefault(true);
    save->setDefault(true);
    cancel->setText(QString::fromUtf8("Отмена"));
    cancel->setAccessibleName(QString::fromUtf8("Отменить редактирование фразы"));
    cancel->setAutoDefault(false);
    dialog.footerLayout()->addWidget(buttons);
    QWidget::setTabOrder(text, save);
    QWidget::setTabOrder(save, cancel);
    text->setFocus();
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto value = text->toPlainText().trimmed();
        if (value.isEmpty()) {
            showNotice(QString::fromUtf8("Введите текст фразы."));
            text->setAccessibleDescription(notice->text());
            text->setFocus(Qt::OtherFocusReason);
            dialog.scrollArea()->ensureWidgetVisible(text, 0, dialog.scaledMetric(8));
            return;
        }
        const auto utf8 = value.toUtf8().toStdString();
        const auto result = editIndex >= 0
            ? AppUpdateBannerText(workspace.directory, workspace.data.bannerTexts, editIndex, utf8,
                                  workspace.metaEventLogger)
            : AppAddBannerText(workspace.directory, workspace.data.bannerTexts, utf8,
                               workspace.metaEventLogger);
        if (!result.ok) {
            showNotice(QString::fromUtf8(result.errorMessage.c_str()));
            return;
        }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}

bool DeleteBannerTextChecked(QtWorkspace& workspace, int index, QString* error) {
    const auto result = AppDeleteBannerText(workspace.directory, workspace.data.bannerTexts, index,
                                            workspace.metaEventLogger);
    if (!result.ok && error) *error = QString::fromUtf8(result.errorMessage.c_str());
    return result.ok;
}
