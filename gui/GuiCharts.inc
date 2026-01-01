// Render a polar chart of skill levels inside the profile preview.
static ImVec4 ChartPaletteColor(int index, int count, float alpha = 1.0f) {
    if (count <= 0) return ImVec4(1, 1, 1, alpha);
    float t = static_cast<float>(index % count) / static_cast<float>(count);
    float h = std::fmod(0.68f + t * 0.90f, 1.0f);
    return ImColor::HSV(h, 0.55f, 0.95f, alpha);
}

void DrawProgressRing(ImDrawList* drawList, ImVec2 center, float radius, float thickness,
                      float ratio, ImU32 trackColor, ImU32 fillColor) {
    if (!drawList || radius <= 1.0f || thickness <= 0.0f) return;
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    constexpr float kPi = 3.14159265358979323846f;
    const float start = -kPi * 0.5f;
    const float fullEnd = start + 2.0f * kPi;
    const float end = start + 2.0f * kPi * ratio;
    const int seg = std::clamp(static_cast<int>(radius * 1.2f), 24, 128);

    drawList->PathClear();
    drawList->PathArcTo(center, radius, start, fullEnd, seg);
    drawList->PathStroke(trackColor, false, thickness);

    if (ratio <= 0.0f) return;
    drawList->PathClear();
    drawList->PathArcTo(center, radius, start, end, seg);
    drawList->PathStroke(fillColor, false, thickness);
}

void DrawSegmentedRing(ImDrawList* drawList, ImVec2 center, float radius, float thickness,
                       const float* values, int valueCount, float maxValue,
                       float gapAngleRad = 0.08f, float startAngleRad = -3.14159265358979323846f * 0.5f) {
    if (!drawList || radius <= 1.0f || thickness <= 0.0f || !values || valueCount <= 0) return;
    if (maxValue <= 0.0f) maxValue = 1.0f;
    constexpr float kPi = 3.14159265358979323846f;
    const float full = 2.0f * kPi;
    const float segmentSpan = full / static_cast<float>(valueCount);
    const float gap = std::clamp(gapAngleRad, 0.0f, segmentSpan * 0.45f);
    const int seg = std::clamp(static_cast<int>(radius * 1.25f), 24, 140);

    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 track = style.Colors[ImGuiCol_TextDisabled];
    track.w = 0.22f;
    const ImU32 trackColor = ImGui::GetColorU32(track);

    for (int i = 0; i < valueCount; ++i) {
        const float v = std::clamp(values[i] / maxValue, 0.0f, 1.0f);
        const float a0 = startAngleRad + segmentSpan * static_cast<float>(i) + gap * 0.5f;
        const float a1 = startAngleRad + segmentSpan * static_cast<float>(i + 1) - gap * 0.5f;
        if (a1 <= a0) continue;

        drawList->PathClear();
        drawList->PathArcTo(center, radius, a0, a1, seg);
        drawList->PathStroke(trackColor, false, thickness);

        if (v <= 0.0f) continue;
        const float av = a0 + (a1 - a0) * v;
        ImVec4 col = ChartPaletteColor(i, valueCount, 0.92f);
        drawList->PathClear();
        drawList->PathArcTo(center, radius, a0, av, seg);
        drawList->PathStroke(ImGui::GetColorU32(col), false, thickness);
    }
}

