static const std::vector<PipelineStep>& DefaultPipelineSteps() {
    static const std::vector<PipelineStep> kDefaultPipelineSteps = {
        {"0. Реф-борд",
R"(Сборка реф-листа, наряду с блокингом - важнейшие этапы!
При надобности, создается отдельный! муд-борд!)"},
        {"1. Блокинг",
R"(Блокинг - это процесс создания базовых пропорций и соотношений между моделями и сценами.

На этом этапе особенно важно правильно отобразить пропорции отдельных деталей и модели в целом. В отличие от других этапов, здесь не так важно уделять внимание сетке, как точности и скорости.

Блокинг позволяет увидеть общую форму и силуэт будущей модели, что дает возможность внести необходимые коррективы, если это необходимо.

!!!Важно не вдаваться в детали на данном этапе!!!)"},
        {"2. Создание Low-Poly",
R"(На этом этапе каждый элемент прорабатывается отдельно. Формы уточняются, а топология модели приводится в порядок.

Всё, что требует симметрии, выполняется с помощью модификатора Mirror и включённого параметра Clipping. Для удобства можно добавить объект-пустышку в нулевой координате, чтобы настроить зеркальное отражение на него.

ДЕЛАЕМ CNTRL+A -> SCALE)"},
        {"3. Шейдинг",
R"(На этом этапе мы расставляем шарпэджи, проверяем шейдинг модели и устанавливаем параметры сглаживания.)"},
        {"4. High-poly",
R"(В нашей сцене мы присваиваем всем объектам суффикс "_low" и создаем их дубликат. В копиях меняем суффикс на "_high", используя встроенный инструмент Blender - Batch Rename.

На одном из объектов мы настраиваем модификаторы Bevel и Subdivision Surface:

* Bevel: - Amount: 0.001 - Segments: 2 - Limit Method: Weight - Profile->Shape: 1
* Subdivision Surface: - Levels: 3

Затем, с помощью аддона Copy Attribute (который можно установить через Preferences), мы копируем эти модификаторы на все объекты High-poly.

После этого мы проходим по каждому объекту и устанавливаем значение Bevel Weight (вкладка Item, меню "N") на 1, применяя этот параметр ко всем шарпэджам.

Если требуется, мы модифицируем сетку, чтобы модель соответствовала Low-poly версии.)"},
        {"5. UV-развертка",
R"(Производится только на Low-poly.

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
        {"6. Запекание и текстурирование",
R"(Более подробно я расскажу об этом позже, а сейчас поделюсь основными шагами:

1. Проводим запекание по всем объектам, используя минимальную длину луча. Проверяем, чтобы области High и Low не пересекались.
2. Выполняем пробные запекания, анализируем результаты и завершаем финальное запекание на уровне 8k.
3. Приступаем к текстурированию. В конце добавляем постобработку в виде Ambient Occlusion и, при необходимости, дополнительные затенения к текстурам.)"},
        {"7. Экспорт в движок",
R"(????При экспорте из Blender, в настройках эспорта во вкладке Geometry, Smoothing меняем на Face.

Экспорт модели и текстур в движок: Модель и текстуры распределяются по соответствующим папкам (Mesh, Material, Texture). Я отдельно опубликую информацию о том, как именовать текстуры и меш.

Настройка шейдеров (материалов) и текстур, анимаций: На этом этапе мы настраиваем шейдеры и текстуры, а затем проверяем результат.

????Соблюдаем нейминг папок, файлов, иерархию.

Упаковка, создание PreFab и отправка разработчикам: все необходимые файлы упаковываются и отправляются разработчикам для дальнейшей работы.

?Каждый этап отправляется Арт-лиду на одобрение!)"}
    };
    return kDefaultPipelineSteps;
}

static std::filesystem::path PipelineStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "pipeline.json";
}

static std::string pipeline_read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool pipeline_write_all(const std::filesystem::path& p, const std::string& data) {
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

static std::string pipeline_json_escape(const std::string& value) {
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

static void pipeline_skip_ws(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

static bool pipeline_parse_string(const std::string& text, size_t& pos, std::string& out) {
    pipeline_skip_ws(text, pos);
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

static bool pipeline_parse_object(const std::string& text, size_t& pos,
                                  std::unordered_map<std::string, std::string>& out) {
    pipeline_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    ++pos;
    while (pos < text.size()) {
        pipeline_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        std::string key;
        if (!pipeline_parse_string(text, pos, key)) return false;
        pipeline_skip_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        ++pos;
        std::string value;
        if (!pipeline_parse_string(text, pos, value)) return false;
        out[key] = value;
        pipeline_skip_ws(text, pos);
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

static std::vector<std::unordered_map<std::string, std::string>> pipeline_parse_object_array(const std::string& text) {
    std::vector<std::unordered_map<std::string, std::string>> objects;
    size_t pos = 0;
    pipeline_skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '[') return objects;
    ++pos;
    while (pos < text.size()) {
        pipeline_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        std::unordered_map<std::string, std::string> obj;
        if (!pipeline_parse_object(text, pos, obj)) break;
        objects.push_back(std::move(obj));
        pipeline_skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return objects;
}

static std::vector<PipelineStep> LoadPipeline(const std::filesystem::path& storageDir) {
    std::vector<PipelineStep> out;
    const auto path = PipelineStoragePath(storageDir);
    std::string content = pipeline_read_all(path);
    if (content.empty()) {
        return DefaultPipelineSteps();
    }
    const auto objects = pipeline_parse_object_array(content);
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

static bool SavePipeline(const std::filesystem::path& storageDir, const std::vector<PipelineStep>& steps) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        out << "  {\"title\":\"" << pipeline_json_escape(step.title)
            << "\",\"description\":\"" << pipeline_json_escape(step.description) << "\"}";
        if (i + 1 < steps.size()) out << ",";
        out << "\n";
    }
    out << "]";
    return pipeline_write_all(PipelineStoragePath(storageDir), out.str());
}
