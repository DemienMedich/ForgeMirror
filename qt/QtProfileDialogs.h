#pragma once
#include <QString>
class QWidget;
class QtWorkspace;
void ShowProfileManager(QWidget* parent, QtWorkspace& workspace, const QString& activeId);
bool ShowProfilePasswordDialog(QWidget* parent, QtWorkspace& workspace, const QString& profileId,
                               const QString& activeId, bool adminReset);
