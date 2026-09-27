#pragma once

#include "AppTeamValueReportService.h"
#include <QString>

bool ExportTeamValueReportCsv(const QString& path, const TeamValueReport& report, QString* error = nullptr);
bool ExportTeamValueReportComparisonCsv(const QString& path, const TeamValueReport& current,
    const QString& currentPeriod, const TeamValueReport& previous, const QString& previousPeriod,
    QString* error = nullptr);
