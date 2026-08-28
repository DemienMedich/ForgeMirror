#pragma once
#include "QtWorkspace.h"
#include <QString>
#include <QByteArray>
#include <optional>
class QWidget;
QString GrantQtAchievement(QtWorkspace& workspace, const std::string& profileId,
    const QString& title, const std::string& skillId, double bonus, int days, const QString& icon = {});
QString UpdateQtAchievement(QtWorkspace& workspace, const std::string& profileId,
    int index, const QByteArray& expectedFile, const QString& title, double bonus,
    std::optional<int> durationDays = std::nullopt, bool revoke = false,
    std::optional<QString> icon = std::nullopt);
void ShowAchievements(QWidget* parent, QtWorkspace& workspace, const std::string& profileId, bool admin);
