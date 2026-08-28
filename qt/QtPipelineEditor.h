#pragma once
#include "QtWorkspace.h"
class QWidget;
bool ShowPipelineEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id = {});
