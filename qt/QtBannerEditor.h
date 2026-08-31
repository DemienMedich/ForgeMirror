#pragma once
#include "QtWorkspace.h"
class QWidget;
class QString;
bool ShowBannerEditor(QWidget* parent, QtWorkspace& workspace, int editIndex = -1);
bool DeleteBannerTextChecked(QtWorkspace& workspace, int index, QString* error = nullptr);
