#include "AppTaskProjectService.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <locale>
#include <sstream>

namespace {

constexpr int kTaskStatusNew = 0;
constexpr int kTaskStatusInProgress = 1;
constexpr int kTaskStatusDone = 2;

constexpr int kTaskPriorityLow = 0;
constexpr int kTaskPriorityMedium = 1;
constexpr int kTaskPriorityHigh = 2;
constexpr int kTaskPriorityCritical = 3;

std::int64_t NowSecondsLocal() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::filesystem::path TasksStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "tasks.json";
}

std::filesystem::path ProjectsStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "projects.json";
}

std::filesystem::path TaskAuditStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "task-audit.log";
}

std::string TrimCopy(const std::string& input) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    std::string out = input;
    out.erase(out.begin(), std::find_if(out.begin(), out.end(), [&](unsigned char c) { return !is_space(c); }));
    out.erase(std::find_if(out.rbegin(), out.rend(), [&](unsigned char c) { return !is_space(c); }).base(), out.end());
    return out;
}

std::string SanitizeLogToken(std::string value) {
    for (char& ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '|') ch = ' ';
    }
    return TrimCopy(value);
}

std::string JsonEscape(const std::string& value) {
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

bool WriteAllUtf8Bom(const std::filesystem::path& path, const std::string& payloadWithoutBom) {
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::string data = payloadWithoutBom;
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    data.insert(0, reinterpret_cast<const char*>(bom), sizeof(bom));

    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out.good()) return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        ec.clear();
        std::filesystem::remove(path, ec);
        if (ec) {
            std::filesystem::remove(tmp);
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        return false;
    }
    return true;
}

std::string SerializeAssignees(const std::vector<std::string>& assignees) {
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

std::string SerializeParticipants(const std::vector<TaskParticipant>& participants) {
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

std::string ToLowerUtf8Copy(std::string s) {
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

std::string FormatDeadlineAudit(std::int64_t deadlineAt) {
    if (deadlineAt <= 0) return std::string(u8"—");
    std::time_t tv = static_cast<std::time_t>(deadlineAt);
    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &tv);
#else
    localtime_r(&tv, &localTm);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &localTm) == 0) {
        return std::string(u8"—");
    }
    return buf;
}

std::string ResolveProjectAuditLabel(const std::string& projectId,
                                     const std::string& fallback,
                                     const std::vector<ProjectEntry>* projects) {
    if (projects && !projectId.empty()) {
        for (const auto& project : *projects) {
            if (project.id == projectId) {
                return project.name;
            }
        }
    }
    if (!fallback.empty()) return fallback;
    return u8"Без проекта";
}

TaskEntry* FindTaskMutable(std::vector<TaskEntry>& tasks, const std::string& taskId) {
    auto it = std::find_if(tasks.begin(), tasks.end(), [&](const TaskEntry& item) { return item.id == taskId; });
    return (it == tasks.end()) ? nullptr : &(*it);
}

bool AppendTaskAuditIfChanged(const std::filesystem::path& storageDir,
                              const std::string& actor,
                              const std::string& taskId,
                              const std::string& field,
                              const std::string& oldValue,
                              const std::string& newValue,
                              std::vector<TaskAuditEntry>* cache) {
    if (oldValue == newValue) return true;
    return AppAppendTaskAudit(storageDir, actor, taskId, field, oldValue, newValue, cache);
}

std::string FormatAssignees(const std::vector<std::string>& ids) {
    if (ids.empty()) return std::string(u8"—");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    bool first = true;
    for (const auto& id : ids) {
        if (id.empty()) continue;
        if (!first) out << ", ";
        first = false;
        out << id;
    }
    const std::string value = out.str();
    return value.empty() ? std::string(u8"—") : value;
}

} // namespace

int AppNormalizeTaskStatus(int value) {
    return std::clamp(value, kTaskStatusNew, kTaskStatusDone);
}

