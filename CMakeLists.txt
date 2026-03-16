cmake_minimum_required(VERSION 3.15)
project(ForgeMirror LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # Ensure UTF-8 string literals so Russian text renders correctly in both CLI and GUI.
    add_compile_options(/utf-8)
endif()

add_executable(ForgeMirror
    src/Skill.cpp
    src/Profile.cpp
    src/FakeStorage.cpp
    src/FakeApiClient.cpp
    src/FileStorage.cpp
    src/AppProfileService.cpp
    src/AppProfileMutationService.cpp
    src/AppTaskProjectService.cpp
    src/AppProfessionService.cpp
    src/AppPipelineService.cpp
    src/AppMetaService.cpp
    src/AppShortcutsService.cpp
    src/AppWorkspaceDataService.cpp
    src/SkillCatalog.cpp
    src/AppUtils.cpp
    src/CloudSync.cpp
    src/GameplayConfig.cpp
    main.cpp
)

target_include_directories(ForgeMirror PRIVATE include)

# Версия приложения
set(APP_VERSION "0.4.20")
target_compile_definitions(ForgeMirror PRIVATE APP_VERSION="${APP_VERSION}")
set_target_properties(ForgeMirror PROPERTIES OUTPUT_NAME "ForgeMirror")

# Для Windows корректно установить кодировку консоли при необходимости вручную в приложении

option(BUILD_IMGUI_GUI "Build ImGui-based GUI" OFF)
option(BUILD_CORE_TESTS "Build core smoke tests" OFF)

if(BUILD_IMGUI_GUI)
    if(NOT IMGUI_DIR)
        message(FATAL_ERROR "Set IMGUI_DIR to the root of the Dear ImGui repository.")
    endif()

    find_package(OpenGL REQUIRED)
    find_package(glfw3 CONFIG REQUIRED)

    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp)

    add_executable(ForgeMirrorGui WIN32
        gui/ForgeMirrorGui.rc
        gui/GuiApp.cpp
        gui/GuiActions.cpp
        libs/ufbx/ufbx.c
        src/Skill.cpp
        src/Profile.cpp
        src/FileStorage.cpp
        src/AppProfileService.cpp
        src/AppProfileMutationService.cpp
        src/AppTaskProjectService.cpp
    src/AppProfessionService.cpp
    src/AppPipelineService.cpp
    src/AppMetaService.cpp
    src/AppShortcutsService.cpp
        src/AppWorkspaceDataService.cpp
        src/SkillCatalog.cpp
        src/AppUtils.cpp
        src/CloudSync.cpp
        src/GameplayConfig.cpp
    )

    target_sources(ForgeMirrorGui PRIVATE ${IMGUI_SOURCES})
    target_include_directories(ForgeMirrorGui PRIVATE include libs/ufbx ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_compile_definitions(ForgeMirrorGui PRIVATE APP_VERSION="${APP_VERSION}")
    set_target_properties(ForgeMirrorGui PROPERTIES OUTPUT_NAME "ForgeMirrorGui")
    target_link_libraries(ForgeMirrorGui PRIVATE glfw OpenGL::GL)
    if (WIN32)
        target_link_libraries(ForgeMirrorGui PRIVATE opengl32 winmm comdlg32 shell32)
    endif()

    add_custom_command(TARGET ForgeMirrorGui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:glfw>
            $<TARGET_FILE_DIR:ForgeMirrorGui>)
endif()

if(BUILD_CORE_TESTS)
    add_executable(smoke_core
        tests/smoke_core.cpp
        src/Profile.cpp
        src/Skill.cpp
        src/SkillCatalog.cpp
        src/AppUtils.cpp
        src/GameplayConfig.cpp
        src/CloudSync.cpp
        src/FileStorage.cpp
        src/FakeStorage.cpp
        src/AppProfileService.cpp
        src/AppProfileMutationService.cpp
        src/AppTaskProjectService.cpp
    src/AppProfessionService.cpp
    src/AppPipelineService.cpp
    src/AppMetaService.cpp
    src/AppShortcutsService.cpp
        src/AppWorkspaceDataService.cpp
    )
    target_include_directories(smoke_core PRIVATE include)
    target_compile_definitions(smoke_core PRIVATE APP_VERSION="0.0.0")
endif()






