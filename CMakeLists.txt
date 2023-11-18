cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Nvy)
add_executable(Nvy WIN32 "resources/third_party/nvim_icon.rc" version_info.rc)

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
    Shcore.lib
    Dwmapi.lib
    imm32.lib
)

target_precompile_headers(Nvy PUBLIC
    <cassert>
    <cmath>
    <cstdint>
    <cstdio>
    <cwchar>
    <windows.h>
    <d3d11_4.h>
    <d2d1_3.h>
    <d2d1_3helper.h>
    <dwrite_3.h>
    <shellscalingapi.h>
    <dwmapi.h>
    <imm.h>
    
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

set_source_files_properties("src/third_party/mpack/mpack.c" PROPERTIES 
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_FLAGS -D_CRT_SECURE_NO_WARNINGS
)

if(MSVC)
	string(REGEX REPLACE "/GR" "/GR-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	string(REGEX REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")
endif()

## Configure a rc file to include version numbers
find_package(Git)

if(GIT_EXECUTABLE)
    # Generate a git-describe version string from Git repository tags
    execute_process(
	COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --match "v*"
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
	RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

# Split a version number into separate components
if(${GIT_DESCRIBE_VERSION} MATCHES "v([0-9]+)\.([0-9]+)\.([0-9]+)\-?([0-9]+)?(.*)?")
    set(VERSION_MAJOR ${CMAKE_MATCH_1})
    set(VERSION_MINOR ${CMAKE_MATCH_2})
    set(VERSION_PATCH ${CMAKE_MATCH_3})
    if(${CMAKE_MATCH_4})
	set(EXTRA_COMMITS ${CMAKE_MATCH_4})
    else()
	set(EXTRA_COMMITS 0)
    endif()
    if(${CMAKE_MATCH_COUNT} GREATER_EQUAL 5)
	set(COMMIT_HASH "${CMAKE_MATCH_5}")
    endif()
else()
    message(AUTHOR_WARNING "Bad version ${GIT_DESCRIBE_VERSION}; falling back to 0 (have you made an initial release?)")
    set(VERSION_MAJOR 0)
    set(VERSION_MINOR 0)
    set(VERSION_PATCH 0)
    set(EXTRA_COMMITS 0)
    set(COMMIT_HASH "-local")
endif()

set(PRODUCT_VERSION ${VERSION_MAJOR},${VERSION_MINOR},${VERSION_PATCH},${EXTRA_COMMITS})
set(FILE_VERSION ${VERSION_MAJOR},${VERSION_MINOR},${VERSION_PATCH},${EXTRA_COMMITS})

set(PRODUCT_VERSION_TEXT "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${EXTRA_COMMITS}${COMMIT_HASH}")
set(FILE_VERSION_TEXT "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${EXTRA_COMMITS}${COMMIT_HASH}")

configure_file(
  resources/version_info.rc.in
  version_info.rc
  @ONLY)
