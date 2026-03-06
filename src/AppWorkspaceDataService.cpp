#include "AppWorkspaceDataService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <locale>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace {

std::string TrimCopy(const std::string& input) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    std::string out = input;
    out.erase(out.begin(), std::find_if(out.begin(), out.end(),
                                        [&](unsigned char c) { return !is_space(c); }));
    out.erase(std::find_if(out.rbegin(), out.rend(),
                           [&](unsigned char c) { return !is_space(c); }).base(),
              out.end());
    return out;
}

void StripUtf8Bom(std::string& text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
}

std::string ReadAllText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string data = ss.str();
    StripUtf8Bom(data);
    return data;
}

void SkipWs(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

bool ParseJsonString(const std::string& text, size_t& pos, std::string& out) {
    SkipWs(text, pos);
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

bool ParseJsonValueToken(const std::string& text, size_t& pos, std::string& out) {
    SkipWs(text, pos);
    if (pos >= text.size()) return false;
    if (text[pos] == '"') {
        return ParseJsonString(text, pos, out);
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

bool ParseJsonObject(const std::string& text, size_t& pos,
                     std::unordered_map<std::string, std::string>& out) {
    SkipWs(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    ++pos;
    while (pos < text.size()) {
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        std::string key;
        if (!ParseJsonString(text, pos, key)) return false;
        SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        ++pos;
        std::string value;
        if (!ParseJsonValueToken(text, pos, value)) return false;
        out[key] = value;
        SkipWs(text, pos);
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

std::vector<std::unordered_map<std::string, std::string>> ParseJsonObjectArray(const std::string& text) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    size_t pos = 0;
    SkipWs(text, pos);
    if (pos >= text.size() || text[pos] != '[') return objects;
    ++pos;
    while (pos < text.size()) {
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        std::unordered_map<std::string, std::string> obj;
        if (!ParseJsonObject(text, pos, obj)) break;
        objects.push_back(std::move(obj));
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return objects;
}

int ParseInt(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::int64_t ParseInt64(const std::string& value, std::int64_t fallback = 0) {
    try {
        return std::stoll(value);
    } catch (...) {
        return fallback;
    }
}

std::string TasksStoragePathStr(const std::filesystem::path& storageDir) {
    return (storageDir / "meta" / "tasks.json").string();
}

std::filesystem::path TasksStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "tasks.json";
}

std::filesystem::path TaskAuditStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "task-audit.log";
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

std::vector<TaskParticipant> ParseParticipants(const std::string& text) {
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
        p.percent = ParseInt(token.substr(pos, sep - pos), 0);
        pos = sep + 1;
        sep = token.find('|', pos);
        if (sep == std::string::npos) continue;
        p.globalXp = ParseInt(token.substr(pos, sep - pos), 0);
        pos = sep + 1;
        p.skillXp = ParseInt(token.substr(pos), 0);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<std::string> ParseAssignees(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        std::string token = TrimCopy(text.substr(start, end - start));
        if (!token.empty()) {
            out.push_back(std::move(token));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return out;
}

constexpr int kTaskStatusNew = 0;
constexpr int kTaskStatusInProgress = 1;
constexpr int kTaskStatusDone = 2;
constexpr int kTaskPriorityMedium = 1;

int NormalizeTaskStatusValue(int value) {
    return std::clamp(value, kTaskStatusNew, kTaskStatusDone);
}

int NormalizeTaskPriorityValue(int value) {
    return std::clamp(value, 0, 3);
}

std::filesystem::path ProjectsStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "projects.json";
}

std::filesystem::path ShortcutsStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "shortcuts.json";
}

std::filesystem::path ProfessionsPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "professions.txt";
}

std::filesystem::path PipelineStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "pipeline.json";
}

const std::vector<PipelineStep>& DefaultPipelineSteps() {
    static const std::vector<PipelineStep> kDefaultPipelineSteps = {
        {"0. Реф-борд", R"(Сборка реф-листа, наряду с блокингом - важнейшие этапы!
При надобности, создается отдельный! муд-борд!)"},
        {"1. Блокинг", R"(Блокинг - это процесс создания базовых пропорций и соотношений между моделями и сценами.

На этом этапе особенно важно правильно отобразить пропорции отдельных деталей и модели в целом. В отличие от других этапов, здесь не так важно уделять внимание сетке, как точности и скорости.

Блокинг позволяет увидеть общую форму и силуэт будущей модели, что дает возможность внести необходимые коррективы, если это необходимо.

!!!Важно не вдаваться в детали на данном этапе!!!)"},
        {"2. Создание Low-Poly", R"(На этом этапе каждый элемент прорабатывается отдельно. Формы уточняются, а топология модели приводится в порядок.

Всё, что требует симметрии, выполняется с помощью модификатора Mirror и включённого параметра Clipping. Для удобства можно добавить объект-пустышку в нулевой координате, чтобы настроить зеркальное отражение на него.

ДЕЛАЕМ CNTRL+A -> SCALE)"},
        {"3. Шейдинг", R"(На этом этапе мы расставляем шарпэджи, проверяем шейдинг модели и устанавливаем параметры сглаживания.)"},
        {"4. High-poly", R"(В нашей сцене мы присваиваем всем объектам суффикс "_low" и создаем их дубликат. В копиях меняем суффикс на "_high", используя встроенный инструмент Blender - Batch Rename.

На одном из объектов мы настраиваем модификаторы Bevel и Subdivision Surface:

* Bevel: - Amount: 0.001 - Segments: 2 - Limit Method: Weight - Profile->Shape: 1
* Subdivision Surface: - Levels: 3

Затем, с помощью аддона Copy Attribute (который можно установить через Preferences), мы копируем эти модификаторы на все объекты High-poly.

После этого мы проходим по каждому объекту и устанавливаем значение Bevel Weight (вкладка Item, меню "N") на 1, применяя этот параметр ко всем шарпэджам.

Если требуется, мы модифицируем сетку, чтобы модель соответствовала Low-poly версии.)"},
        {"5. UV-развертка", R"(Производится только на Low-poly.

Проходим по всем объектам, выравнивая швы на шарпэдах. При необходимости добавляем дополнительные швы.

Затем разворачиваем объект и проверяем цвет UV-Stretch. Он должен быть максимально холодным.

Проверяем, нет ли искажений или деформаций на островках. При необходимости выравниваем их и, если нужно, поворачиваем в нужные нам координаты.

Когда развертка нас устраивает, применяем Mirror, создавая Overlaps.

После завершения работы со всеми объектами, мы приступаем к общей упаковке:

1. Нажимаем CNTRL+A и выбираем SCALE.
2. Выделяем все объекты, переключаемся в Edit Mode и в меню UV выбираем Average Islands Scale.
3. Упаковываем, отключив поворот островков.

margin - 0.004

Texel Density (TD) - 2px/cm при разрешении 4k (средний параметр, для каждого проекта рассчитывается отдельно)

Если необходимо достичь заданной плотности Texels, делим UV на несколько частей.)"},
        {"6. Запекание и текстурирование", R"(Более подробно я расскажу об этом позже, а сейчас поделюсь основными шагами:

1. Проводим запекание по всем объектам, используя минимальную длину луча. Проверяем, чтобы области High и Low не пересекались.
2. Выполняем пробные запекания, анализируем результаты и завершаем финальное запекание на уровне 8k.
3. Приступаем к текстурированию. В конце добавляем постобработку в виде Ambient Occlusion и, при необходимости, дополнительные затенения к текстурам.)"},
        {"7. Экспорт в движок", R"(????При экспорте из Blender, в настройках эспорта во вкладке Geometry, Smoothing меняем на Face.

Экспорт модели и текстур в движок: Модель и текстуры распределяются по соответствующим папкам (Mesh, Material, Texture). Я отдельно опубликую информацию о том, как именовать текстуры и меш.

Настройка шейдеров (материалов) и текстур, анимаций: На этом этапе мы настраиваем шейдеры и текстуры, а затем проверяем результат.

????Соблюдаем нейминг папок, файлов, иерархию.

Упаковка, создание PreFab и отправка разработчикам: все необходимые файлы упаковываются и отправляются разработчикам для дальнейшей работы.

?Каждый этап отправляется Арт-лиду на одобрение!)"}
    };
    return kDefaultPipelineSteps;
}

} // namespace

std::vector<TaskAuditEntry> LoadTaskAuditData(const std::filesystem::path& storageDir, size_t maxEntries) {
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
        std::string trimmed = TrimCopy(line);
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
        entry.timestamp = ParseInt64(parts[0]);
        entry.actor = parts[1];
        entry.taskId = parts[2];
        entry.field = parts[3];
        entry.oldValue = parts[4];
        entry.newValue = parts[5];
        if (entry.taskId.empty() || entry.field.empty()) continue;
        out.push_back(std::move(entry));
    }
    if (maxEntries > 0 && out.size() > maxEntries) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(maxEntries));
    }
    return out;
}

std::vector<TaskEntry> LoadTasksData(const std::filesystem::path& storageDir) {
    std::vector<TaskEntry> out;
    const std::string content = ReadAllText(TasksStoragePath(storageDir));
    if (content.empty()) return out;
    const auto objects = ParseJsonObjectArray(content);
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
        if (auto v = find_value("deadlineAt")) entry.deadlineAt = ParseInt64(*v, 0);
        bool hasStatus = false;
        if (auto v = find_value("status")) {
            entry.status = NormalizeTaskStatusValue(ParseInt(*v, kTaskStatusNew));
            hasStatus = true;
        }
        if (auto v = find_value("priority")) {
            entry.priority = NormalizeTaskPriorityValue(ParseInt(*v, kTaskPriorityMedium));
        } else {
            entry.priority = kTaskPriorityMedium;
        }
        if (auto v = find_value("category")) entry.category = ParseInt(*v, 0);
        if (auto v = find_value("score")) entry.score = ParseInt(*v, 0);
        if (auto v = find_value("baseXp")) entry.baseXp = ParseInt(*v, 0);
        if (auto v = find_value("basePool")) entry.basePool = ParseInt(*v, 0);
        if (auto v = find_value("createdAt")) entry.createdAt = ParseInt64(*v, 0);
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
        }
        out.push_back(std::move(entry));
    }
    return out;
}

