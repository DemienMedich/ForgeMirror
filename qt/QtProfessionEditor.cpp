#include "QtProfessionEditor.h"
#include "AppProfessionService.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
std::string u(const QString& value) { return value.toUtf8().toStdString(); }
bool safe(const QString& value) {
    for (const auto ch : value) if (ch == '|' || ch.category() == QChar::Other_Control ||
        ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) return false;
    return true;
}
}
bool ShowProfessionEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id) {
    const auto found = std::find_if(workspace.data.professions.begin(), workspace.data.professions.end(),
        [&](const auto& p) { return p.id == id; });
    if (!id.empty() && found == workspace.data.professions.end()) return false;
    QDialog dialog(parent);
    dialog.setObjectName("professionEditor");
    dialog.setWindowTitle(QString::fromUtf8(id.empty() ? "Новая профессия" : "Редактирование профессии"));
    dialog.setMinimumWidth(480);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(id.empty() ? QString() : q(found->name));
    name->setObjectName("professionName");
    auto* description = new QLineEdit(id.empty() ? QString() : q(found->description));
    description->setObjectName("professionDescription");
    form->addRow(QString::fromUtf8("Название"), name);
    form->addRow(QString::fromUtf8("Описание"), description);
    auto* notice = new QLabel;
    notice->setObjectName("professionNotice");
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText);
    form->addRow(notice);
    QObject::connect(name, &QLineEdit::textChanged, notice, &QLabel::clear);
    QObject::connect(description, &QLineEdit::textChanged, notice, &QLabel::clear);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto title = name->text().trimmed(), desc = description->text().trimmed();
        if (title.isEmpty() || !safe(title) || !safe(desc)) {
            notice->setText(QString::fromUtf8("Укажите название. Переносы строк, управляющие символы и | не поддерживаются.")); return;
        }
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) {
            notice->setText(QString::fromUtf8("Сначала завершите восстановление данных.")); return;
        }
        auto candidate = workspace.data.professions;
        for (const auto& p : candidate) if (p.id != id && q(p.name).compare(title, Qt::CaseInsensitive) == 0) {
            notice->setText(QString::fromUtf8("Профессия с таким названием уже существует.")); return;
        }
        const auto current = std::find_if(candidate.begin(), candidate.end(), [&](const auto& p) { return p.id == id; });
        if (!id.empty() && current == candidate.end()) { notice->setText(QString::fromUtf8("Профессия больше не существует.")); return; }
        const int index = id.empty() ? -1 : int(std::distance(candidate.begin(), current));
        QTemporaryDir staging;
        if (!staging.isValid()) { notice->setText(QString::fromUtf8("Не удалось создать временный каталог.")); return; }
        const auto directory = std::filesystem::u8path(u(staging.path()));
        const auto result = AppSaveProfessionEntry(directory, candidate, index, u(title), u(desc));
        const auto checked = LoadProfessionsData(directory);
        bool same = checked.size() == candidate.size();
        for (size_t i = 0; same && i < candidate.size(); ++i)
            same = candidate[i].id == checked[i].id && candidate[i].name == checked[i].name && candidate[i].description == checked[i].description;
        if (!result.ok || !same) { notice->setText(QString::fromUtf8("Проверка сохранения не пройдена. Исходные данные не изменены.")); return; }
        QFile file(staging.path() + "/meta/professions.txt");
        if (!file.open(QIODevice::ReadOnly)) { notice->setText(QString::fromUtf8("Ошибка чтения временного файла.")); return; }
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError) { notice->setText(QString::fromUtf8("Ошибка чтения временного файла.")); return; }
        const auto meta = q((workspace.directory / "meta").u8string());
        QSaveFile output(meta + "/professions.txt");
        output.setDirectWriteFallback(false);
        if (!QDir().mkpath(meta) || !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            notice->setText(QString::fromUtf8("Не удалось записать профессии. Исходный файл сохранён.")); return;
        }
        workspace.data.professions = std::move(candidate);
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
