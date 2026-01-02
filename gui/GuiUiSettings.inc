std::string UiSettingsPath(const std::filesystem::path& storageDir) {
    return (storageDir / "meta" / "ui.ini").string();
}

std::filesystem::path UiPresetsDir(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "ui-presets";
}

std::string SanitizePresetName(std::string name) {
    name = TrimStringGui(name);
    std::string out;
    out.reserve(name.size());
    for (unsigned char ch : name) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            out.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "preset";
    }
    return out;
}

std::filesystem::path UiPresetPath(const std::filesystem::path& storageDir, const std::string& name) {
    auto dir = UiPresetsDir(storageDir);
    return dir / (SanitizePresetName(name) + ".ini");
}

std::vector<std::string> LoadUiPresetNames(const std::filesystem::path& storageDir) {
    std::vector<std::string> out;
    std::error_code ec;
    auto dir = UiPresetsDir(storageDir);
    if (!std::filesystem::exists(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        out.push_back(entry.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string NormalizeFloatList(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isdigit(ch) || ch == '-' || ch == '+') out.push_back(static_cast<char>(ch));
        else if (ch == '.' || ch == ',') out.push_back('.');
        else if (ch == ' ' || ch == '\t') out.push_back(' ');
    }
    return out;
}

float ParseFloat(const std::string& value, float fallback = 0.0f) {
    std::stringstream ss(NormalizeFloatList(value));
    ss.imbue(std::locale::classic());
    float out = 0.0f;
    if (ss >> out) return out;
    return fallback;
}

float ClampFinite(float value, float minValue, float maxValue, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minValue, maxValue);
}

int ParseInt(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(TrimStringGui(value));
    } catch (...) {
        return fallback;
    }
}

bool ParseBool(const std::string& value, bool fallback = false) {
    std::string v = TrimStringGui(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (v == "1" || v == "true" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "no") return false;
    return fallback;
}

ImVec2 ParseVec2(const std::string& value, const ImVec2& fallback) {
    std::stringstream ss(NormalizeFloatList(value));
    ss.imbue(std::locale::classic());
    float x = 0.0f;
    float y = 0.0f;
    if (ss >> x >> y) return ImVec2(x, y);
    return fallback;
}

ImVec4 ParseVec4(const std::string& value, const ImVec4& fallback) {
    std::stringstream ss(NormalizeFloatList(value));
    ss.imbue(std::locale::classic());
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    if (ss >> x >> y >> z >> w) return ImVec4(x, y, z, w);
    return fallback;
}

std::string Vec4ToString(const ImVec4& v) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(3)
       << v.x << " " << v.y << " " << v.z << " " << v.w;
    return ss.str();
}

