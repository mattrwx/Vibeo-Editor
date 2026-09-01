cmake_minimum_required(VERSION 3.20)
project(AIVideoEditor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

add_executable(aive WIN32
    src/main.cpp
    src/app.cpp
    src/util.cpp
    src/texture.cpp
    src/ffmpeg.cpp
    src/project.cpp
    src/prompt.cpp
    src/edit.cpp
    src/script.cpp
    src/render.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
)

target_include_directories(aive PRIVATE src ${IMGUI_DIR})
target_compile_definitions(aive PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(aive PRIVATE d3d11 dxgi d3dcompiler dwmapi ole32 uuid shell32 user32 gdi32 shlwapi winmm)

if(MINGW)
    # static-link the runtime so the exe runs without MSYS2 DLLs on PATH
    target_link_options(aive PRIVATE -static)
endif()
