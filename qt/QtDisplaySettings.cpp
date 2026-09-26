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
    auto bytes = file.readAll(); if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3); QString section;
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const auto line = raw.trimmed(); if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size() - 2); continue; }
        const auto key = line.section('=', 0, 0).trimmed(), value = line.section('=', 1).trimmed();
        if (section == "profile" && key == "lastProfileId") out.lastProfileId = value;
        if (section == "ui" && key == "windowDecorated") out.decorated = value != "0";
        if (section == "projects") {
            if (key == "sortMode") { bool ok = false; const int index = value.toInt(&ok); out.projectSortMode = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "overdueOnly") out.projectsOverdueOnly = value == "1";
            else if (key == "xpPendingOnly") out.projectsXpPendingOnly = value == "1";
        }
        if (section == "qt") {
            if (key == "scalePercent") out.scalePercent = normalizedScale(value.toInt()); else if (key == "compactRows") out.compactRows = value == "1"; else if (key == "fullscreen") out.fullscreen = value == "1"; else if (key == "decorated") out.decorated = value != "0"; else if (key == "minimizeToTray") out.minimizeToTray = value == "1";
            else if (key == "lastProfileId") out.lastProfileId = value;
            else if (key == "lastPage") { bool ok = false; const int page = value.toInt(&ok); out.lastPage = ok ? std::clamp(page, 0, 16) : 0; }
            else if (key == "taskStatusFilter") { bool ok = false; const int index = value.toInt(&ok); out.taskStatusFilter = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "taskPriorityFilter") { bool ok = false; const int index = value.toInt(&ok); out.taskPriorityFilter = ok ? std::clamp(index, 0, 4) : 0; }
            else if (key == "taskQuickFilter") { bool ok = false; const int index = value.toInt(&ok); out.taskQuickFilter = ok ? std::clamp(index, 0, 8) : 0; }
            else if (key == "taskProjectId") out.taskProjectId = value;
            else if (key == "taskPipelineStepId") out.taskPipelineStepId = value;
            else if (key == "catalogProfessionId") out.catalogProfessionId = value;
            else if (key == "reportView") { bool ok = false; const int index = value.toInt(&ok); out.reportView = ok ? std::clamp(index, 0, 1) : 0; }
            else if (key == "reportDateRange") { bool ok = false; const int index = value.toInt(&ok); out.reportDateRange = ok ? std::clamp(index, 0, 4) : 0; }
            else if (key == "reportDateFrom") out.reportDateFrom = QDate::fromString(value, Qt::ISODate);
            else if (key == "reportDateTo") out.reportDateTo = QDate::fromString(value, Qt::ISODate);
            else if (key == "projectSortMode") { bool ok = false; const int index = value.toInt(&ok); out.projectSortMode = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "projectsOverdueOnly") out.projectsOverdueOnly = value == "1";
            else if (key == "projectsXpPendingOnly") out.projectsXpPendingOnly = value == "1";
            else if (key == "auditSourceFilter") { bool ok = false; const int index = value.toInt(&ok); out.auditSourceFilter = ok ? std::clamp(index, 0, 2) : 0; }
            else if (key == "logAutoScroll") out.logAutoScroll = value != "0";
            else if (key == "logCompactView") out.logCompactView = value == "1";
        }
    }
    const auto today = QDate::currentDate();
    if (!out.reportDateFrom.isValid()) out.reportDateFrom = today.addDays(-29);
    if (!out.reportDateTo.isValid()) out.reportDateTo = today;
    if (out.reportDateFrom > out.reportDateTo) out.reportDateFrom = out.reportDateTo;
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
    set("scalePercent", QString::number(normalizedScale(settings.scalePercent))); set("compactRows", settings.compactRows ? "1" : "0"); set("fullscreen", settings.fullscreen ? "1" : "0"); set("decorated", settings.decorated ? "1" : "0"); set("minimizeToTray", settings.minimizeToTray ? "1" : "0");
    auto profileId = settings.lastProfileId; profileId.remove('\r'); profileId.remove('\n');
    set("lastProfileId", profileId); set("lastPage", QString::number(std::clamp(settings.lastPage, 0, 16)));
    set("taskStatusFilter", QString::number(std::clamp(settings.taskStatusFilter, 0, 3)));
    set("taskPriorityFilter", QString::number(std::clamp(settings.taskPriorityFilter, 0, 4)));
    set("taskQuickFilter", QString::number(std::clamp(settings.taskQuickFilter, 0, 8)));
    auto projectId = settings.taskProjectId; projectId.remove('\r'); projectId.remove('\n');
    auto pipelineId = settings.taskPipelineStepId; pipelineId.remove('\r'); pipelineId.remove('\n');
    set("taskProjectId", projectId); set("taskPipelineStepId", pipelineId);
    auto catalogProfessionId = settings.catalogProfessionId; catalogProfessionId.remove('\r'); catalogProfessionId.remove('\n');
    set("catalogProfessionId", catalogProfessionId);
    set("reportView", QString::number(std::clamp(settings.reportView, 0, 1)));
    set("reportDateRange", QString::number(std::clamp(settings.reportDateRange, 0, 4)));
    const auto today = QDate::currentDate();
    const auto reportFrom = settings.reportDateFrom.isValid() ? settings.reportDateFrom : today.addDays(-29);
    const auto reportTo = settings.reportDateTo.isValid() ? settings.reportDateTo : today;
    set("reportDateFrom", (reportFrom <= reportTo ? reportFrom : reportTo).toString(Qt::ISODate));
    set("reportDateTo", reportTo.toString(Qt::ISODate));
    set("projectSortMode", QString::number(std::clamp(settings.projectSortMode, 0, 3)));
    set("projectsOverdueOnly", settings.projectsOverdueOnly ? "1" : "0");
    set("projectsXpPendingOnly", settings.projectsXpPendingOnly ? "1" : "0");
    set("auditSourceFilter", QString::number(std::clamp(settings.auditSourceFilter, 0, 2)));
    set("logAutoScroll", settings.logAutoScroll ? "1" : "0");
    set("logCompactView", settings.logCompactView ? "1" : "0");
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
    auto* fullscreen = new QCheckBox(QString::fromUtf8("Полноэкранный режим (F11)")); fullscreen->setObjectName("qtFullscreen"); fullscreen->setChecked(settings.fullscreen);
    auto* decorated = new QCheckBox(QString::fromUtf8("Показывать рамку окна")); decorated->setObjectName("qtDecorated"); decorated->setChecked(settings.decorated);
    auto* tray = new QCheckBox(QString::fromUtf8("При закрытии сворачивать в трей и продолжать напоминания"));
    tray->setObjectName("qtMinimizeToTray");
    tray->setChecked(settings.minimizeToTray);
    tray->setEnabled(QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages());
    tray->setToolTip(tray->isEnabled() ? QString::fromUtf8("Окно скроется, но приложение останется запущенным. Выход доступен из меню значка в трее.")
        : QString::fromUtf8("Системный трей недоступен в этой среде."));
    form->addRow(QString::fromUtf8("Масштаб текста"), scale); form->addRow(compact); form->addRow(fullscreen); form->addRow(decorated); form->addRow(tray);
    auto* hint = new QLabel(QString::fromUtf8("Цветовая схема зафиксирована для миграции и здесь не меняется.")); hint->setWordWrap(true); form->addRow(hint);
    auto* notice = new QLabel; notice->setObjectName("qtSettingsNotice"); notice->setWordWrap(true); form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true); buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { auto next = settings; next.scalePercent = scale->currentData().toInt(); next.compactRows = compact->isChecked(); next.fullscreen = fullscreen->isChecked(); next.decorated = decorated->isChecked(); next.minimizeToTray = tray->isEnabled() && tray->isChecked(); if (!SaveQtDisplaySettings(directory, next)) { notice->setText(QString::fromUtf8("Не удалось атомарно сохранить настройки.")); return; } settings = next; dialog.accept(); });
    return dialog.exec() == QDialog::Accepted;
}
