static float BannerHeight() {
    return 28.0f;
}

static void EnsureBannerSelection(GuiState& state) {
    if (state.bannerTexts.empty()) {
        state.bannerTextCurrent.clear();
        return;
    }
    if (state.bannerTextCurrent.empty()) {
        state.bannerTextCurrent = state.bannerTexts.front();
        return;
    }
    const auto it = std::find(state.bannerTexts.begin(), state.bannerTexts.end(), state.bannerTextCurrent);
    if (it == state.bannerTexts.end()) {
        state.bannerTextCurrent = state.bannerTexts.front();
    }
}

static void UpdateBannerRotation(GuiState& state) {
    const std::int64_t now = NowSeconds();
    if (state.bannerTexts.empty()) {
        state.bannerTextCurrent.clear();
        state.bannerLastRotateAt = now;
        return;
    }
    EnsureBannerSelection(state);
    if (state.bannerLastRotateAt <= 0) {
        state.bannerLastRotateAt = now;
        return;
    }
    if (now - state.bannerLastRotateAt < 60) {
        return;
    }
    static std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<size_t> dist(0, state.bannerTexts.size() - 1);
    state.bannerTextCurrent = state.bannerTexts[dist(rng)];
    state.bannerLastRotateAt = now;
}

static void DrawBannerStrip(GuiState& state) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        const ImVec2 pos = viewport->WorkPos;
        const ImVec2 size(viewport->WorkSize.x, BannerHeight());
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    }
    ImGui::Begin("##banner_strip", nullptr, flags);
    DrawWindowBackground(state.ui, UiWindowId::MainMenu, state.storageDir);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = BannerHeight();
    const float textHeight = ImGui::GetTextLineHeight();
    const float y = std::max(0.0f, (height - textHeight) * 0.5f);
    const float baseX = ImGui::GetCursorPosX();
    const float left = ImGui::GetCursorScreenPos().x;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - style.WindowPadding.x;
    const float avail = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    if (!state.bannerTextCurrent.empty()) {
        const std::string& text = state.bannerTextCurrent;
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        if (textSize.x <= avail) {
            ImGui::SetCursorPosY(y);
            ImGui::SetCursorPosX(baseX + std::max(0.0f, (avail - textSize.x) * 0.5f));
            ImGui::TextUnformatted(text.c_str());
        } else {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float gap = style.ItemSpacing.x * 4.0f;
            const float loop = textSize.x + gap;
            const float speed = 40.0f;
            const float t = static_cast<float>(ImGui::GetTime());
            const float offset = std::fmod(t * speed, loop);
            const float clipMinX = left;
            const float clipMaxX = right;
            const float baseY = ImGui::GetWindowPos().y + y;
            drawList->PushClipRect(ImVec2(clipMinX, ImGui::GetWindowPos().y),
                                   ImVec2(clipMaxX, ImGui::GetWindowPos().y + height), true);
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            float x = clipMinX - offset;
            drawList->AddText(ImVec2(x, baseY), col, text.c_str());
            drawList->AddText(ImVec2(x + loop, baseY), col, text.c_str());
            drawList->PopClipRect();
        }
    }
    ImGui::End();
}

void DrawBannerAdminPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::BannerAdmin, state.storageDir);
    DrawCardHeader(u8"Баннер");
    if (!state.isAdmin) {
        ImGui::TextDisabled(u8"Только для администратора.");
        return;
    }
    ImGui::TextDisabled(u8"Хранится в meta/banner.json, синхронизируется через облако.");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextDisabled(u8"Новая фраза");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##banner_input", u8"Введите текст", state.bannerInput.data(), state.bannerInput.size());
    const bool editing = state.bannerEditIndex >= 0 && state.bannerEditIndex < static_cast<int>(state.bannerTexts.size());
    const char* actionLabel = editing ? u8"Сохранить" : u8"Добавить";
    if (ImGui::Button(actionLabel, ImVec2(-1.0f, 0.0f))) {
        std::string text = TrimStringGui(state.bannerInput.data());
        if (!text.empty()) {
            if (editing) {
                state.bannerTexts[static_cast<size_t>(state.bannerEditIndex)] = text;
                state.bannerEditIndex = -1;
                SetStatus(state, u8"Фраза обновлена.", 0.45f, 0.9f, 0.45f);
            } else {
                state.bannerTexts.push_back(text);
                SetStatus(state, u8"Фраза добавлена.", 0.45f, 0.9f, 0.45f);
            }
            std::fill(state.bannerInput.begin(), state.bannerInput.end(), '\0');
            SaveBannerTexts(state.storageDir, state.bannerTexts);
            EnsureBannerSelection(state);
            state.bannerLastRotateAt = NowSeconds();
        } else {
            SetStatus(state, u8"Введите текст фразы.", 1.0f, 0.75f, 0.35f);
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    if (editing) {
        if (ImGui::SmallButton(u8"Отмена")) {
            state.bannerEditIndex = -1;
            std::fill(state.bannerInput.begin(), state.bannerInput.end(), '\0');
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    DrawCardHeader(u8"Фразы");
    const float listHeight = std::max(180.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f);
    if (ImGui::BeginChild("banner_list", ImVec2(0.0f, listHeight), true)) {
        if (state.bannerTexts.empty()) {
            ImGui::TextDisabled(u8"Фраз пока нет.");
        } else if (ImGui::BeginTable("banner_rows", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(u8"Текст", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(u8"Правка", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(u8"Удалить", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            for (size_t i = 0; i < state.bannerTexts.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(state.bannerTexts[i].c_str());
                ImGui::TableSetColumnIndex(1);
                if (ImGui::SmallButton(u8"Редактировать")) {
                    state.bannerEditIndex = static_cast<int>(i);
                    std::snprintf(state.bannerInput.data(), state.bannerInput.size(), "%s", state.bannerTexts[i].c_str());
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton(u8"Удалить")) {
                    state.bannerTexts.erase(state.bannerTexts.begin() + static_cast<std::ptrdiff_t>(i));
                    SaveBannerTexts(state.storageDir, state.bannerTexts);
                    EnsureBannerSelection(state);
                    state.bannerLastRotateAt = NowSeconds();
                    SetStatus(state, u8"Фраза удалена.", 0.45f, 0.9f, 0.45f);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}
