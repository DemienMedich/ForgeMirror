#pragma once
#include "QtWorkspace.h"
#include <QString>
class QWidget;
QString GrantQtAchievement(QtWorkspace& workspace, const std::string& profileId,
    const QString& title, const std::string& skillId, double bonus, int days);
void ShowAchievements(QWidget* parent, QtWorkspace& workspace, const std::string& profileId, bool admin);
