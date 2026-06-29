#include "AppWorkspaceDataService.h"
#include "AppRecoveryStorage.h"
#include "AppUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <initializer_list>
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

bool ParseJsonObjectArrayStrict(
    const std::string& text,
    std::vector<std::unordered_map<std::string, std::string>>& objects) {
    objects.clear();
    size_t pos = 0;
    SkipWs(text, pos);
    if (pos >= text.size() || text[pos] != '[') return false;
    ++pos;
    while (pos < text.size()) {
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            SkipWs(text, pos);
            return pos == text.size();
        }
        std::unordered_map<std::string, std::string> obj;
        if (!ParseJsonObject(text, pos, obj)) return false;
        objects.push_back(std::move(obj));
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            SkipWs(text, pos);
            if (pos >= text.size() || text[pos] == ']') return false;
            continue;
        }
        if (pos >= text.size() || text[pos] != ']') return false;
    }
    return false;
}

std::vector<std::unordered_map<std::string, std::string>> ParseJsonObjectArray(const std::string& text) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    if (!ParseJsonObjectArrayStrict(text, objects)) objects.clear();
    return objects;
}

std::vector<std::unordered_map<std::string, std::string>> ParsePipelineObjectsFromContent(const std::string& content) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    if (ParseJsonObjectArrayStrict(content, objects)) {
        return objects;
    }
    const std::string trimmed = TrimCopy(content);
    if (trimmed.size() < 2 || trimmed.front() != '{' || trimmed.back() != '}') return {};
    const size_t stepsPos = content.find("\"steps\"");
    const size_t lb = content.find('[', stepsPos);
    const size_t rb = content.rfind(']');
    if (stepsPos != std::string::npos && lb != std::string::npos && rb != std::string::npos && rb > lb) {
        if (ParseJsonObjectArrayStrict(content.substr(lb, rb - lb + 1), objects)) return objects;
    }
    return {};
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
        out << p.profileId << "|" << p.percent << "|" << p.globalXp << "|" << p.skillXp
            << "|" << EncodePassword(p.rollbackSnapshot);
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
        sep = token.find('|', pos);
        if (sep == std::string::npos) {
            p.skillXp = ParseInt(token.substr(pos), 0);
        } else {
            p.skillXp = ParseInt(token.substr(pos, sep - pos), 0);
            pos = sep + 1;
            p.rollbackSnapshot = DecodePassword(token.substr(pos));
        }
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

std::vector<std::string> ParseSkillIds(const std::string& text) {
    return ParseAssignees(text);
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

std::vector<std::string> SplitStringList(const std::string& text, char delimiter) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(delimiter, start);
        if (end == std::string::npos) end = text.size();
        std::string token = TrimCopy(text.substr(start, end - start));
        if (!token.empty()) out.push_back(std::move(token));
        if (end == text.size()) break;
        start = end + 1;
    }
    return out;
}

PipelineStep MakePipelineStep(const char* id,
                              const char* stageCode,
                              const char* branch,
                              const char* title,
                              const char* description,
                              const char* input,
                              const char* output,
                              const char* owner,
                              const char* doneCriteria,
                              const char* engineCheck,
                              const char* risk,
                              const char* nextStageLabel = "",
                              std::initializer_list<const char*> nextIds = {},
                              std::initializer_list<const char*> hints = {},
                              const char* legacyNotes = "") {
    PipelineStep step;
    step.id = id ? id : "";
    step.stageCode = stageCode ? stageCode : "";
    step.branch = branch ? branch : "";
    step.title = title ? title : "";
    step.description = description ? description : "";
    step.input = input ? input : "";
    step.output = output ? output : "";
    step.owner = owner ? owner : "";
    step.doneCriteria = doneCriteria ? doneCriteria : "";
    step.engineCheck = engineCheck ? engineCheck : "";
    step.risk = risk ? risk : "";
    step.nextStageLabel = nextStageLabel ? nextStageLabel : "";
    step.legacyNotes = legacyNotes ? legacyNotes : "";
    for (const char* next : nextIds) {
        if (next && next[0] != '\0') step.nextIds.emplace_back(next);
    }
    for (const char* hint : hints) {
        if (hint && hint[0] != '\0') step.hints.emplace_back(hint);
    }
    return step;
}

