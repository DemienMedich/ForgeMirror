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
    List,
    Launch,
    Save,
    Edit,
    Award,
    Export,
    Task,
    Project,
    Shortcut,
    Sync,
    Apply,
    Calendar,
    Merge,
    Info,
    SpiritGood,
    SpiritEvil,
    Play,
    Stop,
    Previous,
    Next,
    Plus,
    Copy,
    Close
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
        case UiIcon::Launch: {
            ImVec2 boxA = IconPoint(center, -0.55f, -0.55f, s);
            ImVec2 boxB = IconPoint(center, 0.35f, 0.35f, s);
            drawList->AddRect(boxA, boxB, color, s * 0.12f, 0, stroke);
            ImVec2 a = IconPoint(center, -0.10f, -0.10f, s);
            ImVec2 b = IconPoint(center, 0.60f, -0.60f, s);
            drawList->AddLine(a, b, color, stroke);
            ImVec2 head0 = IconPoint(center, 0.60f, -0.60f, s);
            ImVec2 head1 = IconPoint(center, 0.28f, -0.60f, s);
            ImVec2 head2 = IconPoint(center, 0.60f, -0.28f, s);
            drawList->AddLine(head0, head1, color, stroke);
            drawList->AddLine(head0, head2, color, stroke);
            return;
        }
        case UiIcon::Save: {
            ImVec2 bodyA = IconPoint(center, -0.58f, -0.58f, s);
            ImVec2 bodyB = IconPoint(center, 0.58f, 0.58f, s);
            drawList->AddRect(bodyA, bodyB, color, s * 0.12f, 0, stroke);
            drawList->AddRectFilled(IconPoint(center, -0.28f, -0.58f, s),
                                    IconPoint(center, 0.32f, -0.18f, s), color, s * 0.06f);
            drawList->AddRect(IconPoint(center, -0.32f, 0.08f, s),
                              IconPoint(center, 0.32f, 0.58f, s), color, s * 0.08f, 0, stroke);
            drawList->AddLine(IconPoint(center, -0.14f, 0.28f, s),
                              IconPoint(center, 0.14f, 0.28f, s), color, stroke * 0.8f);
            return;
        }
        case UiIcon::Edit: {
            ImVec2 a = IconPoint(center, -0.48f, 0.42f, s);
            ImVec2 b = IconPoint(center, 0.34f, -0.40f, s);
            ImVec2 c = IconPoint(center, 0.55f, -0.18f, s);
            ImVec2 d = IconPoint(center, -0.28f, 0.62f, s);
            drawList->AddLine(a, b, color, stroke);
            drawList->AddLine(IconPoint(center, -0.34f, 0.55f, s), c, color, stroke);
            drawList->AddTriangleFilled(a, d, IconPoint(center, -0.10f, 0.48f, s), color);
            drawList->AddLine(IconPoint(center, 0.22f, -0.52f, s),
                              IconPoint(center, 0.58f, -0.16f, s), color, stroke);
            return;
        }
        case UiIcon::Award: {
            ImVec2 top = IconPoint(center, 0.0f, -0.62f, s);
            ImVec2 right = IconPoint(center, 0.58f, -0.12f, s);
            ImVec2 lowerRight = IconPoint(center, 0.36f, 0.60f, s);
            ImVec2 lowerLeft = IconPoint(center, -0.36f, 0.60f, s);
            ImVec2 left = IconPoint(center, -0.58f, -0.12f, s);
            drawList->AddLine(top, right, color, stroke);
            drawList->AddLine(right, lowerRight, color, stroke);
            drawList->AddLine(lowerRight, lowerLeft, color, stroke);
            drawList->AddLine(lowerLeft, left, color, stroke);
            drawList->AddLine(left, top, color, stroke);
            drawList->AddText(IconPoint(center, -0.22f, -0.26f, s), color, "XP");
            return;
        }
        case UiIcon::Export: {
            ImVec2 trayA = IconPoint(center, -0.58f, 0.12f, s);
            ImVec2 trayB = IconPoint(center, 0.58f, 0.58f, s);
            drawList->AddRect(trayA, trayB, color, s * 0.10f, 0, stroke);
            ImVec2 shaftA = IconPoint(center, 0.0f, 0.32f, s);
            ImVec2 shaftB = IconPoint(center, 0.0f, -0.56f, s);
            drawList->AddLine(shaftA, shaftB, color, stroke);
            drawList->AddTriangleFilled(IconPoint(center, 0.0f, -0.64f, s),
                                        IconPoint(center, -0.28f, -0.30f, s),
                                        IconPoint(center, 0.28f, -0.30f, s), color);
            return;
        }
        case UiIcon::Task: {
            ImVec2 boxA = IconPoint(center, -0.58f, -0.58f, s);
            ImVec2 boxB = IconPoint(center, 0.58f, 0.58f, s);
            drawList->AddRect(boxA, boxB, color, s * 0.12f, 0, stroke);
            for (int i = 0; i < 3; ++i) {
                const float y = -0.30f + 0.30f * static_cast<float>(i);
                drawList->AddCircleFilled(IconPoint(center, -0.30f, y, s), s * 0.07f, color, 10);
                drawList->AddLine(IconPoint(center, -0.12f, y, s), IconPoint(center, 0.36f, y, s),
                                  color, stroke * 0.85f);
            }
            return;
        }
        case UiIcon::Project: {
            ImVec2 tabA = IconPoint(center, -0.58f, -0.38f, s);
            ImVec2 tabB = IconPoint(center, -0.12f, -0.16f, s);
            ImVec2 bodyA = IconPoint(center, -0.58f, -0.16f, s);
            ImVec2 bodyB = IconPoint(center, 0.58f, 0.54f, s);
            drawList->AddRect(tabA, tabB, color, s * 0.08f, 0, stroke);
            drawList->AddRect(bodyA, bodyB, color, s * 0.12f, 0, stroke);
            drawList->AddLine(IconPoint(center, -0.12f, -0.16f, s),
                              IconPoint(center, 0.58f, -0.16f, s), color, stroke);
            return;
        }
        case UiIcon::Shortcut: {
            drawList->AddCircle(IconPoint(center, -0.34f, 0.18f, s), s * 0.28f, color, 18, stroke);
            drawList->AddCircle(IconPoint(center, 0.34f, -0.18f, s), s * 0.28f, color, 18, stroke);
            drawList->AddLine(IconPoint(center, -0.12f, 0.06f, s),
                              IconPoint(center, 0.12f, -0.06f, s), color, stroke);
            return;
        }
        case UiIcon::Sync: {
            float r = s * 0.58f;
            drawList->PathClear();
            drawList->PathArcTo(center, r, -kPi * 0.85f, kPi * 0.18f, 18);
            drawList->PathStroke(color, false, stroke);
            drawList->PathClear();
            drawList->PathArcTo(center, r, kPi * 0.15f, kPi * 1.18f, 18);
            drawList->PathStroke(color, false, stroke);
            drawList->AddTriangleFilled(IconPoint(center, 0.60f, -0.10f, s),
                                        IconPoint(center, 0.30f, -0.24f, s),
                                        IconPoint(center, 0.42f, 0.08f, s), color);
            drawList->AddTriangleFilled(IconPoint(center, -0.60f, 0.10f, s),
                                        IconPoint(center, -0.30f, 0.24f, s),
                                        IconPoint(center, -0.42f, -0.08f, s), color);
            return;
        }
        case UiIcon::Apply: {
            drawList->AddLine(IconPoint(center, -0.56f, 0.02f, s),
                              IconPoint(center, -0.16f, 0.42f, s), color, stroke * 1.15f);
            drawList->AddLine(IconPoint(center, -0.16f, 0.42f, s),
                              IconPoint(center, 0.58f, -0.44f, s), color, stroke * 1.15f);
            return;
        }
        case UiIcon::Calendar: {
            ImVec2 a = IconPoint(center, -0.58f, -0.48f, s);
            ImVec2 b = IconPoint(center, 0.58f, 0.58f, s);
            drawList->AddRect(a, b, color, s * 0.12f, 0, stroke);
            drawList->AddLine(IconPoint(center, -0.58f, -0.18f, s),
                              IconPoint(center, 0.58f, -0.18f, s), color, stroke);
            drawList->AddLine(IconPoint(center, -0.28f, -0.62f, s),
                              IconPoint(center, -0.28f, -0.34f, s), color, stroke);
            drawList->AddLine(IconPoint(center, 0.28f, -0.62f, s),
                              IconPoint(center, 0.28f, -0.34f, s), color, stroke);
            drawList->AddCircleFilled(IconPoint(center, -0.22f, 0.08f, s), s * 0.06f, color, 8);
            drawList->AddCircleFilled(IconPoint(center, 0.12f, 0.08f, s), s * 0.06f, color, 8);
            drawList->AddCircleFilled(IconPoint(center, -0.22f, 0.36f, s), s * 0.06f, color, 8);
            drawList->AddCircleFilled(IconPoint(center, 0.12f, 0.36f, s), s * 0.06f, color, 8);
            return;
        }
        case UiIcon::Merge: {
            ImVec2 left = IconPoint(center, -0.58f, -0.34f, s);
            ImVec2 mid = IconPoint(center, 0.02f, 0.0f, s);
            ImVec2 right = IconPoint(center, 0.58f, 0.0f, s);
            drawList->AddLine(left, mid, color, stroke);
            drawList->AddLine(IconPoint(center, -0.58f, 0.34f, s), mid, color, stroke);
            drawList->AddLine(mid, right, color, stroke);
            drawList->AddTriangleFilled(right,
                                        IconPoint(center, 0.36f, -0.18f, s),
                                        IconPoint(center, 0.36f, 0.18f, s), color);
            drawList->AddCircleFilled(left, s * 0.10f, color, 10);
            drawList->AddCircleFilled(IconPoint(center, -0.58f, 0.34f, s), s * 0.10f, color, 10);
            return;
        }
        case UiIcon::Info: {
            drawList->AddCircle(center, s * 0.62f, color, 20, stroke);
            ImVec2 dot = IconPoint(center, 0.0f, -0.28f, s);
            drawList->AddCircleFilled(dot, s * 0.10f, color, 12);
            ImVec2 stemA = IconPoint(center, 0.0f, -0.08f, s);
            ImVec2 stemB = IconPoint(center, 0.0f, 0.38f, s);
            drawList->AddLine(stemA, stemB, color, stroke);
            return;
        }
        case UiIcon::SpiritGood: {
            drawList->AddCircle(IconPoint(center, 0.0f, -0.72f, s), s * 0.32f, color, 20, stroke);
            drawList->AddCircle(center, s * 0.48f, color, 20, stroke);
            drawList->AddCircleFilled(IconPoint(center, -0.18f, -0.10f, s), s * 0.07f, color, 10);
            drawList->AddCircleFilled(IconPoint(center, 0.18f, -0.10f, s), s * 0.07f, color, 10);
            drawList->PathClear();
            drawList->PathArcTo(IconPoint(center, 0.0f, 0.05f, s), s * 0.28f, 0.20f * kPi, 0.80f * kPi, 12);
            drawList->PathStroke(color, false, stroke);
            return;
        }
        case UiIcon::SpiritEvil: {
            ImVec2 leftHornA = IconPoint(center, -0.34f, -0.42f, s);
            ImVec2 leftHornB = IconPoint(center, -0.54f, -0.84f, s);
            ImVec2 leftHornC = IconPoint(center, -0.12f, -0.52f, s);
            ImVec2 rightHornA = IconPoint(center, 0.34f, -0.42f, s);
            ImVec2 rightHornB = IconPoint(center, 0.54f, -0.84f, s);
            ImVec2 rightHornC = IconPoint(center, 0.12f, -0.52f, s);
            drawList->AddTriangle(leftHornA, leftHornB, leftHornC, color, stroke);
            drawList->AddTriangle(rightHornA, rightHornB, rightHornC, color, stroke);
            drawList->AddCircle(center, s * 0.50f, color, 20, stroke);
            drawList->AddLine(IconPoint(center, -0.30f, -0.14f, s), IconPoint(center, -0.06f, -0.02f, s), color, stroke);
            drawList->AddLine(IconPoint(center, 0.30f, -0.14f, s), IconPoint(center, 0.06f, -0.02f, s), color, stroke);
            drawList->AddLine(IconPoint(center, -0.20f, 0.30f, s), IconPoint(center, 0.20f, 0.30f, s), color, stroke);
            return;
        }
        case UiIcon::Play: {
            ImVec2 a = IconPoint(center, -0.20f, -0.58f, s);
            ImVec2 b = IconPoint(center, 0.60f, 0.0f, s);
            ImVec2 c = IconPoint(center, -0.20f, 0.58f, s);
            drawList->AddTriangleFilled(a, b, c, color);
            drawList->AddTriangle(a, b, c, color, stroke * 0.8f);
            return;
        }
        case UiIcon::Stop: {
            ImVec2 a = IconPoint(center, -0.50f, -0.50f, s);
            ImVec2 b = IconPoint(center, 0.50f, 0.50f, s);
            drawList->AddRectFilled(a, b, color, s * 0.10f);
            drawList->AddRect(a, b, color, s * 0.10f, 0, stroke);
            return;
        }
        case UiIcon::Previous: {
            ImVec2 a1 = IconPoint(center, 0.55f, -0.50f, s);
            ImVec2 b1 = IconPoint(center, -0.05f, 0.0f, s);
            ImVec2 c1 = IconPoint(center, 0.55f, 0.50f, s);
            ImVec2 a2 = IconPoint(center, -0.05f, -0.50f, s);
            ImVec2 b2 = IconPoint(center, -0.65f, 0.0f, s);
            ImVec2 c2 = IconPoint(center, -0.05f, 0.50f, s);
            drawList->AddTriangleFilled(a1, b1, c1, color);
            drawList->AddTriangleFilled(a2, b2, c2, color);
            drawList->AddTriangle(a1, b1, c1, color, stroke * 0.8f);
            drawList->AddTriangle(a2, b2, c2, color, stroke * 0.8f);
            return;
        }
        case UiIcon::Next: {
            ImVec2 a1 = IconPoint(center, -0.55f, -0.50f, s);
            ImVec2 b1 = IconPoint(center, 0.05f, 0.0f, s);
            ImVec2 c1 = IconPoint(center, -0.55f, 0.50f, s);
            ImVec2 a2 = IconPoint(center, 0.05f, -0.50f, s);
            ImVec2 b2 = IconPoint(center, 0.65f, 0.0f, s);
            ImVec2 c2 = IconPoint(center, 0.05f, 0.50f, s);
            drawList->AddTriangleFilled(a1, b1, c1, color);
            drawList->AddTriangleFilled(a2, b2, c2, color);
            drawList->AddTriangle(a1, b1, c1, color, stroke * 0.8f);
            drawList->AddTriangle(a2, b2, c2, color, stroke * 0.8f);
            return;
        }
        case UiIcon::Plus: {
            drawList->AddLine(IconPoint(center, -0.55f, 0.0f, s),
                              IconPoint(center, 0.55f, 0.0f, s), color, stroke);
            drawList->AddLine(IconPoint(center, 0.0f, -0.55f, s),
                              IconPoint(center, 0.0f, 0.55f, s), color, stroke);
            return;
        }
        case UiIcon::Close: {
            drawList->AddLine(IconPoint(center, -0.48f, -0.48f, s),
                              IconPoint(center, 0.48f, 0.48f, s), color, stroke);
            drawList->AddLine(IconPoint(center, 0.48f, -0.48f, s),
                              IconPoint(center, -0.48f, 0.48f, s), color, stroke);
            return;
        }
        case UiIcon::Copy: {
            ImVec2 backA = IconPoint(center, -0.46f, -0.58f, s);
            ImVec2 backB = IconPoint(center, 0.32f, 0.20f, s);
            ImVec2 frontA = IconPoint(center, -0.22f, -0.30f, s);
            ImVec2 frontB = IconPoint(center, 0.56f, 0.58f, s);
            drawList->AddRect(backA, backB, color, s * 0.10f, 0, stroke);
            drawList->AddRectFilled(frontA, frontB, ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), s * 0.10f);
            drawList->AddRect(frontA, frontB, color, s * 0.10f, 0, stroke);
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
    base.w = std::max(base.w, 0.55f);
    border.w = std::max(border.w, 0.18f);

    if (filled) {
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(base), 24);
    }
    if (border.w > 0.01f) {
        drawList->AddCircle(center, radius, ImGui::GetColorU32(border), 24, 1.0f);
    }

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
    ImVec4 bg = base;
    if (active) bg = down;
    else if (hovered) bg = hover;
    bg.w = std::max(bg.w, 0.55f);
    border.w = std::max(border.w, (hovered || selected) ? 0.20f : 0.0f);

    drawList->AddRectFilled(pMin, pMax, ImGui::GetColorU32(bg), rounding);
    drawList->AddRect(pMin, pMax, ImGui::GetColorU32(border), rounding, 0, 1.0f);

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

