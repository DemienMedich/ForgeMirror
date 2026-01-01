// Render a polar chart of skill levels inside the profile preview.
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
