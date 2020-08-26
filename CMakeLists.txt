cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Nvy)
add_executable(Nvy WIN32 "resources/nvim_icon.rc")

set(Nvy_HEADERS
    "src/common/dx_helper.h"
    "src/common/mpack_helper.h"
    "src/common/vec.h"
    "src/common/window_messages.h"
    "src/nvim/nvim.h"
    "src/renderer/glyph_renderer.h"
    "src/renderer/renderer.h"
    "src/third_party/mpack/mpack.h"
)

set(Nvy_SOURCES
    "src/main.cpp"
    "src/nvim/nvim.cpp"
    "src/renderer/glyph_renderer.cpp"
    "src/renderer/renderer.cpp"
    "src/third_party/mpack/mpack.c"
)

target_sources(Nvy PUBLIC
    ${Nvy_HEADERS} 
    ${Nvy_SOURCES}
)

target_include_directories(Nvy PUBLIC
    "src/"
)

target_link_libraries(Nvy PUBLIC 
    user32.lib 
    d3d11.lib 
    d2d1.lib 
    dwrite.lib
)

target_precompile_headers(Nvy PUBLIC
    <cassert>
    <cmath>
    <cstdint>
    <cstdio>
    <windows.h>
    <d3d11_4.h>
    <d2d1_3.h>
    <d2d1_3helper.h>
    <dwrite_3.h>
    
    "src/third_party/mpack/mpack.h"

    "src/common/dx_helper.h"
    "src/common/mpack_helper.h"
    "src/common/vec.h"
    "src/common/window_messages.h"
)

target_compile_definitions(Nvy PUBLIC
    MPACK_EXTENSIONS
    UNICODE
)

set_source_files_properties("src/third_party/mpack/mpack.c" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)