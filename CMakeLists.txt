cmake_minimum_required(VERSION 3.16)

project(Lumen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEN_BUILD_TESTS "Build headless unit tests" ON)
option(LUMEN_BUILD_BUNDLED_IMGUI "Build the vendored Dear ImGui source for tests and local development" ON)
option(LUMEN_ENABLE_WARNINGS "Enable compiler warnings for Lumen targets" ON)
option(LUMEN_BUILD_GALLERY "Build the native X11/OpenGL Lumen gallery when prerequisites exist" ON)

if(NOT TARGET imgui::imgui)
    if(LUMEN_BUILD_BUNDLED_IMGUI)
        add_library(lumen_imgui STATIC
            imgui/imgui.cpp
            imgui/imgui_draw.cpp
            imgui/imgui_tables.cpp
            imgui/imgui_widgets.cpp
        )
        add_library(imgui::imgui ALIAS lumen_imgui)
        target_include_directories(lumen_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
        target_compile_features(lumen_imgui PUBLIC cxx_std_17)
    else()
        find_package(imgui CONFIG REQUIRED)
    endif()
endif()

add_library(lumen_core STATIC
    core/arena.cpp
    core/clock.cpp
    core/context.cpp
    core/id.cpp
    core/motion.cpp
    core/node.cpp
    core/imgui_bridge.cpp
    core/state_store.cpp
)
add_library(lumen::core ALIAS lumen_core)

target_include_directories(lumen_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(lumen_core PUBLIC imgui::imgui)
target_compile_features(lumen_core PUBLIC cxx_std_17)

if(LUMEN_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(lumen_core PRIVATE /W4 /permissive-)
    else()
        target_compile_options(lumen_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endif()

# Kit components are built only from the public engine API.
add_library(lumen_kit STATIC
    kit/button.cpp
    kit/controls.cpp
    kit/ink.cpp
    kit/overlays.cpp
)
add_library(lumen::kit ALIAS lumen_kit)
target_include_directories(lumen_kit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kit/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(lumen_kit PUBLIC lumen::core)
target_compile_features(lumen_kit PUBLIC cxx_std_17)
if(LUMEN_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(lumen_kit PRIVATE /W4 /permissive-)
    else()
        target_compile_options(lumen_kit PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endif()

if(LUMEN_BUILD_TESTS)
    enable_testing()
    add_executable(lumen_tests tests/test_main.cpp)
    target_link_libraries(lumen_tests PRIVATE lumen::core)
    target_compile_features(lumen_tests PRIVATE cxx_std_17)
    if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
        target_compile_options(lumen_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME lumen_m1_tests COMMAND lumen_tests)

    add_executable(lumen_ui_tests tests/test_ui.cpp)
    target_link_libraries(lumen_ui_tests PRIVATE lumen::core)
    target_compile_definitions(lumen_ui_tests PRIVATE LUMEN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(lumen_ui_tests PRIVATE cxx_std_17)
    if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
        target_compile_options(lumen_ui_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME lumen_ui_tests COMMAND lumen_ui_tests)

    add_executable(lumen_behavior_tests tests/test_behaviors.cpp)
    target_link_libraries(lumen_behavior_tests PRIVATE lumen::core)
    target_compile_features(lumen_behavior_tests PRIVATE cxx_std_17)
    if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
        target_compile_options(lumen_behavior_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME lumen_behavior_tests COMMAND lumen_behavior_tests)

    add_executable(lumen_layer_tests tests/test_layers.cpp)
    target_link_libraries(lumen_layer_tests PRIVATE lumen::core)
    target_compile_features(lumen_layer_tests PRIVATE cxx_std_17)
    if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
        target_compile_options(lumen_layer_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME lumen_layer_tests COMMAND lumen_layer_tests)

    add_executable(lumen_kit_tests tests/test_kit.cpp)
    target_link_libraries(lumen_kit_tests PRIVATE lumen::kit)
    target_compile_features(lumen_kit_tests PRIVATE cxx_std_17)
    if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
        target_compile_options(lumen_kit_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME lumen_kit_tests COMMAND lumen_kit_tests)
endif()


# Headless interaction-state screenshot tool (no window system or GPU required).
add_executable(lumen_interaction_screenshot EXCLUDE_FROM_ALL tools/screenshot_interaction.cpp)
target_include_directories(lumen_interaction_screenshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(lumen_interaction_screenshot PRIVATE lumen::core)
target_compile_features(lumen_interaction_screenshot PRIVATE cxx_std_17)
if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
    target_compile_options(lumen_interaction_screenshot PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# The host is a sample, not an engine dependency. The core remains backend/windowing agnostic.
if(LUMEN_BUILD_GALLERY AND UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    find_library(LUMEN_GL_LIBRARY NAMES GL libGL.so.1)
    if(X11_FOUND AND LUMEN_GL_LIBRARY)
        add_executable(lumen_gallery
            examples/gallery_main_x11.cpp
            examples/gallery_app.cpp
            imgui/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(lumen_gallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui ${CMAKE_CURRENT_SOURCE_DIR}/examples)
        target_compile_definitions(lumen_gallery PRIVATE
            LUMEN_GALLERY_FONT="${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/fonts/Roboto-Medium.ttf")
        target_link_libraries(lumen_gallery PRIVATE lumen::kit ${X11_LIBRARIES} ${LUMEN_GL_LIBRARY} ${CMAKE_DL_LIBS})
        target_compile_features(lumen_gallery PRIVATE cxx_std_17)
        if(LUMEN_ENABLE_WARNINGS AND NOT MSVC)
            target_compile_options(lumen_gallery PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        endif()
        message(STATUS "Lumen gallery enabled (X11/OpenGL host)")
    else()
        message(STATUS "Lumen gallery disabled: X11 development files or OpenGL library not found")
    endif()
endif()

