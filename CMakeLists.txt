# ---------------------------------------------------------------------------
# Root build script. Run from an out-of-source build directory:
#
#     cmake -S . -B build
#     cmake --build build
#
# This file wires together third-party dependencies and our own targets.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

project(3DGameEngine
    VERSION     0.1.0
    DESCRIPTION "A 3D game engine built from scratch with OpenGL"
    LANGUAGES  C CXX)

# --- Global compiler settings ----------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)           # use -std=c++17, not -std=gnu++17
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # generate compile_commands.json for IntelliSense

# Optionally link the C/C++ runtime statically so a packaged build needs no
# redistributable (Windows /MT). Off by default; turn on for distribution:
#   cmake -S . -B build -DGAMEENGINE_STATIC_RUNTIME=ON
option(GAMEENGINE_STATIC_RUNTIME "Statically link he C/C++ runtime" OFF)
if(GAMEEGINE_STATIC_RUNTIME)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        add_link_options(-static-libgcc -static-libstdc++)
    endif()
endif()

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
set(ASSIMP_BUILD_ZLIB                       ON  CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS               OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT   OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT   OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER     ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER     ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER    ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER     ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER     ON CACHE BOOL "" FORCE)
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
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
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
#add_subdirectory(game)     # the game that links against the engine
#add_subdirectory(breakout) # a second game (Breakout) on the same engine + ECS
#add_subdirectory(viewer)   # a model-viewer tool (loads + renders OBJ models)
#add_subdirectory(scene)    # a PBR lighting + ECS scene demo (shadows, skybox)
add_subdirectory(editor)

# ---------------------------------------------------------------------------
# Packaging: `cmake --install build --prefix dist` stages a runnable folder,
# and `cpack` (run in the build dir) zips it. See docs/PACKAGING.md.
# ---------------------------------------------------------------------------
# The executable sits at the package root; the game finds assets/ beside it
# (engine::ExecutableDir()), so the layout must be:  <root>/game[.exe] + assets/
#install(TARGETS breakout RUNTIME DESTINATION ".")
#install(DIRECTORY breakout/assets/ DESTINATION "assets")
#install(FILES breakout.cfg DESTINATION "." OPTIONAL)

#set(CPACK_PACKAGE_NAME "Breakout")
#set(CPACK_PACKAGE_VENDOR "3DGEngine")
#set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
#set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Breakout - a game built on 3DGEngine")
#set(CPACK_PACKAGE_FILE_NAME "Breakout-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
#set(CPACK_PACKAGE_INSTALL_DIRECTORY "Breakout")
#set(CPACK_GENERATOR "ZIP")                 # portable on every platform
#if(UNIX AND NOT APPLE)
#    list(APPEND CPACK_GENERATOR "TGZ")     # also a .tar.gz on Linux
#endif()
#include(CPack)