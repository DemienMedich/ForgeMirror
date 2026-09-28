#include "QtBannerEditor.h"
#include "AppMetaService.h"
#include <QtWidgets>

bool ShowBannerEditor(QWidget* parent, QtWorkspace& workspace, int editIndex) {
    if (editIndex < -1 || editIndex >= int(workspace.data.bannerTexts.size())) return false;
    QDialog dialog(parent);
    dialog.setObjectName("bannerEditor");
    dialog.setWindowTitle(QString::fromUtf8(editIndex >= 0 ? "Редактировать фразу" : "Добавить фразу"));
    dialog.setMinimumWidth(520);
    auto* form = new QFormLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8("Фразы показываются в верхней панели и хранятся в локальном meta/banner.json."));
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* text = new QPlainTextEdit;
    text->setObjectName("bannerText");
    text->setMaximumHeight(110);
    if (editIndex >= 0) text->setPlainText(QString::fromUtf8(workspace.data.bannerTexts[size_t(editIndex)]));
    form->addRow(QString::fromUtf8("Текст"), text);
    auto* notice = new QLabel;
    notice->setObjectName("bannerNotice");
    notice->setWordWrap(true);
    form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8(editIndex >= 0 ? "Сохранить" : "Добавить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto value = text->toPlainText().trimmed();
        if (value.isEmpty()) {
            notice->setText(QString::fromUtf8("Введите текст фразы."));
            return;
        }
        const auto utf8 = value.toUtf8().toStdString();
        const auto result = editIndex >= 0
            ? AppUpdateBannerText(workspace.directory, workspace.data.bannerTexts, editIndex, utf8,
                                  workspace.metaEventLogger)
            : AppAddBannerText(workspace.directory, workspace.data.bannerTexts, utf8,
                               workspace.metaEventLogger);
        if (!result.ok) {
            notice->setText(QString::fromUtf8(result.errorMessage.c_str()));
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
