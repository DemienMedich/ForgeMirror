#pragma once
#include "QtWorkspace.h"
#include <QString>
class QWidget;
bool ShowProfessionEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id = {},
                          const std::string& restoreProfileId = {});
QString MergeQtProfessions(QtWorkspace& workspace, const std::string& restoreProfileId,
                           const std::string& fromId, const std::string& toId,
                           const QString& destinationDescription);
