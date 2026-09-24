#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

bool ExportQtAuditCsv(const QString& path, const QStringList& headers,
                      const QVector<QStringList>& rows, QString* error = nullptr);
