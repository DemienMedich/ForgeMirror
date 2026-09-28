#pragma once
class QApplication;
void ApplyQtTheme(QApplication& app);
void ApplyQtLayoutMetrics(QApplication& app, int spacingPercent, int cornerRadius,
    double windowRounding, double frameRounding, double scrollbarRounding, double grabRounding,
    double framePaddingX, double framePaddingY, double itemSpacingX, double itemSpacingY);
