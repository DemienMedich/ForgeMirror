#include "QtBannerEditor.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>

namespace {
bool SaveDraft(QtWorkspace& workspace, const std::vector<std::string>& draft, QString* error) {
    QTemporaryDir staging;
    if (!staging.isValid() || !SaveBannerTexts(std::filesystem::u8path(staging.path().toUtf8().toStdString()), draft)) {
        if (error) *error = QString::fromUtf8("Не удалось сериализовать фразы."); return false;
    }
    const auto checked = LoadBannerTexts(std::filesystem::u8path(staging.path().toUtf8().toStdString()));
    QFile source(staging.path() + "/meta/banner.json");
    if (checked != draft || !source.open(QIODevice::ReadOnly)) {
        if (error) *error = QString::fromUtf8("Проверка фраз не пройдена."); return false;
    }
    const auto target = QString::fromUtf8((workspace.directory / "meta/banner.json").u8string());
    if (QFileInfo(target).isSymLink() || QFileInfo(QFileInfo(target).absolutePath()).isSymLink()) {
        if (error) *error = QString::fromUtf8("Символьная ссылка banner.json не поддерживается."); return false;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile output(target); output.setDirectWriteFallback(false);
    const auto bytes = source.readAll();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить баннер."); return false;
    }
    const auto persisted = LoadBannerTexts(workspace.directory);
    if (persisted != draft) { if (error) *error = QString::fromUtf8("Сохранённые фразы не прошли проверку."); return false; }
    workspace.data.bannerTexts = persisted;
    return true;
}
}

bool ShowBannerEditor(QWidget* parent, QtWorkspace& workspace, int editIndex) {
    if (editIndex < -1 || editIndex >= int(workspace.data.bannerTexts.size())) return false;
    QDialog dialog(parent); dialog.setObjectName("bannerEditor"); dialog.setWindowTitle(QString::fromUtf8(editIndex >= 0 ? "Редактировать фразу" : "Добавить фразу")); dialog.setMinimumWidth(520);
    auto* form = new QFormLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8("Фразы показываются в верхней панели и хранятся в локальном meta/banner.json.")); hint->setWordWrap(true); form->addRow(hint);
    auto* text = new QPlainTextEdit; text->setObjectName("bannerText"); text->setMaximumHeight(110);
    if (editIndex >= 0) text->setPlainText(QString::fromUtf8(workspace.data.bannerTexts[size_t(editIndex)]));
    form->addRow(QString::fromUtf8("Текст"), text);
    auto* notice = new QLabel; notice->setObjectName("bannerNotice"); notice->setWordWrap(true); form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8(editIndex >= 0 ? "Сохранить" : "Добавить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto value = text->toPlainText().trimmed();
        if (value.isEmpty()) { notice->setText(QString::fromUtf8("Введите текст фразы.")); return; }
        auto draft = workspace.data.bannerTexts;
        if (editIndex >= 0) draft[size_t(editIndex)] = value.toUtf8().toStdString(); else draft.push_back(value.toUtf8().toStdString());
        QString error; if (!SaveDraft(workspace, draft, &error)) { notice->setText(error); return; }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}

bool DeleteBannerTextChecked(QtWorkspace& workspace, int index, QString* error) {
    if (index < 0 || index >= int(workspace.data.bannerTexts.size())) { if (error) *error = QString::fromUtf8("Фраза не найдена."); return false; }
    auto draft = workspace.data.bannerTexts; draft.erase(draft.begin() + index);
    return SaveDraft(workspace, draft, error);
}