UiSettings LoadUiSettingsFromPath(const std::filesystem::path& path, const ImGuiStyle& style) {
    UiSettings settings;
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        settings.colors[i] = style.Colors[entries[i].col];
    }
    std::ifstream in(path);
    if (!in) return settings;
    in.imbue(std::locale::classic());
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        auto t = TrimStringGui(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = TrimStringGui(t.substr(0, eq));
        std::string value = TrimStringGui(t.substr(eq + 1));
        if (section == "style") {
            if (key == "theme") settings.theme = ParseInt(value, settings.theme);
            else if (key == "fontScale") settings.fontScale = ParseFloat(value, settings.fontScale);
            else if (key == "alpha") settings.alpha = ParseFloat(value, settings.alpha);
            else if (key == "windowRounding") settings.windowRounding = ParseFloat(value, settings.windowRounding);
            else if (key == "frameRounding") settings.frameRounding = ParseFloat(value, settings.frameRounding);
            else if (key == "scrollbarRounding") settings.scrollbarRounding = ParseFloat(value, settings.scrollbarRounding);
            else if (key == "grabRounding") settings.grabRounding = ParseFloat(value, settings.grabRounding);
            else if (key == "windowPadding") settings.windowPadding = ParseVec2(value, settings.windowPadding);
            else if (key == "framePadding") settings.framePadding = ParseVec2(value, settings.framePadding);
            else if (key == "itemSpacing") settings.itemSpacing = ParseVec2(value, settings.itemSpacing);
            else if (key == "customColors") settings.customColors = ParseBool(value, settings.customColors);
            else if (key == "backgroundAlpha") settings.backgroundAlpha = ParseFloat(value, settings.backgroundAlpha);
            else if (key == "backgroundTiled") settings.backgroundTiled = ParseBool(value, settings.backgroundTiled);
            else if (key == "backgroundTileScale") settings.backgroundTileScale = ParseFloat(value, settings.backgroundTileScale);
            else if (key == "windowDecorated") settings.windowDecorated = ParseBool(value, settings.windowDecorated);
            else if (key == "windowFullscreen") settings.windowFullscreen = ParseBool(value, settings.windowFullscreen);
            else if (key == "profileTab") settings.profileTab = ParseInt(value, settings.profileTab);
        } else if (section == "profile") {
            if (key == "lastProfileId") settings.lastProfileId = value;
            else if (key == "viewMode") settings.profileViewMode = ParseInt(value, settings.profileViewMode);
        } else if (section == "logs") {
            if (key == "filter") settings.logFilter = value;
            else if (key == "source") settings.logSourceFilter = value;
            else if (key == "showInfo") settings.logShowInfo = ParseBool(value, settings.logShowInfo);
            else if (key == "showWarning") settings.logShowWarning = ParseBool(value, settings.logShowWarning);
            else if (key == "showError") settings.logShowError = ParseBool(value, settings.logShowError);
            else if (key == "autoScroll") settings.logAutoScroll = ParseBool(value, settings.logAutoScroll);
            else if (key == "compactView") settings.logCompactView = ParseBool(value, settings.logCompactView);
        } else if (section == "admin_stats") {
            if (key == "filter") settings.adminStatsFilter = value;
            else if (key == "includeArchived") settings.adminStatsIncludeArchived = ParseBool(value, settings.adminStatsIncludeArchived);
            else if (key == "rankFilter") settings.adminStatsRankFilter = ParseInt(value, settings.adminStatsRankFilter);
            else if (key == "autoRefresh") settings.adminStatsAutoRefresh = ParseBool(value, settings.adminStatsAutoRefresh);
            else if (key == "refreshSeconds") settings.adminStatsRefreshSeconds = ParseInt(value, settings.adminStatsRefreshSeconds);
            else if (key == "inactivityDays") settings.inactivityThresholdDays = ParseInt(value, settings.inactivityThresholdDays);
        } else if (section == "colors") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (key == entries[i].name) {
                    settings.colors[i] = ParseVec4(value, settings.colors[i]);
                    settings.customColors = true;
                    break;
                }
            }
        } else if (section == "backgrounds") {
                const auto& windows = UiWindows();
                for (size_t i = 0; i < windows.size(); ++i) {
                if (key == windows[i].label) {
                    settings.backgrounds[i] = value;
                    break;
                }
            }
        } else if (section == "view3d") {
            if (key == "modelPath") settings.modelPath = value;
            else if (key == "yaw") settings.modelYaw = ParseFloat(value, settings.modelYaw);
            else if (key == "pitch") settings.modelPitch = ParseFloat(value, settings.modelPitch);
            else if (key == "zoom") settings.modelZoom = ParseFloat(value, settings.modelZoom);
            else if (key == "autoRotate") settings.modelAutoRotate = ParseBool(value, settings.modelAutoRotate);
            else if (key == "autoSpeed") settings.modelAutoSpeed = ParseFloat(value, settings.modelAutoSpeed);
            else if (key == "color") settings.modelColor = ParseVec4(value, settings.modelColor);
        }
    }
    settings.theme = std::clamp(settings.theme, 0, 2);
    settings.fontScale = ClampFinite(settings.fontScale, 0.6f, 2.0f, 1.0f);
    settings.alpha = ClampFinite(settings.alpha, 0.4f, 1.0f, 1.0f);
    settings.windowRounding = ClampFinite(settings.windowRounding, 0.0f, 24.0f, style.WindowRounding);
    settings.frameRounding = ClampFinite(settings.frameRounding, 0.0f, 24.0f, style.FrameRounding);
    settings.scrollbarRounding = ClampFinite(settings.scrollbarRounding, 0.0f, 24.0f, style.ScrollbarRounding);
    settings.grabRounding = ClampFinite(settings.grabRounding, 0.0f, 24.0f, style.GrabRounding);
    settings.backgroundAlpha = ClampFinite(settings.backgroundAlpha, 0.0f, 1.0f, 0.25f);
    settings.backgroundTileScale = ClampFinite(settings.backgroundTileScale, 0.25f, 4.0f, 1.0f);
    settings.profileTab = std::clamp(settings.profileTab, 0, 4);
    settings.profileViewMode = std::clamp(settings.profileViewMode, 0, 1);
    settings.adminStatsRankFilter = std::max(0, settings.adminStatsRankFilter);
    settings.adminStatsRefreshSeconds = std::clamp(settings.adminStatsRefreshSeconds, 5, 120);
    settings.inactivityThresholdDays = std::clamp(settings.inactivityThresholdDays, 1, 365);
    settings.windowPadding.x = ClampFinite(settings.windowPadding.x, 0.0f, 32.0f, style.WindowPadding.x);
    settings.windowPadding.y = ClampFinite(settings.windowPadding.y, 0.0f, 32.0f, style.WindowPadding.y);
    settings.framePadding.x = ClampFinite(settings.framePadding.x, 0.0f, 24.0f, style.FramePadding.x);
    settings.framePadding.y = ClampFinite(settings.framePadding.y, 0.0f, 24.0f, style.FramePadding.y);
    settings.itemSpacing.x = ClampFinite(settings.itemSpacing.x, 0.0f, 32.0f, style.ItemSpacing.x);
    settings.itemSpacing.y = ClampFinite(settings.itemSpacing.y, 0.0f, 32.0f, style.ItemSpacing.y);
    settings.modelYaw = ClampFinite(settings.modelYaw, -50.0f, 50.0f, 0.0f);
    settings.modelPitch = ClampFinite(settings.modelPitch, -5.0f, 5.0f, 0.0f);
    settings.modelZoom = ClampFinite(settings.modelZoom, 0.3f, 3.0f, 1.0f);
    settings.modelAutoSpeed = ClampFinite(settings.modelAutoSpeed, 0.0f, 5.0f, 0.6f);
    settings.modelColor.x = ClampFinite(settings.modelColor.x, 0.0f, 1.0f, settings.modelColor.x);
    settings.modelColor.y = ClampFinite(settings.modelColor.y, 0.0f, 1.0f, settings.modelColor.y);
    settings.modelColor.z = ClampFinite(settings.modelColor.z, 0.0f, 1.0f, settings.modelColor.z);
    settings.modelColor.w = ClampFinite(settings.modelColor.w, 0.0f, 1.0f, settings.modelColor.w);
    if (settings.customColors) {
        for (size_t i = 0; i < settings.colors.size(); ++i) {
            settings.colors[i].x = ClampFinite(settings.colors[i].x, 0.0f, 1.0f, style.Colors[entries[i].col].x);
            settings.colors[i].y = ClampFinite(settings.colors[i].y, 0.0f, 1.0f, style.Colors[entries[i].col].y);
            settings.colors[i].z = ClampFinite(settings.colors[i].z, 0.0f, 1.0f, style.Colors[entries[i].col].z);
            settings.colors[i].w = ClampFinite(settings.colors[i].w, 0.0f, 1.0f, style.Colors[entries[i].col].w);
        }
    }
    return settings;
}

