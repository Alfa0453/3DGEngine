# ---------------------------------------------------------------------------
# Root build script. Run from an out-of-source build directory:
#
#     cmake -S . -B build
#     cmake --build build
#
# This file wires together third-party dependencies and our own targets.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

# CMake 4.0+ removed compatibility with policies from CMake < 3.5. Some fetched dependencies
# (e.g. glfw 3.4 declares `cmake_minimum_required(VERSION 3.4...)`) still request a pre-3.5 floor,
# which makes CMake 4.x abort with a fatal error. Treat those old floors as 3.5 so the dependency
# sub-builds configure. FetchContent forwards this to the populate sub-builds too. Harmless on
# CMake 3.x (the variable is simply ignored there).
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(3DGameEngine
    VERSION     0.1.0
    DESCRIPTION "A 3D game engine built from scratch with OpenGL"
    LANGUAGES  C CXX)

# Tests are OFF by default: a normal build produces only the files required to run the engine
# (engine, game module, editor + launcher + tools, player). Opt in with -DTHREEDG_BUILD_TESTS=ON.
# BUILD_TESTING must be set before include(CTest), which otherwise defaults it ON.
option(THREEDG_BUILD_TESTS "Build the engine test suite" OFF)
set(BUILD_TESTING ${THREEDG_BUILD_TESTS} CACHE BOOL "Enable testing" FORCE)
include(CTest)

# --- Global compiler settings ----------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)           # use -std=c++17, not -std=gnu++17
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # generate compile_commands.json for IntelliSense

# Optionally link the C/C++ runtime statically so a packaged build needs no
# redistributable (Windows /MT). Off by default; turn on for distribution:
#   cmake -S . -B build -DGAMEENGINE_STATIC_RUNTIME=ON
option(GAMEENGINE_STATIC_RUNTIME "Statically link the C/C++ runtime" OFF)
if(GAMEENGINE_STATIC_RUNTIME)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        add_link_options(-static-libgcc -static-libstdc++)
    endif()
endif()

# Option: build optional demo/sample targets (disabled by default for releases).
option(BUILD_DEMOS "Build demo and sample targets (viewer, demos, etc.)" OFF)
option(THREEDG_SCRIPT_IDE_ONLY "Generate only engine and native game-script IDE targets" OFF)

# Collect every executable/DLL in one folder so assets are easy to locate.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Single-config generators (Make, Ninja) have no build type by default.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# --- Third-party dependencies ----------------------------------------------
# FetchContent downloads and builds dependencies as part of our build, so the
# project is self-contained: no system-wide installs required.
include(FetchContent)

# GLFW — creates the window, the OpenGL context, and handles input.
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM — header-only math (vectors, matrices, quaternions) modelled on GLSL.
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# Lua — embedded gameplay scripting. Pin the stable 5.4 ABI and build the
# official sources directly so editor Play and packaged games use the same VM.
FetchContent_Declare(
    lua
    URL      https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)

add_library(lua_static STATIC
    ${lua_SOURCE_DIR}/src/lapi.c
    ${lua_SOURCE_DIR}/src/lauxlib.c
    ${lua_SOURCE_DIR}/src/lbaselib.c
    ${lua_SOURCE_DIR}/src/lcode.c
    ${lua_SOURCE_DIR}/src/lcorolib.c
    ${lua_SOURCE_DIR}/src/lctype.c
    ${lua_SOURCE_DIR}/src/ldblib.c
    ${lua_SOURCE_DIR}/src/ldebug.c
    ${lua_SOURCE_DIR}/src/ldo.c
    ${lua_SOURCE_DIR}/src/ldump.c
    ${lua_SOURCE_DIR}/src/lfunc.c
    ${lua_SOURCE_DIR}/src/lgc.c
    ${lua_SOURCE_DIR}/src/linit.c
    ${lua_SOURCE_DIR}/src/liolib.c
    ${lua_SOURCE_DIR}/src/llex.c
    ${lua_SOURCE_DIR}/src/lmathlib.c
    ${lua_SOURCE_DIR}/src/lmem.c
    ${lua_SOURCE_DIR}/src/loadlib.c
    ${lua_SOURCE_DIR}/src/lobject.c
    ${lua_SOURCE_DIR}/src/lopcodes.c
    ${lua_SOURCE_DIR}/src/loslib.c
    ${lua_SOURCE_DIR}/src/lparser.c
    ${lua_SOURCE_DIR}/src/lstate.c
    ${lua_SOURCE_DIR}/src/lstring.c
    ${lua_SOURCE_DIR}/src/lstrlib.c
    ${lua_SOURCE_DIR}/src/ltable.c
    ${lua_SOURCE_DIR}/src/ltablib.c
    ${lua_SOURCE_DIR}/src/ltm.c
    ${lua_SOURCE_DIR}/src/lundump.c
    ${lua_SOURCE_DIR}/src/lutf8lib.c
    ${lua_SOURCE_DIR}/src/lvm.c
    ${lua_SOURCE_DIR}/src/lzio.c
)
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR}/src)
set_target_properties(lua_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(lua_static PRIVATE /w)
else()
    target_compile_options(lua_static PRIVATE -w)
endif()

# Assimp — model import (FBX / glTF / OBJ / …). Built from source via
# FetchContent, so no system install is needed (cross-platform, no vcpkg/apt).
# We trim it hard — static lib, no tools/tests/exporters, only the importers we
# use — to keep the first build reasonable.
set(BUILD_SHARED_LIBS                       OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS                      OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS               OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES                    OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                          OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT                        ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                       OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS               OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT   OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT   OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER     ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER     ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER    ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER     ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER     ON CACHE BOOL "" FORCE)
# Enable Assimp's bundled zlib so the dependency builds even without a system zlib install.
set(ASSIMP_BUILD_ZLIB                       ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v5.4.3
)
FetchContent_MakeAvailable(assimp)

# GLAD — loads OpenGL 3.3 core function pointers. Vendored (see vendor/glad).
add_subdirectory(vendor/glad)

# OpenGL itself (opengl32 on Windows, libGL on Linux, the framework on macOS).
find_package(OpenGL REQUIRED)

# Dear ImGui — immediate-mode UI used by development tools such as the editor.
# imgui has no CMakeLists.txt, so we populate-only and build it ourselves below.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)

target_link_libraries(imgui PUBLIC
    glfw
    glad
    OpenGL::GL
)

target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_OPENGL_LOADER_GLAD
)

