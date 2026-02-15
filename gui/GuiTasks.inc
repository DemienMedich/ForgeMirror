int CountSelectedProfileShares(const std::vector<TaskProfileShare>& shares);
void BalanceProfileShares(std::vector<TaskProfileShare>& shares, int fixedIndex);

static constexpr int kTaskStatusNew = 0;
static constexpr int kTaskStatusInProgress = 1;
static constexpr int kTaskStatusDone = 2;

static constexpr int kTaskPriorityLow = 0;
static constexpr int kTaskPriorityMedium = 1;
static constexpr int kTaskPriorityHigh = 2;
static constexpr int kTaskPriorityCritical = 3;

static int NormalizeTaskStatus(int value) {
    return std::clamp(value, kTaskStatusNew, kTaskStatusDone);
}

static const char* TaskStatusLabel(int status) {
    switch (NormalizeTaskStatus(status)) {
        case kTaskStatusInProgress: return u8"В работе";
        case kTaskStatusDone: return u8"Выполнена";
        default: return u8"Новая";
    }
}

static int NormalizeTaskPriority(int value) {
    return std::clamp(value, kTaskPriorityLow, kTaskPriorityCritical);
}

static const char* TaskPriorityLabel(int priority) {
    switch (NormalizeTaskPriority(priority)) {
        case kTaskPriorityLow: return u8"Низкий";
        case kTaskPriorityHigh: return u8"Высокий";
        case kTaskPriorityCritical: return u8"Критический";
        default: return u8"Средний";
    }
}

static std::filesystem::path TasksStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "tasks.json";
}

static std::filesystem::path TaskAuditStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "task-audit.log";
}

static std::string tasks_sanitize_log_token(std::string value) {
    for (char& ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '|') ch = ' ';
    }
    return TrimStringGui(value);
}

static std::int64_t tasks_parse_log_ts(const std::string& value) {
    try {
        return std::stoll(value);
    } catch (...) {
        return 0;
    }
}

static std::vector<TaskAuditEntry> LoadTaskAudit(const std::filesystem::path& storageDir, size_t maxEntries = 200) {
    std::vector<TaskAuditEntry> out;
    std::ifstream in(TaskAuditStoragePath(storageDir), std::ios::binary);
    if (!in) return out;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (firstLine) {
            StripUtf8Bom(line);
            firstLine = false;
        }
        std::string trimmed = TrimStringGui(line);
        if (trimmed.empty()) continue;
        std::array<std::string, 6> parts{};
        size_t start = 0;
        int idx = 0;
        while (idx < 5) {
            size_t sep = trimmed.find('|', start);
            if (sep == std::string::npos) break;
            parts[static_cast<size_t>(idx)] = trimmed.substr(start, sep - start);
            start = sep + 1;
            ++idx;
        }
        if (idx != 5 || start > trimmed.size()) continue;
        parts[5] = trimmed.substr(start);
        TaskAuditEntry entry;
        entry.timestamp = tasks_parse_log_ts(parts[0]);
        entry.actor = parts[1];
        entry.taskId = parts[2];
        entry.field = parts[3];
        entry.oldValue = parts[4];
        entry.newValue = parts[5];
        if (entry.taskId.empty() || entry.field.empty()) continue;
        out.push_back(std::move(entry));
    }
    if (maxEntries > 0 && out.size() > maxEntries) {
        out.erase(out.begin(), out.end() - maxEntries);
    }
    return out;
}

static bool AppendTaskAudit(const std::filesystem::path& storageDir,
                            const std::string& actor,
                            const std::string& taskId,
                            const std::string& field,
                            const std::string& oldValue,
                            const std::string& newValue,
                            std::vector<TaskAuditEntry>* cache = nullptr) {
    const std::string safeTaskId = tasks_sanitize_log_token(taskId);
    const std::string safeField = tasks_sanitize_log_token(field);
    if (safeTaskId.empty() || safeField.empty()) return false;

    std::error_code ec;
    const auto path = TaskAuditStoragePath(storageDir);
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return false;

    TaskAuditEntry entry;
    entry.timestamp = NowSeconds();
    entry.actor = tasks_sanitize_log_token(actor);
    entry.taskId = safeTaskId;
    entry.field = safeField;
    entry.oldValue = tasks_sanitize_log_token(oldValue);
    entry.newValue = tasks_sanitize_log_token(newValue);

    out << entry.timestamp << "|"
        << entry.actor << "|"
        << entry.taskId << "|"
        << entry.field << "|"
        << entry.oldValue << "|"
        << entry.newValue << "\n";
    if (!out.good()) return false;

    if (cache) {
        cache->push_back(entry);
        constexpr size_t kTaskAuditMaxEntries = 200;
        if (cache->size() > kTaskAuditMaxEntries) {
            cache->erase(cache->begin(), cache->end() - kTaskAuditMaxEntries);
        }
    }
    return true;
}