UiSettings LoadUiSettings(const std::filesystem::path& storageDir, const ImGuiStyle& style) {
    return LoadUiSettingsFromPath(UiSettingsPath(storageDir), style);
}

void SaveUiSettingsToPath(const std::filesystem::path& path, const UiSettings& settings) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.imbue(std::locale::classic());
    out << "[style]\n";
    out << "theme=" << settings.theme << "\n";
    out << "fontScale=" << settings.fontScale << "\n";
    out << "alpha=" << settings.alpha << "\n";
    out << "windowRounding=" << settings.windowRounding << "\n";
    out << "frameRounding=" << settings.frameRounding << "\n";
    out << "scrollbarRounding=" << settings.scrollbarRounding << "\n";
    out << "grabRounding=" << settings.grabRounding << "\n";
    out << "windowPadding=" << settings.windowPadding.x << " " << settings.windowPadding.y << "\n";
    out << "framePadding=" << settings.framePadding.x << " " << settings.framePadding.y << "\n";
    out << "itemSpacing=" << settings.itemSpacing.x << " " << settings.itemSpacing.y << "\n";
    out << "customColors=" << (settings.customColors ? 1 : 0) << "\n";
    out << "backgroundAlpha=" << settings.backgroundAlpha << "\n\n";
    out << "backgroundTiled=" << (settings.backgroundTiled ? 1 : 0) << "\n";
    out << "backgroundTileScale=" << settings.backgroundTileScale << "\n\n";
    out << "windowDecorated=" << (settings.windowDecorated ? 1 : 0) << "\n\n";
    out << "windowFullscreen=" << (settings.windowFullscreen ? 1 : 0) << "\n\n";
    out << "profileTab=" << settings.profileTab << "\n\n";

    out << "[profile]\n";
    out << "lastProfileId=" << SanitizeLogLine(settings.lastProfileId) << "\n";
    out << "viewMode=" << settings.profileViewMode << "\n\n";

    out << "[logs]\n";
    out << "filter=" << SanitizeLogLine(settings.logFilter) << "\n";
    out << "source=" << SanitizeLogLine(settings.logSourceFilter) << "\n";
    out << "showInfo=" << (settings.logShowInfo ? 1 : 0) << "\n";
    out << "showWarning=" << (settings.logShowWarning ? 1 : 0) << "\n";
    out << "showError=" << (settings.logShowError ? 1 : 0) << "\n";
    out << "autoScroll=" << (settings.logAutoScroll ? 1 : 0) << "\n";
    out << "compactView=" << (settings.logCompactView ? 1 : 0) << "\n\n";

    out << "[admin_stats]\n";
    out << "filter=" << SanitizeLogLine(settings.adminStatsFilter) << "\n";
    out << "includeArchived=" << (settings.adminStatsIncludeArchived ? 1 : 0) << "\n";
    out << "rankFilter=" << settings.adminStatsRankFilter << "\n";
    out << "autoRefresh=" << (settings.adminStatsAutoRefresh ? 1 : 0) << "\n";
    out << "refreshSeconds=" << settings.adminStatsRefreshSeconds << "\n";
    out << "inactivityDays=" << settings.inactivityThresholdDays << "\n\n";

    out << "[colors]\n";
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        out << entries[i].name << "=" << Vec4ToString(settings.colors[i]) << "\n";
    }
    out << "\n[backgrounds]\n";
                const auto& windows = UiWindows();
                for (size_t i = 0; i < windows.size(); ++i) {
        out << windows[i].label << "=" << settings.backgrounds[i] << "\n";
    }
    out << "\n[view3d]\n";
    out << "modelPath=" << settings.modelPath << "\n";
    out << "yaw=" << settings.modelYaw << "\n";
    out << "pitch=" << settings.modelPitch << "\n";
    out << "zoom=" << settings.modelZoom << "\n";
    out << "autoRotate=" << (settings.modelAutoRotate ? 1 : 0) << "\n";
    out << "autoSpeed=" << settings.modelAutoSpeed << "\n";
    out << "color=" << Vec4ToString(settings.modelColor) << "\n";
}

