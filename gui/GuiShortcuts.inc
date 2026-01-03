static std::filesystem::path ShortcutsStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "shortcuts.json";
}

static std::string shortcuts_read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool shortcuts_write_all(const std::filesystem::path& p, const std::string& data) {
    auto parent = p.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}

static std::string shortcuts_json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

static void shortcuts_skip_ws(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

static bool shortcuts_parse_string(const std::string& text, size_t& pos, std::string& out) {
    shortcuts_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '"') return false;
    ++pos;
    while (pos < text.size()) {
        char c = text[pos++];
        if (c == '"') return true;
        if (c == '\\') {
            if (pos >= text.size()) return false;
            char esc = text[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default:
                    out.push_back('\\');
                    out.push_back(esc);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

static bool shortcuts_parse_object(const std::string& text, size_t& pos,
                                   std::unordered_map<std::string, std::string>& out) {
    shortcuts_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    ++pos;
    while (pos < text.size()) {
        shortcuts_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        std::string key;
        if (!shortcuts_parse_string(text, pos, key)) return false;
        shortcuts_skip_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        ++pos;
        std::string value;
        if (!shortcuts_parse_string(text, pos, value)) return false;
        out[key] = value;
        shortcuts_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
    }
    return false;
}

static std::vector<std::unordered_map<std::string, std::string>> shortcuts_parse_object_array(const std::string& text) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    size_t pos = 0;
    shortcuts_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '[') return objects;
    ++pos;
    while (pos < text.size()) {
        shortcuts_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        std::unordered_map<std::string, std::string> obj;
        if (!shortcuts_parse_object(text, pos, obj)) break;
        objects.push_back(std::move(obj));
        shortcuts_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return objects;
}

static std::vector<ShortcutEntry> LoadShortcuts(const std::filesystem::path& storageDir) {
    std::vector<ShortcutEntry> out;
    const auto path = ShortcutsStoragePath(storageDir);
    std::string content = shortcuts_read_all(path);
    if (content.empty()) return out;
    const auto objects = shortcuts_parse_object_array(content);
    for (const auto& obj : objects) {
        auto find_value = [&](const char* key) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            return it->second;
        };
        ShortcutEntry entry;
        if (auto v = find_value("id")) entry.id = *v;
        if (auto v = find_value("label")) entry.label = *v;
        if (auto v = find_value("path")) entry.path = *v;
        if (entry.id.empty() || entry.path.empty()) continue;
        out.push_back(std::move(entry));
    }
    return out;
}

static bool SaveShortcuts(const std::filesystem::path& storageDir, const std::vector<ShortcutEntry>& shortcuts) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        const auto& entry = shortcuts[i];
        out << "  {\"id\":\"" << shortcuts_json_escape(entry.id)
            << "\",\"label\":\"" << shortcuts_json_escape(entry.label)
            << "\",\"path\":\"" << shortcuts_json_escape(entry.path) << "\"}";
        if (i + 1 < shortcuts.size()) out << ",";
        out << "\n";
    }
    out << "]";
    return shortcuts_write_all(ShortcutsStoragePath(storageDir), out.str());
}

static std::string GenerateShortcutId(std::int64_t nowSeconds, size_t index) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "shortcut_" << nowSeconds << "_" << index;
    return out.str();
}

static bool LaunchShortcut(const std::string& path) {
    if (path.empty()) return false;
#if defined(_WIN32)
    std::filesystem::path full(path);
    std::error_code ec;
    if (!full.is_absolute()) {
        full = std::filesystem::absolute(full, ec);
    }
    return ShellExecuteA(nullptr, "open", full.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > (HINSTANCE)32;
#else
    (void)path;
    return false;
#endif
}

static bool PickShortcutFile(std::string& outPath) {
#if defined(_WIN32)
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Programs (*.lnk;*.exe;*.bat;*.cmd)\0*.lnk;*.exe;*.bat;*.cmd\0All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        outPath = filename;
        return true;
    }
    return false;
#else
    (void)outPath;
    return false;
#endif
}

