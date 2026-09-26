#pragma once
#include "QtWorkspace.h"
#include <QString>

class QWidget;

// Empty id creates a skill. Returns an empty string only after checked persistence.
QString SaveQtSkill(QtWorkspace& workspace, const std::string& id, const QString& name,
                    double weight, const QString& description, const QString& category,
                    const std::optional<std::vector<std::string>>& professions = std::nullopt);
QString MergeQtSkills(QtWorkspace& workspace, const std::string& restoreProfileId,
                      const std::string& fromId, const std::string& toId,
                      const QString& name, double weight, const QString& description,
                      const QString& category, const std::vector<std::string>& professions);
bool ShowSkillEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id = {},
                     const std::string& restoreProfileId = {});
