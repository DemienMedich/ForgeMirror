struct IconTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

struct IconCache {
    std::unordered_map<std::string, IconTexture> textures;
    std::unordered_set<std::string> missing;
};

IconCache& GetIconCache() {
    static IconCache cache;
    return cache;
}

std::string NormalizeIconKey(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = path.lexically_normal();
    if (!normalized.is_absolute()) {
        normalized = std::filesystem::absolute(normalized, ec);
        if (ec) {
            normalized = path.lexically_normal();
        }
    }
    auto key = normalized.generic_string();
    if (key.empty()) key = path.generic_string();
    return key;
}

std::optional<std::filesystem::path> ResolveIconPath(const std::string& icon, const std::filesystem::path& storageDir) {
    if (icon.empty()) return std::nullopt;
    std::filesystem::path path(icon);
    std::error_code ec;
    if (path.is_relative()) {
        const std::filesystem::path candidates[] = {
            storageDir / path,
            std::filesystem::current_path() / path,
            storageDir.parent_path() / path
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    if (std::filesystem::exists(path, ec)) {
        return path;
    }
    return std::nullopt;
}

struct IconChoice {
    std::string label;
    std::string relativePath;
    std::filesystem::path absolutePath;
};

std::vector<IconChoice> LoadAchievementIconChoices(const std::filesystem::path& storageDir) {
    std::vector<IconChoice> out;
    std::error_code ec;
    auto iconDir = storageDir / "achievements" / "icons";
    if (!std::filesystem::exists(iconDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(iconDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".png") continue;
        const auto filename = entry.path().filename();
        auto rel = std::filesystem::path("achievements") / "icons" / filename;
        IconChoice choice;
        choice.label = filename.string();
        choice.relativePath = rel.generic_string();
        choice.absolutePath = entry.path();
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(), [](const IconChoice& a, const IconChoice& b) {
        return a.label < b.label;
    });
    return out;
}

struct BackgroundChoice {
    std::string label;
    std::string relativePath;
    std::filesystem::path absolutePath;
};

std::vector<BackgroundChoice> LoadUiBackgroundChoices(const std::filesystem::path& storageDir) {
    std::vector<BackgroundChoice> out;
    std::error_code ec;
    auto bgDir = storageDir / "ui" / "backgrounds";
    if (!std::filesystem::exists(bgDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(bgDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".png") continue;
        const auto filename = entry.path().filename();
        auto rel = std::filesystem::path("ui") / "backgrounds" / filename;
        BackgroundChoice choice;
        choice.label = filename.string();
        choice.relativePath = rel.generic_string();
        choice.absolutePath = entry.path();
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(), [](const BackgroundChoice& a, const BackgroundChoice& b) {
        return a.label < b.label;
    });
    return out;
}

bool LoadIconTexture(const std::filesystem::path& path, IconTexture& out) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_set_flip_vertically_on_load(0);
    std::string pathStr = path.string();
    unsigned char* data = stbi_load(pathStr.c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return false;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    GLint lastTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTex);
    GLint lastUnpack = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &lastUnpack);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, lastUnpack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTex));

    stbi_image_free(data);
    out.id = tex;
    out.width = w;
    out.height = h;
    return true;
}

const IconTexture* GetIconTexture(const std::filesystem::path& path) {
    auto& cache = GetIconCache();
    const std::string key = NormalizeIconKey(path);
    if (auto it = cache.textures.find(key); it != cache.textures.end()) {
        return &it->second;
    }
    if (cache.missing.find(key) != cache.missing.end()) {
        return nullptr;
    }
    IconTexture tex;
    if (!LoadIconTexture(path, tex)) {
        cache.missing.insert(key);
        return nullptr;
    }
    auto [it, inserted] = cache.textures.emplace(key, tex);
    return &it->second;
}

ImVec2 FitIconSize(const IconTexture& tex, float maxSize) {
    if (tex.width <= 0 || tex.height <= 0) return ImVec2(maxSize, maxSize);
    const float maxDim = static_cast<float>(std::max(tex.width, tex.height));
    const float scale = maxDim > 0.0f ? (maxSize / maxDim) : 1.0f;
    return ImVec2(tex.width * scale, tex.height * scale);
}

void ReleaseIconTextures() {
    auto& cache = GetIconCache();
    for (auto& kv : cache.textures) {
        if (kv.second.id != 0) {
            glDeleteTextures(1, &kv.second.id);
            kv.second.id = 0;
        }
    }
    cache.textures.clear();
    cache.missing.clear();
}
