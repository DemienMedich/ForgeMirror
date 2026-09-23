#pragma once
#include <filesystem>
#include <QString>
class QApplication;
class QWidget;
struct QtDisplaySettings {
    int scalePercent = 100;
    bool compactRows = false;
    bool fullscreen = false;
    bool decorated = true;
    QString lastProfileId;
    int lastPage = 0;
};
QtDisplaySettings LoadQtDisplaySettings(const std::filesystem::path& directory);
bool SaveQtDisplaySettings(const std::filesystem::path& directory, const QtDisplaySettings& settings);
void ApplyQtDisplaySettings(QApplication& app, const QtDisplaySettings& settings);
bool ShowQtDisplaySettings(QWidget* parent, const std::filesystem::path& directory, QtDisplaySettings& settings);
