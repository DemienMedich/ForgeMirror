#pragma once

#include "AppDomainTypes.h"
#include <vector>

class QWidget;

bool ShowQtPipelineMap(QWidget* parent, const std::vector<PipelineStep>& steps);
