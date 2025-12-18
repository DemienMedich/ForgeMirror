#include "IJobStorage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* kArchiveFolder = "archive";

bool is_profile_file(const std::filesystem::directory_entry& entry) {
    return entry.is_regular_file() && entry.path().extension() == ".ini";
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

int parse_int(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(sanitize_int(value));
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

std::string read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
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

        std::istringstream in(txt);
        std::string line;
        std::string section;
        std::string name;
        int storedOverall = -1;
        int storedTotalXp = -1;
        int storedProgress = -1;
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
                } else if (key == "lastTaskTs") {
                    try { storedLastTask = std::stoll(sanitize_int(val)); } catch (...) {}
                } else if (key == "inertiaTasks") {
                    storedInertiaTasks = parse_int(val, 0);
                } else if (key == "recoveryTasks") {
                    storedRecoveryTasks = parse_int(val, 0);
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

        token_ = token;
        queue_ = std::move(queue);
        return profile;
    }

    bool save_profile(const Profile& profile) override {
        if (!is_active()) return false;

        std::ostringstream ss;
        ss.imbue(std::locale::classic());
        ss << "[auth]\n";
        if (token_) ss << "token=" << *token_ << "\n";

        ss << "\n[profile]\n";
        ss << "id=" << activeId_ << "\n";
        ss << "name=" << profile.name() << "\n";
        ss << "overall=" << profile.overall_level() << "\n";
        ss << "progress=" << profile.level_progress() << "\n";
        ss << "totalXp=" << profile.total_xp() << "\n";
        ss << "lastTaskTs=" << profile.last_task_timestamp() << "\n";
        ss << "inertiaTasks=" << profile.inactivity_tasks() << "\n";
        ss << "recoveryTasks=" << profile.recovery_tasks_remaining() << "\n";

        ss << "\n[skills]\n";
        auto skills = profile.list_skills();
        ss << "names=";
        for (size_t i = 0; i < skills.size(); ++i) {
            if (i) ss << ",";
            ss << skills[i].name;
        }
        ss << "\n";
        for (const auto& s : skills) {
            ss << "level_" << s.name << "=" << s.level << "\n";
            ss << "xp_" << s.name << "=" << s.xp << "\n";
            ss << "xpToNext_" << s.name << "=" << s.xpToNext << "\n";
            ss << "weight_" << s.name << "=" << s.weight << "\n";
        }

        ss << "\n[categories]\n";
        const auto& catScores = profile.category_best_scores();
        const auto& cooldowns = profile.category_cooldowns();
        for (size_t idx = 0; idx < catScores.size(); ++idx) {
            ss << "score_" << Profile::kCategoryLabels[idx] << "=" << catScores[idx] << "\n";
        }
        for (size_t idx = 0; idx < cooldowns.size(); ++idx) {
            ss << "cooldown_" << Profile::kCategoryLabels[idx] << "=" << cooldowns[idx] << "\n";
        }

        ss << "\n[queue]\n";
        ss << "items=";
        for (size_t i = 0; i < queue_.size(); ++i) {
            if (i) ss << ",";
            ss << queue_[i].skill << ":" << queue_[i].amount;
        }
        ss << "\n";

        return write_all(activePath_, ss.str());
    }

    std::optional<ProfileInfo> create_profile(const Profile& profile) override {
        const std::string id = generate_id(nextId_++);
        activeId_ = id;
        activePath_ = baseDir_ / (id + ".ini");
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
        if (ec) return false;
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
            return save_profile(*profile);
        }
        std::ostringstream ss;
        ss << "[auth]\n";
        ss << "token=" << token << "\n\n[profile]\nid=" << activeId_ << "\nname=" << activeId_
           << "\noverall=1\n\n[skills]\nnames=\n\n[queue]\nitems=\n";
        return write_all(activePath_, ss.str());
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
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!is_profile_file(entry)) continue;
            auto id = entry.path().stem().string();
            out.push_back(ProfileInfo{id, file_profile_name(entry.path()), archived});
        }
    }

    std::optional<std::filesystem::path> find_profile_path(const std::string& id, bool includeArchived) const {
        auto path = baseDir_ / (id + ".ini");
        if (std::filesystem::exists(path)) return path;
        if (includeArchived) {
            auto arch = archive_dir() / (id + ".ini");
            if (std::filesystem::exists(arch)) return arch;
        }
        return std::nullopt;
    }

    void normalize_directory(const std::filesystem::path& dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return;
        int maxId = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!is_profile_file(entry)) continue;
            auto stem = entry.path().stem().string();
            if (is_numeric_id(stem)) {
                try { maxId = std::max(maxId, std::stoi(stem)); } catch (...) {}
                continue;
            }
            const std::string newId = generate_id(++maxId);
            auto target = entry.path().parent_path() / (newId + ".ini");
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

    std::filesystem::path baseDir_;
    std::string activeId_;
    std::filesystem::path activePath_;
    std::optional<std::string> token_;
    std::vector<XpEvent> queue_;
    int nextId_ = 1;
};

IJobStorage* CreateFileStorage(const std::filesystem::path& dir) { return new FileStorage(dir); }