const std::vector<PipelineStep>& DefaultPipelineSteps() {
    static const std::vector<PipelineStep> kDefaultPipelineSteps = {
        MakePipelineStep(
            "brief_constraints", "1", u8"Общее", u8"ТЗ и техограничения",
            u8"До старта производства фиксируем технические рамки, чтобы весь пайплайн опирался на согласованные лимиты и правила экспорта.",
            u8"Бриф", u8"Согласованные ограничения", u8"Лид / техарт / продюсер",
            u8"Poly budget, texel density, skeleton, naming и формат экспорта подтверждены до старта.",
            u8"Заранее определяем, что валидируем в движке: scale, naming, pivots, import и perf.",
            u8"Поздние техограничения ломают весь пайплайн и делают возвраты дорогими.",
            u8"Реф-борд",
            {"ref_board"},
            {
                u8"Не начинайте производство без подтверждённых техлимитов.",
                u8"Сразу зафиксируйте naming, skeleton и формат экспорта."
            }),
        MakePipelineStep(
            "ref_board", "2", u8"Общее", u8"Реф-борд",
            u8"Собираем визуальные и анимационные референсы, чтобы всем участникам было одинаково понятно, какой образ и движение нужны ассету.",
            u8"ТЗ, стиль проекта", u8"Согласованный пакет рефов", u8"Концепт / лид",
            u8"Понятны форма, материалы и характер движения.",
            u8"Подбираем эталон для будущей визуальной сверки уже в тестовой сцене движка.",
            u8"Расплывчатый визуал и спорный стиль ведут к неверному блокингу.",
            u8"Блокинг",
            {"blocking"},
            {
                u8"Сверяйте не только форму, но и материал, масштаб и поведение в движении.",
                u8"Если визуал сложный, собирайте отдельный mood-board по материалам."
            },
            R"FM(Сборка реф-листа, наряду с блокингом - важнейшие этапы!
При надобности, создается отдельный! муд-борд!)FM"),
        MakePipelineStep(
            "blocking", "3", u8"Общее", u8"Блокинг",
            u8"Собираем грубую форму, силуэт, пропорции и масштаб. Это точка, где дешевле всего исправлять направление.",
            u8"Рефы и ограничения", u8"Утвержденный блокинг", u8"3D artist",
            u8"Пропорции и масштаб согласованы, силуэт читается.",
            u8"Проверяем масштаб, силуэт, читаемость форм и общую сборку в тестовой сцене.",
            u8"Неверный масштаб и силуэт создают цепочку возвратов на всех ветках.",
            u8"Развилка на ветку 1 или 2",
            {"hs_low_base", "sculpt_master"},
            {
                u8"На блокинге важнее точность пропорций и скорость, чем чистая сетка.",
                u8"Не уходите в детали, пока не подтверждены силуэт и масштаб."
            },
            R"FM(Блокинг - это процесс создания базовых пропорций и соотношений между моделями и сценами.

На этом этапе особенно важно правильно отобразить пропорции отдельных деталей и модели в целом. В отличие от других этапов, здесь не так важно уделять внимание сетке, как точности и скорости.

Блокинг позволяет увидеть общую форму и силуэт будущей модели, что дает возможность внести необходимые коррективы, если это необходимо.

!!!Важно не вдаваться в детали на данном этапе!!!)FM"),
        MakePipelineStep(
            "hs_low_base", "4A", u8"Ветка 1: hard-surface", u8"Low-Poly база",
            u8"Строим чистую игровую сетку как базу под hard-surface high-poly, приводим в порядок топологию и подготавливаем low-poly к корректному шейдингу.",
            u8"Утвержденный блокинг", u8"Low-Poly база", u8"3D artist",
            u8"Сетка чистая, scale применён, пропорции выдержаны, шейдинг low-poly читается.",
            u8"Проверяем силуэт, scale, посадку в сцене и общий игровой вид.",
            u8"Ранний уход в детали без чистой базы и сломанный scale удорожают всё, что идёт дальше.",
            u8"Hard-surface / High-Poly",
            {"hs_high_poly"},
            {
                u8"Всё симметричное ведите через Mirror + Clipping.",
                u8"Перед продолжением применяйте scale: Ctrl+A -> Scale.",
                u8"Проверьте sharp edges и smoothing до ухода в high-poly."
            },
            R"FM(На этом этапе каждый элемент прорабатывается отдельно. Формы уточняются, а топология модели приводится в порядок.

Всё, что требует симметрии, выполняется с помощью модификатора Mirror и включённого параметра Clipping. Для удобства можно добавить объект-пустышку в нулевой координате, чтобы настроить зеркальное отражение на него.

ДЕЛАЕМ CNTRL+A -> SCALE

На этом этапе мы расставляем шарпэджи, проверяем шейдинг модели и устанавливаем параметры сглаживания.)FM"),
        MakePipelineStep(
            "hs_high_poly", "5A", u8"Ветка 1: hard-surface", u8"Hard-surface / High-Poly",
            u8"На основе low-poly собираем high-poly, добавляем фаски и плавающую геометрию и готовим источник для bake.",
            u8"Low-Poly база", u8"High-Poly для bake", u8"3D artist",
            u8"Форма соответствует техтребованиям и не ломает исходный игровой силуэт.",
            u8"Сверяем, не уводят ли новые детали силуэт и читаемость ассета в движке.",
            u8"Изменения формы на этой стадии уже дорогие и тянут возвраты назад.",
            u8"UV",
            {"hs_uv"},
            {
                u8"Следите, чтобы high-poly уточнял форму, а не менял утверждённый образ.",
                u8"Проверяйте соответствие low/high по ключевым ребрам и фаскам."
            },
            R"FM(В нашей сцене мы присваиваем всем объектам суффикс "_low" и создаем их дубликат. В копиях меняем суффикс на "_high", используя встроенный инструмент Blender - Batch Rename.

На одном из объектов мы настраиваем модификаторы Bevel и Subdivision Surface:

* Bevel: - Amount: 0.001 - Segments: 2 - Limit Method: Weight - Profile->Shape: 1
* Subdivision Surface: - Levels: 3

Затем, с помощью аддона Copy Attribute (который можно установить через Preferences), мы копируем эти модификаторы на все объекты High-poly.

После этого мы проходим по каждому объекту и устанавливаем значение Bevel Weight (вкладка Item, меню "N") на 1, применяя этот параметр ко всем шарпэджам.

Если требуется, мы модифицируем сетку, чтобы модель соответствовала Low-poly версии.)FM"),
        MakePipelineStep(
            "hs_uv", "6A", u8"Ветка 1: hard-surface", u8"UV",
            u8"Делаем развертку под texel density, швы и упаковку, чтобы bake и текстуры читались без артефактов.",
            u8"Финальный low-poly", u8"Чистая UV-развертка", u8"3D artist",
            u8"Нет критичных растяжений, паддинг выдержан, плотность texel контролируется.",
            u8"Проверяем texel density и читаемость на checker-материале.",
            u8"Плохой шов ломает bake и текстуры, а неравномерный TD разрушает читаемость.",
            u8"Bake",
            {"hs_bake"},
            {
                u8"Швы удобно выравнивать по sharp edges и проверять по UV-Stretch.",
                u8"Average Islands Scale и финальная упаковка обязательны перед bake."
            },
            R"FM(Производится только на Low-poly.

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

Если необходимо достичь заданной плотности Texels, делим UV на несколько частей.)FM"),
        MakePipelineStep(
            "hs_bake", "7A", u8"Ветка 1: hard-surface", u8"Bake",
            u8"Запекаем normal, AO и нужные карты и снимаем артефакты до перехода к материалам.",
            u8"High-Poly, Low-Poly, UV", u8"Bake maps", u8"3D / texture artist",
            u8"Артефакты устранены, карты дают ожидаемую форму и объём.",
            u8"Проверяем normal и AO на тестовом шейдере в движке.",
            u8"Швы, cage-проблемы и неверные smoothing groups быстро размножают дефекты.",
            u8"Текстуры / материалы",
            {"hs_textures"},
            {
                u8"Сначала делайте пробные bake, только потом финальный.",
                u8"Контролируйте пересечения high/low перед длинными запеканиями."
            },
            R"FM(1. Проводим запекание по всем объектам, используя минимальную длину луча. Проверяем, чтобы области High и Low не пересекались.
2. Выполняем пробные запекания, анализируем результаты и завершаем финальное запекание на уровне 8k.)FM"),
        MakePipelineStep(
            "hs_textures", "8A", u8"Ветка 1: hard-surface", u8"Текстуры / материалы",
            u8"Собираем PBR-текстуры и material slots, проверяем стиль, roughness/metalness и сценовое освещение.",
            u8"Bake maps и рефы", u8"Набор текстур и material slots", u8"Texture artist",
            u8"Материалы читаются, соответствуют стилю и не перегружают пайплайн ассета.",
            u8"Проверяем материалы, roughness/metalness и вид под сценовым светом.",
            u8"Несоответствие стилю и лишний размер карт ведут к переработкам и perf-проблемам.",
            u8"Риг / интеграция",
            {"rig"},
            {
                u8"Финальный вид проверяйте не в изоляции, а под сценовым светом.",
                u8"Следите за количеством карт и размером текстур ещё до интеграции."
            },
            R"FM(3. Приступаем к текстурированию. В конце добавляем постобработку в виде Ambient Occlusion и, при необходимости, дополнительные затенения к текстурам.)FM"),
        MakePipelineStep(
            "sculpt_master", "4B", u8"Ветка 2: sculpt", u8"Скульпт",
            u8"Детализируем форму через скульпт и уточняем объём, не теряя утверждённый игровой образ.",
            u8"Утвержденный блокинг", u8"Скульпт-master", u8"3D artist",
            u8"Форма и пластика готовы для ретопа и не расходятся с техограничениями.",
            u8"Сверяем форму и пластику на тестовой сборке, чтобы не уйти от игрового образа.",
            u8"Скульпт, ушедший от техлимитов, делает ретоп дорогим и затягивает сроки.",
            u8"Retopo / Low-Poly",
            {"retopo_low"},
            {
                u8"Детализация должна усиливать форму, а не ломать approved silhouette.",
                u8"Регулярно сверяйтесь с рефами и блокингом, а не только с красивым скульптом."
            }),
        MakePipelineStep(
            "retopo_low", "5B", u8"Ветка 2: sculpt", u8"Retopo / Low-Poly",
            u8"Строим игровую топологию по скульпту и готовим mesh к деформациям и следующему UV-циклу.",
            u8"Скульпт и лимиты проекта", u8"Low-Poly mesh", u8"3D artist",
            u8"Сетка чистая, деформируемые зоны подготовлены, silhouette совпадает со скульптом.",
            u8"Проверяем силуэт, scale и базовую деформацию на тестовой сборке.",
            u8"Перегруженный скульпт делает ретоп избыточно дорогим и медленным.",
            u8"UV",
            {"sculpt_uv"},
            {
                u8"Retopo должен поддерживать анимацию, а не только повторять скульпт.",
                u8"Сразу контролируйте петли в деформируемых зонах."
            },
            R"FM(На этом этапе каждый элемент прорабатывается отдельно. Формы уточняются, а топология модели приводится в порядок.

Всё, что требует симметрии, выполняется с помощью модификатора Mirror и включённого параметра Clipping. Для удобства можно добавить объект-пустышку в нулевой координате, чтобы настроить зеркальное отражение на него.

ДЕЛАЕМ CNTRL+A -> SCALE)FM"),
        MakePipelineStep(
            "sculpt_uv", "6B", u8"Ветка 2: sculpt", u8"UV",
            u8"Делаем развертку под texel density и швы для ветки sculpt, чтобы bake прошёл без искажений.",
            u8"Low-Poly", u8"Чистая UV-развертка", u8"3D artist",
            u8"Нет критичных растяжений, паддинг соблюден, плотность texel выдержана.",
            u8"Проверяем texel density и читаемость на тестовом материале.",
            u8"Пересечения и неравномерная плотность texel ломают bake и текстуры.",
            u8"Bake",
            {"sculpt_bake"},
            {
                u8"UV в sculpt-ветке должен поддерживать и bake, и анимационные деформации.",
                u8"Контролируйте паддинг и упаковку до запуска bake."
            },
            R"FM(Производится только на Low-poly.

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

Если необходимо достичь заданной плотности Texels, делим UV на несколько частей.)FM"),
        MakePipelineStep(
            "sculpt_bake", "7B", u8"Ветка 2: sculpt", u8"Bake",
            u8"Запекаем карты со скульпта на low-poly и устраняем артефакты до перехода к материалам.",
            u8"Скульпт, Low-Poly, UV", u8"Bake maps", u8"3D / texture artist",
            u8"Артефакты устранены, normal data читается стабильно.",
            u8"Проверяем normal и AO на тестовом шейдере в движке.",
            u8"Швы, cage-проблемы и неверные normal data быстро ломают весь материал.",
            u8"Текстуры / материалы",
            {"sculpt_textures"},
            {
                u8"Пробные bake обязательны перед финальным прогоном.",
                u8"Следите за согласованностью cage и low/high соответствия."
            },
            R"FM(1. Проводим запекание по всем объектам, используя минимальную длину луча. Проверяем, чтобы области High и Low не пересекались.
2. Выполняем пробные запекания, анализируем результаты и завершаем финальное запекание на уровне 8k.)FM"),
        MakePipelineStep(
            "sculpt_textures", "8B", u8"Ветка 2: sculpt", u8"Текстуры / материалы",
            u8"Собираем PBR-текстуры и material slots, проверяем, что ветка sculpt попадает в стиль и бюджет.",
            u8"Bake maps и рефы", u8"Набор текстур и material slots", u8"Texture artist",
            u8"Материалы читаются и соответствуют стилю проекта.",
            u8"Проверяем материалы, roughness/metalness и вид под сценовым светом.",
            u8"Несоответствие стилю и перегрузка текстур сдвигают и арт, и perf.",
            u8"Риг / интеграция",
            {"rig"},
            {
                u8"Материал должен подчёркивать форму скульпта, а не маскировать её.",
                u8"Контролируйте размер карт до интеграции в движок."
            },
            R"FM(3. Приступаем к текстурированию. В конце добавляем постобработку в виде Ambient Occlusion и, при необходимости, дополнительные затенения к текстурам.)FM"),
        MakePipelineStep(
            "rig", "9", u8"Общее после веток", u8"Риг",
            u8"Собираем скелет, контроллеры и constraints и сразу проверяем совместимость структуры с движком.",
            u8"Финальная модель и техтребования", u8"Рабочий риг", u8"Rigger / tech artist",
            u8"Кости, pivots и иерархия соответствуют требованиям движка.",
            u8"Проверяем hierarchy, pivots и совместимость импорта на тестовой сцене.",
            u8"Несовместимость со скелетом движка возвращает нас сразу на предыдущие этапы.",
            u8"Skinning / веса",
            {"skinning"},
            {
                u8"Не тяните проверку иерархии до конца: импорт-совместимость нужна сразу.",
                u8"Проверяйте pivots и naming вместе с hierarchy."
            }),
        MakePipelineStep(
            "skinning", "10", u8"Общее после веток", u8"Skinning / веса",
            u8"Настраиваем веса и деформации так, чтобы модель спокойно переживала ключевые позы и экспорт.",
            u8"Рабочий риг и low-poly", u8"Подготовленная к анимации модель", u8"Rigger / animator",
            u8"Деформации чистые в ключевых позах.",
            u8"Гоняем ключевые позы и смотрим деформации уже в движке.",
            u8"Ломаются локти, плечи и пальцы, если веса не проверять в реальных позах.",
            u8"Анимация",
            {"animation"},
            {
                u8"Проверяйте веса в граничных позах, а не только в нейтрали.",
                u8"Лучше поймать проблемы в движке сразу, чем после пакета анимаций."
            }),
        MakePipelineStep(
            "animation", "11", u8"Общее после веток", u8"Анимация",
            u8"Делаем клипы, циклы и экспортные наборы и проверяем поведение ассета как набора состояний, а не отдельных сцен.",
            u8"Риг и список клипов", u8"Экспортный пакет анимаций", u8"Animator",
            u8"Ключевые состояния закрыты, циклы чистые, экспортные наборы собраны.",
            u8"Проверяем клипы, transitions, root motion и events.",
            u8"Разный FPS, root motion и naming ломают интеграцию уже на импорте.",
            u8"Импорт в движок",
            {"engine_import"},
            {
                u8"Сразу фиксируйте экспортные пресеты и FPS для всех клипов.",
                u8"Проверяйте events и root motion до интеграции."
            }),
        MakePipelineStep(
            "engine_import", "12", u8"Общее после веток", u8"Импорт в движок",
            u8"Импортируем модель, риг, анимации и текстуры и собираем чистую структуру файлов проекта.",
            u8"Модель, риг, анимации, текстуры", u8"Ассет в проекте", u8"Tech artist / integrator",
            u8"Имена, scale, pivots и hierarchy корректны уже на импортированном ассете.",
            u8"Это основной контур проверки: все отклонения фиксируем сразу после импорта.",
            u8"Ломаются ссылки, scale и файловая структура, если импорт делают без строгих правил.",
            u8"Настройка в движке",
            {"engine_setup"},
            {
                u8"После импорта сразу проверяйте scale, pivots и иерархию.",
                u8"Держите единый нейминг папок, файлов и мешей."
            },
            R"FM(????При экспорте из Blender, в настройках эспорта во вкладке Geometry, Smoothing меняем на Face.

Экспорт модели и текстур в движок: Модель и текстуры распределяются по соответствующим папкам (Mesh, Material, Texture). Я отдельно опубликую информацию о том, как именовать текстуры и меш.

????Соблюдаем нейминг папок, файлов, иерархию.)FM"),
        MakePipelineStep(
            "engine_setup", "13", u8"Общее после веток", u8"Настройка в движке",
            u8"Подключаем шейдеры, controller, events и коллизии, чтобы ассет начал работать как игровой объект.",
            u8"Импортированный ассет", u8"Игровая сборка ассета", u8"Tech artist / integrator",
            u8"Ассет работает в целевой сцене и использует правильные материалы и контроллеры.",
            u8"Проверяем шейдеры, controller, collisions и perf.",
            u8"Не совпадают материалы, события и коллизии, если настройка отрывается от импортированного пакета.",
            u8"Тестирование в движке",
            {"engine_test"},
            {
                u8"Настройку материалов и контроллеров делайте сразу на целевой сцене.",
                u8"Не забывайте проверить collisions и perf вместе с визуалом."
            },
            R"FM(Настройка шейдеров (материалов) и текстур, анимаций: На этом этапе мы настраиваем шейдеры и текстуры, а затем проверяем результат.)FM"),
        MakePipelineStep(
            "engine_test", "14", u8"Общее после веток", u8"Тестирование в движке",
            u8"Проверяем сцену, свет, камеру, perf и геймплейные кейсы по явному чеклисту, а не по ощущению готовности.",
            u8"Настроенный ассет", u8"Список найденных проблем или отметка OK", u8"QA / lead / integrator",
            u8"Нет критических багов, список правок прозрачен и воспроизводим.",
            u8"Полная регрессия по чеклисту: визуал, collisions, animation, perf и интеграционные кейсы.",
            u8"Тест без чеклиста даёт ложное ощущение готовности и пропускает регрессии.",
            u8"Правки и финальная приемка",
            {"final_handoff"},
            {
                u8"Проверяйте ассет в целевых камерах, свете и gameplay-кейсах.",
                u8"Без чеклиста тестирование теряет повторяемость."
            }),
        MakePipelineStep(
            "final_handoff", "15", u8"Общее после веток", u8"Правки и финальная приемка",
            u8"Возвращаем задачи в нужный этап, закрываем замечания повторным тестом и готовим финальный handoff.",
            u8"Список замечаний", u8"Финальная версия ассета", u8"Владелец этапа + lead",
            u8"Все критические замечания закрыты, ассет повторно проверен на затронутом контуре.",
            u8"После каждой правки повторно тестируем затронутый контур и финальную сборку.",
            u8"Чаще всего возвраты приходят в риг, анимацию, материалы и импорт.",
            u8"Релиз / handoff",
            {"release_handoff"},
            {
                u8"Возврат всегда должен идти в конкретный этап, а не «назад в пайплайн вообще».",
                u8"Финальный handoff делайте только после повторного теста."
            },
            R"FM(Упаковка, создание PreFab и отправка разработчикам: все необходимые файлы упаковываются и отправляются разработчикам для дальнейшей работы.

?Каждый этап отправляется Арт-лиду на одобрение!)FM")
        ,
        MakePipelineStep(
            "release_handoff", "16", u8"Общее после веток", u8"Релиз / handoff",
            u8"Упаковываем финальные файлы, prefab и документацию и передаем ассет в разработку или релизный контур.",
            u8"Финальная версия ассета", u8"Релизный пакет / handoff", u8"Lead / tech artist / producer",
            u8"Все файлы, зависимости и договоренности по handoff подтверждены принимающей стороной.",
            u8"Делаем финальную smoke-проверку импортированной релизной сборки, ссылок и prefab/scene wiring.",
            u8"Потеря зависимостей, путаница версий и неполный handoff ломают интеграцию уже после приемки.",
            u8"",
            {},
            {
                u8"Перед handoff проверьте финальный комплект файлов и их версии.",
                u8"Список передаваемых артефактов и ответственных лучше фиксировать явно."
            })
    };
    return kDefaultPipelineSteps;
}

