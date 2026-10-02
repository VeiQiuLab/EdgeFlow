cmake_minimum_required(VERSION 3.20)

project(EdgeFlow VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(EdgeFlow WIN32
    src/main.cpp
    src/anim_pump.cpp
    src/config.cpp
    src/debuglog.cpp
    src/managed_window.cpp
    src/manager.cpp
    src/tray.cpp
    src/EdgeFlow.rc
)

target_compile_definitions(EdgeFlow PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(EdgeFlow PRIVATE
    user32
    shell32
    gdi32
    dwmapi
)

if(MSVC)
    target_compile_options(EdgeFlow PRIVATE /W4 /permissive- /utf-8)
endif()
