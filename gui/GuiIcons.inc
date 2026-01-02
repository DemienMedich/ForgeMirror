enum class UiIcon {
    User = 0,
    Users,
    Settings,
    Bell,
    Search,
    Chart,
    Refresh,
    Exit,
    LockClosed,
    LockOpen,
    Home,
    Book,
    Flow,
    Cube,
    List
};

static ImVec2 IconPoint(ImVec2 center, float x, float y, float size) {
    return ImVec2(center.x + x * size, center.y + y * size);
}

static void DrawIconGlyph(ImDrawList* drawList, UiIcon icon, ImVec2 center, float size, ImU32 color) {
    if (!drawList || size <= 0.0f) return;
    const float s = size * 0.5f;
    const float stroke = std::max(1.6f, size * 0.10f);
    constexpr float kPi = 3.14159265358979323846f;
    switch (icon) {
        case UiIcon::Home: {
            ImVec2 roofTop = IconPoint(center, 0.0f, -0.62f, s);
            ImVec2 roofLeft = IconPoint(center, -0.70f, -0.05f, s);
            ImVec2 roofRight = IconPoint(center, 0.70f, -0.05f, s);
            drawList->AddTriangle(roofTop, roofLeft, roofRight, color, stroke);
            ImVec2 bodyA = IconPoint(center, -0.55f, -0.05f, s);
            ImVec2 bodyB = IconPoint(center, 0.55f, 0.62f, s);
            drawList->AddRect(bodyA, bodyB, color, s * 0.18f, 0, stroke);
            ImVec2 doorA = IconPoint(center, -0.12f, 0.15f, s);
            ImVec2 doorB = IconPoint(center, 0.12f, 0.62f, s);
            drawList->AddRect(doorA, doorB, color, s * 0.12f, 0, stroke);
            return;
        }
        case UiIcon::Book: {
            ImVec2 a = IconPoint(center, -0.62f, -0.55f, s);
            ImVec2 b = IconPoint(center, 0.62f, 0.55f, s);
            drawList->AddRect(a, b, color, s * 0.14f, 0, stroke);
            ImVec2 spineA = IconPoint(center, -0.18f, -0.55f, s);
            ImVec2 spineB = IconPoint(center, -0.18f, 0.55f, s);
            drawList->AddLine(spineA, spineB, color, stroke);
            ImVec2 line0A = IconPoint(center, 0.02f, -0.28f, s);
            ImVec2 line0B = IconPoint(center, 0.46f, -0.28f, s);
            ImVec2 line1A = IconPoint(center, 0.02f, 0.02f, s);
            ImVec2 line1B = IconPoint(center, 0.46f, 0.02f, s);
            drawList->AddLine(line0A, line0B, color, stroke);
            drawList->AddLine(line1A, line1B, color, stroke);
            return;
        }
        case UiIcon::Flow: {
            ImVec2 n0 = IconPoint(center, -0.55f, -0.18f, s);
            ImVec2 n1 = IconPoint(center, 0.0f, 0.0f, s);
            ImVec2 n2 = IconPoint(center, 0.55f, 0.18f, s);
            float r = s * 0.18f;
            drawList->AddLine(n0, n1, color, stroke);
            drawList->AddLine(n1, n2, color, stroke);
            drawList->AddCircle(n0, r, color, 14, stroke);
            drawList->AddCircle(n1, r, color, 14, stroke);
            drawList->AddCircle(n2, r, color, 14, stroke);
            return;
        }
        case UiIcon::Cube: {
            ImVec2 a = IconPoint(center, -0.42f, -0.22f, s);
            ImVec2 b = IconPoint(center, 0.42f, 0.62f, s);
            ImVec2 off(s * 0.22f, -s * 0.22f);
            ImVec2 a2(a.x + off.x, a.y + off.y);
            ImVec2 b2(b.x + off.x, b.y + off.y);
            drawList->AddRect(a, b, color, s * 0.10f, 0, stroke);
            drawList->AddRect(a2, b2, color, s * 0.10f, 0, stroke);
            drawList->AddLine(a, a2, color, stroke);
            drawList->AddLine(ImVec2(b.x, a.y), ImVec2(b2.x, a2.y), color, stroke);
            drawList->AddLine(ImVec2(a.x, b.y), ImVec2(a2.x, b2.y), color, stroke);
            drawList->AddLine(b, b2, color, stroke);
            return;
        }
        case UiIcon::List: {
            float xL = center.x - s * 0.55f;
            float xR = center.x + s * 0.55f;
            float y0 = center.y - s * 0.30f;
            float y1 = center.y;
            float y2 = center.y + s * 0.30f;
            float r = s * 0.10f;
            drawList->AddCircleFilled(ImVec2(xL, y0), r, color, 12);
            drawList->AddCircleFilled(ImVec2(xL, y1), r, color, 12);
            drawList->AddCircleFilled(ImVec2(xL, y2), r, color, 12);
            drawList->AddLine(ImVec2(xL + r * 2.2f, y0), ImVec2(xR, y0), color, stroke);
            drawList->AddLine(ImVec2(xL + r * 2.2f, y1), ImVec2(xR, y1), color, stroke);
            drawList->AddLine(ImVec2(xL + r * 2.2f, y2), ImVec2(xR, y2), color, stroke);
            return;
        }
        case UiIcon::Search: {
            drawList->AddCircle(center, s * 0.58f, color, 20, stroke);
            ImVec2 a = IconPoint(center, 0.35f, 0.35f, s);
            ImVec2 b = IconPoint(center, 0.68f, 0.68f, s);
            drawList->AddLine(a, b, color, stroke);
            return;
        }
        case UiIcon::Bell: {
            ImVec2 top = IconPoint(center, 0.0f, -0.55f, s);
            ImVec2 left = IconPoint(center, -0.55f, 0.15f, s);
            ImVec2 right = IconPoint(center, 0.55f, 0.15f, s);
            ImVec2 bottom = IconPoint(center, 0.0f, 0.55f, s);
            drawList->PathClear();
            drawList->PathArcTo(top, s * 0.62f, kPi, 2.0f * kPi, 18);
            drawList->PathLineTo(right);
            drawList->PathLineTo(left);
            drawList->PathLineTo(IconPoint(center, -0.28f, 0.15f, s));
            drawList->PathStroke(color, true, stroke);
            drawList->AddCircleFilled(bottom, s * 0.12f, color, 12);
            return;
        }
        case UiIcon::User: {
            ImVec2 head = IconPoint(center, 0.0f, -0.20f, s);
            drawList->AddCircle(head, s * 0.34f, color, 18, stroke);
            drawList->PathClear();
            drawList->PathArcTo(IconPoint(center, 0.0f, 0.70f, s), s * 0.74f, kPi * 1.15f, kPi * 1.85f, 18);
            drawList->PathStroke(color, false, stroke);
            return;
        }
        case UiIcon::Users: {
            ImVec2 headA = IconPoint(center, -0.22f, -0.18f, s);
            ImVec2 headB = IconPoint(center, 0.24f, -0.08f, s);
            drawList->AddCircle(headA, s * 0.28f, color, 18, stroke);
            drawList->AddCircle(headB, s * 0.26f, color, 18, stroke);
            drawList->PathClear();
            drawList->PathArcTo(IconPoint(center, -0.18f, 0.70f, s), s * 0.70f, kPi * 1.18f, kPi * 1.82f, 18);
            drawList->PathStroke(color, false, stroke);
            drawList->PathClear();
            drawList->PathArcTo(IconPoint(center, 0.30f, 0.78f, s), s * 0.60f, kPi * 1.22f, kPi * 1.78f, 18);
            drawList->PathStroke(color, false, stroke);
            return;
        }
        case UiIcon::Chart: {
            float w = s * 0.20f;
            float gap = s * 0.12f;
            float baseY = center.y + s * 0.55f;
            float x0 = center.x - (w * 1.5f + gap);
            ImVec2 r0a(x0, baseY - s * 0.55f);
            ImVec2 r0b(x0 + w, baseY);
            ImVec2 r1a(x0 + w + gap, baseY - s * 0.35f);
            ImVec2 r1b(x0 + w + gap + w, baseY);
            ImVec2 r2a(x0 + (w + gap) * 2.0f, baseY - s * 0.70f);
            ImVec2 r2b(x0 + (w + gap) * 2.0f + w, baseY);
            drawList->AddRectFilled(r0a, r0b, color, w * 0.35f);
            drawList->AddRectFilled(r1a, r1b, color, w * 0.35f);
            drawList->AddRectFilled(r2a, r2b, color, w * 0.35f);
            return;
        }
        case UiIcon::Settings: {
            drawList->AddCircle(center, s * 0.32f, color, 18, stroke);
            const int teeth = 6;
            for (int i = 0; i < teeth; ++i) {
                float a = (2.0f * kPi) * (static_cast<float>(i) / teeth);
                ImVec2 p0(center.x + std::cos(a) * s * 0.50f, center.y + std::sin(a) * s * 0.50f);
                ImVec2 p1(center.x + std::cos(a) * s * 0.78f, center.y + std::sin(a) * s * 0.78f);
                drawList->AddLine(p0, p1, color, stroke);
            }
            drawList->AddCircleFilled(center, s * 0.12f, color, 12);
            return;
        }
        case UiIcon::Refresh: {
            float r = s * 0.62f;
            float a0 = -kPi * 0.20f;
            float a1 = kPi * 1.10f;
            drawList->PathClear();
            drawList->PathArcTo(center, r, a0, a1, 20);
            drawList->PathStroke(color, false, stroke);
            ImVec2 tip(center.x + std::cos(a0) * r, center.y + std::sin(a0) * r);
            ImVec2 ah0(center.x + std::cos(a0 + 0.55f) * r * 0.22f, center.y + std::sin(a0 + 0.55f) * r * 0.22f);
            ImVec2 ah1(center.x + std::cos(a0 - 0.55f) * r * 0.22f, center.y + std::sin(a0 - 0.55f) * r * 0.22f);
            drawList->AddTriangleFilled(tip, ImVec2(tip.x + (ah0.x - center.x), tip.y + (ah0.y - center.y)),
                                        ImVec2(tip.x + (ah1.x - center.x), tip.y + (ah1.y - center.y)), color);
            return;
        }
        case UiIcon::Exit: {
            ImVec2 doorA = IconPoint(center, -0.60f, -0.55f, s);
            ImVec2 doorB = IconPoint(center, -0.05f, 0.55f, s);
            drawList->AddRect(doorA, doorB, color, s * 0.18f, 0, stroke);
            ImVec2 mid = IconPoint(center, 0.05f, 0.0f, s);
            ImVec2 out = IconPoint(center, 0.62f, 0.0f, s);
            drawList->AddLine(mid, out, color, stroke);
            drawList->AddTriangleFilled(IconPoint(center, 0.62f, 0.0f, s),
                                        IconPoint(center, 0.42f, -0.18f, s),
                                        IconPoint(center, 0.42f, 0.18f, s), color);
            return;
        }
        case UiIcon::LockClosed:
        case UiIcon::LockOpen: {
            ImVec2 bodyA = IconPoint(center, -0.50f, -0.05f, s);
            ImVec2 bodyB = IconPoint(center, 0.50f, 0.62f, s);
            drawList->AddRect(bodyA, bodyB, color, s * 0.18f, 0, stroke);
            ImVec2 shCenter = IconPoint(center, icon == UiIcon::LockOpen ? -0.18f : 0.0f, -0.05f, s);
            float shR = s * 0.46f;
            float shA0 = kPi;
            float shA1 = 2.0f * kPi;
            drawList->PathClear();
            drawList->PathArcTo(shCenter, shR, shA0, shA1, 18);
            drawList->PathStroke(color, false, stroke);
            return;
        }
        default:
            return;
    }
}