bool PipelineStepHasRichData(const PipelineStep& step) {
    return !step.id.empty() ||
           !step.stageCode.empty() ||
           !step.branch.empty() ||
           !step.input.empty() ||
           !step.output.empty() ||
           !step.owner.empty() ||
           !step.doneCriteria.empty() ||
           !step.engineCheck.empty() ||
           !step.risk.empty() ||
           !step.nextStageLabel.empty() ||
           !step.legacyNotes.empty() ||
           !step.nextIds.empty() ||
           !step.hints.empty();
}

int FindPipelineStepIndexById(const std::vector<PipelineStep>& steps, const std::string& id) {
    if (id.empty()) return -1;
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        if (steps[i].id == id) return i;
    }
    return -1;
}

int FindPipelineStepIndexByStageCode(const std::vector<PipelineStep>& steps, const std::string& stageCode) {
    if (stageCode.empty()) return -1;
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        if (steps[i].stageCode == stageCode) return i;
    }
    return -1;
}

int FindPipelineStepIndexByTitle(const std::vector<PipelineStep>& steps, const std::string& title) {
    if (title.empty()) return -1;
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        if (steps[i].title == title) return i;
    }
    return -1;
}

void AssignIfEmpty(std::string& target, const std::string& fallback) {
    if (target.empty()) target = fallback;
}