static void DrawShortcutsSidebar(GuiState& state) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
    accent.w = 1.0f;
    if (ImGui::BeginChild("shortcuts_sidebar", ImVec2(0.0f, 0.0f), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        DrawCardHeader(u8"Ярлыки");
        const float iconSize = std::max(44.0f, ImGui::GetFrameHeight() * 1.35f);
        if (state.shortcuts.empty()) {
            ImGui::TextDisabled(u8"Нет ярлыков.");
        } else {
            for (size_t i = 0; i < state.shortcuts.size(); ++i) {
                const auto& entry = state.shortcuts[i];
                ImGui::PushID(static_cast<int>(i));
                if (IconButtonGui("shortcut_launch", UiIcon::Launch, entry.label.c_str(), accent, iconSize, true, false)) {
                    if (!LaunchShortcut(entry.path)) {
                        SetStatus(state, u8"Не удалось открыть ярлык.", 1.0f, 0.75f, 0.35f);
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

static void DrawShortcutsPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::Shortcuts, state.storageDir);
    DrawCardHeader(u8"Ярлыки");
    ImGui::TextDisabled(u8"Добавьте ярлыки программ и запускайте их из правой панели.");

    DrawCardHeader(u8"Добавить ярлык");
    ImGui::TextDisabled(u8"Название");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(u8"##shortcut_label", state.shortcutLabel.data(), state.shortcutLabel.size());
    ImGui::TextDisabled(u8"Путь");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(u8"##shortcut_path", state.shortcutPath.data(), state.shortcutPath.size());
    if (ImGui::Button(u8"Выбрать файл")) {
        std::string picked;
        if (PickShortcutFile(picked)) {
            std::snprintf(state.shortcutPath.data(), state.shortcutPath.size(), "%s", picked.c_str());
        }
    }
    if (ImGui::Button(u8"Добавить", ImVec2(-1.0f, 0.0f))) {
        std::string path = TrimStringGui(state.shortcutPath.data());
        std::string label = TrimStringGui(state.shortcutLabel.data());
        if (path.empty()) {
            SetStatus(state, u8"Укажите путь к ярлыку.", 1.0f, 0.45f, 0.45f);
        } else {
            if (label.empty()) {
                label = std::filesystem::path(path).stem().string();
            }
            ShortcutEntry entry;
            entry.id = GenerateShortcutId(NowSeconds(), state.shortcuts.size() + 1);
            entry.label = label;
            entry.path = path;
            state.shortcuts.push_back(std::move(entry));
            SaveShortcuts(state.storageDir, state.shortcuts);
            state.shortcutLabel.fill('\0');
            state.shortcutPath.fill('\0');
            SetStatus(state, u8"Ярлык добавлен.", 0.45f, 0.9f, 0.45f);
        }
    }

    DrawCardHeader(u8"Список");
    if (state.shortcuts.empty()) {
        ImGui::TextDisabled(u8"Ярлыков пока нет.");
        return;
    }
    if (ImGui::BeginTable("shortcuts_list", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                  ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"Название", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn(u8"Путь", ImGuiTableColumnFlags_WidthStretch, 1.9f);
        ImGui::TableSetupColumn(u8"Действия", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableHeadersRow();
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(state.shortcuts.size()); ++i) {
            const auto& entry = state.shortcuts[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", entry.path.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(i);
            if (ImGui::Button(u8"Открыть", ImVec2(72.0f, 0.0f))) {
                if (!LaunchShortcut(entry.path)) {
                    SetStatus(state, u8"Не удалось открыть ярлык.", 1.0f, 0.75f, 0.35f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"Удалить", ImVec2(72.0f, 0.0f))) {
                removeIndex = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removeIndex >= 0 && removeIndex < static_cast<int>(state.shortcuts.size())) {
            state.shortcuts.erase(state.shortcuts.begin() + removeIndex);
            SaveShortcuts(state.storageDir, state.shortcuts);
            SetStatus(state, u8"Ярлык удалён.", 0.45f, 0.9f, 0.45f);
        }
    }
}