static bool DrawEmptyStateGui(const char* id, UiIcon icon, const char* title, const char* body,
                              UiIcon actionIcon = UiIcon::Plus, const char* actionTooltip = nullptr,
                              bool actionEnabled = true) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    ImVec4 bg = BlendColor(style.Colors[ImGuiCol_WindowBg], style.Colors[ImGuiCol_FrameBg], 0.55f);
    ImVec4 border = BlendColor(style.Colors[ImGuiCol_Border], accent, 0.20f);
    bg.w = std::max(bg.w, 0.72f);
    border.w = std::max(border.w, 0.20f);

    const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float frame = ImGui::GetFrameHeight();
    const float pad = style.FramePadding.x * 2.0f + 8.0f;
    const float iconRadius = std::max(15.0f, frame * 0.55f);
    const float textWidth = std::max(80.0f, width - iconRadius * 2.0f - pad * 4.0f -
                                           (actionTooltip ? frame + style.ItemSpacing.x : 0.0f));
    const ImVec2 titleSize = ImGui::CalcTextSize(title ? title : "");
    const ImVec2 bodySize = ImGui::CalcTextSize(body ? body : "", nullptr, false, textWidth);
    const float height = std::max(frame * 2.4f, pad * 2.0f + titleSize.y + bodySize.y + 3.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), CardRounding());
    drawList->AddRect(p0, p1, ImGui::GetColorU32(border), CardRounding(), 0, 1.0f);
    DrawIconBadge(drawList, icon, ImVec2(p0.x + pad + iconRadius, p0.y + height * 0.5f),
                  iconRadius, accent, true);

    const float textX = p0.x + pad + iconRadius * 2.0f + style.ItemSpacing.x;
    const float textY = p0.y + std::max(pad * 0.65f, (height - titleSize.y - bodySize.y - 3.0f) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(textX, textY));
    ImGui::TextUnformatted(title ? title : "");
    if (body && body[0] != '\0') {
        ImGui::SetCursorScreenPos(ImVec2(textX, textY + titleSize.y + 3.0f));
        ImGui::PushTextWrapPos(textX + textWidth);
        ImGui::TextDisabled("%s", body);
        ImGui::PopTextWrapPos();
    }

    bool pressed = false;
    if (actionTooltip && actionTooltip[0] != '\0') {
        ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - frame, p0.y + (height - frame) * 0.5f));
        pressed = IconButtonGui("empty_action", actionIcon, actionTooltip, accent, frame,
                                actionEnabled, false);
    }
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(width, height + style.ItemSpacing.y));
    ImGui::PopID();
    return pressed;
}