# --- Our libraries and executables -----------------------------------------
add_subdirectory(engine)   # the reusable engine (static library)
add_subdirectory(game)     # shared game module: your scripts, linked by editor + player

if(NOT THREEDG_SCRIPT_IDE_ONLY)
add_subdirectory(editor)
endif()

if(NOT THREEDG_SCRIPT_IDE_ONLY)
add_subdirectory(player)   # standalone runtime player (game mode) — see RUNTIME_ROADMAP.md
endif()

# Project-enabled native plugins are generated by the editor's Plugin Manager.
# Plugin CMake files are included last so they may add sources or link libraries
# to the already-created engine, editor, player, and game targets.
if(THREEDG_ACTIVE_PROJECT_ROOT)
    set(_3dg_enabled_plugins_file
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Intermediate/Plugins/EnabledPlugins.cmake")
    if(EXISTS "${_3dg_enabled_plugins_file}")
        include("${_3dg_enabled_plugins_file}")
        foreach(_3dg_plugin_dir IN LISTS THREEDG_ENABLED_PLUGIN_DIRS)
            if(EXISTS "${_3dg_plugin_dir}/CMakeLists.txt")
                string(MD5 _3dg_plugin_hash "${_3dg_plugin_dir}")
                add_subdirectory("${_3dg_plugin_dir}"
                    "${CMAKE_BINARY_DIR}/plugins/${_3dg_plugin_hash}")
            else()
                message(WARNING "Enabled 3DG plugin has no CMakeLists.txt: ${_3dg_plugin_dir}")
            endif()
        endforeach()
    endif()
endif()

# CI and plugin development can compile the bundled examples without modifying
# a project's Plugin Manager state. Normal editor builds leave this off.
option(THREEDG_BUILD_EXAMPLE_PLUGINS "Build bundled Plugin API examples" OFF)
if(THREEDG_BUILD_EXAMPLE_PLUGINS)
    if(NOT TARGET three_dg_gameplay_utilities)
        add_subdirectory(Plugins/GameplayUtilities
            "${CMAKE_BINARY_DIR}/plugins/example_gameplay_utilities")
    endif()
    if(NOT TARGET three_dg_photo_mode)
        add_subdirectory(Plugins/PhotoMode
            "${CMAKE_BINARY_DIR}/plugins/example_photo_mode")
    endif()
endif()

# Tests build only when explicitly requested (THREEDG_BUILD_TESTS=ON). Excluded from a normal /
# release engine build.
if(THREEDG_BUILD_TESTS AND BUILD_TESTING AND NOT THREEDG_SCRIPT_IDE_ONLY)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Packaging the standalone runtime player (the "game mode"). Stage a runnable
# folder and zip it:
#     cmake -S . -B build -DPLAYER_COOKED_DIR=E:/path/to/Build/Cooked/MyGame
#     cmake --build build --target player
#     cmake --install build --prefix dist --component player
#       -> dist/player[.exe] + player.cfg + Content/ + CookManifest.3dgmanifest
#     cd build && cpack                       # -> 3DGPlayer-<ver>-<os>.zip
#
# Only the `player` target has install() rules, so the package contains just the
# player, its config, and the bundled game content (not the editor or demos).
# The executable finds player.cfg + relative scenes beside it (engine::ExecutableDir()).
set(CPACK_PACKAGE_NAME "3DGPlayer")
set(CPACK_PACKAGE_VENDOR "3DGEngine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone runtime player for 3DGEngine games")
set(CPACK_PACKAGE_FILE_NAME "3DGPlayer-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "3DGPlayer")
set(CPACK_GENERATOR "ZIP")                 # portable on every platform
if(UNIX AND NOT APPLE)
    list(APPEND CPACK_GENERATOR "TGZ")     # also a .tar.gz on Linux
endif()
# Package ONLY the player component, so the archive excludes the FetchContent
# dependencies' own install rules (glfw/glm/etc). Component-install ON restricts
# the archive to CPACK_COMPONENTS_ALL; the name override drops the component suffix.
set(CPACK_COMPONENTS_ALL player)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_ARCHIVE_PLAYER_FILE_NAME "3DGPlayer-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
include(CPack)