int AppNormalizeTaskPriority(int value) {
    return std::clamp(value, kTaskPriorityLow, kTaskPriorityCritical);
}

bool AppIsTaskStatusTransitionAllowed(int fromStatus, int toStatus) {
    const int from = AppNormalizeTaskStatus(fromStatus);
    const int to = AppNormalizeTaskStatus(toStatus);
    if (from == to) return true;
    return !(from == kTaskStatusDone && to == kTaskStatusNew);
}

const char* AppTaskStatusLabel(int status) {
    switch (AppNormalizeTaskStatus(status)) {
        case kTaskStatusInProgress: return u8"В работе";
        case kTaskStatusDone: return u8"Выполнена";
        default: return u8"Новая";
    }
}

const char* AppTaskPriorityLabel(int priority) {
    switch (AppNormalizeTaskPriority(priority)) {
        case kTaskPriorityLow: return u8"Низкий";
        case kTaskPriorityHigh: return u8"Высокий";
        case kTaskPriorityCritical: return u8"Критический";
        default: return u8"Средний";
    }
}

bool AppParseTaskDeadlineInput(const std::string& input, std::int64_t& outTs) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if (std::sscanf(input.c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) != 5) {
        return false;
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t timeValue = std::mktime(&tm);
    if (timeValue <= 0) return false;
    std::tm verify{};
#if defined(_WIN32)
    localtime_s(&verify, &timeValue);
#else
    localtime_r(&timeValue, &verify);
#endif
    if (verify.tm_year != tm.tm_year || verify.tm_mon != tm.tm_mon || verify.tm_mday != tm.tm_mday ||
        verify.tm_hour != tm.tm_hour || verify.tm_min != tm.tm_min) {
        return false;
    }
    outTs = static_cast<std::int64_t>(timeValue);
    return true;
}

std::string AppGenerateTaskId(std::int64_t nowSeconds, size_t index) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "task_" << nowSeconds << "_" << index;
    return out.str();
}

std::string AppGenerateProjectId(const std::vector<ProjectEntry>& projects) {
    const std::int64_t now = NowSecondsLocal();
    int suffix = static_cast<int>(projects.size()) + 1;
    std::string base = "prj_" + std::to_string(now);
    auto exists = [&](const std::string& id) {
        return std::any_of(projects.begin(), projects.end(), [&](const ProjectEntry& p) { return p.id == id; });
    };
    std::string candidate = base;
    while (exists(candidate)) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

std::string AppTaskDisplayTitle(const TaskEntry& task) {
    if (!task.project.empty() && !task.title.empty()) {
        return task.project + " - " + task.title;
    }
    if (!task.title.empty()) return task.title;
    return task.project;
}

bool AppSaveTasks(const std::filesystem::path& storageDir, const std::vector<TaskEntry>& tasks) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& entry = tasks[i];
        out << "  {\"id\":\"" << JsonEscape(entry.id)
            << "\",\"projectId\":\"" << JsonEscape(entry.projectId)
            << "\",\"project\":\"" << JsonEscape(entry.project)
            << "\",\"title\":\"" << JsonEscape(entry.title)
            << "\",\"description\":\"" << JsonEscape(entry.description)
            << "\",\"deadlineAt\":" << entry.deadlineAt
            << ",\"status\":" << AppNormalizeTaskStatus(entry.status)
            << ",\"priority\":" << AppNormalizeTaskPriority(entry.priority)
            << ",\"category\":" << entry.category
            << ",\"score\":" << entry.score
            << ",\"baseXp\":" << entry.baseXp
            << ",\"basePool\":" << entry.basePool
            << ",\"createdAt\":" << entry.createdAt
            << ",\"assignees\":\"" << JsonEscape(SerializeAssignees(entry.assignees)) << "\""
            << ",\"participants\":\"" << JsonEscape(SerializeParticipants(entry.participants)) << "\"}";
        if (i + 1 < tasks.size()) out << ",";
        out << "\n";
    }
    out << "]";
    return WriteAllUtf8Bom(TasksStoragePath(storageDir), out.str());
}

