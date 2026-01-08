#include "IJobStorage.h"
#include "JsonLite.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr const char* kArchiveFolder = "archive";

bool is_profile_file(const std::filesystem::directory_entry& entry) {
    if (!entry.is_regular_file()) return false;
    const auto ext = entry.path().extension();
    return ext == ".json" || ext == ".ini";
}

bool is_profile_json_path(const std::filesystem::path& path) {
    return path.extension() == ".json";
}

bool is_profile_ini_path(const std::filesystem::path& path) {
    return path.extension() == ".ini";
}

std::filesystem::path profile_json_path_for(const std::filesystem::path& path) {
    return path.parent_path() / (path.stem().string() + ".json");
}

std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base(), s.end());
    return s;
}

std::string sanitize_int(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isdigit(ch)) {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '-' && out.empty()) {
            out.push_back('-');
        }
    }
    if (out.empty()) return value;
    return out;
}

std::string sanitize_float(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isdigit(ch)) {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '.' || ch == ',') {
            out.push_back('.');
        } else if (ch == '-' && out.empty()) {
            out.push_back('-');
        }
    }
    if (out.empty()) return value;
    return out;
}

void append_utf8(std::string& out, uint32_t cp) {
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
}

std::string json_escape(const std::string& value) {
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

int parse_int(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(sanitize_int(value));
    } catch (...) {
        return fallback;
    }
}

std::int64_t parse_int64(const std::string& value, std::int64_t fallback = 0) {
    try {
        return std::stoll(sanitize_int(value));
    } catch (...) {
        return fallback;
    }
}