void SaveUiSettings(const std::filesystem::path& storageDir, const UiSettings& settings) {
    SaveUiSettingsToPath(std::filesystem::path(UiSettingsPath(storageDir)), settings);
}

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}


struct ModelChoice {
    std::string label;
    std::string relativePath;
    std::filesystem::path absolutePath;
};

std::vector<ModelChoice> LoadModelChoices(const std::filesystem::path& storageDir) {
    std::vector<ModelChoice> out;
    std::error_code ec;
    auto modelDir = storageDir / "models";
    if (!std::filesystem::exists(modelDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(modelDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".obj" && ext != ".fbx") continue;
        const auto filename = entry.path().filename();
        auto rel = std::filesystem::path("models") / filename;
        ModelChoice choice;
        choice.label = filename.string();
        choice.relativePath = rel.generic_string();
        choice.absolutePath = entry.path();
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(), [](const ModelChoice& a, const ModelChoice& b) {
        return a.label < b.label;
    });
    return out;
}



void ApplyUiTheme(int theme, ImGuiStyle& style) {
    switch (theme) {
        case 1: ImGui::StyleColorsLight(&style); break;
        case 2: ImGui::StyleColorsClassic(&style); break;
        default: ImGui::StyleColorsDark(&style); break;
    }
}

void ApplyThemePreset(int preset, UiSettings& settings, ImGuiStyle& style) {
    switch (preset) {
        case 0: { // Graphite
            settings.theme = 0;
            ImGui::StyleColorsDark(&style);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.08f, 1.00f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.18f, 0.92f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.25f, 0.95f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.27f, 0.33f, 1.00f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.17f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.20f, 0.24f, 0.60f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.27f, 0.32f, 0.75f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.30f, 0.36f, 0.90f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.19f, 0.23f, 0.60f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.23f, 0.27f, 0.33f, 0.80f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.29f, 0.35f, 0.95f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.13f, 0.16f, 1.00f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.28f, 0.35f, 0.75f);
            style.Colors[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.20f, 0.26f, 1.00f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.20f, 0.24f, 0.35f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.20f, 0.24f, 0.35f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.24f, 0.28f, 0.34f, 0.65f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.31f, 0.38f, 0.85f);
            break;
        }
        case 1: { // Sapphire
            settings.theme = 0;
            ImGui::StyleColorsDark(&style);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.09f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.18f, 0.28f, 0.85f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.26f, 0.40f, 0.95f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.32f, 0.48f, 1.00f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.16f, 0.26f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.22f, 0.34f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.30f, 0.50f, 0.65f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.38f, 0.62f, 0.85f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.40f, 0.66f, 1.00f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.28f, 0.48f, 0.70f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.36f, 0.62f, 0.90f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.38f, 0.64f, 1.00f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.18f, 0.32f, 1.00f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.36f, 0.58f, 0.85f);
            style.Colors[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.28f, 0.48f, 1.00f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.26f, 0.38f, 0.45f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.26f, 0.38f, 0.45f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.24f, 0.34f, 0.48f, 0.70f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.28f, 0.40f, 0.58f, 0.90f);
            break;
        }
        case 2: { // Sand
            settings.theme = 1;
            ImGui::StyleColorsLight(&style);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.98f, 0.96f, 0.93f, 1.00f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.93f, 0.89f, 0.82f, 1.00f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.96f, 0.92f, 0.86f, 1.00f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.84f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.90f, 0.85f, 0.78f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.88f, 0.82f, 0.72f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.86f, 0.78f, 0.66f, 0.75f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.90f, 0.82f, 0.70f, 0.90f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.88f, 0.80f, 0.68f, 1.00f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.86f, 0.78f, 0.66f, 0.70f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.82f, 0.70f, 0.90f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.84f, 0.76f, 0.64f, 1.00f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.92f, 0.86f, 0.76f, 1.00f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.86f, 0.80f, 0.70f, 0.90f);
            style.Colors[ImGuiCol_TabSelected] = ImVec4(0.88f, 0.82f, 0.72f, 1.00f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.72f, 0.66f, 0.56f, 0.60f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.72f, 0.66f, 0.56f, 0.60f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.66f, 0.60f, 0.50f, 0.75f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.62f, 0.56f, 0.46f, 0.90f);
            break;
        }
        case 3: { // Techno Glass
            settings.theme = 0;
            ImGui::StyleColorsDark(&style);
            style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 1.00f, 1.00f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.70f, 1.00f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.94f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.09f, 0.13f, 0.40f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.09f, 0.14f, 0.94f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.40f, 0.46f, 0.58f, 0.35f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.46f, 0.58f, 0.30f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.52f, 0.60f, 0.74f, 0.55f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.62f, 0.70f, 0.88f, 0.80f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.16f, 0.22f, 0.55f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.24f, 0.33f, 0.75f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.28f, 0.39f, 0.88f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.08f, 0.12f, 0.90f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.12f, 0.18f, 0.96f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.14f, 0.19f, 0.26f, 0.55f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.27f, 0.38f, 0.75f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.31f, 0.44f, 0.90f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.14f, 0.19f, 0.27f, 0.45f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.26f, 0.38f, 0.70f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.31f, 0.46f, 0.88f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.11f, 0.17f, 0.90f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.29f, 0.43f, 0.80f);
            style.Colors[ImGuiCol_TabSelected] = ImVec4(0.15f, 0.22f, 0.34f, 0.95f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.67f, 0.56f, 1.00f, 0.95f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.60f, 0.50f, 0.95f, 0.80f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.72f, 0.62f, 1.00f, 1.00f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.60f, 0.50f, 0.95f, 0.35f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.72f, 0.62f, 1.00f, 0.55f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.78f, 0.68f, 1.00f, 0.80f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.68f, 0.56f, 1.00f, 0.85f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.62f, 0.70f, 0.98f, 1.00f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.62f, 0.56f, 0.98f, 1.00f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.78f, 0.70f, 1.00f, 1.00f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.14f, 0.22f, 0.65f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.10f, 0.12f, 0.18f, 0.35f);
            break;
        }
        default:
            return;
    }
    settings.customColors = true;
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        settings.colors[i] = style.Colors[entries[i].col];
    }
}