void DrawSkillRadarChart(const std::vector<Skill>& skills, const ImVec2& canvasPos, const ImVec2& canvasSize, ImDrawList* drawList) {
    if (skills.size() < 3) {
        drawList->AddText(canvasPos, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          "Недостаточно данных для диаграммы (минимум 3 навыка).");
        return;
    }

    const float radius = 0.4f * std::min(canvasSize.x, canvasSize.y);
    if (radius <= 1.0f) {
        return;
    }

    const ImVec2 center = ImVec2(canvasPos.x + canvasSize.x * 0.5f,
                                 canvasPos.y + canvasSize.y * 0.5f);

    std::vector<float> values;
    values.reserve(skills.size());
    float maxValue = 0.0f;
    for (const auto& skill : skills) {
        float fractionalLevel = static_cast<float>(skill.level);
        if (skill.xpToNext > 0) {
            fractionalLevel += static_cast<float>(skill.xp) / static_cast<float>(skill.xpToNext);
        }
        values.push_back(fractionalLevel);
        if (fractionalLevel > maxValue) maxValue = fractionalLevel;
    }
    if (maxValue <= 0.0f) {
        maxValue = 1.0f;
    }

    std::vector<ImVec2> polygon(values.size());
    std::vector<ImVec2> temp(values.size());

    constexpr float kPi = 3.14159265358979323846f;
    const float angleStep = 2.0f * kPi / static_cast<float>(values.size());
    const float startAngle = -kPi * 0.5f;

    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 text = style.Colors[ImGuiCol_Text];
    ImVec4 textMuted = style.Colors[ImGuiCol_TextDisabled];

    ImVec4 grid = textMuted;
    grid.w = 0.22f;
    ImVec4 axis = textMuted;
    axis.w = 0.35f;
    ImVec4 fill = accent;
    fill.w = 0.18f;
    ImVec4 outline = accent;
    outline.w = 0.75f;
    ImVec4 point = BlendColor(accent, text, 0.40f);
    point.w = 0.95f;
    const ImVec4 label = text;

    const ImU32 gridColor = ImGui::GetColorU32(grid);
    const ImU32 axisColor = ImGui::GetColorU32(axis);
    const ImU32 fillColor = ImGui::GetColorU32(fill);
    const ImU32 outlineColor = ImGui::GetColorU32(outline);
    const ImU32 pointColor = ImGui::GetColorU32(point);
    const ImU32 labelColor = ImGui::GetColorU32(label);

    const int ringCount = 4;
    for (int ring = 1; ring <= ringCount; ++ring) {
        float ringRatio = static_cast<float>(ring) / ringCount;
        float ringRadius = radius * ringRatio;
        for (size_t i = 0; i < values.size(); ++i) {
            float angle = startAngle + angleStep * static_cast<float>(i);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            temp[i] = ImVec2(center.x + cosA * ringRadius,
                             center.y + sinA * ringRadius);
        }
        drawList->AddPolyline(temp.data(), static_cast<int>(temp.size()), gridColor, true, 1.0f);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        float angle = startAngle + angleStep * static_cast<float>(i);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        ImVec2 axisEnd(center.x + cosA * radius, center.y + sinA * radius);
        drawList->AddLine(center, axisEnd, axisColor, 1.0f);

        ImVec2 labelPos(center.x + cosA * (radius + 14.0f),
                        center.y + sinA * (radius + 14.0f));
        const std::string& name = skills[i].name;
        ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
        drawList->AddText(ImVec2(labelPos.x - textSize.x * 0.5f,
                                 labelPos.y - textSize.y * 0.5f),
                          labelColor, name.c_str());
    }

    for (size_t i = 0; i < values.size(); ++i) {
        float angle = startAngle + angleStep * static_cast<float>(i);
        float ratio = values[i] / maxValue;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        polygon[i] = ImVec2(center.x + cosA * radius * ratio,
                            center.y + sinA * radius * ratio);
    }

    drawList->AddConvexPolyFilled(polygon.data(), static_cast<int>(polygon.size()), fillColor);
    drawList->AddPolyline(polygon.data(), static_cast<int>(polygon.size()), outlineColor, true, 2.0f);
    for (const ImVec2& p : polygon) {
        drawList->AddCircleFilled(p, 3.0f, pointColor);
    }

    std::ostringstream legend;
    legend << "Макс. уровень: " << std::fixed << std::setprecision(1) << maxValue;
    drawList->AddText(ImVec2(canvasPos.x + 5.0f, canvasPos.y + 5.0f),
                      ImGui::GetColorU32(text),
                      legend.str().c_str());
}
