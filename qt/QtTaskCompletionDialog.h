#pragma once
#include "AppDomainTypes.h"
#include <QString>
#include <functional>
#include <string>
class QWidget;
class QtWorkspace;
bool ShowTaskCompletionDialog(QWidget* parent, QtWorkspace& workspace,
                              const QString& taskId, const QString& activeProfileId,
                              std::function<void(AppLogLevel, const std::string&)> eventLogger = {});
