# Native Windows build (Visual Studio / MSVC):
#   cmake -B build -A x64 && cmake --build build --config Release
cmake_minimum_required(VERSION 3.16)
project(htcm_helper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(htcm_helper SHARED
    src/main.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp)
target_include_directories(htcm_helper PRIVATE third_party/imgui)
target_compile_definitions(htcm_helper PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