static void DrawAnchorIcon(UiIcon icon, float size = 12.0f) {
    const float cellWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float cellHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 cellPos = ImGui::GetCursorScreenPos();
    const float iconSize = std::min(size, cellHeight * 0.85f);
    const ImVec2 center(cellPos.x + cellWidth * 0.5f, cellPos.y + cellHeight * 0.5f);
    ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    color.w = std::max(color.w, 0.65f);
    DrawIconGlyph(ImGui::GetWindowDrawList(), icon, center, iconSize, ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(cellWidth, cellHeight));
}

static void DrawIconBadge(ImDrawList* drawList, UiIcon icon, ImVec2 center, float radius, const ImVec4& accent,
                          bool filled = true) {
    if (!drawList || radius <= 0.0f) return;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 base = style.Colors[ImGuiCol_FrameBg];
    ImVec4 border = style.Colors[ImGuiCol_Border];
    ImVec4 glow = accent;
    base.w = std::max(base.w, 0.55f);
    border.w = std::max(border.w, 0.25f);
    glow.w = 0.10f;

    if (filled) {
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(base), 24);
    }
    drawList->AddCircle(center, radius, ImGui::GetColorU32(border), 24, 1.0f);
    drawList->AddCircle(center, radius + 1.0f, ImGui::GetColorU32(glow), 24, 1.5f);

    ImVec4 iconColor = accent;
    iconColor.w = 1.0f;
    DrawIconGlyph(drawList, icon, center, radius * 1.25f, ImGui::GetColorU32(iconColor));
}

