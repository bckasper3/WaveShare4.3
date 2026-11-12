# Environment Logger - PC UI simulator (MinGW gcc + Ninja on Windows).
# Builds the SAME main/ui sources as the device, the real envlogger_core library, LVGL v8.3.11 and
# fake backends (sim/*.c) for app_state, app_events, storage and the board.
#
#   powershell -File tools\sim_shots.ps1           build + write every screenshot
#   <build>\el_sim.exe                              interactive window (mouse = touch)
#   <build>\el_sim.exe --shots <dir>                headless: one PNG per screen/state
#
# Dependencies are downloaded into the BUILD folder (never into the repo):
#   LVGL  v8.3.11 (git, FetchContent)
#   SDL2  2.30.11 MinGW development package from the official libsdl-org GitHub release.
cmake_minimum_required(VERSION 3.20)
project(el_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(FW_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---- LVGL: sources only (SOURCE_SUBDIR without a CMakeLists = populate, do not add) ----
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v8.3.11
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _populate_only_)
FetchContent_MakeAvailable(lvgl)
file(GLOB_RECURSE LVGL_SRCS CONFIGURE_DEPENDS ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRCS})
target_include_directories(lvgl PUBLIC ${lvgl_SOURCE_DIR} ${lvgl_SOURCE_DIR}/src ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w -O2)

# ---- SDL2 prebuilt MinGW package ----
set(SDL2_VER 2.30.11)
set(SDL2_SHA256 0590db0a47b564aab92499ed03eaa75db9aee17c371080caa3b05df9d49d9ff9)
set(SDL2_URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VER}/SDL2-devel-${SDL2_VER}-mingw.tar.gz)
set(SDL2_TGZ ${CMAKE_BINARY_DIR}/_deps/SDL2-devel-${SDL2_VER}-mingw.tar.gz)
set(SDL2_DIR_X ${CMAKE_BINARY_DIR}/_deps/SDL2-${SDL2_VER})
if(NOT EXISTS ${SDL2_DIR_X}/x86_64-w64-mingw32/lib/libSDL2.dll.a)
    if(NOT EXISTS ${SDL2_TGZ})
        message(STATUS "Downloading SDL2 ${SDL2_VER} (MinGW dev package)")
        file(DOWNLOAD ${SDL2_URL} ${SDL2_TGZ} STATUS dl_status TLS_VERIFY ON)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            # CMake's bundled curl may not trust the Windows certificate store: let PowerShell
            # (which does) fetch it. Integrity is checked by the pinned SHA-256 below either way.
            file(REMOVE ${SDL2_TGZ})
            execute_process(COMMAND powershell -NoProfile -Command
                "Invoke-WebRequest -UseBasicParsing '${SDL2_URL}' -OutFile '${SDL2_TGZ}'"
                RESULT_VARIABLE ps_rc)
            if(NOT ps_rc EQUAL 0)
                message(FATAL_ERROR "SDL2 download failed: ${dl_status}")
            endif()
        endif()
    endif()
    file(SHA256 ${SDL2_TGZ} got_hash)
    if(NOT got_hash STREQUAL SDL2_SHA256)
        file(REMOVE ${SDL2_TGZ})
        message(FATAL_ERROR "SDL2 package hash mismatch (${got_hash}); deleted, re-run to retry")
    endif()
    file(ARCHIVE_EXTRACT INPUT ${SDL2_TGZ} DESTINATION ${CMAKE_BINARY_DIR}/_deps)
endif()
set(SDL2_ROOT ${SDL2_DIR_X}/x86_64-w64-mingw32)
add_library(sdl2 SHARED IMPORTED)
set_target_properties(sdl2 PROPERTIES
    IMPORTED_IMPLIB ${SDL2_ROOT}/lib/libSDL2.dll.a
    IMPORTED_LOCATION ${SDL2_ROOT}/bin/SDL2.dll
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_ROOT}/include;${SDL2_ROOT}/include/SDL2"
    INTERFACE_COMPILE_DEFINITIONS SDL_MAIN_HANDLED)

# ---- envlogger_core (real library, same flags as the host tests) ----
add_subdirectory(${FW_ROOT}/components/envlogger_core ${CMAKE_BINARY_DIR}/envlogger_core)

# ---- the device UI, unchanged. *_esp.c files are device-only adapters. ----
# main/CMakeLists.txt compiles main/ui non-recursively, so the UI is one flat folder.
file(GLOB UI_SRCS CONFIGURE_DEPENDS ${FW_ROOT}/main/ui/*.c)
list(FILTER UI_SRCS EXCLUDE REGEX ".*_esp\\.c$")
file(GLOB SIM_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)

add_executable(el_sim ${SIM_SRCS} ${UI_SRCS})
target_include_directories(el_sim PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/stubs
    ${FW_ROOT}/main/ui
    ${FW_ROOT}/main/app
    ${FW_ROOT}/main/storage
    ${FW_ROOT}/main/board)
target_compile_definitions(el_sim PRIVATE EL_SIM=1)
target_compile_options(el_sim PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(el_sim PRIVATE lvgl envlogger_core sdl2 m)
add_custom_command(TARGET el_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_ROOT}/bin/SDL2.dll $<TARGET_FILE_DIR:el_sim>)
