std::string UiSettingsPath(const std::filesystem::path& storageDir) {
    return (storageDir / "meta" / "ui.json").string();
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
    return dir / (SanitizePresetName(name) + ".json");
}

UiSettings LoadUiSettingsFromIniPath(const std::filesystem::path& path, const ImGuiStyle& style);
void SaveUiSettingsToPath(const std::filesystem::path& path, const UiSettings& settings);

std::vector<std::string> LoadUiPresetNames(const std::filesystem::path& storageDir) {
    std::vector<std::string> out;
    std::error_code ec;
    auto dir = UiPresetsDir(storageDir);
    if (!std::filesystem::exists(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        out.push_back(entry.path().stem().string());
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        const std::string name = entry.path().stem().string();
        if (std::find(out.begin(), out.end(), name) != out.end()) continue;
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void MigrateUiPresets(const std::filesystem::path& storageDir, const ImGuiStyle& style) {
    std::error_code ec;
    auto dir = UiPresetsDir(storageDir);
    if (!std::filesystem::exists(dir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        UiSettings settings = LoadUiSettingsFromIniPath(entry.path(), style);
        auto jsonPath = entry.path();
        jsonPath.replace_extension(".json");
        SaveUiSettingsToPath(jsonPath, settings);
        std::filesystem::remove(entry.path(), ec);
    }
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

bool ParseVec2Json(const JsonLite::Value* value, ImVec2& out) {
    if (!value || value->type != JsonLite::Type::Array || value->arrayValue.size() < 2) return false;
    out.x = static_cast<float>(JsonLite::GetDouble(value->arrayValue[0], out.x));
    out.y = static_cast<float>(JsonLite::GetDouble(value->arrayValue[1], out.y));
    return true;
}

bool ParseVec4Json(const JsonLite::Value* value, ImVec4& out) {
    if (!value || value->type != JsonLite::Type::Array || value->arrayValue.size() < 4) return false;
    out.x = static_cast<float>(JsonLite::GetDouble(value->arrayValue[0], out.x));
    out.y = static_cast<float>(JsonLite::GetDouble(value->arrayValue[1], out.y));
    out.z = static_cast<float>(JsonLite::GetDouble(value->arrayValue[2], out.z));
    out.w = static_cast<float>(JsonLite::GetDouble(value->arrayValue[3], out.w));
    return true;
}

bool IsZeroColor(const ImVec4& color) {
    return std::fabs(color.x) <= 0.001f && std::fabs(color.y) <= 0.001f
        && std::fabs(color.z) <= 0.001f && std::fabs(color.w) <= 0.001f;
}

bool AreColorsMostlyZero(const UiSettings& settings) {
    size_t zeros = 0;
    for (const auto& color : settings.colors) {
        if (IsZeroColor(color)) {
            ++zeros;
        }
    }
    return !settings.colors.empty() && zeros >= settings.colors.size() * 3 / 5;
}

void SanitizeUiSettings(UiSettings& settings, const ImGuiStyle& style) {
    settings.theme = std::clamp(settings.theme, 0, 2);
    settings.fontScale = ClampFinite(settings.fontScale, 0.6f, 2.0f, 1.0f);
    settings.alpha = ClampFinite(settings.alpha, 0.4f, 1.0f, 1.0f);
    settings.windowRounding = ClampFinite(settings.windowRounding, 0.0f, 24.0f, style.WindowRounding);
    settings.frameRounding = ClampFinite(settings.frameRounding, 0.0f, 24.0f, style.FrameRounding);
    settings.scrollbarRounding = ClampFinite(settings.scrollbarRounding, 0.0f, 24.0f, style.ScrollbarRounding);
    settings.grabRounding = ClampFinite(settings.grabRounding, 0.0f, 24.0f, style.GrabRounding);
    settings.backgroundAlpha = ClampFinite(settings.backgroundAlpha, 0.0f, 1.0f, 0.25f);
    settings.backgroundTileScale = ClampFinite(settings.backgroundTileScale, 0.25f, 4.0f, 1.0f);
    settings.profileTab = std::clamp(settings.profileTab, 0, 5);
    settings.profileViewMode = std::clamp(settings.profileViewMode, 0, 2);
    settings.adminStatsRankFilter = std::max(0, settings.adminStatsRankFilter);
    settings.adminStatsRefreshSeconds = std::clamp(settings.adminStatsRefreshSeconds, 5, 120);
    settings.inactivityThresholdDays = std::clamp(settings.inactivityThresholdDays, 1, 365);
    settings.pomodoroWorkMinutes = std::clamp(settings.pomodoroWorkMinutes, 5, 120);
    settings.pomodoroBreakMinutes = std::clamp(settings.pomodoroBreakMinutes, 3, 60);
    settings.pomodoroLongBreakMinutes = std::clamp(settings.pomodoroLongBreakMinutes, 5, 90);
    settings.pomodoroCyclesBeforeLong = std::clamp(settings.pomodoroCyclesBeforeLong, 1, 8);
    settings.pomodoroSoundVolume = std::clamp(settings.pomodoroSoundVolume, 0, 100);
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
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < settings.colors.size(); ++i) {
            settings.colors[i].x = ClampFinite(settings.colors[i].x, 0.0f, 1.0f, style.Colors[entries[i].col].x);
            settings.colors[i].y = ClampFinite(settings.colors[i].y, 0.0f, 1.0f, style.Colors[entries[i].col].y);
            settings.colors[i].z = ClampFinite(settings.colors[i].z, 0.0f, 1.0f, style.Colors[entries[i].col].z);
            settings.colors[i].w = ClampFinite(settings.colors[i].w, 0.0f, 1.0f, style.Colors[entries[i].col].w);
        }
    }
}

UiSettings LoadUiSettingsFromIniPath(const std::filesystem::path& path, const ImGuiStyle& style) {
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
        } else if (section == "pomodoro") {
            if (key == "workMinutes") settings.pomodoroWorkMinutes = ParseInt(value, settings.pomodoroWorkMinutes);
            else if (key == "breakMinutes") settings.pomodoroBreakMinutes = ParseInt(value, settings.pomodoroBreakMinutes);
            else if (key == "longBreakMinutes") settings.pomodoroLongBreakMinutes = ParseInt(value, settings.pomodoroLongBreakMinutes);
            else if (key == "cyclesBeforeLong") settings.pomodoroCyclesBeforeLong = ParseInt(value, settings.pomodoroCyclesBeforeLong);
            else if (key == "soundEnabled") settings.pomodoroSoundEnabled = ParseBool(value, settings.pomodoroSoundEnabled);
            else if (key == "soundFocus") settings.pomodoroSoundFocus = value;
            else if (key == "soundBreak") settings.pomodoroSoundBreak = value;
            else if (key == "soundVolume") settings.pomodoroSoundVolume = ParseInt(value, settings.pomodoroSoundVolume);
            else if (key == "autoAdvance") settings.pomodoroAutoAdvance = ParseBool(value, settings.pomodoroAutoAdvance);
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
    SanitizeUiSettings(settings, style);
    return settings;
}

UiSettings LoadUiSettingsFromJsonPath(const std::filesystem::path& path, const ImGuiStyle& style) {
    UiSettings settings;
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        settings.colors[i] = style.Colors[entries[i].col];
    }
    std::ifstream in(path);
    if (!in) return settings;
    std::ostringstream ss;
    ss << in.rdbuf();
    JsonLite::Value root;
    if (!JsonLite::Parse(ss.str(), root, nullptr)) {
        return settings;
    }
    auto get_obj = [&](const JsonLite::Value& obj, const char* key) -> const JsonLite::Value* {
        return JsonLite::GetObjectValue(obj, key);
    };
    if (const auto* styleObj = get_obj(root, "style")) {
        if (const auto* v = get_obj(*styleObj, "theme")) settings.theme = JsonLite::GetInt(*v, settings.theme);
        if (const auto* v = get_obj(*styleObj, "fontScale")) settings.fontScale = static_cast<float>(JsonLite::GetDouble(*v, settings.fontScale));
        if (const auto* v = get_obj(*styleObj, "alpha")) settings.alpha = static_cast<float>(JsonLite::GetDouble(*v, settings.alpha));
        if (const auto* v = get_obj(*styleObj, "windowRounding")) settings.windowRounding = static_cast<float>(JsonLite::GetDouble(*v, settings.windowRounding));
        if (const auto* v = get_obj(*styleObj, "frameRounding")) settings.frameRounding = static_cast<float>(JsonLite::GetDouble(*v, settings.frameRounding));
        if (const auto* v = get_obj(*styleObj, "scrollbarRounding")) settings.scrollbarRounding = static_cast<float>(JsonLite::GetDouble(*v, settings.scrollbarRounding));
        if (const auto* v = get_obj(*styleObj, "grabRounding")) settings.grabRounding = static_cast<float>(JsonLite::GetDouble(*v, settings.grabRounding));
        ParseVec2Json(get_obj(*styleObj, "windowPadding"), settings.windowPadding);
        ParseVec2Json(get_obj(*styleObj, "framePadding"), settings.framePadding);
        ParseVec2Json(get_obj(*styleObj, "itemSpacing"), settings.itemSpacing);
        if (const auto* v = get_obj(*styleObj, "customColors")) settings.customColors = JsonLite::GetBool(*v, settings.customColors);
        if (const auto* v = get_obj(*styleObj, "backgroundAlpha")) settings.backgroundAlpha = static_cast<float>(JsonLite::GetDouble(*v, settings.backgroundAlpha));
        if (const auto* v = get_obj(*styleObj, "backgroundTiled")) settings.backgroundTiled = JsonLite::GetBool(*v, settings.backgroundTiled);
        if (const auto* v = get_obj(*styleObj, "backgroundTileScale")) settings.backgroundTileScale = static_cast<float>(JsonLite::GetDouble(*v, settings.backgroundTileScale));
        if (const auto* v = get_obj(*styleObj, "windowDecorated")) settings.windowDecorated = JsonLite::GetBool(*v, settings.windowDecorated);
        if (const auto* v = get_obj(*styleObj, "windowFullscreen")) settings.windowFullscreen = JsonLite::GetBool(*v, settings.windowFullscreen);
        if (const auto* v = get_obj(*styleObj, "profileTab")) settings.profileTab = JsonLite::GetInt(*v, settings.profileTab);
    }
    if (const auto* profileObj = get_obj(root, "profile")) {
        if (const auto* v = get_obj(*profileObj, "lastProfileId")) settings.lastProfileId = JsonLite::GetString(*v, settings.lastProfileId);
        if (const auto* v = get_obj(*profileObj, "viewMode")) settings.profileViewMode = JsonLite::GetInt(*v, settings.profileViewMode);
    }
    if (const auto* logs = get_obj(root, "logs")) {
        if (const auto* v = get_obj(*logs, "filter")) settings.logFilter = JsonLite::GetString(*v, settings.logFilter);
        if (const auto* v = get_obj(*logs, "source")) settings.logSourceFilter = JsonLite::GetString(*v, settings.logSourceFilter);
        if (const auto* v = get_obj(*logs, "showInfo")) settings.logShowInfo = JsonLite::GetBool(*v, settings.logShowInfo);
        if (const auto* v = get_obj(*logs, "showWarning")) settings.logShowWarning = JsonLite::GetBool(*v, settings.logShowWarning);
        if (const auto* v = get_obj(*logs, "showError")) settings.logShowError = JsonLite::GetBool(*v, settings.logShowError);
        if (const auto* v = get_obj(*logs, "autoScroll")) settings.logAutoScroll = JsonLite::GetBool(*v, settings.logAutoScroll);
        if (const auto* v = get_obj(*logs, "compactView")) settings.logCompactView = JsonLite::GetBool(*v, settings.logCompactView);
    }
    if (const auto* adminStats = get_obj(root, "adminStats")) {
        if (const auto* v = get_obj(*adminStats, "filter")) settings.adminStatsFilter = JsonLite::GetString(*v, settings.adminStatsFilter);
        if (const auto* v = get_obj(*adminStats, "includeArchived")) settings.adminStatsIncludeArchived = JsonLite::GetBool(*v, settings.adminStatsIncludeArchived);
        if (const auto* v = get_obj(*adminStats, "rankFilter")) settings.adminStatsRankFilter = JsonLite::GetInt(*v, settings.adminStatsRankFilter);
        if (const auto* v = get_obj(*adminStats, "autoRefresh")) settings.adminStatsAutoRefresh = JsonLite::GetBool(*v, settings.adminStatsAutoRefresh);
        if (const auto* v = get_obj(*adminStats, "refreshSeconds")) settings.adminStatsRefreshSeconds = JsonLite::GetInt(*v, settings.adminStatsRefreshSeconds);
        if (const auto* v = get_obj(*adminStats, "inactivityDays")) settings.inactivityThresholdDays = JsonLite::GetInt(*v, settings.inactivityThresholdDays);
    }
    if (const auto* pomodoro = get_obj(root, "pomodoro")) {
        if (const auto* v = get_obj(*pomodoro, "workMinutes")) settings.pomodoroWorkMinutes = JsonLite::GetInt(*v, settings.pomodoroWorkMinutes);
        if (const auto* v = get_obj(*pomodoro, "breakMinutes")) settings.pomodoroBreakMinutes = JsonLite::GetInt(*v, settings.pomodoroBreakMinutes);
        if (const auto* v = get_obj(*pomodoro, "longBreakMinutes")) settings.pomodoroLongBreakMinutes = JsonLite::GetInt(*v, settings.pomodoroLongBreakMinutes);
        if (const auto* v = get_obj(*pomodoro, "cyclesBeforeLong")) settings.pomodoroCyclesBeforeLong = JsonLite::GetInt(*v, settings.pomodoroCyclesBeforeLong);
        if (const auto* v = get_obj(*pomodoro, "soundEnabled")) settings.pomodoroSoundEnabled = JsonLite::GetBool(*v, settings.pomodoroSoundEnabled);
        if (const auto* v = get_obj(*pomodoro, "soundFocus")) settings.pomodoroSoundFocus = JsonLite::GetString(*v, settings.pomodoroSoundFocus);
        if (const auto* v = get_obj(*pomodoro, "soundBreak")) settings.pomodoroSoundBreak = JsonLite::GetString(*v, settings.pomodoroSoundBreak);
        if (const auto* v = get_obj(*pomodoro, "soundVolume")) settings.pomodoroSoundVolume = JsonLite::GetInt(*v, settings.pomodoroSoundVolume);
        if (const auto* v = get_obj(*pomodoro, "autoAdvance")) settings.pomodoroAutoAdvance = JsonLite::GetBool(*v, settings.pomodoroAutoAdvance);
    }
    if (const auto* colors = get_obj(root, "colors")) {
        if (colors->type == JsonLite::Type::Object) {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (const auto* v = get_obj(*colors, entries[i].name)) {
                    ParseVec4Json(v, settings.colors[i]);
                    settings.customColors = true;
                }
            }
        }
    }
    if (settings.customColors && AreColorsMostlyZero(settings)) {
        settings.customColors = false;
        for (size_t i = 0; i < entries.size(); ++i) {
            settings.colors[i] = style.Colors[entries[i].col];
        }
    }
    if (const auto* backgrounds = get_obj(root, "backgrounds")) {
        if (backgrounds->type == JsonLite::Type::Object) {
            const auto& windows = UiWindows();
            for (size_t i = 0; i < windows.size(); ++i) {
                if (const auto* v = get_obj(*backgrounds, windows[i].label)) {
                    settings.backgrounds[i] = JsonLite::GetString(*v, settings.backgrounds[i]);
                }
            }
        }
    }
    if (const auto* view3d = get_obj(root, "view3d")) {
        if (const auto* v = get_obj(*view3d, "modelPath")) settings.modelPath = JsonLite::GetString(*v, settings.modelPath);
        if (const auto* v = get_obj(*view3d, "yaw")) settings.modelYaw = static_cast<float>(JsonLite::GetDouble(*v, settings.modelYaw));
        if (const auto* v = get_obj(*view3d, "pitch")) settings.modelPitch = static_cast<float>(JsonLite::GetDouble(*v, settings.modelPitch));
        if (const auto* v = get_obj(*view3d, "zoom")) settings.modelZoom = static_cast<float>(JsonLite::GetDouble(*v, settings.modelZoom));
        if (const auto* v = get_obj(*view3d, "autoRotate")) settings.modelAutoRotate = JsonLite::GetBool(*v, settings.modelAutoRotate);
        if (const auto* v = get_obj(*view3d, "autoSpeed")) settings.modelAutoSpeed = static_cast<float>(JsonLite::GetDouble(*v, settings.modelAutoSpeed));
        ParseVec4Json(get_obj(*view3d, "color"), settings.modelColor);
    }
    SanitizeUiSettings(settings, style);
    return settings;
}

UiSettings LoadUiSettingsFromPath(const std::filesystem::path& path, const ImGuiStyle& style) {
    if (path.extension() == ".json") {
        if (std::ifstream in(path); in.good()) {
            return LoadUiSettingsFromJsonPath(path, style);
        }
        auto legacy = path;
        legacy.replace_extension(".ini");
        if (std::ifstream legacyIn(legacy); legacyIn.good()) {
            UiSettings settings = LoadUiSettingsFromIniPath(legacy, style);
            SaveUiSettingsToPath(path, settings);
            std::error_code ec;
            std::filesystem::remove(legacy, ec);
            return settings;
        }
        return UiSettings{};
    }
    if (path.extension() == ".ini") {
        UiSettings settings = LoadUiSettingsFromIniPath(path, style);
        auto jsonPath = path;
        jsonPath.replace_extension(".json");
        SaveUiSettingsToPath(jsonPath, settings);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return settings;
    }
    return LoadUiSettingsFromJsonPath(path, style);
}

UiSettings LoadUiSettings(const std::filesystem::path& storageDir, const ImGuiStyle& style) {
    return LoadUiSettingsFromPath(std::filesystem::path(UiSettingsPath(storageDir)), style);
}

void SaveUiSettingsToPath(const std::filesystem::path& path, const UiSettings& settings) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.imbue(std::locale::classic());
    UiSettings local = settings;
    if (!local.customColors) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const auto& entries = UiColorEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            local.colors[i] = style.Colors[entries[i].col];
        }
    }
    out << "{\n";
    out << "  \"style\": {\n";
    out << "    \"theme\": " << local.theme << ",\n";
    out << "    \"fontScale\": " << local.fontScale << ",\n";
    out << "    \"alpha\": " << local.alpha << ",\n";
    out << "    \"windowRounding\": " << local.windowRounding << ",\n";
    out << "    \"frameRounding\": " << local.frameRounding << ",\n";
    out << "    \"scrollbarRounding\": " << local.scrollbarRounding << ",\n";
    out << "    \"grabRounding\": " << local.grabRounding << ",\n";
    out << "    \"windowPadding\": [" << local.windowPadding.x << ", " << local.windowPadding.y << "],\n";
    out << "    \"framePadding\": [" << local.framePadding.x << ", " << local.framePadding.y << "],\n";
    out << "    \"itemSpacing\": [" << local.itemSpacing.x << ", " << local.itemSpacing.y << "],\n";
    out << "    \"customColors\": " << (local.customColors ? "true" : "false") << ",\n";
    out << "    \"backgroundAlpha\": " << local.backgroundAlpha << ",\n";
    out << "    \"backgroundTiled\": " << (local.backgroundTiled ? "true" : "false") << ",\n";
    out << "    \"backgroundTileScale\": " << local.backgroundTileScale << ",\n";
    out << "    \"windowDecorated\": " << (local.windowDecorated ? "true" : "false") << ",\n";
    out << "    \"windowFullscreen\": " << (local.windowFullscreen ? "true" : "false") << ",\n";
    out << "    \"profileTab\": " << local.profileTab << "\n";
    out << "  },\n";
    out << "  \"profile\": {\n";
    out << "    \"lastProfileId\": \"" << JsonLite::Escape(SanitizeLogLine(local.lastProfileId)) << "\",\n";
    out << "    \"viewMode\": " << local.profileViewMode << "\n";
    out << "  },\n";
    out << "  \"logs\": {\n";
    out << "    \"filter\": \"" << JsonLite::Escape(SanitizeLogLine(local.logFilter)) << "\",\n";
    out << "    \"source\": \"" << JsonLite::Escape(SanitizeLogLine(local.logSourceFilter)) << "\",\n";
    out << "    \"showInfo\": " << (local.logShowInfo ? "true" : "false") << ",\n";
    out << "    \"showWarning\": " << (local.logShowWarning ? "true" : "false") << ",\n";
    out << "    \"showError\": " << (local.logShowError ? "true" : "false") << ",\n";
    out << "    \"autoScroll\": " << (local.logAutoScroll ? "true" : "false") << ",\n";
    out << "    \"compactView\": " << (local.logCompactView ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"adminStats\": {\n";
    out << "    \"filter\": \"" << JsonLite::Escape(SanitizeLogLine(local.adminStatsFilter)) << "\",\n";
    out << "    \"includeArchived\": " << (local.adminStatsIncludeArchived ? "true" : "false") << ",\n";
    out << "    \"rankFilter\": " << local.adminStatsRankFilter << ",\n";
    out << "    \"autoRefresh\": " << (local.adminStatsAutoRefresh ? "true" : "false") << ",\n";
    out << "    \"refreshSeconds\": " << local.adminStatsRefreshSeconds << ",\n";
    out << "    \"inactivityDays\": " << local.inactivityThresholdDays << "\n";
    out << "  },\n";
    out << "  \"pomodoro\": {\n";
    out << "    \"workMinutes\": " << local.pomodoroWorkMinutes << ",\n";
    out << "    \"breakMinutes\": " << local.pomodoroBreakMinutes << ",\n";
    out << "    \"longBreakMinutes\": " << local.pomodoroLongBreakMinutes << ",\n";
    out << "    \"cyclesBeforeLong\": " << local.pomodoroCyclesBeforeLong << ",\n";
    out << "    \"soundEnabled\": " << (local.pomodoroSoundEnabled ? "true" : "false") << ",\n";
    out << "    \"soundFocus\": \"" << JsonLite::Escape(SanitizeLogLine(local.pomodoroSoundFocus)) << "\",\n";
    out << "    \"soundBreak\": \"" << JsonLite::Escape(SanitizeLogLine(local.pomodoroSoundBreak)) << "\",\n";
    out << "    \"soundVolume\": " << local.pomodoroSoundVolume << ",\n";
    out << "    \"autoAdvance\": " << (local.pomodoroAutoAdvance ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"colors\": {\n";
    const auto& entries = UiColorEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        out << "    \"" << entries[i].name << "\": [" << local.colors[i].x << ", " << local.colors[i].y
            << ", " << local.colors[i].z << ", " << local.colors[i].w << "]";
        out << (i + 1 < entries.size() ? ",\n" : "\n");
    }
    out << "  },\n";
    out << "  \"backgrounds\": {\n";
    const auto& windows = UiWindows();
    for (size_t i = 0; i < windows.size(); ++i) {
        out << "    \"" << windows[i].label << "\": \"" << JsonLite::Escape(local.backgrounds[i]) << "\"";
        out << (i + 1 < windows.size() ? ",\n" : "\n");
    }
    out << "  },\n";
    out << "  \"view3d\": {\n";
    out << "    \"modelPath\": \"" << JsonLite::Escape(local.modelPath) << "\",\n";
    out << "    \"yaw\": " << local.modelYaw << ",\n";
    out << "    \"pitch\": " << local.modelPitch << ",\n";
    out << "    \"zoom\": " << local.modelZoom << ",\n";
    out << "    \"autoRotate\": " << (local.modelAutoRotate ? "true" : "false") << ",\n";
    out << "    \"autoSpeed\": " << local.modelAutoSpeed << ",\n";
    out << "    \"color\": [" << local.modelColor.x << ", " << local.modelColor.y << ", "
        << local.modelColor.z << ", " << local.modelColor.w << "]\n";
    out << "  }\n";
    out << "}\n";
}

void SaveUiSettings(const std::filesystem::path& storageDir, const UiSettings& settings) {
    SaveUiSettingsToPath(std::filesystem::path(UiSettingsPath(storageDir)), settings);
    std::error_code ec;
    std::filesystem::remove(storageDir / "meta" / "ui.ini", ec);
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