static bool IconButtonGui(const char* id, UiIcon icon, const char* tooltip, const ImVec4& accent,
                          float size, bool enabled = true, bool selected = false) {
    if (size <= 0.0f) return false;
    ImGui::PushID(id);
    if (!enabled) ImGui::BeginDisabled();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float rounding = std::max(4.0f, style.FrameRounding + 2.0f);
    const ImVec2 buttonSize(size, size);
    const bool pressed = ImGui::InvisibleButton("##icon_button", buttonSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();
    const ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool active = ImGui::IsItemActive();

    ImVec4 base = style.Colors[ImGuiCol_Button];
    ImVec4 hover = style.Colors[ImGuiCol_ButtonHovered];
    ImVec4 down = style.Colors[ImGuiCol_ButtonActive];
    ImVec4 border = style.Colors[ImGuiCol_Border];
    ImVec4 glow = accent;
    glow.w = 0.10f;

    if (selected) {
        base = BlendColor(base, accent, 0.35f);
        hover = BlendColor(hover, accent, 0.35f);
        down = BlendColor(down, accent, 0.35f);
    }

    ImVec4 bg = base;
    if (active) bg = down;
    else if (hovered) bg = hover;
    bg.w = std::max(bg.w, 0.55f);
    border.w = std::max(border.w, 0.22f);

    drawList->AddRectFilled(pMin, pMax, ImGui::GetColorU32(bg), rounding);
    drawList->AddRect(pMin, pMax, ImGui::GetColorU32(border), rounding, 0, 1.0f);
    drawList->AddRect(pMin, pMax, ImGui::GetColorU32(glow), rounding, 0, 1.5f);

    ImVec4 iconColor = selected ? accent : style.Colors[ImGuiCol_Text];
    if (!enabled) iconColor = style.Colors[ImGuiCol_TextDisabled];
    iconColor.w = 1.0f;
    DrawIconGlyph(drawList, icon, center, size * 0.70f, ImGui::GetColorU32(iconColor));

    if (hovered && tooltip && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }

    if (!enabled) ImGui::EndDisabled();
    ImGui::PopID();
    return pressed;
}

static bool NavIconButtonGui(const char* id, UiIcon icon, const char* label, const ImVec4& accent,
                             float height, bool enabled = true, bool selected = false) {
    if (height <= 0.0f) return false;
    ImGui::PushID(id);
    if (!enabled) ImGui::BeginDisabled();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float rounding = std::max(6.0f, style.FrameRounding + 4.0f);
    const ImVec2 size(width, height);
    const bool pressed = ImGui::InvisibleButton("##nav_button", size);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();
    const ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool active = ImGui::IsItemActive();

    ImVec4 base = style.Colors[ImGuiCol_Button];
    ImVec4 hover = style.Colors[ImGuiCol_ButtonHovered];
    ImVec4 down = style.Colors[ImGuiCol_ButtonActive];
    ImVec4 border = style.Colors[ImGuiCol_Border];
    ImVec4 glow = accent;
    glow.w = 0.10f;

    if (selected) {
        base = BlendColor(base, accent, 0.35f);
        hover = BlendColor(hover, accent, 0.35f);
        down = BlendColor(down, accent, 0.35f);
    }

    ImVec4 bg = base;
    if (active) bg = down;
    else if (hovered) bg = hover;
    bg.w = std::max(bg.w, 0.55f);
    border.w = std::max(border.w, 0.22f);

    drawList->AddRectFilled(pMin, pMax, ImGui::GetColorU32(bg), rounding);
    drawList->AddRect(pMin, pMax, ImGui::GetColorU32(border), rounding, 0, 1.0f);
    drawList->AddRect(pMin, pMax, ImGui::GetColorU32(glow), rounding, 0, 1.5f);

    if (selected) {
        ImVec4 mark = accent;
        mark.w = 0.90f;
        const float markW = 3.0f;
        drawList->AddRectFilled(ImVec2(pMin.x, pMin.y + rounding * 0.35f),
                                ImVec2(pMin.x + markW, pMax.y - rounding * 0.35f),
                                ImGui::GetColorU32(mark), markW);
    }

    const float padX = 10.0f;
    const float badgeRadius = std::clamp((height - 16.0f) * 0.5f, 14.0f, 18.0f);
    const ImVec2 badgeCenter(pMin.x + padX + badgeRadius, center.y);
    ImVec4 badgeAccent = selected ? accent : BlendColor(accent, style.Colors[ImGuiCol_TextDisabled], 0.55f);
    badgeAccent.w = 1.0f;
    DrawIconBadge(drawList, icon, badgeCenter, badgeRadius, badgeAccent, true);

    const float textX = badgeCenter.x + badgeRadius + 10.0f;
    const char* text = label ? label : "";
    const ImU32 textCol = ImGui::GetColorU32(!enabled ? style.Colors[ImGuiCol_TextDisabled]
                                                     : (selected ? accent : style.Colors[ImGuiCol_Text]));
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 textPos(textX, center.y - textSize.y * 0.5f);
    drawList->AddText(textPos, textCol, text);

    if (!enabled) ImGui::EndDisabled();
    ImGui::PopID();
    return pressed;
}
