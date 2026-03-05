void EnsureWindowVisible(float padding = 12.0f, ImVec2 minSize = ImVec2(240.0f, 160.0f)) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;
    if (viewport->WorkSize.x <= 0.0f || viewport->WorkSize.y <= 0.0f) return;
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (size.x < minSize.x || size.y < minSize.y) {
        ImVec2 target(std::max(size.x, minSize.x), std::max(size.y, minSize.y));
        ImGui::SetWindowSize(target, ImGuiCond_Always);
        size = target;
    }
    ImVec2 min = viewport->WorkPos;
    ImVec2 max(min.x + viewport->WorkSize.x, min.y + viewport->WorkSize.y);
    const bool offscreen =
        pos.x + size.x < min.x + padding ||
        pos.y + size.y < min.y + padding ||
        pos.x > max.x - padding ||
        pos.y > max.y - padding;
    if (!offscreen) return;
    if (size.x >= viewport->WorkSize.x) {
        pos.x = min.x;
    } else {
        pos.x = std::clamp(pos.x, min.x + padding, max.x - size.x - padding);
    }
    if (size.y >= viewport->WorkSize.y) {
        pos.y = min.y;
    } else {
        pos.y = std::clamp(pos.y, min.y + padding, max.y - size.y - padding);
    }
    ImGui::SetWindowPos(pos, ImGuiCond_Always);
}

void DrawWindowBackground(const UiSettings& settings, UiWindowId id, const std::filesystem::path& storageDir) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    // Soft vertical gradient keeps depth without extra card decoration.
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 top = style.Colors[ImGuiCol_WindowBg];
    ImVec4 child = style.Colors[ImGuiCol_ChildBg];
    const float baseAlpha = std::clamp(settings.alpha, 0.0f, 1.0f);
    const float mix = (settings.theme == 1) ? 0.20f : 0.45f; // light theme gets subtler gradient
    ImVec4 bottom(
        top.x + (child.x - top.x) * mix,
        top.y + (child.y - top.y) * mix,
        top.z + (child.z - top.z) * mix,
        top.w + (child.w - top.w) * mix
    );
    top.w = std::clamp(top.w * baseAlpha, 0.20f, 1.0f);
    bottom.w = std::clamp(bottom.w * baseAlpha, 0.20f, 1.0f);
    drawList->AddRectFilledMultiColor(
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        ImGui::GetColorU32(top),
        ImGui::GetColorU32(top),
        ImGui::GetColorU32(bottom),
        ImGui::GetColorU32(bottom));

    const auto idx = static_cast<size_t>(id);
    if (idx >= settings.backgrounds.size()) return;
    const std::string& relPath = settings.backgrounds[idx];
    if (relPath.empty()) return;
    auto resolved = ResolveIconPath(relPath, storageDir);
    if (!resolved) return;
    if (const auto* tex = GetIconTexture(*resolved)) {
        ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, settings.backgroundAlpha));
        if (!settings.backgroundTiled) {
            drawList->AddImage((ImTextureID)(intptr_t)tex->id, pos, ImVec2(pos.x + size.x, pos.y + size.y),
                               ImVec2(0, 0), ImVec2(1, 1), tint);
            return;
        }
        const float scale = std::max(0.05f, settings.backgroundTileScale);
        float tileW = std::max(8.0f, static_cast<float>(tex->width) * scale);
        float tileH = std::max(8.0f, static_cast<float>(tex->height) * scale);
        ImVec2 end(pos.x + size.x, pos.y + size.y);
        for (float y = pos.y; y < end.y; y += tileH) {
            float y2 = std::min(y + tileH, end.y);
            float v1 = (y2 - y) / tileH;
            for (float x = pos.x; x < end.x; x += tileW) {
                float x2 = std::min(x + tileW, end.x);
                float u1 = (x2 - x) / tileW;
                drawList->AddImage((ImTextureID)(intptr_t)tex->id, ImVec2(x, y), ImVec2(x2, y2),
                                   ImVec2(0, 0), ImVec2(u1, v1), tint);
            }
        }
    }
}

void ApplyWindowDecorations(GLFWwindow* window, bool decorated, bool& lastDecorated) {
    if (!window) return;
    if (decorated == lastDecorated) return;
    glfwSetWindowAttrib(window, GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    lastDecorated = decorated;
}

GLFWmonitor* FindMonitorForWindow(GLFWwindow* window) {
    if (!window) return glfwGetPrimaryMonitor();
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (!monitors || count == 0) return glfwGetPrimaryMonitor();
    const int cx = wx + ww / 2;
    const int cy = wy + wh / 2;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        if (cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
            return monitors[i];
        }
    }
    return monitors[0];
}

void ApplyWindowMode(GLFWwindow* window, GuiState& state) {
    if (!window) return;
    if (state.ui.windowFullscreen == state.windowFullscreenLast) return;
    if (state.ui.windowFullscreen) {
        glfwGetWindowPos(window, &state.windowedX, &state.windowedY);
        glfwGetWindowSize(window, &state.windowedW, &state.windowedH);
        GLFWmonitor* monitor = FindMonitorForWindow(window);
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor && mode) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    } else {
        int restoreW = state.windowedW > 0 ? state.windowedW : 1280;
        int restoreH = state.windowedH > 0 ? state.windowedH : 720;
        glfwSetWindowMonitor(window, nullptr, state.windowedX, state.windowedY, restoreW, restoreH, 0);
    }
    state.windowFullscreenLast = state.ui.windowFullscreen;
}