std::vector<ProjectEntry> LoadProjectsData(const std::filesystem::path& storageDir) {
    std::vector<ProjectEntry> out;
    const std::string content = ReadAllText(ProjectsStoragePath(storageDir));
    if (content.empty()) return out;
    const auto objects = ParseJsonObjectArray(content);
    out.reserve(objects.size());
    for (const auto& obj : objects) {
        auto find_value = [&](const char* key) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            return it->second;
        };
        ProjectEntry project;
        if (auto v = find_value("id")) project.id = *v;
        if (auto v = find_value("name")) project.name = *v;
        if (auto v = find_value("description")) project.description = *v;
        if (auto v = find_value("createdAt")) project.createdAt = ParseInt64(*v, 0);
        if (project.id.empty() || project.name.empty()) continue;
        out.push_back(std::move(project));
    }
    std::sort(out.begin(), out.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
        return a.name < b.name;
    });
    return out;
}

std::vector<ShortcutEntry> LoadShortcutsData(const std::filesystem::path& storageDir) {
    std::vector<ShortcutEntry> out;
    const std::string content = ReadAllText(ShortcutsStoragePath(storageDir));
    if (content.empty()) return out;
    const auto objects = ParseJsonObjectArray(content);
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

std::vector<ProfessionEntry> LoadProfessionsData(const std::filesystem::path& storageDir) {
    std::vector<ProfessionEntry> out;
    std::ifstream in(ProfessionsPath(storageDir), std::ios::binary);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        std::string part;
        std::istringstream ss(line);
        while (std::getline(ss, part, '|')) {
            parts.push_back(TrimCopy(part));
        }
        if (parts.size() < 2) continue;
        ProfessionEntry e;
        size_t idx = 0;
        e.id = parts[idx++];
        e.name = parts[idx++];
        if (idx < parts.size()) {
            e.description = parts[idx++];
        }
        if (!e.id.empty() && !e.name.empty()) {
            out.push_back(std::move(e));
        }
    }
    return out;
}

