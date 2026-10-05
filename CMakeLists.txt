cmake_minimum_required(VERSION 3.16)
project(msgold CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3's exported CMake config is unreliable on some installs, so locate it
# through pkg-config instead.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
find_package(Threads REQUIRED)

set(IMGUI_DIR src/vendor/imgui)
add_executable(msgold
    src/main.cpp
    src/gold.cpp
    src/ui.cpp
    src/chart.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(msgold PRIVATE ${IMGUI_DIR})
target_link_libraries(msgold PRIVATE SDL3::SDL3 Threads::Threads)

# unit tests for the pure domain layer; no SDL or ImGui involved
enable_testing()
add_executable(gold_tests tests/gold_tests.cpp src/gold.cpp)
target_include_directories(gold_tests PRIVATE src)
add_test(NAME gold_tests COMMAND gold_tests)
