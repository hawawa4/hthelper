# Native Windows build (Visual Studio / MSVC):
#   cmake -B build -A x64 && cmake --build build --config Release
cmake_minimum_required(VERSION 3.16)
project(htcm_helper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ImGui 1.80 (the version Nexus uses), downloaded at configure time.
include(FetchContent)
FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.80.tar.gz
    URL_HASH SHA256=d7e4e1c7233409018437a646680316040e6977b9a635c02da93d172baad94ce9)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(htcm_helper SHARED
    src/main.cpp
    src/Recorder.cpp
    src/Encounter.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp)
target_include_directories(htcm_helper PRIVATE ${imgui_SOURCE_DIR})
target_compile_definitions(htcm_helper PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(htcm_helper PRIVATE winmm)