static void DrawStatusPillGui(const char* id, const char* label, const ImVec4& accent,
                              const char* tooltip = nullptr, bool filled = false) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* safeLabel = label ? label : "";
    const ImVec2 textSize = ImGui::CalcTextSize(safeLabel);
    const float padX = std::max(6.0f, style.FramePadding.x * 1.35f);
    const float height = std::max(ImGui::GetTextLineHeight() + 4.0f, ImGui::GetFrameHeight() * 0.78f);
    const ImVec2 size(textSize.x + padX * 2.0f, height);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton("##status_pill", size);

    ImVec4 bg = filled
        ? BlendColor(style.Colors[ImGuiCol_FrameBg], accent, 0.58f)
        : BlendColor(style.Colors[ImGuiCol_FrameBg], accent, 0.22f);
    ImVec4 border = BlendColor(style.Colors[ImGuiCol_Border], accent, filled ? 0.45f : 0.28f);
    ImVec4 text = filled
        ? BlendColor(style.Colors[ImGuiCol_Text], accent, 0.28f)
        : BlendColor(style.Colors[ImGuiCol_Text], accent, 0.34f);
    bg.w = filled ? std::max(bg.w, 0.62f) : std::max(bg.w, 0.42f);
    border.w = filled ? std::max(border.w, 0.34f) : std::max(border.w, 0.22f);
    text.w = 1.0f;

    const float rounding = height * 0.45f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), rounding);
    drawList->AddRect(p0, p1, ImGui::GetColorU32(border), rounding, 0, 1.0f);
    drawList->AddText(ImVec2(p0.x + padX, p0.y + (height - textSize.y) * 0.5f),
                      ImGui::GetColorU32(text), safeLabel);
    if (tooltip && tooltip[0] != '\0' && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
}

