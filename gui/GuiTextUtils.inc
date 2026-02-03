const char* LogLevelLabel(AppLogLevel level) {
    switch (level) {
        case AppLogLevel::Warning: return u8"Предупреждение";
        case AppLogLevel::Error: return u8"Ошибка";
        default: return u8"Инфо";
    }
}

ImVec4 LogLevelColor(AppLogLevel level) {
    switch (level) {
        case AppLogLevel::Warning: return ImVec4(0.95f, 0.75f, 0.3f, 1.0f);
        case AppLogLevel::Error: return ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
        default: return ImVec4(0.45f, 0.8f, 0.9f, 1.0f);
    }
}

std::string ToLowerAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string ToLowerUtf8(std::string s) {
    auto decode_utf8 = [](const std::string& text, size_t& pos, uint32_t& out) {
        unsigned char c0 = static_cast<unsigned char>(text[pos]);
        if (c0 < 0x80) {
            out = c0;
            ++pos;
            return true;
        }
        if ((c0 >> 5) == 0x6 && pos + 1 < text.size()) {
            unsigned char c1 = static_cast<unsigned char>(text[pos + 1]);
            if ((c1 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            pos += 2;
            return true;
        }
        if ((c0 >> 4) == 0xE && pos + 2 < text.size()) {
            unsigned char c1 = static_cast<unsigned char>(text[pos + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[pos + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            pos += 3;
            return true;
        }
        if ((c0 >> 3) == 0x1E && pos + 3 < text.size()) {
            unsigned char c1 = static_cast<unsigned char>(text[pos + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[pos + 2]);
            unsigned char c3 = static_cast<unsigned char>(text[pos + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            pos += 4;
            return true;
        }
        return false;
    };
    auto append_utf8 = [](std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    auto lower_codepoint = [](uint32_t cp) {
        if (cp >= 'A' && cp <= 'Z') return cp + 32;
        if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
        if (cp == 0x0401) return static_cast<uint32_t>(0x0451);
        return cp;
    };

    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        size_t next = i;
        if (!decode_utf8(s, next, cp)) {
            out.push_back(s[i]);
            ++i;
            continue;
        }
        i = next;
        append_utf8(out, lower_codepoint(cp));
    }
    return out;
}

std::string NormalizeSkillNameGui(const std::string& name) {
    auto decode_utf8 = [](const std::string& s, size_t& i, uint32_t& out) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80) {
            out = c0;
            ++i;
            return true;
        }
        if ((c0 >> 5) == 0x6 && i + 1 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c1 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            i += 2;
            return true;
        }
        if ((c0 >> 4) == 0xE && i + 2 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            i += 3;
            return true;
        }
        if ((c0 >> 3) == 0x1E && i + 3 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            i += 4;
            return true;
        }
        return false;
    };
    auto append_utf8 = [](std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    auto lower_codepoint = [](uint32_t cp) {
        if (cp >= 'A' && cp <= 'Z') return cp + 32;
        if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
        if (cp == 0x0401) return static_cast<uint32_t>(0x0451);
        return cp;
    };

    std::string out;
    out.reserve(name.size());
    size_t i = 0;
    while (i < name.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(name, i, cp)) break;
        if (cp <= 0x7F && std::isspace(static_cast<unsigned char>(cp))) continue;
        append_utf8(out, lower_codepoint(cp));
    }
    return out;
}

std::string TrimStringGui(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}


bool MatchesFilter(const std::string& text, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    std::string needle = TrimStringGui(filter);
    if (needle.empty()) return true;
    std::string hay = ToLowerUtf8(text);
    needle = ToLowerUtf8(needle);
    return hay.find(needle) != std::string::npos;
}

std::string FormatTimestamp(std::int64_t ts, const char* format) {
    if (ts <= 0) return {};
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    if (std::strftime(buf, sizeof(buf), format, &tm)) {
        return std::string(buf);
    }
    return {};
}

std::string FormatDurationShort(std::int64_t seconds) {
    if (seconds <= 0) return "00:00:00";
    constexpr std::int64_t daySeconds = 24 * 3600;
    const std::int64_t days = seconds / daySeconds;
    seconds %= daySeconds;
    const std::int64_t hours = seconds / 3600;
    seconds %= 3600;
    const std::int64_t minutes = seconds / 60;
    const std::int64_t secs = seconds % 60;
    std::ostringstream out;
    out << std::setfill('0');
    if (days > 0) {
        out << days << u8"д ";
    }
    out << std::setw(2) << hours << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    return out.str();
}

int WeightCategoryIndex(double weight) {
    if (weight >= 1.3) return 1; // A
    if (weight >= 1.1) return 2; // B
    if (weight >= 0.9) return 3; // C
    if (weight >= 0.7) return 4; // D
    return 5; // E
}

const char* WeightCategoryLabel(int index) {
    static const char* labels[] = {
        u8"Все",
        u8"A (>=1.30)",
        u8"B (1.10-1.29)",
        u8"C (0.90-1.09)",
        u8"D (0.70-0.89)",
        u8"E (<0.70)"
    };
    const int clamped = std::clamp(index, 0, static_cast<int>(IM_ARRAYSIZE(labels)) - 1);
    return labels[clamped];
}

std::string KeyboardLayoutLabel() {
#if defined(_WIN32)
    const HKL layout = GetKeyboardLayout(0);
    const LANGID langId = LOWORD(layout);
    switch (PRIMARYLANGID(langId)) {
        case LANG_RUSSIAN:
            return "RU";
        case LANG_ENGLISH:
            return "EN";
        case LANG_UKRAINIAN:
            return "UK";
        default:
            break;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04x", static_cast<unsigned int>(langId));
    return std::string(buf);
#else
    return std::string("N/A");
#endif
}

bool IsCapsLockOn() {
#if defined(_WIN32)
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
#else
    return false;
#endif
}

bool IsShiftDown() {
#if defined(_WIN32)
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    return false;
#endif
}

std::string KeyboardHintCompactLabel(bool includeShift = false) {
    std::string label = KeyboardLayoutLabel();
    label += u8" | Caps: ";
    label += IsCapsLockOn() ? u8"Вкл" : u8"Выкл";
    if (includeShift) {
        label += u8" | Shift: ";
        label += IsShiftDown() ? u8"Нажат" : u8"Нет";
    }
    return label;
}

void DrawKeyboardHintLine(bool includeShift = true) {
    const std::string layout = KeyboardLayoutLabel();
    const char* capsLabel = IsCapsLockOn() ? u8"Вкл" : u8"Выкл";
    if (includeShift) {
        const char* shiftLabel = IsShiftDown() ? u8"Нажат" : u8"Нет";
        ImGui::TextDisabled(u8"Раскладка: %s | Caps: %s | Shift: %s", layout.c_str(), capsLabel, shiftLabel);
    } else {
        ImGui::TextDisabled(u8"Раскладка: %s | Caps: %s", layout.c_str(), capsLabel);
    }
}

std::string SanitizeLogLine(std::string text) {
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    return text;
}

bool MatchesLogFilters(const GuiState& state, const AppLogEntry& entry) {
    if (entry.level == AppLogLevel::Info && !state.logShowInfo) return false;
    if (entry.level == AppLogLevel::Warning && !state.logShowWarning) return false;
    if (entry.level == AppLogLevel::Error && !state.logShowError) return false;
    if (state.filters.logSource[0] != '\0') {
        if (entry.source.empty()) return false;
        if (!MatchesFilter(entry.source, state.filters.logSource.data())) return false;
    }
    if (state.filters.logs[0] == '\0') return true;
    std::string hay = entry.source + " " + entry.message;
    return MatchesFilter(hay, state.filters.logs.data());
}