static std::string tasks_read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string data = ss.str();
    // Strip UTF-8 BOM if present
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.erase(0, 3);
    }
    return data;
}

static bool tasks_write_all(const std::filesystem::path& p, const std::string& data) {
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

static std::string tasks_json_escape(const std::string& value) {
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

static void tasks_skip_ws(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

static bool tasks_parse_string(const std::string& text, size_t& pos, std::string& out) {
    tasks_skip_ws(text, pos);
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

static bool tasks_parse_value_token(const std::string& text, size_t& pos, std::string& out) {
    tasks_skip_ws(text, pos);
    if (pos >= text.size()) return false;
    if (text[pos] == '"') {
        return tasks_parse_string(text, pos, out);
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

static bool tasks_parse_object(const std::string& text, size_t& pos,
                               std::unordered_map<std::string, std::string>& out) {
    tasks_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    ++pos;
    while (pos < text.size()) {
        tasks_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        std::string key;
        if (!tasks_parse_string(text, pos, key)) return false;
        tasks_skip_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        ++pos;
        std::string value;
        if (!tasks_parse_value_token(text, pos, value)) return false;
        out[key] = value;
        tasks_skip_ws(text, pos);
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

static std::vector<std::unordered_map<std::string, std::string>> tasks_parse_object_array(const std::string& text) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    size_t pos = 0;
    tasks_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '[') return objects;
    ++pos;
    while (pos < text.size()) {
        tasks_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        std::unordered_map<std::string, std::string> obj;
        if (!tasks_parse_object(text, pos, obj)) break;
        objects.push_back(std::move(obj));
        tasks_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return objects;
}

static int tasks_parse_int(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

static std::int64_t tasks_parse_int64(const std::string& value, std::int64_t fallback = 0) {
    try {
        return std::stoll(value);
    } catch (...) {
        return fallback;
    }
}

static std::string SerializeParticipants(const std::vector<TaskParticipant>& participants) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    bool first = true;
    for (const auto& p : participants) {
        if (!first) out << ';';
        first = false;
        out << p.profileId << "|" << p.percent << "|" << p.globalXp << "|" << p.skillXp;
    }
    return out.str();
}

static std::vector<TaskParticipant> ParseParticipants(const std::string& text) {
    std::vector<TaskParticipant> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        std::string token = text.substr(start, end - start);
        start = end + 1;
        if (token.empty()) continue;
        TaskParticipant p;
        size_t pos = 0;
        size_t sep = token.find('|', pos);
        if (sep == std::string::npos) continue;
        p.profileId = token.substr(0, sep);
        pos = sep + 1;
        sep = token.find('|', pos);
        if (sep == std::string::npos) continue;
        p.percent = tasks_parse_int(token.substr(pos, sep - pos), 0);
        pos = sep + 1;
        sep = token.find('|', pos);
        if (sep == std::string::npos) continue;
        p.globalXp = tasks_parse_int(token.substr(pos, sep - pos), 0);
        pos = sep + 1;
        p.skillXp = tasks_parse_int(token.substr(pos), 0);
        out.push_back(std::move(p));
    }
    return out;
}

static std::string SerializeAssignees(const std::vector<std::string>& assignees) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    bool first = true;
    for (const auto& id : assignees) {
        if (id.empty()) continue;
        if (!first) out << ';';
        first = false;
        out << id;
    }
    return out.str();
}

static std::vector<std::string> ParseAssignees(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        std::string token = TrimStringGui(text.substr(start, end - start));
        if (!token.empty()) {
            out.push_back(std::move(token));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return out;
}

static std::vector<TaskEntry> LoadTasks(const std::filesystem::path& storageDir) {
    std::vector<TaskEntry> out;
    const auto path = TasksStoragePath(storageDir);
    std::string content = tasks_read_all(path);
    if (content.empty()) return out;
    const auto objects = tasks_parse_object_array(content);
    for (const auto& obj : objects) {
        auto find_value = [&](const char* key) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            return it->second;
        };
        TaskEntry entry;
        if (auto v = find_value("id")) entry.id = *v;
        if (auto v = find_value("projectId")) entry.projectId = *v;
        if (auto v = find_value("project")) entry.project = *v;
        if (auto v = find_value("title")) entry.title = *v;
        if (auto v = find_value("description")) entry.description = *v;
        if (auto v = find_value("deadlineAt")) entry.deadlineAt = tasks_parse_int64(*v, 0);
        bool hasStatus = false;
        if (auto v = find_value("status")) {
            entry.status = NormalizeTaskStatus(tasks_parse_int(*v, kTaskStatusNew));
            hasStatus = true;
        }
        if (auto v = find_value("priority")) {
            entry.priority = NormalizeTaskPriority(tasks_parse_int(*v, kTaskPriorityMedium));
        } else {
            entry.priority = kTaskPriorityMedium;
        }
        if (auto v = find_value("category")) entry.category = tasks_parse_int(*v, 0);
        if (auto v = find_value("score")) entry.score = tasks_parse_int(*v, 0);
        if (auto v = find_value("baseXp")) entry.baseXp = tasks_parse_int(*v, 0);
        if (auto v = find_value("basePool")) entry.basePool = tasks_parse_int(*v, 0);
        if (auto v = find_value("createdAt")) entry.createdAt = tasks_parse_int64(*v, 0);
        if (auto v = find_value("assignees")) entry.assignees = ParseAssignees(*v);
        if (auto v = find_value("participants")) entry.participants = ParseParticipants(*v);
        if (entry.id.empty()) continue;
        if (entry.assignees.empty()) {
            for (const auto& participant : entry.participants) {
                if (!participant.profileId.empty()) {
                    entry.assignees.push_back(participant.profileId);
                }
            }
        }
        if (!hasStatus) {
            entry.status = entry.participants.empty() ? kTaskStatusNew : kTaskStatusDone;
        } else if (entry.status < kTaskStatusNew || entry.status > kTaskStatusDone) {
            entry.status = entry.participants.empty() ? kTaskStatusNew : kTaskStatusDone;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

static bool SaveTasks(const std::filesystem::path& storageDir, const std::vector<TaskEntry>& tasks) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& entry = tasks[i];
        out << "  {\"id\":\"" << tasks_json_escape(entry.id)
            << "\",\"projectId\":\"" << tasks_json_escape(entry.projectId)
            << "\",\"project\":\"" << tasks_json_escape(entry.project)
            << "\",\"title\":\"" << tasks_json_escape(entry.title)
            << "\",\"description\":\"" << tasks_json_escape(entry.description)
            << "\",\"deadlineAt\":" << entry.deadlineAt
            << ",\"status\":" << NormalizeTaskStatus(entry.status)
            << ",\"priority\":" << NormalizeTaskPriority(entry.priority)
            << ",\"category\":" << entry.category
            << ",\"score\":" << entry.score
            << ",\"baseXp\":" << entry.baseXp
            << ",\"basePool\":" << entry.basePool
            << ",\"createdAt\":" << entry.createdAt
            << ",\"assignees\":\"" << tasks_json_escape(SerializeAssignees(entry.assignees)) << "\""
            << ",\"participants\":\"" << tasks_json_escape(SerializeParticipants(entry.participants)) << "\"}";
        if (i + 1 < tasks.size()) out << ",";
        out << "\n";
    }
    out << "]";
    std::string data = out.str();
    // Write BOM for editors (Notepad) to recognize UTF-8
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    data.insert(0, reinterpret_cast<const char*>(bom), sizeof(bom));
    return tasks_write_all(TasksStoragePath(storageDir), data);
}

static const TaskParticipant* FindTaskParticipant(const TaskEntry& task, const std::string& profileId) {
    for (const auto& p : task.participants) {
        if (p.profileId == profileId) return &p;
    }
    return nullptr;
}

static bool TaskHasAssignee(const TaskEntry& task, const std::string& profileId) {
    if (profileId.empty()) return false;
    for (const auto& id : task.assignees) {
        if (id == profileId) return true;
    }
    return FindTaskParticipant(task, profileId) != nullptr;
}

static std::string TaskDisplayTitle(const TaskEntry& task) {
    if (!task.project.empty() && !task.title.empty()) {
        return task.project + " - " + task.title;
    }
    if (!task.title.empty()) return task.title;
    return task.project;
}

static std::string BuildParticipantLabel(const TaskEntry& task,
                                         const std::unordered_map<std::string, std::string>& names) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    bool first = true;
    if (!task.participants.empty()) {
        for (const auto& p : task.participants) {
            if (!first) out << ", ";
            first = false;
            auto it = names.find(p.profileId);
            const std::string label = (it == names.end()) ? p.profileId : it->second;
            out << label;
            if (p.percent > 0) {
                out << " " << p.percent << "%";
            }
        }
        return out.str();
    }
    for (const auto& profileId : task.assignees) {
        if (!first) out << ", ";
        first = false;
        auto it = names.find(profileId);
        out << ((it == names.end()) ? profileId : it->second);
    }
    return out.str();
}

static bool ExportTasksCsv(const GuiState& state, const std::vector<const TaskEntry*>& tasks,
                           const std::unordered_map<std::string, std::string>& names,
                           std::filesystem::path& outPath, int& exportedCount) {
    exportedCount = 0;
    if (tasks.empty()) return false;
    std::error_code ec;
    auto reportDir = state.storageDir / "meta" / "reports";
    std::filesystem::create_directories(reportDir, ec);
    if (ec) return false;
    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) stamp = std::to_string(NowSeconds());
    outPath = reportDir / ("tasks-" + stamp + ".csv");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    // UTF-8 BOM for spreadsheet tools
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    out << "Date,Deadline,Project,Task,Description,Status,Priority,Category,Score,Participants,BaseXP,BasePool\n";
    for (const TaskEntry* task : tasks) {
        if (!task) continue;
        ++exportedCount;
        std::string stampText = FormatTimestamp(task->createdAt, "%Y-%m-%d %H:%M");
        if (stampText.empty()) stampText = "-";
        const std::string participants = BuildParticipantLabel(*task, names);
        const std::string deadlineText = task->deadlineAt > 0 ? FormatTimestamp(task->deadlineAt, "%Y-%m-%d %H:%M") : std::string();
        out << CsvEscape(stampText) << ","
            << CsvEscape(deadlineText) << ","
            << CsvEscape(task->project) << ","
            << CsvEscape(task->title) << ","
            << CsvEscape(task->description) << ","
            << CsvEscape(TaskStatusLabel(task->status)) << ","
            << CsvEscape(TaskPriorityLabel(task->priority)) << ","
            << CsvEscape(ClassificationLabel(task->category)) << ","
            << task->score << ","
            << CsvEscape(participants) << ","
            << task->baseXp << ","
            << task->basePool << "\n";
    }
    return exportedCount > 0;
}

static bool ExportTasksTxt(const GuiState& state, const std::vector<const TaskEntry*>& tasks,
                           const std::unordered_map<std::string, std::string>& names,
                           std::filesystem::path& outPath, int& exportedCount) {
    exportedCount = 0;
    if (tasks.empty()) return false;
    std::error_code ec;
    auto reportDir = state.storageDir / "meta" / "reports";
    std::filesystem::create_directories(reportDir, ec);
    if (ec) return false;
    std::string stamp = FormatTimestamp(NowSeconds(), "%Y%m%d-%H%M%S");
    if (stamp.empty()) stamp = std::to_string(NowSeconds());
    outPath = reportDir / ("tasks-" + stamp + ".txt");
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    // UTF-8 BOM for editors
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    out << "Выполненные задачи\n";
    for (const TaskEntry* task : tasks) {
        if (!task) continue;
        ++exportedCount;
        std::string stampText = FormatTimestamp(task->createdAt, "%Y-%m-%d %H:%M");
        if (stampText.empty()) stampText = "-";
        const std::string participants = BuildParticipantLabel(*task, names);
        const std::string deadlineText = task->deadlineAt > 0 ? FormatTimestamp(task->deadlineAt, "%Y-%m-%d %H:%M") : std::string(u8"без дедлайна");
        out << "- " << stampText << " | "
            << SanitizeLogLine(deadlineText) << " | "
            << SanitizeLogLine(task->project) << " | "
            << SanitizeLogLine(task->title) << " | "
            << SanitizeLogLine(TaskStatusLabel(task->status)) << " | "
            << SanitizeLogLine(TaskPriorityLabel(task->priority)) << " | "
            << SanitizeLogLine(ClassificationLabel(task->category)) << " | "
            << task->score << "/10 | "
            << SanitizeLogLine(participants) << "\n";
        if (!task->description.empty()) {
            out << "  " << SanitizeLogLine(task->description) << "\n";
        }
    }
    return exportedCount > 0;
}

static std::string GenerateTaskId(std::int64_t nowSeconds, size_t index) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "task_" << nowSeconds << "_" << index;
    return out.str();
}

static void PrepareTaskShares(GuiState& state) {
    std::unordered_map<std::string, TaskProfileShare> existing;
    existing.reserve(state.taskShares.size());
    for (const auto& share : state.taskShares) {
        existing.emplace(share.id, share);
    }
    state.taskShares.clear();
    state.taskShares.reserve(state.profiles.size());
    for (const auto& info : state.profiles) {
        TaskProfileShare share;
        share.id = info.id;
        share.name = info.name;
        share.archived = info.archived;
        auto it = existing.find(info.id);
        if (it != existing.end()) {
            share.selected = it->second.selected;
            share.percent = it->second.percent;
        }
        if (share.archived) {
            share.selected = false;
            share.percent = 0;
        }
        state.taskShares.push_back(std::move(share));
    }
    bool anySelected = false;
    for (const auto& share : state.taskShares) {
        if (share.selected) {
            anySelected = true;
            break;
        }
    }
    if (!anySelected && state.active) {
        for (auto& share : state.taskShares) {
            if (share.id == state.active->id) {
                share.selected = true;
                share.percent = 100;
                break;
            }
        }
    }
    if (CountSelectedProfileShares(state.taskShares) > 0) {
        BalanceProfileShares(state.taskShares, 0);
    }
}