void ApplyUiSettings(const UiSettings& settings, ImGuiStyle& style, ImGuiIO& io) {
    io.FontGlobalScale = settings.fontScale;
    style.Alpha = settings.alpha;
    style.WindowRounding = settings.windowRounding;
    style.ChildRounding = settings.windowRounding;
    style.FrameRounding = settings.frameRounding;
    style.PopupRounding = settings.windowRounding;
    style.ScrollbarRounding = settings.scrollbarRounding;
    style.GrabRounding = settings.grabRounding;
    style.TabRounding = settings.frameRounding;
    style.WindowPadding = settings.windowPadding;
    style.FramePadding = settings.framePadding;
    style.ItemSpacing = settings.itemSpacing;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    if (settings.customColors) {
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            style.Colors[entries[i].col] = settings.colors[i];
        }
    }
    ImVec4 titleBg = style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_TitleBg] = titleBg;
    style.Colors[ImGuiCol_TitleBgActive] = titleBg;
    style.Colors[ImGuiCol_TitleBgCollapsed] = titleBg;
}

void ApplyUiFiltersToState(GuiState& state) {
    std::snprintf(state.filters.logs.data(), state.filters.logs.size(), "%s", state.ui.logFilter.c_str());
    std::snprintf(state.filters.logSource.data(), state.filters.logSource.size(), "%s", state.ui.logSourceFilter.c_str());
    state.logShowInfo = state.ui.logShowInfo;
    state.logShowWarning = state.ui.logShowWarning;
    state.logShowError = state.ui.logShowError;
    state.logAutoScroll = state.ui.logAutoScroll;
    state.logCompactView = state.ui.logCompactView;
    std::snprintf(state.filters.adminStats.data(), state.filters.adminStats.size(), "%s",
                  state.ui.adminStatsFilter.c_str());
    state.adminStatsIncludeArchived = state.ui.adminStatsIncludeArchived;
    state.adminStatsRankFilter = state.ui.adminStatsRankFilter;
    state.adminStatsAutoRefresh = state.ui.adminStatsAutoRefresh;
    state.adminStatsRefreshSeconds = state.ui.adminStatsRefreshSeconds;
    state.inactivityThresholdDays = state.ui.inactivityThresholdDays;
}

void SyncFiltersToUi(GuiState& state) {
    state.ui.logFilter = state.filters.logs.data();
    state.ui.logSourceFilter = state.filters.logSource.data();
    state.ui.logShowInfo = state.logShowInfo;
    state.ui.logShowWarning = state.logShowWarning;
    state.ui.logShowError = state.logShowError;
    state.ui.logAutoScroll = state.logAutoScroll;
    state.ui.logCompactView = state.logCompactView;
    state.ui.adminStatsFilter = state.filters.adminStats.data();
    state.ui.adminStatsIncludeArchived = state.adminStatsIncludeArchived;
    state.ui.adminStatsRankFilter = state.adminStatsRankFilter;
    state.ui.adminStatsAutoRefresh = state.adminStatsAutoRefresh;
    state.ui.adminStatsRefreshSeconds = state.adminStatsRefreshSeconds;
    state.ui.inactivityThresholdDays = state.inactivityThresholdDays;
}

