#include "QtAuditExport.h"

#include <QFileInfo>
#include <QSaveFile>

namespace {
QByteArray csvCell(const QString& value) {
    QByteArray encoded = value.toUtf8();
    if (!value.contains(',') && !value.contains('"') && !value.contains('\n') && !value.contains('\r')) return encoded;
    encoded.replace("\"", "\"\"");
    return '"' + encoded + '"';
}

QByteArray csvRecord(const QStringList& values) {
    QByteArray result;
    for (qsizetype column = 0; column < values.size(); ++column) {
        if (column) result += ',';
        result += csvCell(values[column]);
    }
    result += "\r\n";
    return result;
}
}

bool ExportQtAuditCsv(const QString& path, const QStringList& headers,
                      const QVector<QStringList>& rows, QString* error) {
    if (error) error->clear();
    if (path.trimmed().isEmpty() || QFileInfo(path).isDir() || headers.isEmpty()) {
        if (error) *error = QString::fromUtf8("Выберите имя CSV-файла.");
        return false;
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    auto write = [&file](const QByteArray& bytes) {
        return file.write(bytes) == bytes.size();
    };
    if (!file.open(QIODevice::WriteOnly) || !write(QByteArray("\xEF\xBB\xBF", 3)) || !write(csvRecord(headers))) {
        if (error) *error = QString::fromUtf8("Не удалось начать атомарный экспорт аудита.");
        return false;
    }
    for (const auto& row : rows) {
        if (row.size() != headers.size() || !write(csvRecord(row))) {
            file.cancelWriting();
            if (error) *error = QString::fromUtf8("Не удалось сформировать CSV аудита.");
            return false;
        }
    }
    if (!file.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить CSV аудита.");
        return false;
    }
    return true;
}