std::vector<PipelineStep> LoadPipelineData(const std::filesystem::path& storageDir) {
    std::vector<PipelineStep> out;
    const std::string content = ReadAllText(PipelineStoragePath(storageDir));
    if (content.empty()) {
        return DefaultPipelineSteps();
    }
    const auto objects = ParseJsonObjectArray(content);
    for (const auto& obj : objects) {
        auto find_value = [&](const char* key) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            return it->second;
        };
        PipelineStep step;
        if (auto v = find_value("title")) step.title = *v;
        if (auto v = find_value("description")) step.description = *v;
        if (step.title.empty()) continue;
        out.push_back(std::move(step));
    }
    if (out.empty()) {
        return DefaultPipelineSteps();
    }
    return out;
}

WorkspaceDataSnapshot LoadWorkspaceDataSnapshot(const std::filesystem::path& storageDir,
                                                const ModuleToggles& modules) {
    WorkspaceDataSnapshot snapshot;
    snapshot.tasks = modules.tasks ? LoadTasksData(storageDir) : std::vector<TaskEntry>();
    snapshot.taskAudit = modules.tasks ? LoadTaskAuditData(storageDir) : std::vector<TaskAuditEntry>();
    snapshot.projects = LoadProjectsData(storageDir);
    snapshot.shortcuts = modules.shortcuts ? LoadShortcutsData(storageDir) : std::vector<ShortcutEntry>();
    snapshot.pipelineSteps = modules.pipeline ? LoadPipelineData(storageDir) : std::vector<PipelineStep>();
    snapshot.professions = modules.professions ? LoadProfessionsData(storageDir) : std::vector<ProfessionEntry>();
    snapshot.bannerTexts = LoadBannerTexts(storageDir);
    snapshot.rulesConfig = LoadGameplayConfig(storageDir);
    snapshot.vault = LoadStorageVault(storageDir);
    return snapshot;
}
