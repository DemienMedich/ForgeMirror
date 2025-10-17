cmake_minimum_required(VERSION 3.15)
project(JobSkill LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(JobSkill
    src/Skill.cpp
    src/Profile.cpp
    src/FakeStorage.cpp
    src/FakeApiClient.cpp
    src/FileStorage.cpp
    src/SkillCatalog.cpp
    src/AppUtils.cpp
    main.cpp
)

target_include_directories(JobSkill PRIVATE include)

# Версия приложения
target_compile_definitions(JobSkill PRIVATE APP_VERSION="0.1.0")

# Для Windows корректно установить кодировку консоли при необходимости вручную в приложении

option(BUILD_IMGUI_GUI "Build ImGui-based GUI" OFF)

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

    add_executable(JobSkillGui
        gui/GuiApp.cpp
        src/Skill.cpp
        src/Profile.cpp
        src/FileStorage.cpp
        src/SkillCatalog.cpp
        src/AppUtils.cpp
    )

    target_sources(JobSkillGui PRIVATE ${IMGUI_SOURCES})
    target_include_directories(JobSkillGui PRIVATE include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(JobSkillGui PRIVATE glfw OpenGL::GL)
    if (WIN32)
        target_link_libraries(JobSkillGui PRIVATE opengl32)
    endif()

    add_custom_command(TARGET JobSkillGui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:glfw>
            $<TARGET_FILE_DIR:JobSkillGui>)
endif()

