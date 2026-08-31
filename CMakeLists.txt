cmake_minimum_required(VERSION 3.15)
project(ForgeMirror LANGUAGES C CXX)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION)
string(STRIP "${APP_VERSION}" APP_VERSION)
if(NOT APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain SemVer MAJOR.MINOR.PATCH, got: ${APP_VERSION}")
endif()
string(REPLACE "." ";" APP_VERSION_PARTS "${APP_VERSION}")
list(GET APP_VERSION_PARTS 0 APP_VERSION_MAJOR)
list(GET APP_VERSION_PARTS 1 APP_VERSION_MINOR)
list(GET APP_VERSION_PARTS 2 APP_VERSION_PATCH)
configure_file(qt/ForgeMirrorVersion.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ForgeMirrorVersion.rc" @ONLY)

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
    src/AppProfileDeletionService.cpp
    src/AppProfileMutationService.cpp
    src/AppRecoveryStorage.cpp
    src/AppTaskProjectService.cpp
    src/AppTaskWorkflowService.cpp
    src/AppTeamValueReportService.cpp
    src/AppProfessionService.cpp
    src/AppSkillService.cpp
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

target_compile_definitions(ForgeMirror PRIVATE APP_VERSION="${APP_VERSION}")
if(WIN32)
    target_sources(ForgeMirror PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ForgeMirrorVersion.rc")
endif()
set_target_properties(ForgeMirror PROPERTIES OUTPUT_NAME "ForgeMirror")

# Для Windows корректно установить кодировку консоли при необходимости вручную в приложении

option(BUILD_IMGUI_GUI "Build ImGui-based GUI" OFF)
option(BUILD_CORE_TESTS "Build core smoke tests" OFF)
option(BUILD_QT_GUI "Build the Qt Widgets migration client" OFF)

if(BUILD_QT_GUI)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
    # Reuse the exact CLI domain implementation, without its entry point.
    get_target_property(FORGEMIRROR_CORE_SOURCES ForgeMirror SOURCES)
    list(REMOVE_ITEM FORGEMIRROR_CORE_SOURCES main.cpp)
    add_library(ForgeMirrorQtCore STATIC ${FORGEMIRROR_CORE_SOURCES})
    target_sources(ForgeMirrorQtCore PRIVATE src/AppTaskCompletionService.cpp)
    target_include_directories(ForgeMirrorQtCore PUBLIC include)
    target_compile_definitions(ForgeMirrorQtCore PRIVATE APP_VERSION="${APP_VERSION}")
    add_library(ForgeMirrorQtUi STATIC qt/QtWindow.cpp qt/QtWorkspace.cpp qt/QtTaskCompletionDialog.cpp qt/QtTheme.cpp)
    target_sources(ForgeMirrorQtUi PRIVATE qt/QtProfileDialogs.cpp qt/QtSkillEditor.cpp qt/QtPipelineEditor.cpp qt/QtPipelineTransition.cpp qt/QtProfileSession.cpp qt/QtProfessionEditor.cpp qt/QtAchievements.cpp qt/QtPomodoro.cpp qt/QtRulesEditor.cpp qt/QtVaultEditor.cpp qt/QtDisplaySettings.cpp qt/QtReportExport.cpp)
    target_include_directories(ForgeMirrorQtUi PUBLIC qt)
    target_link_libraries(ForgeMirrorQtUi PUBLIC ForgeMirrorQtCore Qt6::Widgets)
    if(WIN32)
        target_link_libraries(ForgeMirrorQtUi PUBLIC winmm)
    endif()
    target_compile_definitions(ForgeMirrorQtUi PRIVATE APP_VERSION="${APP_VERSION}")
    add_executable(ForgeMirrorQt WIN32 qt/main.cpp)
    target_compile_definitions(ForgeMirrorQt PRIVATE APP_VERSION="${APP_VERSION}")
    if(WIN32)
        target_sources(ForgeMirrorQt PRIVATE gui/ForgeMirrorGui.rc "${CMAKE_CURRENT_BINARY_DIR}/ForgeMirrorVersion.rc")
    endif()
    target_link_libraries(ForgeMirrorQt PRIVATE ForgeMirrorQtUi)
    enable_testing()
    add_executable(smoke_qt tests/smoke_qt.cpp)
    target_link_libraries(smoke_qt PRIVATE ForgeMirrorQtUi Qt6::Test)
    add_test(NAME smoke_qt COMMAND smoke_qt)
    set_tests_properties(smoke_qt PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()

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
        src/AppRecoveryStorage.cpp
        src/AppTaskProjectService.cpp
        src/AppTaskWorkflowService.cpp
        src/AppTeamValueReportService.cpp
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
    if(WIN32)
        target_sources(ForgeMirrorGui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ForgeMirrorVersion.rc")
    endif()

    target_sources(ForgeMirrorGui PRIVATE ${IMGUI_SOURCES})
    target_include_directories(ForgeMirrorGui PRIVATE include libs/ufbx ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_compile_definitions(ForgeMirrorGui PRIVATE APP_VERSION="${APP_VERSION}")
    set_target_properties(ForgeMirrorGui PROPERTIES OUTPUT_NAME "ForgeMirrorGui")
    target_link_libraries(ForgeMirrorGui PRIVATE glfw OpenGL::GL)
    if (WIN32)
        target_link_libraries(ForgeMirrorGui PRIVATE opengl32 winmm comdlg32 shell32 dwmapi)
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
        src/AppRecoveryStorage.cpp
        src/AppTaskProjectService.cpp
        src/AppTaskWorkflowService.cpp
        src/AppTeamValueReportService.cpp
    src/AppProfessionService.cpp
    src/AppPipelineService.cpp
    src/AppMetaService.cpp
    src/AppShortcutsService.cpp
        src/AppWorkspaceDataService.cpp
    )
    target_include_directories(smoke_core PRIVATE include)
    target_compile_definitions(smoke_core PRIVATE APP_VERSION="${APP_VERSION}")
endif()






