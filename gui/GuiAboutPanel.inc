void DrawAboutPanel(GuiState& state) {
    DrawWindowBackground(state.ui, UiWindowId::About, state.storageDir);
    if (BeginCard("about_card")) {
        DrawCardHeader(u8"О программе");
        ImGui::TextDisabled(u8"ForgeMirror - геймифицированный трекер навыков и задач.");
        ImGui::TextWrapped(u8"Инструмент помогает фиксировать прогресс, фокусироваться на развитии и видеть динамику.");
        ImGui::Separator();
        ImGui::TextDisabled(u8"Авторы");
        ImGui::BulletText("ChatGPT");
        ImGui::BulletText("Codex");
        ImGui::BulletText(u8"Роман Рощин");
        ImGui::Separator();
        ImGui::TextDisabled(u8"Версия");
#ifdef APP_VERSION
        ImGui::TextUnformatted(APP_VERSION);
#else
        ImGui::TextUnformatted("dev");
#endif
    }
    EndCard();
}