static ImVec4 ActiveRowTintGui(float strength = 0.18f) {
    ImVec4 tint = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    tint.w = std::clamp(tint.w * strength, 0.10f, 0.26f);
    return tint;
}

static ImVec4 FocusRowTintGui(float strength = 0.12f) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 tint = BlendColor(style.Colors[ImGuiCol_FrameBg], style.Colors[ImGuiCol_CheckMark], 0.28f);
    tint.w = std::clamp(tint.w * strength, 0.06f, 0.18f);
    return tint;
}

static void ApplyTableRowStateTintGui(bool active, bool focus = false) {
    if (active) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ActiveRowTintGui()));
    } else if (focus) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(FocusRowTintGui()));
    }
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
    ImVec4 bg = base;
    if (active) bg = down;
    else if (hovered) bg = hover;
    bg.w = std::max(bg.w, 0.55f);
    border.w = std::max(border.w, (hovered || selected) ? 0.20f : 0.0f);

    drawList->AddRectFilled(pMin, pMax, ImGui::GetColorU32(bg), rounding);
    drawList->AddRect(pMin, pMax, ImGui::GetColorU32(border), rounding, 0, 1.0f);

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
    ImVec4 badgeAccent = selected ? accent : style.Colors[ImGuiCol_TextDisabled];
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
