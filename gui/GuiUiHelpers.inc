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
    const auto idx = static_cast<size_t>(id);
    if (idx >= settings.backgrounds.size()) return;
    const std::string& relPath = settings.backgrounds[idx];
    if (relPath.empty()) return;
    auto resolved = ResolveIconPath(relPath, storageDir);
    if (!resolved) return;
    if (const auto* tex = GetIconTexture(*resolved)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
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

void HandleBorderlessDrag(GLFWwindow* window, const UiSettings& settings) {
    if (!window) return;
    if (settings.windowDecorated || settings.windowFullscreen) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyAlt) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;
    ImVec2 delta = io.MouseDelta;
    if (delta.x == 0.0f && delta.y == 0.0f) return;
    int wx = 0, wy = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwSetWindowPos(window, wx + static_cast<int>(delta.x), wy + static_cast<int>(delta.y));
}

void ResetUiSettings(UiSettings& settings, ImGuiStyle& style, ImGuiIO& io) {
    settings = UiSettings();
    ApplyUiTheme(settings.theme, style);
    ApplyUiSettings(settings, style, io);
}