bool AppSaveProjects(const std::filesystem::path& storageDir, const std::vector<ProjectEntry>& projects) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    bool first = true;
    for (const auto& p : projects) {
        if (p.id.empty() || p.name.empty()) continue;
        if (!first) out << ",\n";
        first = false;
        out << "  {\"id\":\"" << JsonEscape(p.id)
            << "\",\"name\":\"" << JsonEscape(p.name)
            << "\",\"description\":\"" << JsonEscape(p.description)
            << "\",\"createdAt\":" << p.createdAt << "}";
    }
    if (!first) out << "\n";
    out << "]";
    return WriteAllUtf8Bom(ProjectsStoragePath(storageDir), out.str());
}

bool AppAppendTaskAudit(const std::filesystem::path& storageDir,
                        const std::string& actor,
                        const std::string& taskId,
                        const std::string& field,
                        const std::string& oldValue,
                        const std::string& newValue,
                        std::vector<TaskAuditEntry>* cache) {
    const std::string safeTaskId = SanitizeLogToken(taskId);
    const std::string safeField = SanitizeLogToken(field);
    if (safeTaskId.empty() || safeField.empty()) return false;

    std::error_code ec;
    const auto path = TaskAuditStoragePath(storageDir);
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return false;

    TaskAuditEntry entry;
    entry.timestamp = NowSecondsLocal();
    entry.actor = SanitizeLogToken(actor);
    entry.taskId = safeTaskId;
    entry.field = safeField;
    entry.oldValue = SanitizeLogToken(oldValue);
    entry.newValue = SanitizeLogToken(newValue);

    out << entry.timestamp << "|" << entry.actor << "|" << entry.taskId << "|"
        << entry.field << "|" << entry.oldValue << "|" << entry.newValue << "\n";
    if (!out.good()) return false;

    if (cache) {
        cache->push_back(entry);
        constexpr size_t kTaskAuditMaxEntries = 200;
        if (cache->size() > kTaskAuditMaxEntries) {
            cache->erase(cache->begin(), cache->end() - static_cast<std::ptrdiff_t>(kTaskAuditMaxEntries));
        }
    }
    return true;
}

AppProjectSaveResult AppSaveProjectEntry(const std::filesystem::path& storageDir,
                                         std::vector<ProjectEntry>& projects,
                                         int editIndex,
                                         const std::string& name,
                                         const std::string& description) {
    AppProjectSaveResult result;
    const std::string trimmedName = TrimCopy(name);
    if (trimmedName.empty()) {
        result.errorMessage = u8"Название проекта обязательно.";
        return result;
    }

    const std::string nameLower = ToLowerUtf8Copy(trimmedName);
    for (int i = 0; i < static_cast<int>(projects.size()); ++i) {
        if (i == editIndex) continue;
        if (ToLowerUtf8Copy(projects[static_cast<size_t>(i)].name) == nameLower) {
            result.duplicateName = true;
            result.errorMessage = u8"Проект с таким названием уже существует.";
            return result;
        }
    }

    const auto prevProjects = projects;
    int nextIndex = editIndex;
    if (editIndex >= 0 && editIndex < static_cast<int>(projects.size())) {
        auto& project = projects[static_cast<size_t>(editIndex)];
        project.name = trimmedName;
        project.description = TrimCopy(description);
    } else {
        ProjectEntry project;
        project.id = AppGenerateProjectId(projects);
        project.name = trimmedName;
        project.description = TrimCopy(description);
        project.createdAt = NowSecondsLocal();
        projects.push_back(std::move(project));
    }

    std::sort(projects.begin(), projects.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
        return ToLowerUtf8Copy(a.name) < ToLowerUtf8Copy(b.name);
    });

    for (int i = 0; i < static_cast<int>(projects.size()); ++i) {
        if (projects[static_cast<size_t>(i)].name == trimmedName) {
            nextIndex = i;
            break;
        }
    }

    if (!AppSaveProjects(storageDir, projects)) {
        projects = prevProjects;
        result.errorMessage = u8"Не удалось сохранить проект.";
        return result;
    }

    result.ok = true;
    result.projectIndex = nextIndex;
    return result;
}

