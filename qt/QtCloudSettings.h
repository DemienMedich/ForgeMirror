#pragma once
#include <filesystem>
class QWidget;
bool ShowCloudSettings(QWidget* parent, const std::filesystem::path& workspaceDirectory);
