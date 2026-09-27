#include "QtReportExport.h"
#include <QFileInfo>
#include <QSaveFile>
#include <sstream>

namespace {
bool saveReportCsv(const QString& path, const std::string& payload, QString* error) {
    if (error) error->clear();
    if (path.trimmed().isEmpty() || QFileInfo(path).isDir()) {
        if (error) *error = QString::fromUtf8("Выберите имя CSV-файла.");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF", 3) != 3 ||
        file.write(payload.data(), qint64(payload.size())) != qint64(payload.size()) || !file.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить CSV-отчёт.");
        return false;
    }
    return true;
}
bool appendReport(std::ostringstream& stream, const QString& label, const QString& period, const TeamValueReport& report) {
    stream << "\nComparisonPeriod," << label.toUtf8().constData() << "," << period.toUtf8().constData() << "\n";
    return WriteTeamValueReportCsv(stream, report);
}
}

bool ExportTeamValueReportCsv(const QString& path, const TeamValueReport& report, QString* error) {
    std::ostringstream stream;
    if (!WriteTeamValueReportCsv(stream, report)) {
        if (error) *error = QString::fromUtf8("Не удалось сформировать CSV-отчёт.");
        return false;
    }
    return saveReportCsv(path, stream.str(), error);
}

bool ExportTeamValueReportComparisonCsv(const QString& path, const TeamValueReport& current,
    const QString& currentPeriod, const TeamValueReport& previous, const QString& previousPeriod, QString* error) {
    std::ostringstream stream;
    if (!appendReport(stream, QString::fromUtf8("Текущий период"), currentPeriod, current) ||
        !appendReport(stream, QString::fromUtf8("Предыдущий период"), previousPeriod, previous)) {
        if (error) *error = QString::fromUtf8("Не удалось сформировать CSV-отчёт сравнения.");
        return false;
    }
    return saveReportCsv(path, stream.str(), error);
}