AppProjectDeleteResult AppDeleteProjectAndDetachTasks(const std::filesystem::path& storageDir,
                                                      std::vector<ProjectEntry>& projects,
                                                      std::vector<TaskEntry>& tasks,
                                                      const std::string& projectId,
                                                      const std::string& actor,
                                                      std::vector<TaskAuditEntry>* auditCache) {
    AppProjectDeleteResult result;
    if (projectId.empty()) {
        result.errorMessage = u8"Не выбран проект для удаления.";
        return result;
    }

    auto it = std::find_if(projects.begin(), projects.end(), [&](const ProjectEntry& p) { return p.id == projectId; });
    if (it == projects.end()) {
        result.errorMessage = u8"Проект не найден.";
        return result;
    }
    const std::string removedProjectName = it->name;
    const auto prevProjects = projects;
    const auto prevTasks = tasks;

    projects.erase(std::remove_if(projects.begin(), projects.end(), [&](const ProjectEntry& p) { return p.id == projectId; }),
                   projects.end());

    std::vector<std::pair<std::string, std::string>> detachedAudit;
    for (auto& task : tasks) {
        if (task.projectId != projectId) continue;
        std::string oldProject = task.project.empty() ? removedProjectName : task.project;
        if (oldProject.empty()) oldProject = std::string(u8"Без проекта");
        detachedAudit.emplace_back(task.id, std::move(oldProject));
        task.projectId.clear();
        task.project.clear();
        result.detachedTasks += 1;
    }

    const bool projectsSaved = AppSaveProjects(storageDir, projects);
    const bool tasksSaved = projectsSaved && AppSaveTasks(storageDir, tasks);
    if (!projectsSaved || !tasksSaved) {
        projects = prevProjects;
        tasks = prevTasks;
        const bool rollbackProjectsOk = AppSaveProjects(storageDir, projects);
        const bool rollbackTasksOk = AppSaveTasks(storageDir, tasks);
        result.errorMessage = (!rollbackProjectsOk || !rollbackTasksOk)
            ? std::string(u8"Ошибка удаления проекта и отката. Проверьте файлы в meta/.")
            : std::string(u8"Не удалось удалить проект: изменения отменены.");
        result.detachedTasks = 0;
        return result;
    }

    for (const auto& entry : detachedAudit) {
        const std::string newValue = std::string(u8"Без проекта");
        if (!AppendTaskAuditIfChanged(storageDir, actor, entry.first, "project", entry.second, newValue, auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }

    result.ok = true;
    return result;
}

AppMutationResult AppCreateTaskEntry(const std::filesystem::path& storageDir,
                                     std::vector<TaskEntry>& tasks,
                                     const TaskEntry& task,
                                     const std::string& actor,
                                     std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    if (task.id.empty()) {
        result.errorMessage = u8"Не задан ID задачи.";
        return result;
    }
    const auto prevTasks = tasks;
    tasks.push_back(task);
    if (!AppSaveTasks(storageDir, tasks)) {
        tasks = prevTasks;
        result.errorMessage = u8"Не удалось сохранить задачу.";
        return result;
    }
    if (!AppendTaskAuditIfChanged(storageDir, actor, task.id, "create", u8"—", AppTaskDisplayTitle(task), auditCache)) {
        result.errorMessage = u8"Не удалось записать task-audit.log";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = 1;
    return result;
}

AppMutationResult AppUpdateTaskStatus(const std::filesystem::path& storageDir,
                                      std::vector<TaskEntry>& tasks,
                                      const std::string& taskId,
                                      int newStatus,
                                      const std::string& actor,
                                      std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    TaskEntry* task = FindTaskMutable(tasks, taskId);
    if (!task) {
        result.errorMessage = u8"Задача не найдена.";
        return result;
    }
    const int prev = AppNormalizeTaskStatus(task->status);
    const int next = AppNormalizeTaskStatus(newStatus);
    if (prev == next) {
        result.ok = true;
        return result;
    }
    if (!AppIsTaskStatusTransitionAllowed(prev, next)) {
        result.errorMessage = u8"Недопустимый переход статуса (Выполнена -> Новая).";
        return result;
    }
    task->status = next;
    if (!AppSaveTasks(storageDir, tasks)) {
        task->status = prev;
        result.errorMessage = u8"Не удалось сохранить статус задачи.";
        return result;
    }
    if (!AppendTaskAuditIfChanged(storageDir, actor, taskId, "status", AppTaskStatusLabel(prev), AppTaskStatusLabel(next), auditCache)) {
        result.errorMessage = u8"Не удалось записать task-audit.log";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = 1;
    return result;
}

AppMutationResult AppUpdateTaskPriority(const std::filesystem::path& storageDir,
                                        std::vector<TaskEntry>& tasks,
                                        const std::string& taskId,
                                        int newPriority,
                                        const std::string& actor,
                                        std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    TaskEntry* task = FindTaskMutable(tasks, taskId);
    if (!task) {
        result.errorMessage = u8"Задача не найдена.";
        return result;
    }
    const int prev = AppNormalizeTaskPriority(task->priority);
    const int next = AppNormalizeTaskPriority(newPriority);
    if (prev == next) {
        result.ok = true;
        return result;
    }
    task->priority = next;
    if (!AppSaveTasks(storageDir, tasks)) {
        task->priority = prev;
        result.errorMessage = u8"Не удалось сохранить приоритет задачи.";
        return result;
    }
    if (!AppendTaskAuditIfChanged(storageDir, actor, taskId, "priority", AppTaskPriorityLabel(prev), AppTaskPriorityLabel(next), auditCache)) {
        result.errorMessage = u8"Не удалось записать task-audit.log";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = 1;
    return result;
}

AppMutationResult AppUpdateTaskProject(const std::filesystem::path& storageDir,
                                       std::vector<TaskEntry>& tasks,
                                       const std::string& taskId,
                                       const std::string& nextProjectId,
                                       const std::string& nextProjectName,
                                       const std::string& actor,
                                       std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    TaskEntry* task = FindTaskMutable(tasks, taskId);
    if (!task) {
        result.errorMessage = u8"Задача не найдена.";
        return result;
    }
    const std::string prevId = task->projectId;
    const std::string prevName = task->project;
    if (prevId == nextProjectId && prevName == nextProjectName) {
        result.ok = true;
        return result;
    }
    task->projectId = nextProjectId;
    task->project = nextProjectName;
    if (!AppSaveTasks(storageDir, tasks)) {
        task->projectId = prevId;
        task->project = prevName;
        result.errorMessage = u8"Не удалось сохранить проект задачи.";
        return result;
    }
    if (!AppendTaskAuditIfChanged(storageDir, actor, taskId, "project",
                                  ResolveProjectAuditLabel(prevId, prevName, nullptr),
                                  ResolveProjectAuditLabel(task->projectId, task->project, nullptr),
                                  auditCache)) {
        result.errorMessage = u8"Не удалось записать task-audit.log";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = 1;
    return result;
}

AppMutationResult AppUpdateTaskDeadline(const std::filesystem::path& storageDir,
                                        std::vector<TaskEntry>& tasks,
                                        const std::string& taskId,
                                        const std::optional<std::int64_t>& deadlineAt,
                                        const std::string& actor,
                                        std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    TaskEntry* task = FindTaskMutable(tasks, taskId);
    if (!task) {
        result.errorMessage = u8"Задача не найдена.";
        return result;
    }
    const std::int64_t prevDeadline = task->deadlineAt;
    const std::int64_t nextDeadline = deadlineAt.value_or(0);
    if (prevDeadline == nextDeadline) {
        result.ok = true;
        return result;
    }
    task->deadlineAt = nextDeadline;
    if (!AppSaveTasks(storageDir, tasks)) {
        task->deadlineAt = prevDeadline;
        result.errorMessage = u8"Не удалось сохранить дедлайн задачи.";
        return result;
    }
    if (!AppendTaskAuditIfChanged(storageDir, actor, taskId, "deadline",
                                  FormatDeadlineAudit(prevDeadline), FormatDeadlineAudit(nextDeadline),
                                  auditCache)) {
        result.errorMessage = u8"Не удалось записать task-audit.log";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = 1;
    return result;
}

AppMutationResult AppUpdateTaskAssignees(const std::filesystem::path& storageDir,
                                         std::vector<TaskEntry>& tasks,
                                         const std::string& taskId,
                                         const std::vector<std::string>& assignees,
                                         const std::string& actor,
                                         std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    if (assignees.empty()) {
        result.errorMessage = u8"Выберите хотя бы одного исполнителя.";
        return result;
    }
    TaskEntry* task = FindTaskMutable(tasks, taskId);
    if (!task) {
        result.errorMessage = u8"Задача не найдена.";
        return result;
    }
    if (!task->participants.empty()) {
        result.errorMessage = u8"Для этой задачи исполнители зафиксированы по записи XP.";
        return result;
    }
    const auto prevAssignees = task->assignees;
    if (prevAssignees == assignees) {
        result.ok = true;
        return result;
    }
    task->assignees = assignees;
    if (!AppSaveTasks(storageDir, tasks)) {
        task->assignees = prevAssignees;
        result.errorMessage = u8"Не удалось сохранить исполнителей задачи.";
        return result;
    }
    if (!AppendTaskAuditIfChanged(storageDir, actor, taskId, "assignees",
                                  FormatAssignees(prevAssignees), FormatAssignees(assignees),
                                  auditCache)) {
        result.errorMessage = u8"Не удалось записать task-audit.log";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = 1;
    return result;
}

AppMutationResult AppBulkUpdateTaskStatus(const std::filesystem::path& storageDir,
                                          std::vector<TaskEntry>& tasks,
                                          const std::unordered_set<std::string>& taskIds,
                                          int targetStatus,
                                          const std::string& actor,
                                          std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    struct PrevState { TaskEntry* task = nullptr; int status = 0; };
    std::vector<PrevState> touched;
    touched.reserve(taskIds.size());
    const int nextStatus = AppNormalizeTaskStatus(targetStatus);
    for (auto& task : tasks) {
        if (taskIds.find(task.id) == taskIds.end()) continue;
        const int prev = AppNormalizeTaskStatus(task.status);
        if (prev == nextStatus) continue;
        if (!AppIsTaskStatusTransitionAllowed(prev, nextStatus)) {
            result.skippedCount += 1;
            continue;
        }
        touched.push_back({&task, prev});
        task.status = nextStatus;
    }
    if (touched.empty()) {
        result.ok = true;
        return result;
    }
    if (!AppSaveTasks(storageDir, tasks)) {
        for (const auto& prev : touched) {
            if (prev.task) prev.task->status = prev.status;
        }
        result.errorMessage = u8"Не удалось сохранить массовое изменение статуса.";
        return result;
    }
    for (const auto& prev : touched) {
        if (!prev.task) continue;
        if (!AppendTaskAuditIfChanged(storageDir, actor, prev.task->id, "status",
                                      AppTaskStatusLabel(prev.status), AppTaskStatusLabel(nextStatus),
                                      auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = static_cast<int>(touched.size());
    return result;
}

AppMutationResult AppBulkUpdateTaskPriority(const std::filesystem::path& storageDir,
                                            std::vector<TaskEntry>& tasks,
                                            const std::unordered_set<std::string>& taskIds,
                                            int targetPriority,
                                            const std::string& actor,
                                            std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    struct PrevState { TaskEntry* task = nullptr; int priority = 0; };
    std::vector<PrevState> touched;
    touched.reserve(taskIds.size());
    const int nextPriority = AppNormalizeTaskPriority(targetPriority);
    for (auto& task : tasks) {
        if (taskIds.find(task.id) == taskIds.end()) continue;
        const int prev = AppNormalizeTaskPriority(task.priority);
        if (prev == nextPriority) continue;
        touched.push_back({&task, prev});
        task.priority = nextPriority;
    }
    if (touched.empty()) {
        result.ok = true;
        return result;
    }
    if (!AppSaveTasks(storageDir, tasks)) {
        for (const auto& prev : touched) {
            if (prev.task) prev.task->priority = prev.priority;
        }
        result.errorMessage = u8"Не удалось сохранить массовое изменение приоритета.";
        return result;
    }
    for (const auto& prev : touched) {
        if (!prev.task) continue;
        if (!AppendTaskAuditIfChanged(storageDir, actor, prev.task->id, "priority",
                                      AppTaskPriorityLabel(prev.priority), AppTaskPriorityLabel(nextPriority),
                                      auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = static_cast<int>(touched.size());
    return result;
}

AppMutationResult AppBulkUpdateTaskProject(const std::filesystem::path& storageDir,
                                           std::vector<TaskEntry>& tasks,
                                           const std::unordered_set<std::string>& taskIds,
                                           const std::string& nextProjectId,
                                           const std::string& nextProjectName,
                                           const std::string& actor,
                                           std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    struct PrevState { TaskEntry* task = nullptr; std::string projectId; std::string projectName; };
    std::vector<PrevState> touched;
    touched.reserve(taskIds.size());
    for (auto& task : tasks) {
        if (taskIds.find(task.id) == taskIds.end()) continue;
        if (task.projectId == nextProjectId && task.project == nextProjectName) continue;
        touched.push_back({&task, task.projectId, task.project});
        task.projectId = nextProjectId;
        task.project = nextProjectName;
    }
    if (touched.empty()) {
        result.ok = true;
        return result;
    }
    if (!AppSaveTasks(storageDir, tasks)) {
        for (const auto& prev : touched) {
            if (!prev.task) continue;
            prev.task->projectId = prev.projectId;
            prev.task->project = prev.projectName;
        }
        result.errorMessage = u8"Не удалось сохранить массовое изменение проекта.";
        return result;
    }
    for (const auto& prev : touched) {
        if (!prev.task) continue;
        if (!AppendTaskAuditIfChanged(storageDir, actor, prev.task->id, "project",
                                      ResolveProjectAuditLabel(prev.projectId, prev.projectName, nullptr),
                                      ResolveProjectAuditLabel(prev.task->projectId, prev.task->project, nullptr),
                                      auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = static_cast<int>(touched.size());
    return result;
}

AppMutationResult AppBulkUpdateTaskDeadline(const std::filesystem::path& storageDir,
                                            std::vector<TaskEntry>& tasks,
                                            const std::unordered_set<std::string>& taskIds,
                                            const std::optional<std::int64_t>& deadlineAt,
                                            const std::string& actor,
                                            std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    struct PrevState { TaskEntry* task = nullptr; std::int64_t deadlineAt = 0; };
    std::vector<PrevState> touched;
    touched.reserve(taskIds.size());
    const std::int64_t nextDeadline = deadlineAt.value_or(0);
    for (auto& task : tasks) {
        if (taskIds.find(task.id) == taskIds.end()) continue;
        if (task.deadlineAt == nextDeadline) continue;
        touched.push_back({&task, task.deadlineAt});
        task.deadlineAt = nextDeadline;
    }
    if (touched.empty()) {
        result.ok = true;
        return result;
    }
    if (!AppSaveTasks(storageDir, tasks)) {
        for (const auto& prev : touched) {
            if (prev.task) prev.task->deadlineAt = prev.deadlineAt;
        }
        result.errorMessage = u8"Не удалось сохранить массовое изменение дедлайна.";
        return result;
    }
    for (const auto& prev : touched) {
        if (!prev.task) continue;
        if (!AppendTaskAuditIfChanged(storageDir, actor, prev.task->id, "deadline",
                                      FormatDeadlineAudit(prev.deadlineAt),
                                      FormatDeadlineAudit(prev.task->deadlineAt),
                                      auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = static_cast<int>(touched.size());
    return result;
}

AppMutationResult AppBulkUpdateTaskAssignees(const std::filesystem::path& storageDir,
                                             std::vector<TaskEntry>& tasks,
                                             const std::unordered_set<std::string>& taskIds,
                                             const std::vector<std::string>& assignees,
                                             const std::string& actor,
                                             std::vector<TaskAuditEntry>* auditCache) {
    AppMutationResult result;
    if (assignees.empty()) {
        result.errorMessage = u8"Выберите хотя бы одного исполнителя.";
        return result;
    }
    struct PrevState { TaskEntry* task = nullptr; std::vector<std::string> assignees; };
    std::vector<PrevState> touched;
    touched.reserve(taskIds.size());
    for (auto& task : tasks) {
        if (taskIds.find(task.id) == taskIds.end()) continue;
        if (!task.participants.empty()) {
            result.skippedCount += 1;
            continue;
        }
        if (task.assignees == assignees) continue;
        touched.push_back({&task, task.assignees});
        task.assignees = assignees;
    }
    if (touched.empty()) {
        result.ok = true;
        return result;
    }
    if (!AppSaveTasks(storageDir, tasks)) {
        for (const auto& prev : touched) {
            if (prev.task) prev.task->assignees = prev.assignees;
        }
        result.errorMessage = u8"Не удалось сохранить массовое изменение исполнителей.";
        return result;
    }
    for (const auto& prev : touched) {
        if (!prev.task) continue;
        if (!AppendTaskAuditIfChanged(storageDir, actor, prev.task->id, "assignees",
                                      FormatAssignees(prev.assignees), FormatAssignees(prev.task->assignees),
                                      auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = static_cast<int>(touched.size());
    return result;
}

AppMutationResult AppDeleteTasksByIds(const std::filesystem::path& storageDir,
                                      std::vector<TaskEntry>& tasks,
                                      const std::vector<std::string>& taskIds,
                                      const std::string& actor,
                                      std::vector<TaskAuditEntry>* auditCache,
                                      std::vector<TaskEntry>* removedTasks) {
    AppMutationResult result;
    if (taskIds.empty()) {
        result.ok = true;
        return result;
    }
    const auto prevTasks = tasks;
    std::unordered_set<std::string> ids(taskIds.begin(), taskIds.end());
    std::vector<TaskEntry> removed;
    removed.reserve(ids.size());
    for (const auto& task : tasks) {
        if (ids.find(task.id) != ids.end()) {
            removed.push_back(task);
        }
    }
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [&](const TaskEntry& item) {
                   return ids.find(item.id) != ids.end();
               }),
               tasks.end());
    if (removed.empty()) {
        result.ok = true;
        return result;
    }
    if (!AppSaveTasks(storageDir, tasks)) {
        tasks = prevTasks;
        result.errorMessage = u8"Не удалось сохранить удаление задач.";
        return result;
    }
    for (const auto& removedTask : removed) {
        if (!AppendTaskAuditIfChanged(storageDir, actor, removedTask.id, "delete",
                                      AppTaskDisplayTitle(removedTask), u8"удалена",
                                      auditCache)) {
            result.errorMessage = u8"Не удалось записать task-audit.log";
            return result;
        }
    }
    if (removedTasks) {
        *removedTasks = removed;
    }
    result.ok = true;
    result.changed = true;
    result.changedCount = static_cast<int>(removed.size());
    return result;
}
