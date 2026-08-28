#include "QtSkillEditor.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>

namespace {
std::string u(const QString& value) { return value.toUtf8().toStdString(); }
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
bool equalCatalogs(const SkillCatalog& a, const SkillCatalog& b) {
    if (a.skills().size() != b.skills().size()) return false;
    for (const auto& id : a.skills()) {
        if (!b.contains_id(id) || a.display_name(id) != b.display_name(id) ||
            std::abs(a.weight(id) - b.weight(id)) > 0.000001 ||
            a.description(id) != b.description(id) || a.category(id) != b.category(id) ||
            a.professions(id) != b.professions(id)) return false;
    }
    return true;
}
bool safeField(const QString& text) {
    for (const auto ch : text)
        if (ch == '|' || ch.category() == QChar::Other_Control ||
            ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) return false;
    return true;
}
}

QString SaveQtSkill(QtWorkspace& workspace, const std::string& id, const QString& name,
                    double weight, const QString& description, const QString& category) {
    const auto title = name.trimmed(), desc = description.trimmed(), cat = category.trimmed();
    if (title.isEmpty() || desc.isEmpty()) return QString::fromUtf8("Название и описание обязательны.");
    if (!safeField(title) || !safeField(desc) || !safeField(cat))
        return QString::fromUtf8("Формат каталога не поддерживает переносы строк, символ | и управляющие символы.");
    if (!std::isfinite(weight) || weight < 0.5 || weight > 1.6)
        return QString::fromUtf8("Вес должен быть от 0,5 до 1,6.");
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return QString::fromUtf8("Сначала завершите восстановление XP через обновление данных.");
    if (!id.empty() && !workspace.catalog.contains_id(id)) return QString::fromUtf8("Навык больше не существует.");
    const auto duplicate = workspace.catalog.id_for_name(u(title));
    if (duplicate && *duplicate != id) return QString::fromUtf8("Навык с таким названием уже существует.");

    // The legacy writer returns void. Confine it to a disposable directory and
    // verify every record after serialization before touching the live catalog.
    const auto path = q((workspace.directory / "skills.txt").u8string());
    const bool existed = QFileInfo::exists(path);
    QByteArray original;
    if (existed) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось прочитать каталог.");
        original = input.readAll();
        if (input.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения каталога.");
    }
    QTemporaryDir staging;
    if (!staging.isValid()) return QString::fromUtf8("Не удалось создать временный каталог.");
    const auto stagedPath = staging.path() + "/skills.txt";
    if (existed && !QFile::copy(path, stagedPath)) return QString::fromUtf8("Не удалось скопировать каталог для проверки.");
    SkillCatalog candidate(std::filesystem::u8path(u(staging.path())));
    if (!equalCatalogs(candidate, workspace.catalog))
        return QString::fromUtf8("Каталог изменился на диске. Обновите данные и повторите действие.");
    if (id.empty()) candidate.add_skill(u(title), weight, u(desc), u(cat));
    else candidate.update_skill(id, u(title), weight, u(desc), u(cat), workspace.catalog.professions(id));
    SkillCatalog verified(std::filesystem::u8path(u(staging.path())));
    const auto savedId = verified.id_for_name(u(title));
    if (!savedId || (!id.empty() && *savedId != id) || !equalCatalogs(candidate, verified) ||
        verified.description(*savedId) != u(desc) || verified.category(*savedId) != u(cat) ||
        std::abs(verified.weight(*savedId) - weight) > 0.000001)
        return QString::fromUtf8("Проверка сохранённого каталога не пройдена. Исходные данные не изменены.");
    QFile staged(stagedPath);
    if (!staged.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось прочитать результат сохранения.");
    const auto bytes = staged.readAll();
    if (staged.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения результата сохранения.");
    QFile current(path);
    if (QFileInfo::exists(path) != existed ||
        (existed && (!current.open(QIODevice::ReadOnly) || current.readAll() != original || current.error() != QFileDevice::NoError)))
        return QString::fromUtf8("Каталог изменился во время сохранения. Обновите данные.");
    current.close();
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return QString::fromUtf8("Не удалось записать каталог. Исходный файл сохранён.");
    workspace.catalog.reload();
    return {};
}

bool ShowSkillEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id) {
    if (!id.empty() && !workspace.catalog.contains_id(id)) return false;
    QDialog dialog(parent);
    dialog.setObjectName("skillEditor");
    dialog.setWindowTitle(QString::fromUtf8(id.empty() ? "Новый навык" : "Редактирование навыка"));
    dialog.setMinimumWidth(480);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(id.empty() ? QString() : q(workspace.catalog.display_name(id)));
    name->setObjectName("skillName");
    auto* description = new QLineEdit(id.empty() ? QString() : q(workspace.catalog.description(id)));
    description->setObjectName("skillDescription");
    auto* category = new QLineEdit(id.empty() ? QString() : q(workspace.catalog.category(id)));
    category->setObjectName("skillCategory");
    auto* weight = new QDoubleSpinBox;
    weight->setObjectName("skillWeight");
    weight->setRange(0.5, 1.6);
    weight->setDecimals(6);
    weight->setSingleStep(0.05);
    weight->setValue(id.empty() ? 1.0 : workspace.catalog.weight(id));
    form->addRow(QString::fromUtf8("Название"), name);
    form->addRow(QString::fromUtf8("Описание"), description);
    form->addRow(QString::fromUtf8("Категория"), category);
    form->addRow(QString::fromUtf8("Вес"), weight);
    auto* hint = new QLabel(QString::fromUtf8("Описание — одна строка. Связи с профессиями и накопленный XP сохраняются."));
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* notice = new QLabel;
    notice->setObjectName("editorNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    form->addRow(notice);
    for (auto* field : {name, description, category})
        QObject::connect(field, &QLineEdit::textChanged, notice, &QLabel::clear);
    QObject::connect(weight, &QDoubleSpinBox::valueChanged, notice, &QLabel::clear);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto error = SaveQtSkill(workspace, id, name->text(), weight->value(), description->text(), category->text());
        if (!error.isEmpty()) { notice->setText(error); return; }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