void DragWindow(GLFWwindow* window, const ImVec2& delta) {
    if (!window) return;
    if (delta.x == 0.0f && delta.y == 0.0f) return;
    int wx = 0, wy = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwSetWindowPos(window, wx + static_cast<int>(delta.x), wy + static_cast<int>(delta.y));
}

void HandleBorderlessDrag(GLFWwindow* window, const UiSettings& settings) {
    if (!window) return;
    if (settings.windowDecorated || settings.windowFullscreen) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyAlt) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;
    ImVec2 delta = io.MouseDelta;
    if (delta.x == 0.0f && delta.y == 0.0f) return;
    DragWindow(window, delta);
}


static bool IsProfileAccessUnlocked(const GuiState& state, const std::string& id) {
    if (id.empty()) return false;
    if (state.unlockedProfiles.count(id) > 0) return true;
    auto it = state.trustedProfiles.find(id);
    if (it == state.trustedProfiles.end()) return false;
    return it->second > NowSeconds();
}

void ResetUiSettings(UiSettings& settings, ImGuiStyle& style, ImGuiIO& io) {
    std::string lastProfileId = std::move(settings.lastProfileId);
    std::string unlocked = std::move(settings.profileUnlocked);
    std::string trusted = std::move(settings.profileTrusted);
    std::string recent = std::move(settings.profileRecent);
    settings = UiSettings();
    settings.lastProfileId = std::move(lastProfileId);
    settings.profileUnlocked = std::move(unlocked);
    settings.profileTrusted = std::move(trusted);
    settings.profileRecent = std::move(recent);
    ApplyUiTheme(settings.theme, style);
    ApplyUiSettings(settings, style, io);
}

ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

struct UiLayoutTokens {
    float cardPaddingX = 12.0f;
    float cardPaddingY = 12.0f;
    float cardSpacingY = 12.0f;
    float sectionSpacingY = 18.0f;
    ImVec2 tableCellPadding = ImVec2(12.0f, 6.0f);
};

const UiLayoutTokens& LayoutTokens() {
    static UiLayoutTokens tokens;
    return tokens;
}

ImVec2 CardPadding() {
    const auto& tokens = LayoutTokens();
    return ImVec2(tokens.cardPaddingX, tokens.cardPaddingY);
}

float CardSpacing() {
    return LayoutTokens().cardSpacingY;
}

float SectionSpacing() {
    return LayoutTokens().sectionSpacingY;
}

ImVec2 TableCellPadding() {
    return LayoutTokens().tableCellPadding;
}

ImVec4 CardBackgroundColor() {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 base = style.Colors[ImGuiCol_WindowBg];
    ImVec4 lift = style.Colors[ImGuiCol_FrameBg];
    ImVec4 out = BlendColor(base, lift, 0.35f);
    out.w = std::max(out.w, 0.72f);
    return out;
}

ImVec4 CardBorderColor() {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 border = style.Colors[ImGuiCol_Border];
    border.w = std::max(border.w, 0.18f);
    return border;
}

float CardRounding() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return std::max(8.0f, style.WindowRounding + 2.0f);
}

void DrawCardHeader(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    ImVec4 base = style.Colors[ImGuiCol_Text];
    ImVec4 title = BlendColor(base, accent, 0.25f);
    title.w = base.w;
    ImVec4 sep = accent;
    sep.w = std::clamp(sep.w * 0.55f, 0.20f, 0.45f);
    ImGui::TextColored(title, "%s", label);
    ImGui::PushStyleColor(ImGuiCol_Separator, sep);
    ImGui::Separator();
    ImGui::PopStyleColor();
}

bool BeginCard(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), ImGuiChildFlags flags = ImGuiChildFlags_AutoResizeY) {
    ImVec4 bg = CardBackgroundColor();
    ImVec4 border = CardBorderColor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, CardRounding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, CardPadding());
    const bool open = ImGui::BeginChild(id, size, flags | ImGuiChildFlags_Border,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    return open;
}


void DrawModalShadow(float padding = 12.0f, float alpha = 0.14f) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList(viewport);
    if (!drawList) return;
    ImVec2 p0 = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
    const float rounding = std::max(ImGui::GetStyle().PopupRounding, 6.0f);
    ImU32 shadeOuter = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha));
    ImU32 shadeInner = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.6f));
    drawList->AddRect(
        ImVec2(p0.x - padding * 1.6f, p0.y - padding * 1.6f),
        ImVec2(p1.x + padding * 1.6f, p1.y + padding * 1.6f),
        shadeOuter,
        rounding + padding * 0.6f,
        0,
        padding * 0.45f);
    drawList->AddRect(
        ImVec2(p0.x - padding, p0.y - padding),
        ImVec2(p1.x + padding, p1.y + padding),
        shadeInner,
        rounding + padding * 0.35f,
        0,
        padding * 0.35f);
}



void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
