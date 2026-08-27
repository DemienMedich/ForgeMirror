#pragma once
#include <QString>
class QWidget;
class QtWorkspace;
bool ShowTaskCompletionDialog(QWidget* parent, QtWorkspace& workspace,
                              const QString& taskId, const QString& activeProfileId);
