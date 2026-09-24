#pragma once
#include <filesystem>
#include <QDate>
#include <QString>
class QApplication;
class QWidget;
struct QtDisplaySettings {
    int scalePercent = 100;
    bool compactRows = false;
    bool fullscreen = false;
    bool decorated = true;
    bool minimizeToTray = false;
    QString lastProfileId;
    int lastPage = 0;
    int taskStatusFilter = 0;
    int taskPriorityFilter = 0;
    int taskQuickFilter = 0;
    QString taskProjectId;
    QString taskPipelineStepId;
    int reportView = 0;
    int reportDateRange = 0;
    QDate reportDateFrom;
    QDate reportDateTo;
    int projectSortMode = 0;
    bool projectsOverdueOnly = false;
    bool projectsXpPendingOnly = false;
    int auditSourceFilter = 0;
};
QtDisplaySettings LoadQtDisplaySettings(const std::filesystem::path& directory);
bool SaveQtDisplaySettings(const std::filesystem::path& directory, const QtDisplaySettings& settings);
void ApplyQtDisplaySettings(QApplication& app, const QtDisplaySettings& settings);
bool ShowQtDisplaySettings(QWidget* parent, const std::filesystem::path& directory, QtDisplaySettings& settings);
