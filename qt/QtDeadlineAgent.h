#pragma once

#include "AppDomainTypes.h"

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct QtDeadlineSummary {
    int upcoming = 0;
    int overdue = 0;
    QByteArray signature;
};

QtDeadlineSummary EvaluateQtDeadlines(const std::vector<TaskEntry>& tasks, std::int64_t now);
bool ConfigureQtDeadlineSchedule(bool enabled, QString* error = nullptr);
int RunQtDeadlineAgent(const std::filesystem::path& workspace);
