#pragma once
#include <filesystem>
#include <QDate>
#include <QStringList>
#include <QString>
class QApplication;
class QWidget;
struct QtDisplaySettings {
    int scalePercent = 100;
    int spacingPercent = 100;
    int cornerRadius = 4;
    bool compactRows = false;
    bool fullscreen = false;
    bool decorated = true;
    bool minimizeToTray = false;
    bool deadlineNotificationsWhenClosed = false;
    QString lastProfileId;
    int lastPage = 0;
    int profileViewMode = 1;
    int profileSkillSort = 0;
    int profileSkillWeightCategory = 0;
    double profileSkillWeightMin = 0.0;
    double profileSkillWeightMax = 2.0;
    int taskStatusFilter = 0;
    int taskPriorityFilter = 0;
    int taskQuickFilter = 0;
    int taskCreatedRange = 0;
    int taskSortMode = 0;
    QString taskAssigneeProfileId;
    QString taskProjectId;
    QString taskPipelineStepId;
    QString catalogProfessionId;
    int reportView = 0;
    int reportDateRange = 0;
    bool reportComparePrevious = false;
    QDate reportDateFrom;
    QDate reportDateTo;
    int projectSortMode = 0;
    bool projectsOverdueOnly = false;
    bool projectsXpPendingOnly = false;
    int auditSourceFilter = 0;
    bool logAutoScroll = true;
    bool logCompactView = false;
    QString adminStatsSearch;
    bool adminStatsIncludeArchived = true;
    int adminStatsRankFilter = 0;
    int adminStatsView = 0;
    bool adminStatsAutoRefresh = true;
    int adminStatsRefreshSeconds = 30;
    int adminStatsInactivityDays = 30;
};
struct QtLayoutPreset {
    QString name;
    int scalePercent = 100;
    int spacingPercent = 100;
    int cornerRadius = 4;
    bool compactRows = false;
    bool fullscreen = false;
    bool decorated = true;
};
QtDisplaySettings LoadQtDisplaySettings(const std::filesystem::path& directory);
bool SaveQtDisplaySettings(const std::filesystem::path& directory, const QtDisplaySettings& settings);
QStringList ListQtLayoutPresets(const std::filesystem::path& directory);
bool LoadQtLayoutPreset(const std::filesystem::path& directory, const QString& name, QtLayoutPreset* preset);
bool SaveQtLayoutPreset(const std::filesystem::path& directory, const QtLayoutPreset& preset, QString* error = nullptr);
bool DeleteQtLayoutPreset(const std::filesystem::path& directory, const QString& name, QString* error = nullptr);
bool IsQtLayoutPresetDeletable(const std::filesystem::path& directory, const QString& name);
void ApplyQtDisplaySettings(QApplication& app, const QtDisplaySettings& settings);
bool ShowQtDisplaySettings(QWidget* parent, const std::filesystem::path& directory, QtDisplaySettings& settings);
