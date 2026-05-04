void FinalizeFrame(GuiState& state, GLFWwindow* window) {
    if (state.requestExit) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    // Windows capture of fullscreen OpenGL windows can lag by one frame.
    // In fullscreen, force GPU completion before present so the compositor sees the current frame.
#if defined(_WIN32)
    if (state.ui.windowFullscreen) {
        glFinish();
    }
#endif
    glfwSwapBuffers(window);

#if defined(_WIN32)
    if (state.ui.windowFullscreen) {
        DwmFlush();
    }
#endif
}
