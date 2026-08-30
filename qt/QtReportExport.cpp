#include "QtReportExport.h"
#include <QFileInfo>
#include <QSaveFile>
#include <sstream>

bool ExportTeamValueReportCsv(const QString& path, const TeamValueReport& report, QString* error) {
    if (error) error->clear();
    if (path.trimmed().isEmpty() || QFileInfo(path).isDir()) {
        if (error) *error = QString::fromUtf8("Выберите имя CSV-файла.");
        return false;
    }
    std::ostringstream stream;
    if (!WriteTeamValueReportCsv(stream, report)) {
        if (error) *error = QString::fromUtf8("Не удалось сформировать CSV-отчёт.");
        return false;
    }
    const auto payload = stream.str();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF", 3) != 3 ||
        file.write(payload.data(), qint64(payload.size())) != qint64(payload.size()) || !file.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить CSV-отчёт.");
        return false;
    }
    return true;
}