std::string AppendPipelineNotes(std::string base, const std::string& title, const std::string& body) {
    if (title.empty() && body.empty()) return base;
    std::string chunk;
    if (!title.empty()) {
        chunk += title;
        if (!body.empty()) chunk += "\n";
    }
    chunk += body;
    if (chunk.empty()) return base;
    if (!base.empty()) base += "\n\n";
    base += chunk;
    return base;
}

std::vector<std::string> LegacyPipelineTargets(const std::string& title) {
    if (title == "0. Реф-борд") return {"ref_board"};
    if (title == "1. Блокинг") return {"blocking"};
    if (title == "2. Создание Low-Poly") return {"hs_low_base", "retopo_low"};
    if (title == "3. Шейдинг") return {"hs_low_base"};
    if (title == "4. High-poly") return {"hs_high_poly"};
    if (title == "5. UV-развертка") return {"hs_uv", "sculpt_uv"};
    if (title == "6. Запекание и текстурирование") return {"hs_bake", "hs_textures", "sculpt_bake", "sculpt_textures"};
    if (title == "7. Экспорт в движок") return {"engine_import", "engine_setup", "release_handoff"};
    return {};
}

void FillPipelineStepMissingFields(PipelineStep& step, const PipelineStep& defaults) {
    AssignIfEmpty(step.id, defaults.id);
    AssignIfEmpty(step.stageCode, defaults.stageCode);
    AssignIfEmpty(step.branch, defaults.branch);
    AssignIfEmpty(step.title, defaults.title);
    AssignIfEmpty(step.description, defaults.description);
    AssignIfEmpty(step.input, defaults.input);
    AssignIfEmpty(step.output, defaults.output);
    AssignIfEmpty(step.owner, defaults.owner);
    AssignIfEmpty(step.doneCriteria, defaults.doneCriteria);
    AssignIfEmpty(step.engineCheck, defaults.engineCheck);
    AssignIfEmpty(step.risk, defaults.risk);
    AssignIfEmpty(step.nextStageLabel, defaults.nextStageLabel);
    if (step.nextIds.empty()) step.nextIds = defaults.nextIds;
    if (step.hints.empty()) step.hints = defaults.hints;
    if (step.legacyNotes.empty()) {
        step.legacyNotes = defaults.legacyNotes;
    } else if (!defaults.legacyNotes.empty() && step.legacyNotes.find(defaults.legacyNotes) == std::string::npos) {
        step.legacyNotes = AppendPipelineNotes(step.legacyNotes, u8"Встроенные заметки Forge Mirror", defaults.legacyNotes);
    }
}

