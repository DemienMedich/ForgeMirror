#pragma once

#include "AppDomainTypes.h"
#include <functional>
#include <string>
#include <vector>

class QWidget;

bool ShowQtPipelineMap(QWidget* parent, const std::vector<PipelineStep>& steps,
                       const std::string& selectedId = {}, std::function<bool()> motionAllowed = {});