double parse_double(const std::string& value, double fallback = 0.0) {
    try {
        return std::stod(sanitize_float(value));
    } catch (...) {
        return fallback;
    }
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void normalize_achievement_times(Achievement& a, std::int64_t nowSec) {
    constexpr std::int64_t kMinEpoch = 1577836800; // 2020-01-01
    if (a.awardedAt < kMinEpoch) {
        a.awardedAt = nowSec;
    }
    if (a.expiresAt > 0) {
        if (a.expiresAt < kMinEpoch) {
            std::int64_t durationDays = std::max<std::int64_t>(0, a.expiresAt);
            a.expiresAt = a.awardedAt + durationDays * 24 * 3600;
        }
        if (a.expiresAt <= a.awardedAt) {
            a.expiresAt = 0;
        }
    }
}

void skip_ws(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

bool parse_string(const std::string& text, size_t& pos, std::string& out) {
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
                case 'u': {
                    auto hex_value = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
                        if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
                        return -1;
                    };
                    if (pos + 3 < text.size()) {
                        uint32_t cp = 0;
                        bool ok = true;
                        for (int i = 0; i < 4; ++i) {
                            int v = hex_value(text[pos + i]);
                            if (v < 0) { ok = false; break; }
                            cp = (cp << 4) | static_cast<uint32_t>(v);
                        }
                        if (ok) {
                            append_utf8(out, cp);
                            pos += 4;
                            break;
                        }
                    }
                    out.push_back('\\');
                    out.push_back('u');
                    break;
                }
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

bool parse_value_token(const std::string& text, size_t& pos, std::string& out) {
    skip_ws(text, pos);
    if (pos >= text.size()) return false;
    if (text[pos] == '"') {
        return parse_string(text, pos, out);
    }
    size_t start = pos;
    while (pos < text.size()) {
        char c = text[pos];
        if (c == ',' || c == '}' || c == ']' || std::isspace(static_cast<unsigned char>(c))) break;
        ++pos;
    }
    if (pos == start) return false;
    out.assign(text.substr(start, pos - start));
    return true;
}

bool parse_object(const std::string& text, size_t& pos, std::unordered_map<std::string, std::string>& out) {
    skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    ++pos;
    while (pos < text.size()) {
        skip_ws(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        std::string key;
        if (!parse_string(text, pos, key)) return false;
        skip_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        ++pos;
        std::string value;
        if (!parse_value_token(text, pos, value)) return false;
        out[key] = value;
        skip_ws(text, pos);
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

std::vector<std::unordered_map<std::string, std::string>> parse_object_array(const std::string& text) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    size_t pos = 0;
    skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '[') return objects;
    ++pos;
    while (pos < text.size()) {
        skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        std::unordered_map<std::string, std::string> obj;
        if (!parse_object(text, pos, obj)) break;
        objects.push_back(std::move(obj));
        skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return objects;
}

        std::string read_all(const std::filesystem::path& p) {
            std::ifstream in(p, std::ios::binary);
            if (!in) return {};
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::vector<Achievement> load_achievements(const std::filesystem::path& baseDir, const std::string& id) {
            std::vector<Achievement> result;
            auto path = baseDir / "achievements" / (id + ".json");
            std::ifstream in(path);
            if (!in) return result;
            const auto nowSec = now_seconds();
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto objects = parse_object_array(content);
            for (const auto& obj : objects) {
                Achievement a;
                auto find_value = [&](const char* key) -> std::optional<std::string> {
                    auto it = obj.find(key);
                    if (it == obj.end()) return std::nullopt;
                    return it->second;
                };
                if (auto v = find_value("title")) a.title = *v;
                if (auto v = find_value("skillId")) a.skill = *v;
                if (a.skill.empty()) {
                    if (auto v = find_value("skill")) a.skill = *v;
                }
                if (auto v = find_value("bonus")) a.bonusPercent = parse_double(*v, 0.0);
                if (auto v = find_value("bonusPercent")) a.bonusPercent = parse_double(*v, a.bonusPercent);
                if (auto v = find_value("awarded")) a.awardedAt = parse_int64(*v, 0);
                if (auto v = find_value("awardedAt")) a.awardedAt = parse_int64(*v, a.awardedAt);
                if (auto v = find_value("icon")) a.icon = *v;

                std::int64_t durationDays = -1;
                if (auto v = find_value("durationDays")) durationDays = parse_int64(*v, -1);
                if (durationDays > 0) {
                    if (a.awardedAt <= 0) a.awardedAt = nowSec;
                    a.expiresAt = a.awardedAt + durationDays * 24 * 3600;
                } else if (durationDays == 0) {
                    a.expiresAt = 0;
                } else {
                    if (auto v = find_value("expires")) a.expiresAt = parse_int64(*v, 0);
                    if (auto v = find_value("expiresAt")) a.expiresAt = parse_int64(*v, a.expiresAt);
                }
                normalize_achievement_times(a, nowSec);
                result.push_back(std::move(a));
            }
            return result;
        }

        void save_achievements(const std::filesystem::path& baseDir, const std::string& id, const std::vector<Achievement>& items) {
            auto dir = baseDir / "achievements";
            std::filesystem::create_directories(dir);
            auto path = dir / (id + ".json");
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out.imbue(std::locale::classic());
            auto normalized = items;
            const auto nowSec = now_seconds();
            for (auto& a : normalized) {
                normalize_achievement_times(a, nowSec);
            }
            out << "[\n";
            for (size_t i = 0; i < normalized.size(); ++i) {
                const auto& a = normalized[i];
                std::int64_t durationDays = 0;
                if (a.expiresAt > 0 && a.awardedAt > 0) {
                    durationDays = (a.expiresAt - a.awardedAt) / (24 * 3600);
                }
                out << "  {\"title\":\"" << json_escape(a.title) << "\",\"skill\":\"" << json_escape(a.skill)
                    << "\",\"bonus\":" << a.bonusPercent << ",\"awarded\":" << a.awardedAt
                    << ",\"durationDays\":" << durationDays << ",\"expires\":" << a.expiresAt
                    << ",\"icon\":\"" << json_escape(a.icon) << "\"}";
                if (i + 1 < normalized.size()) out << ",";
                out << "\n";
            }
            out << "]";
        }

        bool write_all(const std::filesystem::path& p, const std::string& data) {
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
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return !ec;
}

bool is_numeric_id(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::string generate_id(int value, int width = 4) {
    std::ostringstream ss;
    ss << std::setw(width) << std::setfill('0') << value;
    return ss.str();
}

std::string file_profile_name(const std::filesystem::path& file) {
    if (is_profile_json_path(file)) {
        std::string content = read_all(file);
        if (!content.empty()) {
            JsonLite::Value root;
            if (JsonLite::Parse(content, root, nullptr)) {
                if (const auto* profile = JsonLite::GetObjectValue(root, "profile")) {
                    if (const auto* name = JsonLite::GetObjectValue(*profile, "name")) {
                        std::string value = JsonLite::GetString(*name);
                        if (!value.empty()) return value;
                    }
                }
                if (const auto* name = JsonLite::GetObjectValue(root, "name")) {
                    std::string value = JsonLite::GetString(*name);
                    if (!value.empty()) return value;
                }
            }
        }
        return file.stem().string();
    }
    std::ifstream in(file);
    if (!in) return file.stem().string();
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        auto t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(t.substr(0, eq));
        auto val = trim(t.substr(eq + 1));
        if (section == "profile" && key == "name" && !val.empty()) {
            return val;
        }
    }
    return file.stem().string();
}

} // namespace

class FileStorage : public IJobStorage {
public:
    explicit FileStorage(std::filesystem::path baseDir)
        : baseDir_(std::move(baseDir)) {
        normalize_directory(baseDir_);
        normalize_directory(archive_dir());
        nextId_ = compute_next_id();
        migrate_legacy_profiles();
        nextId_ = compute_next_id();
    }

    bool set_active_profile(const std::string& id) override {
        if (id.empty()) return false;
        auto path = find_profile_path(id, /*includeArchived*/false);
        if (!path) return false;
        activeId_ = id;
        activePath_ = *path;
        token_.reset();
        queue_.clear();
        return true;
    }

    std::vector<ProfileInfo> list_profiles() override {
        std::vector<ProfileInfo> out;
        list_dir(baseDir_, false, out);
        list_dir(archive_dir(), true, out);
        std::sort(out.begin(), out.end(), [](const ProfileInfo& a, const ProfileInfo& b) {
            return a.id < b.id;
        });
        return out;
    }

    std::optional<Profile> load_profile() override {
        if (!is_active() || !std::filesystem::exists(activePath_)) return std::nullopt;

        auto txt = read_all(activePath_);
        if (txt.empty()) return std::nullopt;

        if (is_profile_json_path(activePath_)) {
            JsonLite::Value root;
            if (!JsonLite::Parse(txt, root, nullptr)) return std::nullopt;

            auto get_obj = [&](const JsonLite::Value& obj, const char* key) -> const JsonLite::Value* {
                return JsonLite::GetObjectValue(obj, key);
            };
            const JsonLite::Value* profileObj = get_obj(root, "profile");
            if (!profileObj || profileObj->type != JsonLite::Type::Object) {
                profileObj = &root;
            }

            std::string name;
            int storedOverall = -1;
            int storedTotalXp = -1;
            int storedProgress = -1;
            bool storedAdmin = false;
            std::int64_t storedLastTask = 0;
            int storedInertiaTasks = 0;
            int storedRecoveryTasks = 0;
            int storedTasksCompleted = 0;

            if (const auto* v = get_obj(*profileObj, "name")) name = JsonLite::GetString(*v);
            if (const auto* v = get_obj(*profileObj, "overall")) storedOverall = JsonLite::GetInt(*v, storedOverall);
            if (const auto* v = get_obj(*profileObj, "totalXp")) storedTotalXp = JsonLite::GetInt(*v, storedTotalXp);
            if (const auto* v = get_obj(*profileObj, "progress")) storedProgress = JsonLite::GetInt(*v, storedProgress);
            if (const auto* v = get_obj(*profileObj, "admin")) storedAdmin = JsonLite::GetBool(*v, storedAdmin);
            if (const auto* v = get_obj(*profileObj, "lastTaskTs")) storedLastTask = JsonLite::GetInt64(*v, storedLastTask);
            if (const auto* v = get_obj(*profileObj, "inertiaTasks")) storedInertiaTasks = JsonLite::GetInt(*v, storedInertiaTasks);
            if (const auto* v = get_obj(*profileObj, "recoveryTasks")) storedRecoveryTasks = JsonLite::GetInt(*v, storedRecoveryTasks);
            if (const auto* v = get_obj(*profileObj, "tasksCompleted")) storedTasksCompleted = JsonLite::GetInt(*v, storedTasksCompleted);

            std::optional<std::string> token;
            if (const auto* authObj = get_obj(root, "auth")) {
                if (const auto* v = get_obj(*authObj, "token")) {
                    token = JsonLite::GetString(*v);
                }
            } else if (const auto* v = get_obj(root, "token")) {
                token = JsonLite::GetString(*v);
            }

            std::vector<XpEvent> queue;
            if (const auto* q = get_obj(root, "queue")) {
                if (q->type == JsonLite::Type::Array) {
                    for (const auto& item : q->arrayValue) {
                        if (item.type != JsonLite::Type::Object) continue;
                        const auto* skill = get_obj(item, "skill");
                        const auto* amount = get_obj(item, "amount");
                        if (!skill || !amount) continue;
                        std::string skillId = JsonLite::GetString(*skill);
                        int amt = JsonLite::GetInt(*amount, 0);
                        if (!skillId.empty() && amt > 0) queue.push_back({skillId, amt});
                    }
                }
            }

            std::array<int, Profile::kCategoryCount> categoryScores{};
            categoryScores.fill(0);
            std::array<int, Profile::kCategoryCount> categoryCooldowns{};
            categoryCooldowns.fill(10);
            if (const auto* cats = get_obj(root, "categories")) {
                if (const auto* scores = get_obj(*cats, "scores")) {
                    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                        if (const auto* v = get_obj(*scores, Profile::kCategoryLabels[idx])) {
                            int score = JsonLite::GetInt(*v, 0);
                            score = std::clamp(score, 0, Profile::kMaxCategoryScore);
                            categoryScores[idx] = score;
                        }
                    }
                }
                if (const auto* cooldowns = get_obj(*cats, "cooldowns")) {
                    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                        if (const auto* v = get_obj(*cooldowns, Profile::kCategoryLabels[idx])) {
                            categoryCooldowns[idx] = JsonLite::GetInt(*v, categoryCooldowns[idx]);
                        }
                    }
                }
            }

            std::vector<Skill> restored;
            if (const auto* skills = get_obj(root, "skills")) {
                if (skills->type == JsonLite::Type::Array) {
                    for (const auto& item : skills->arrayValue) {
                        if (item.type != JsonLite::Type::Object) continue;
                        std::string skillId;
                        if (const auto* v = get_obj(item, "id")) {
                            skillId = JsonLite::GetString(*v);
                        } else if (const auto* v = get_obj(item, "name")) {
                            skillId = JsonLite::GetString(*v);
                        }
                        if (skillId.empty()) continue;
                        int level = 1;
                        if (const auto* v = get_obj(item, "level")) level = std::max(1, JsonLite::GetInt(*v, 1));
                        double weight = 1.0;
                        if (const auto* v = get_obj(item, "weight")) weight = JsonLite::GetDouble(*v, 1.0);
                        Skill skill(skillId, level, weight);
                        if (const auto* v = get_obj(item, "xp")) skill.xp = std::max(0, JsonLite::GetInt(*v, 0));
                        if (const auto* v = get_obj(item, "xpToNext")) {
                            int next = JsonLite::GetInt(*v, 0);
                            skill.xpToNext = next > 0 ? next : Skill::required_xp_for(skill.level + 1);
                        } else {
                            skill.xpToNext = Skill::required_xp_for(skill.level + 1);
                        }
                        restored.push_back(skill);
                    }
                }
            }

            if (name.empty()) name = file_profile_name(activePath_);
            if (name.empty()) name = activeId_;

            Profile profile(name);
            profile.set_skills(restored);
            if (storedTotalXp >= 0) {
                profile.set_total_xp(storedTotalXp);
            } else if (storedOverall > 0 && storedProgress >= 0) {
                profile.set_level_and_progress(storedOverall, storedProgress);
            } else {
                if (storedOverall > 0) profile.set_overall_level(storedOverall);
                if (storedProgress >= 0) profile.set_level_progress(storedProgress);
            }
            profile.set_category_best_scores(categoryScores);
            profile.set_category_cooldowns(categoryCooldowns);
            profile.set_last_task_timestamp(storedLastTask);
            profile.set_inactivity_tasks(storedInertiaTasks);
            profile.start_penalty_recovery(storedRecoveryTasks);
            profile.set_tasks_completed(storedTasksCompleted);
            profile.set_admin(storedAdmin);
            profile.set_achievements(load_achievements(baseDir_, activeId_));

            token_ = token;
            queue_ = std::move(queue);
            return profile;
        }

        std::istringstream in(txt);
        std::string line;
        std::string section;
        std::string name;
        int storedOverall = -1;
        int storedTotalXp = -1;
        int storedProgress = -1;
        bool storedAdmin = false;
        std::vector<std::string> skillNames;
        std::vector<XpEvent> queue;
        std::optional<std::string> token;
        std::unordered_map<std::string, int> levelBySkill;
        std::unordered_map<std::string, int> xpBySkill;
        std::unordered_map<std::string, int> xpNextBySkill;
        std::unordered_map<std::string, double> weightBySkill;
        std::array<int, Profile::kCategoryCount> categoryScores{};
        categoryScores.fill(0);
        std::array<int, Profile::kCategoryCount> categoryCooldowns{};
        categoryCooldowns.fill(10);
        std::int64_t storedLastTask = 0;
        int storedInertiaTasks = 0;
        int storedRecoveryTasks = 0;
        int storedTasksCompleted = 0;

        while (std::getline(in, line)) {
            auto t = trim(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            if (t.front() == '[' && t.back() == ']') {
                section = t.substr(1, t.size() - 2);
                continue;
            }
            auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            auto key = trim(t.substr(0, eq));
            auto val = trim(t.substr(eq + 1));

            if (section == "auth") {
                if (key == "token") token = val;
            } else if (section == "profile") {
                if (key == "name") name = val;
                else if (key == "overall") {
                    storedOverall = parse_int(val, -1);
                } else if (key == "totalXp" || key == "totalXP") {
                    storedTotalXp = parse_int(val, -1);
                } else if (key == "progress") {
                    storedProgress = parse_int(val, -1);
                } else if (key == "admin") {
                    storedAdmin = parse_int(val, 0) != 0;
                } else if (key == "lastTaskTs") {
                    try { storedLastTask = std::stoll(sanitize_int(val)); } catch (...) {}
                } else if (key == "inertiaTasks") {
                    storedInertiaTasks = parse_int(val, 0);
                } else if (key == "recoveryTasks") {
                    storedRecoveryTasks = parse_int(val, 0);
                } else if (key == "tasksCompleted") {
                    storedTasksCompleted = parse_int(val, 0);
                }
            } else if (section == "skills") {
                if (key == "names") {
                    skillNames.clear();
                    std::istringstream ss(val);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item = trim(item);
                        if (!item.empty()) skillNames.push_back(item);
                    }
                } else if (key.rfind("level_", 0) == 0) {
                    auto sk = key.substr(6);
                    levelBySkill[sk] = parse_int(val, 0);
                } else if (key.rfind("xp_", 0) == 0) {
                    auto sk = key.substr(3);
                    xpBySkill[sk] = parse_int(val, 0);
                } else if (key.rfind("xpToNext_", 0) == 0) {
                    auto sk = key.substr(9);
                    xpNextBySkill[sk] = parse_int(val, 0);
                } else if (key.rfind("weight_", 0) == 0) {
                    auto sk = key.substr(7);
                    weightBySkill[sk] = parse_double(val, 0.0);
                }
            } else if (section == "queue" && key == "items") {
                queue.clear();
                std::istringstream ss(val);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    auto col = item.find(':');
                    if (col == std::string::npos) continue;
                    std::string skill = trim(item.substr(0, col));
                    std::string amt = trim(item.substr(col + 1));
                    int amount = 0;
                    try { amount = std::stoi(amt); } catch (...) { amount = 0; }
                    if (!skill.empty() && amount > 0) queue.push_back({skill, amount});
                }
            } else if (section == "categories") {
                if (key.rfind("score_", 0) == 0) {
                    auto label = key.substr(6);
                    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                        if (label == Profile::kCategoryLabels[idx]) {
                            int score = 0;
                            score = parse_int(val, 0);
                            if (score < 0) score = 0;
                            if (score > Profile::kMaxCategoryScore) score = Profile::kMaxCategoryScore;
                            categoryScores[idx] = score;
                            break;
                        }
                    }
                } else if (key.rfind("cooldown_", 0) == 0) {
                    auto label = key.substr(9);
                    for (size_t idx = 0; idx < Profile::kCategoryCount; ++idx) {
                        if (label == Profile::kCategoryLabels[idx]) {
                            int value = 0;
                            value = parse_int(val, 0);
                            categoryCooldowns[idx] = value;
                            break;
                        }
                    }
                }
            }
        }

        if (name.empty()) name = file_profile_name(activePath_);
        if (name.empty()) name = activeId_;

        Profile profile(name);
        std::vector<Skill> restored;
        restored.reserve(skillNames.size());
        for (const auto& skillName : skillNames) {
            int level = 1;
            if (auto it = levelBySkill.find(skillName); it != levelBySkill.end() && it->second > 0) {
                level = it->second;
            }
            double weight = 1.0;
            if (auto it = weightBySkill.find(skillName); it != weightBySkill.end() && it->second > 0.0) {
                weight = it->second;
            }
            Skill skill(skillName, level, weight);
            if (auto it = xpBySkill.find(skillName); it != xpBySkill.end() && it->second >= 0) {
                skill.xp = it->second;
            }
            if (auto it = xpNextBySkill.find(skillName); it != xpNextBySkill.end() && it->second > 0) {
                skill.xpToNext = it->second;
            } else {
                skill.xpToNext = Skill::required_xp_for(skill.level + 1);
            }
            restored.push_back(skill);
        }
        profile.set_skills(restored);
        if (storedTotalXp >= 0) {
            profile.set_total_xp(storedTotalXp);
        } else if (storedOverall > 0 && storedProgress >= 0) {
            profile.set_level_and_progress(storedOverall, storedProgress);
        } else {
            if (storedOverall > 0) profile.set_overall_level(storedOverall);
            if (storedProgress >= 0) profile.set_level_progress(storedProgress);
        }
        profile.set_category_best_scores(categoryScores);
        profile.set_category_cooldowns(categoryCooldowns);
        profile.set_last_task_timestamp(storedLastTask);
        profile.set_inactivity_tasks(storedInertiaTasks);
        profile.start_penalty_recovery(storedRecoveryTasks);
        profile.set_tasks_completed(storedTasksCompleted);
        profile.set_admin(storedAdmin);
        profile.set_achievements(load_achievements(baseDir_, activeId_));

        token_ = token;
        queue_ = std::move(queue);

        const std::filesystem::path legacyPath = activePath_;
        activePath_ = profile_json_path_for(activePath_);
        save_profile(profile);
        std::error_code ec;
        std::filesystem::remove(legacyPath, ec);
        return profile;
    }

    bool save_profile(const Profile& profile) override {
        if (!is_active()) return false;

        const std::filesystem::path jsonPath = is_profile_json_path(activePath_)
            ? activePath_
            : profile_json_path_for(activePath_);
        std::ostringstream ss;
        ss.imbue(std::locale::classic());
        ss << "{\n";
        ss << "  \"profile\": {";
        ss << "\"id\":\"" << JsonLite::Escape(activeId_) << "\",";
        ss << "\"name\":\"" << JsonLite::Escape(profile.name()) << "\",";
        ss << "\"overall\":" << profile.overall_level() << ",";
        ss << "\"progress\":" << profile.level_progress() << ",";
        ss << "\"totalXp\":" << profile.total_xp() << ",";
        ss << "\"admin\":" << (profile.is_admin() ? "true" : "false") << ",";
        ss << "\"lastTaskTs\":" << profile.last_task_timestamp() << ",";
        ss << "\"inertiaTasks\":" << profile.inactivity_tasks() << ",";
        ss << "\"recoveryTasks\":" << profile.recovery_tasks_remaining() << ",";
        ss << "\"tasksCompleted\":" << profile.tasks_completed();
        ss << "},\n";
        ss << "  \"auth\": {\"token\": \"";
        if (token_) ss << JsonLite::Escape(*token_);
        ss << "\"},\n";
        ss << "  \"skills\": [\n";
        auto skills = profile.list_skills();
        for (size_t i = 0; i < skills.size(); ++i) {
            const auto& s = skills[i];
            ss << "    {\"id\":\"" << JsonLite::Escape(s.name)
               << "\",\"level\":" << s.level
               << ",\"xp\":" << s.xp
               << ",\"xpToNext\":" << s.xpToNext
               << ",\"weight\":" << s.weight << "}";
            ss << (i + 1 < skills.size() ? ",\n" : "\n");
        }
        ss << "  ],\n";
        ss << "  \"categories\": {\n";
        ss << "    \"scores\": {";
        const auto& catScores = profile.category_best_scores();
        for (size_t idx = 0; idx < catScores.size(); ++idx) {
            ss << "\"" << Profile::kCategoryLabels[idx] << "\":" << catScores[idx];
            if (idx + 1 < catScores.size()) ss << ",";
        }
        ss << "},\n";
        ss << "    \"cooldowns\": {";
        const auto& cooldowns = profile.category_cooldowns();
        for (size_t idx = 0; idx < cooldowns.size(); ++idx) {
            ss << "\"" << Profile::kCategoryLabels[idx] << "\":" << cooldowns[idx];
            if (idx + 1 < cooldowns.size()) ss << ",";
        }
        ss << "}\n";
        ss << "  },\n";
        ss << "  \"queue\": [";
        for (size_t i = 0; i < queue_.size(); ++i) {
            ss << "{\"skill\":\"" << JsonLite::Escape(queue_[i].skill)
               << "\",\"amount\":" << queue_[i].amount << "}";
            if (i + 1 < queue_.size()) ss << ",";
        }
        ss << "]\n";
        ss << "}\n";

        bool ok = write_all(jsonPath, ss.str());
        if (ok && is_profile_ini_path(activePath_)) {
            std::error_code ec;
            std::filesystem::remove(activePath_, ec);
            activePath_ = jsonPath;
        }
        save_achievements(baseDir_, activeId_, profile.achievements());
        return ok;
    }

    std::optional<ProfileInfo> create_profile(const Profile& profile) override {
        const std::string id = generate_id(nextId_++);
        activeId_ = id;
        activePath_ = baseDir_ / (id + ".json");
        token_.reset();
        queue_.clear();
        if (!save_profile(profile)) {
            activeId_.clear();
            activePath_.clear();
            return std::nullopt;
        }
        return ProfileInfo{id, profile.name(), false};
    }

    bool set_archived(const std::string& id, bool archived) override {
        auto current = find_profile_path(id, /*includeArchived*/true);
        if (!current) return false;
        const bool currentlyArchived = current->parent_path() == archive_dir();
        if (currentlyArchived == archived) return true;

        auto targetDir = archived ? archive_dir() : baseDir_;
        std::error_code ec;
        std::filesystem::create_directories(targetDir, ec);
        if (ec) return false;

        auto target = targetDir / current->filename();
        std::filesystem::rename(*current, target, ec);
        if (ec) return false;

        if (activeId_ == id) {
            if (archived) {
                activeId_.clear();
                activePath_.clear();
            } else {
                activePath_ = target;
            }
            token_.reset();
            queue_.clear();
        }
        return true;
    }

    bool delete_profile(const std::string& id) override {
        auto current = find_profile_path(id, /*includeArchived*/true);
        if (!current) return false;
        std::error_code ec;
        std::filesystem::remove(*current, ec);
        std::filesystem::remove(current->parent_path() / (id + ".json"), ec);
        std::filesystem::remove(current->parent_path() / (id + ".ini"), ec);
        if (ec) return false;
        std::error_code achEc;
        auto achPath = baseDir_ / "achievements" / (id + ".json");
        std::filesystem::remove(achPath, achEc);
        if (activeId_ == id) {
            activeId_.clear();
            activePath_.clear();
            token_.reset();
            queue_.clear();
        }
        return true;
    }

    std::optional<std::string> load_token() override {
        if (!is_active()) return std::nullopt;
        if (token_) return token_;
        if (!std::filesystem::exists(activePath_)) return std::nullopt;
        auto txt = read_all(activePath_);
        if (txt.empty()) return std::nullopt;
        std::istringstream in(txt);
        std::string line;
        std::string section;
        while (std::getline(in, line)) {
            auto t = trim(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            if (t.front() == '[' && t.back() == ']') {
                section = t.substr(1, t.size() - 2);
                continue;
            }
            if (section != "auth") continue;
            auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            auto key = trim(t.substr(0, eq));
            auto val = trim(t.substr(eq + 1));
            if (key == "token" && !val.empty()) {
                token_ = val;
                return token_;
            }
        }
        return std::nullopt;
    }

    bool save_token(const std::string& token) override {
        if (!is_active()) return false;
        token_ = token;
        if (auto profile = load_profile()) {
            token_ = token;
            return save_profile(*profile);
        }
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"auth\": {\"token\": \"" << JsonLite::Escape(token) << "\"},\n";
        ss << "  \"profile\": {\"id\": \"" << JsonLite::Escape(activeId_) << "\",\"name\": \""
           << JsonLite::Escape(activeId_) << "\",\"overall\": 1}\n";
        ss << "}\n";
        auto target = is_profile_json_path(activePath_) ? activePath_ : profile_json_path_for(activePath_);
        const bool ok = write_all(target, ss.str());
        if (ok && is_profile_ini_path(activePath_)) {
            std::error_code ec;
            std::filesystem::remove(activePath_, ec);
            activePath_ = target;
        }
        return ok;
    }

    std::vector<XpEvent> load_queue() override {
        return queue_;
    }

    bool save_queue(const std::vector<XpEvent>& q) override {
        queue_ = q;
        return true;
    }

private:
    bool is_active() const { return !activeId_.empty() && !activePath_.empty(); }

    std::filesystem::path archive_dir() const { return baseDir_ / kArchiveFolder; }

    void list_dir(const std::filesystem::path& dir, bool archived, std::vector<ProfileInfo>& out) const {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return;
        std::unordered_set<std::string> jsonIds;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!is_profile_file(entry)) continue;
            if (!is_profile_json_path(entry.path())) continue;
            auto id = entry.path().stem().string();
            jsonIds.insert(id);
            out.push_back(ProfileInfo{id, file_profile_name(entry.path()), archived});
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!is_profile_file(entry)) continue;
            if (!is_profile_ini_path(entry.path())) continue;
            auto id = entry.path().stem().string();
            if (jsonIds.count(id)) continue;
            out.push_back(ProfileInfo{id, file_profile_name(entry.path()), archived});
        }
    }

    std::optional<std::filesystem::path> find_profile_path(const std::string& id, bool includeArchived) const {
        auto path = baseDir_ / (id + ".json");
        if (std::filesystem::exists(path)) return path;
        auto legacy = baseDir_ / (id + ".ini");
        if (std::filesystem::exists(legacy)) return legacy;
        if (includeArchived) {
            auto arch = archive_dir() / (id + ".json");
            if (std::filesystem::exists(arch)) return arch;
            auto legacyArch = archive_dir() / (id + ".ini");
            if (std::filesystem::exists(legacyArch)) return legacyArch;
        }
        return std::nullopt;
    }

    void normalize_directory(const std::filesystem::path& dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return;
        auto rename_achievement = [&](const std::string& fromId, const std::string& toId) {
            std::error_code achEc;
            auto achDir = baseDir_ / "achievements";
            auto from = achDir / (fromId + ".json");
            auto to = achDir / (toId + ".json");
            if (!std::filesystem::exists(from, achEc)) return;
            if (std::filesystem::exists(to, achEc)) {
                std::filesystem::remove(to, achEc);
            }
            std::filesystem::rename(from, to, achEc);
        };
        int maxId = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!is_profile_file(entry)) continue;
            auto stem = entry.path().stem().string();
            if (is_numeric_id(stem)) {
                try { maxId = std::max(maxId, std::stoi(stem)); } catch (...) {}
                continue;
            }
            const std::string newId = generate_id(++maxId);
            auto target = entry.path().parent_path() / (newId + entry.path().extension().string());
            rename_achievement(stem, newId);
            std::filesystem::rename(entry.path(), target, ec);
        }
        if (maxId >= nextId_) nextId_ = maxId + 1;
    }

    int compute_next_id() const {
        int maxId = 0;
        std::error_code ec;
        if (std::filesystem::exists(baseDir_, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(baseDir_, ec)) {
                if (!is_profile_file(entry)) continue;
                auto stem = entry.path().stem().string();
                if (is_numeric_id(stem)) {
                    try { maxId = std::max(maxId, std::stoi(stem)); } catch (...) {}
                }
            }
        }
        auto arch = archive_dir();
        if (std::filesystem::exists(arch, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(arch, ec)) {
                if (!is_profile_file(entry)) continue;
                auto stem = entry.path().stem().string();
                if (is_numeric_id(stem)) {
                    try { maxId = std::max(maxId, std::stoi(stem)); } catch (...) {}
                }
            }
        }
        return maxId + 1;
    }

    void migrate_legacy_profiles() {
        std::vector<std::filesystem::path> legacy;
        auto collect = [&](const std::filesystem::path& dir) {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec)) return;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".ini") continue;
                legacy.push_back(entry.path());
            }
        };
        collect(baseDir_);
        collect(archive_dir());
        if (legacy.empty()) return;

        const std::string prevId = activeId_;
        const std::filesystem::path prevPath = activePath_;
        const std::optional<std::string> prevToken = token_;
        const std::vector<XpEvent> prevQueue = queue_;

        for (const auto& path : legacy) {
            activeId_ = path.stem().string();
            activePath_ = path;
            token_.reset();
            queue_.clear();
            load_profile();
        }

        activeId_ = prevId;
        activePath_ = prevPath;
        token_ = prevToken;
        queue_ = prevQueue;
    }

    std::filesystem::path baseDir_;
    std::string activeId_;
    std::filesystem::path activePath_;
    std::optional<std::string> token_;
    std::vector<XpEvent> queue_;
    int nextId_ = 1;
};

IJobStorage* CreateFileStorage(const std::filesystem::path& dir) { return new FileStorage(dir); }
