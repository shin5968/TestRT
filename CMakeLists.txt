cmake_minimum_required(VERSION 3.20)
project(rtprobe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RTPROBE_SOURCES
    src/main.cpp
    src/app.cpp
    src/util.cpp
    src/d3d12_ctx.cpp
    src/probe.cpp
    src/dxr_test.cpp
    src/scene.cpp
    src/renderer.cpp
    src/vk_rt.cpp
    src/overlay.cpp
    src/bench.cpp
    src/report.cpp
)

add_executable(rtprobe WIN32 ${RTPROBE_SOURCES})

target_include_directories(rtprobe PRIVATE third_party)
target_compile_definitions(rtprobe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
    INITGUID
)
target_compile_options(rtprobe PRIVATE
    -march=x86-64
    -O2
    -fms-extensions
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)
target_link_options(rtprobe PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
    -municode
)
target_link_libraries(rtprobe PRIVATE user32 gdi32 version)

set_target_properties(rtprobe PROPERTIES OUTPUT_NAME "rtprobe")
