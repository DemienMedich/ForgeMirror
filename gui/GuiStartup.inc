void ConfigureLocale() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "");
    try {
        std::locale sys("");
        std::locale::global(sys);
        std::cout.imbue(sys);
        std::cerr.imbue(sys);
        std::clog.imbue(sys);
        std::wcout.imbue(sys);
        std::wcerr.imbue(sys);
    } catch (...) {
#if defined(_WIN32)
        try {
            std::locale utf8(".UTF-8");
            std::locale::global(utf8);
            std::cout.imbue(utf8);
            std::cerr.imbue(utf8);
            std::clog.imbue(utf8);
            std::wcout.imbue(utf8);
            std::wcerr.imbue(utf8);
        } catch (...) {
            // ignore
        }
#endif
    }
}

void LoadGuiFonts(ImGuiIO& io) {
    const std::filesystem::path fontCandidates[] = {
        "gui/fonts/Rubik-Regular.ttf",
        "fonts/Rubik-Regular.ttf",
        "Rubik-Regular.ttf",
        "gui/fonts/Roboto-Medium.ttf",
        "gui/fonts/Roboto-Regular.ttf",
        "fonts/Roboto-Medium.ttf",
        "fonts/Roboto-Regular.ttf",
        "Roboto-Medium.ttf",
        "Roboto-Regular.ttf",
        "libs/imgui/misc/fonts/Roboto-Medium.ttf",
        "libs/imgui/misc/fonts/DroidSans.ttf",
        "libs/imgui/misc/fonts/Karla-Regular.ttf",
        "libs/imgui/misc/fonts/Cousine-Regular.ttf"
    };
    ImFont* primaryFont = nullptr;
    for (const auto& candidate : fontCandidates) {
        if (auto resolved = FindAssetUpwards(candidate)) {
            primaryFont = io.Fonts->AddFontFromFileTTF(resolved->string().c_str(), 18.0f, nullptr,
                                                       io.Fonts->GetGlyphRangesCyrillic());
            if (primaryFont) break;
        }
    }
    if (!primaryFont) {
        ImFont* fallback = io.Fonts->AddFontDefault();
        (void)fallback;
        if (auto droid = FindAssetUpwards("libs/imgui/misc/fonts/DroidSans.ttf")) {
            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(droid->string().c_str(), 18.0f, &cfg,
                                         io.Fonts->GetGlyphRangesCyrillic());
        }
    }
}

void ConfigureLayoutPath(ImGuiIO& io, const std::filesystem::path& storageDir, std::string& layoutPath) {
    auto metaDir = storageDir / "meta";
    std::error_code ec;
    std::filesystem::create_directories(metaDir, ec);
    layoutPath = (metaDir / "gui-layout.ini").string();
    if (!layoutPath.empty()) {
        io.IniFilename = layoutPath.c_str();
    }
}

void InitImGuiBackend(GLFWwindow* window) {
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
}

ImGuiIO& InitImGuiContext() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    return io;
}