void OverlayPipelineStep(PipelineStep& target, const PipelineStep& loaded) {
    auto assign = [](std::string& dst, const std::string& src) {
        if (!src.empty()) dst = src;
    };
    assign(target.id, loaded.id);
    assign(target.stageCode, loaded.stageCode);
    assign(target.branch, loaded.branch);
    assign(target.title, loaded.title);
    assign(target.description, loaded.description);
    assign(target.input, loaded.input);
    assign(target.output, loaded.output);
    assign(target.owner, loaded.owner);
    assign(target.doneCriteria, loaded.doneCriteria);
    assign(target.engineCheck, loaded.engineCheck);
    assign(target.risk, loaded.risk);
    assign(target.nextStageLabel, loaded.nextStageLabel);
    assign(target.legacyNotes, loaded.legacyNotes);
    if (!loaded.nextIds.empty()) target.nextIds = loaded.nextIds;
    if (!loaded.hints.empty()) target.hints = loaded.hints;
}

std::vector<PipelineStep> MergeLoadedPipelineWithDefaults(const std::vector<PipelineStep>& loaded) {
    const auto& defaults = DefaultPipelineSteps();
    if (loaded.empty()) return defaults;

    const bool legacyOnly = std::all_of(loaded.begin(), loaded.end(), [](const PipelineStep& step) {
        return !PipelineStepHasRichData(step);
    });

    if (legacyOnly) {
        std::vector<PipelineStep> merged = defaults;
        int customIndex = 0;
        for (const auto& step : loaded) {
            const auto targets = LegacyPipelineTargets(step.title);
            if (targets.empty()) {
                PipelineStep extra = step;
                extra.id = "legacy-custom-" + std::to_string(++customIndex);
                extra.stageCode = "L" + std::to_string(customIndex);
                extra.branch = u8"Наследие Forge Mirror";
                extra.nextStageLabel = u8"Уточнить вручную";
                merged.push_back(std::move(extra));
                continue;
            }
            for (const auto& targetId : targets) {
                const int idx = FindPipelineStepIndexById(merged, targetId);
                if (idx < 0) continue;
                merged[idx].legacyNotes = AppendPipelineNotes(merged[idx].legacyNotes, step.title, step.description);
            }
        }
        return merged;
    }

    std::vector<PipelineStep> merged = defaults;
    std::vector<bool> consumed(loaded.size(), false);
    for (size_t i = 0; i < loaded.size(); ++i) {
        const auto& step = loaded[i];
        int idx = FindPipelineStepIndexById(merged, step.id);
        if (idx < 0) idx = FindPipelineStepIndexByStageCode(merged, step.stageCode);
        if (idx < 0) idx = FindPipelineStepIndexByTitle(merged, step.title);
        if (idx < 0) continue;
        OverlayPipelineStep(merged[static_cast<size_t>(idx)], step);
        FillPipelineStepMissingFields(merged[static_cast<size_t>(idx)], defaults[static_cast<size_t>(idx)]);
        consumed[i] = true;
    }

    for (size_t i = 0; i < merged.size(); ++i) {
        FillPipelineStepMissingFields(merged[i], defaults[i]);
    }

    int customIndex = 0;
    for (size_t i = 0; i < loaded.size(); ++i) {
        if (consumed[i]) continue;
        PipelineStep extra = loaded[i];
        if (extra.id.empty()) extra.id = "custom-merged-" + std::to_string(++customIndex);
        if (extra.stageCode.empty()) extra.stageCode = "C" + std::to_string(customIndex);
        if (extra.branch.empty()) extra.branch = u8"Пользовательские блоки";
        merged.push_back(std::move(extra));
    }
    return merged;
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

std::vector<TaskEntry> LoadTasksDataFromFile(const std::filesystem::path& filePath) {
    std::vector<TaskEntry> out;
    const std::string content = ReadAllText(filePath);
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
        if (auto v = find_value("pipelineStepId")) entry.pipelineStepId = *v;
        if (auto v = find_value("pipelineStep")) entry.pipelineStep = *v;
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
        if (auto v = find_value("deadlinePenaltyPercent")) entry.deadlinePenaltyPercent = std::clamp(ParseInt(*v, 0), 0, 100);
        if (auto v = find_value("score")) entry.score = ParseInt(*v, 0);
        if (auto v = find_value("baseXp")) entry.baseXp = ParseInt(*v, 0);
        if (auto v = find_value("basePool")) entry.basePool = ParseInt(*v, 0);
        if (auto v = find_value("createdAt")) entry.createdAt = ParseInt64(*v, 0);
        if (auto v = find_value("assignees")) entry.assignees = ParseAssignees(*v);
        if (auto v = find_value("skillIds")) entry.skillIds = ParseSkillIds(*v);
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

static std::vector<TaskEntry> LoadTasksDataWithRecovery(const std::filesystem::path& storageDir,
                                                        std::vector<std::string>* recoveryWarnings) {
    const auto path = TasksStoragePath(storageDir);
    auto is_valid = [](const std::filesystem::path& candidate) {
        const std::string content = ReadAllText(candidate);
        std::vector<std::unordered_map<std::string, std::string>> objects;
        if (!ParseJsonObjectArrayStrict(content, objects)) return false;
        return std::all_of(objects.begin(), objects.end(), [](const auto& obj) {
            const auto id = obj.find("id");
            return id != obj.end() && !id->second.empty();
        });
    };
    if (is_valid(path)) return LoadTasksDataFromFile(path);

    const auto backupPath = AppRecoveryBackupPath(path);
    if (!is_valid(backupPath)) return {};
    std::filesystem::path damagedCopyPath;
    const bool restored = AppRestoreRecoveryBackup(path, &damagedCopyPath);
    if (recoveryWarnings) {
        std::string warning = restored
            ? std::string(u8"Задачи восстановлены из last-good копии.")
            : std::string(u8"Задачи загружены из last-good копии, но основной файл не восстановлен.");
        if (!damagedCopyPath.empty()) {
            warning += std::string(u8" Повреждённый JSON сохранён: ") + damagedCopyPath.u8string();
        }
        recoveryWarnings->push_back(std::move(warning));
    }
    return LoadTasksDataFromFile(backupPath);
}

std::vector<TaskEntry> LoadTasksData(const std::filesystem::path& storageDir) {
    return LoadTasksDataWithRecovery(storageDir, nullptr);
}

static std::vector<ProjectEntry> LoadProjectsDataFromFile(const std::filesystem::path& filePath) {
    std::vector<ProjectEntry> out;
    const std::string content = ReadAllText(filePath);
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

static std::vector<ProjectEntry> LoadProjectsDataWithRecovery(
    const std::filesystem::path& storageDir,
    std::vector<std::string>* recoveryWarnings) {
    const auto path = ProjectsStoragePath(storageDir);
    auto is_valid = [](const std::filesystem::path& candidate) {
        std::vector<std::unordered_map<std::string, std::string>> objects;
        if (!ParseJsonObjectArrayStrict(ReadAllText(candidate), objects)) return false;
        return std::all_of(objects.begin(), objects.end(), [](const auto& obj) {
            const auto id = obj.find("id");
            const auto name = obj.find("name");
            return id != obj.end() && !id->second.empty() &&
                   name != obj.end() && !name->second.empty();
        });
    };
    if (is_valid(path)) return LoadProjectsDataFromFile(path);

    const auto backupPath = AppRecoveryBackupPath(path);
    if (!is_valid(backupPath)) return {};
    std::filesystem::path damagedCopyPath;
    const bool restored = AppRestoreRecoveryBackup(path, &damagedCopyPath);
    if (recoveryWarnings) {
        std::string warning = restored
            ? std::string(u8"Проекты восстановлены из last-good копии.")
            : std::string(u8"Проекты загружены из last-good копии, но основной файл не восстановлен.");
        if (!damagedCopyPath.empty()) {
            warning += std::string(u8" Повреждённый JSON сохранён: ") + damagedCopyPath.u8string();
        }
        recoveryWarnings->push_back(std::move(warning));
    }
    return LoadProjectsDataFromFile(backupPath);
}

std::vector<ProjectEntry> LoadProjectsData(const std::filesystem::path& storageDir) {
    return LoadProjectsDataWithRecovery(storageDir, nullptr);
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

WorkspaceSyncHealth InspectWorkspaceSyncHealth(const std::filesystem::path& storageDir,
                                               const ModuleToggles& modules) {
    WorkspaceSyncHealth health;
    auto append_issue = [&](WorkspaceSyncFileHealth file, const std::string& details) {
        file.valid = false;
        file.message = details;
        health.issues.push_back(file.relativePath + ": " + details);
        health.files.push_back(std::move(file));
        health.issueCount = static_cast<int>(health.issues.size());
    };
    auto append_ok = [&](WorkspaceSyncFileHealth file, const std::string& details) {
        file.valid = true;
        file.message = details;
        health.files.push_back(std::move(file));
    };
    auto count_non_empty_lines = [&](const std::string& content) {
        size_t count = 0;
        std::istringstream in(content);
        std::string line;
        bool firstLine = true;
        while (std::getline(in, line)) {
            if (firstLine) {
                StripUtf8Bom(line);
                firstLine = false;
            }
            if (!TrimCopy(line).empty()) {
                count += 1;
            }
        }
        return count;
    };

    if (modules.tasks) {
        WorkspaceSyncFileHealth tasksFile;
        tasksFile.relativePath = "meta/tasks.json";
        const auto tasksPath = TasksStoragePath(storageDir);
        std::error_code ec;
        tasksFile.exists = std::filesystem::exists(tasksPath, ec);
        bool tasksDataPresent = false;
        size_t loadedTaskCount = 0;
        if (!tasksFile.exists || ec) {
            append_ok(std::move(tasksFile), u8"файл ещё не создан.");
        } else {
            const std::string content = ReadAllText(tasksPath);
            const std::string trimmed = TrimCopy(content);
            tasksFile.empty = trimmed.empty();
            if (trimmed.empty()) {
                append_issue(std::move(tasksFile), u8"файл пустой.");
            } else {
                const auto objects = ParseJsonObjectArray(content);
                tasksFile.rawEntries = objects.size();
                const auto loaded = LoadTasksDataFromFile(tasksPath);
                loadedTaskCount = loaded.size();
                tasksFile.loadedEntries = loadedTaskCount;
                tasksDataPresent = trimmed != "[]" || loadedTaskCount > 0;
                if (objects.empty() && trimmed != "[]") {
                    append_issue(std::move(tasksFile), u8"JSON не распознан.");
                } else if (!objects.empty() && loadedTaskCount == 0) {
                    append_issue(std::move(tasksFile), u8"все записи отброшены при загрузке.");
                } else if (loadedTaskCount < objects.size()) {
                    append_issue(std::move(tasksFile), u8"часть задач пропущена при загрузке.");
                } else {
                    append_ok(std::move(tasksFile),
                              loadedTaskCount > 0 ? u8"файл читается корректно." : u8"корректный пустой список.");
                }
            }
        }

        WorkspaceSyncFileHealth auditFile;
        auditFile.relativePath = "meta/task-audit.log";
        const auto auditPath = TaskAuditStoragePath(storageDir);
        ec.clear();
        auditFile.exists = std::filesystem::exists(auditPath, ec);
        if (!auditFile.exists || ec) {
            if (tasksDataPresent && loadedTaskCount > 0) {
                append_issue(std::move(auditFile), u8"журнал отсутствует при наличии задач.");
            } else {
                append_ok(std::move(auditFile), u8"журнал ещё не создан.");
            }
        } else {
            const std::string content = ReadAllText(auditPath);
            auditFile.empty = TrimCopy(content).empty();
            auditFile.rawEntries = count_non_empty_lines(content);
            const auto loadedAudit = LoadTaskAuditData(storageDir, 0);
            auditFile.loadedEntries = loadedAudit.size();
            if (auditFile.rawEntries == 0) {
                if (tasksDataPresent && loadedTaskCount > 0) {
                    append_issue(std::move(auditFile), u8"журнал пустой при наличии задач.");
                } else {
                    append_ok(std::move(auditFile), u8"журнал пуст, но задач ещё нет.");
                }
            } else if (auditFile.loadedEntries < auditFile.rawEntries) {
                append_issue(std::move(auditFile), u8"часть строк журнала не распознана.");
            } else {
                append_ok(std::move(auditFile), u8"журнал читается корректно.");
            }
        }
    }

    if (modules.pipeline) {
        WorkspaceSyncFileHealth pipelineFile;
        pipelineFile.relativePath = "meta/pipeline.json";
        const auto pipelinePath = PipelineStoragePath(storageDir);
        std::error_code ec;
        pipelineFile.exists = std::filesystem::exists(pipelinePath, ec);
        if (!pipelineFile.exists || ec) {
            append_ok(std::move(pipelineFile), u8"файл ещё не создан; используется встроенный пайплайн.");
        } else {
            const std::string content = ReadAllText(pipelinePath);
            const std::string trimmed = TrimCopy(content);
            pipelineFile.empty = trimmed.empty();
            if (trimmed.empty()) {
                append_issue(std::move(pipelineFile), u8"файл пустой.");
            } else {
                const auto objects = ParsePipelineObjectsFromContent(content);
                pipelineFile.rawEntries = objects.size();
                size_t titledEntries = 0;
                for (const auto& obj : objects) {
                    auto it = obj.find("title");
                    if (it != obj.end() && !it->second.empty()) {
                        titledEntries += 1;
                    }
                }
                pipelineFile.loadedEntries = LoadPipelineDataFromFile(pipelinePath).size();
                if (objects.empty()) {
                    append_issue(std::move(pipelineFile),
                                 trimmed == "[]" ? u8"список этапов пустой." : u8"JSON не распознан.");
                } else if (titledEntries == 0) {
                    append_issue(std::move(pipelineFile), u8"нет ни одного валидного этапа.");
                } else if (titledEntries < objects.size()) {
                    append_issue(std::move(pipelineFile), u8"часть этапов будет проигнорирована при загрузке.");
                } else {
                    append_ok(std::move(pipelineFile), u8"файл читается корректно.");
                }
            }
        }
    }

    return health;
}

std::vector<PipelineStep> LoadPipelineDataFromFile(const std::filesystem::path& filePath) {
    std::vector<PipelineStep> out;
    const std::string content = ReadAllText(filePath);
    if (content.empty()) {
        return DefaultPipelineSteps();
    }
    auto objects = ParsePipelineObjectsFromContent(content);
    for (const auto& obj : objects) {
        auto find_value = [&](const char* key) -> std::optional<std::string> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            return it->second;
        };
        PipelineStep step;
        if (auto v = find_value("id")) step.id = *v;
        if (auto v = find_value("stage_code")) step.stageCode = *v;
        if (auto v = find_value("branch")) step.branch = *v;
        if (auto v = find_value("title")) step.title = *v;
        if (auto v = find_value("description")) step.description = *v;
        if (auto v = find_value("input")) step.input = *v;
        if (auto v = find_value("output")) step.output = *v;
        if (auto v = find_value("owner")) step.owner = *v;
        if (auto v = find_value("done_criteria")) step.doneCriteria = *v;
        if (auto v = find_value("engine_check")) step.engineCheck = *v;
        if (auto v = find_value("risk")) step.risk = *v;
        if (auto v = find_value("next_stage_label")) step.nextStageLabel = *v;
        if (auto v = find_value("legacy_notes")) step.legacyNotes = *v;
        if (auto v = find_value("next_ids")) step.nextIds = SplitStringList(*v, ';');
        if (auto v = find_value("hints")) step.hints = SplitStringList(*v, '\n');
        if (step.title.empty()) continue;
        out.push_back(std::move(step));
    }
    if (out.empty()) {
        return DefaultPipelineSteps();
    }
    return MergeLoadedPipelineWithDefaults(out);
}

static std::vector<PipelineStep> LoadPipelineDataWithRecovery(
    const std::filesystem::path& storageDir,
    std::vector<std::string>* recoveryWarnings) {
    const auto path = PipelineStoragePath(storageDir);
    auto is_valid = [](const std::filesystem::path& candidate) {
        const auto objects = ParsePipelineObjectsFromContent(ReadAllText(candidate));
        return !objects.empty() && std::all_of(objects.begin(), objects.end(), [](const auto& obj) {
            const auto title = obj.find("title");
            return title != obj.end() && !title->second.empty();
        });
    };
    if (is_valid(path)) return LoadPipelineDataFromFile(path);

    const auto backupPath = AppRecoveryBackupPath(path);
    if (!is_valid(backupPath)) return DefaultPipelineSteps();
    std::filesystem::path damagedCopyPath;
    const bool restored = AppRestoreRecoveryBackup(path, &damagedCopyPath);
    if (recoveryWarnings) {
        std::string warning = restored
            ? std::string(u8"Пайплайн восстановлен из last-good копии.")
            : std::string(u8"Пайплайн загружен из last-good копии, но основной файл не восстановлен.");
        if (!damagedCopyPath.empty()) {
            warning += std::string(u8" Повреждённый JSON сохранён: ") + damagedCopyPath.u8string();
        }
        recoveryWarnings->push_back(std::move(warning));
    }
    return LoadPipelineDataFromFile(backupPath);
}

std::vector<PipelineStep> LoadPipelineData(const std::filesystem::path& storageDir) {
    return LoadPipelineDataWithRecovery(storageDir, nullptr);
}

WorkspaceDataSnapshot LoadWorkspaceDataSnapshot(const std::filesystem::path& storageDir,
                                                const ModuleToggles& modules) {
    WorkspaceDataSnapshot snapshot;
    snapshot.tasks = modules.tasks
        ? LoadTasksDataWithRecovery(storageDir, &snapshot.recoveryWarnings)
        : std::vector<TaskEntry>();
    snapshot.taskAudit = modules.tasks ? LoadTaskAuditData(storageDir) : std::vector<TaskAuditEntry>();
    snapshot.projects = LoadProjectsDataWithRecovery(storageDir, &snapshot.recoveryWarnings);
    snapshot.shortcuts = modules.shortcuts ? LoadShortcutsData(storageDir) : std::vector<ShortcutEntry>();
    snapshot.pipelineSteps = modules.pipeline
        ? LoadPipelineDataWithRecovery(storageDir, &snapshot.recoveryWarnings)
        : std::vector<PipelineStep>();
    snapshot.professions = modules.professions ? LoadProfessionsData(storageDir) : std::vector<ProfessionEntry>();
    snapshot.bannerTexts = LoadBannerTexts(storageDir);
    snapshot.rulesConfig = LoadGameplayConfig(storageDir);
    snapshot.vault = LoadStorageVault(storageDir);
    return snapshot;
}
