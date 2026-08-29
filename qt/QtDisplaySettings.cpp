#include "QtDisplaySettings.h"
#include <QtWidgets>
#include <QSaveFile>
#include <algorithm>

namespace {
QString pathFor(const std::filesystem::path& directory) { return QString::fromUtf8((directory / "meta/ui.ini").u8string()); }
int normalizedScale(int value) { for (int allowed : {90, 100, 110, 125}) if (value == allowed) return value; return 100; }
}
QtDisplaySettings LoadQtDisplaySettings(const std::filesystem::path& directory) {
    QtDisplaySettings out; QFile file(pathFor(directory)); if (!file.open(QIODevice::ReadOnly)) return out;
    auto bytes = file.readAll(); if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3); bool section = false;
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const auto line = raw.trimmed(); if (line.startsWith('[')) { section = line == "[qt]"; continue; } if (!section) continue;
        const auto key = line.section('=', 0, 0).trimmed(), value = line.section('=', 1).trimmed();
        if (key == "scalePercent") out.scalePercent = normalizedScale(value.toInt()); else if (key == "compactRows") out.compactRows = value == "1";
    }
    return out;
}
bool SaveQtDisplaySettings(const std::filesystem::path& directory, const QtDisplaySettings& settings) {
    const auto path = pathFor(directory), meta = QFileInfo(path).absolutePath();
    if (QFileInfo(meta).isSymLink() || QFileInfo(path).isSymLink()) return false;
    QFile input(path); QByteArray original; if (input.open(QIODevice::ReadOnly)) original = input.readAll(); input.close();
    const bool bom = original.startsWith("\xEF\xBB\xBF"); if (bom) original.remove(0, 3);
    auto lines = QString::fromUtf8(original).split('\n'); int begin = -1, end = lines.size();
    for (int i = 0; i < lines.size(); ++i) { const auto line = lines[i].trimmed(); if (line == "[qt]") { begin = i; continue; } if (begin >= 0 && i > begin && line.startsWith('[')) { end = i; break; } }
    if (begin < 0) { if (!lines.isEmpty() && !lines.back().isEmpty()) lines << ""; begin = lines.size(); lines << "[qt]"; end = lines.size(); }
    auto set = [&](const QString& key, const QString& value) { for (int i = begin + 1; i < end; ++i) if (lines[i].section('=', 0, 0).trimmed() == key) { lines[i] = key + '=' + value; return; } lines.insert(end++, key + '=' + value); };
    set("scalePercent", QString::number(normalizedScale(settings.scalePercent))); set("compactRows", settings.compactRows ? "1" : "0");
    const auto bytes = (bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + lines.join('\n').toUtf8();
    QDir().mkpath(meta); QSaveFile output(path); output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}
void ApplyQtDisplaySettings(QApplication& app, const QtDisplaySettings& settings) {
    double base = app.property("forgeBasePointSize").toDouble();
    if (base <= 0.0) { base = app.font().pointSizeF(); app.setProperty("forgeBasePointSize", base); }
    auto font = app.font(); font.setPointSizeF(base * normalizedScale(settings.scalePercent) / 100.0); app.setFont(font);
}
bool ShowQtDisplaySettings(QWidget* parent, const std::filesystem::path& directory, QtDisplaySettings& settings) {
    QDialog dialog(parent); dialog.setObjectName("qtDisplaySettings"); dialog.setWindowTitle(QString::fromUtf8("Настройки интерфейса Qt")); dialog.setMinimumWidth(420);
    auto* form = new QFormLayout(&dialog); auto* scale = new QComboBox; scale->setObjectName("qtScale");
    for (int value : {90, 100, 110, 125}) scale->addItem(QString::number(value) + "%", value);
    scale->setCurrentIndex(std::max(0, scale->findData(normalizedScale(settings.scalePercent))));
    auto* compact = new QCheckBox(QString::fromUtf8("Компактные строки таблиц")); compact->setObjectName("qtCompactRows"); compact->setChecked(settings.compactRows);
    form->addRow(QString::fromUtf8("Масштаб текста"), scale); form->addRow(compact);
    auto* hint = new QLabel(QString::fromUtf8("Цветовая схема зафиксирована для миграции и здесь не меняется.")); hint->setWordWrap(true); form->addRow(hint);
    auto* notice = new QLabel; notice->setObjectName("qtSettingsNotice"); notice->setWordWrap(true); form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true); buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { QtDisplaySettings next{scale->currentData().toInt(), compact->isChecked()}; if (!SaveQtDisplaySettings(directory, next)) { notice->setText(QString::fromUtf8("Не удалось атомарно сохранить настройки.")); return; } settings = next; dialog.accept(); });
    return dialog.exec() == QDialog::Accepted;
}
