#pragma once

#include "AppTeamValueReportService.h"
#include <QString>

bool ExportTeamValueReportCsv(const QString& path, const TeamValueReport& report, QString* error = nullptr);
