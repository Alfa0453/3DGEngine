# ---------------------------------------------------------------------------
# Shared GAME MODULE (like Unreal's game module).
#
# Your gameplay + behaviour-tree scripts, compiled ONCE into a static library
# that BOTH the editor (Play mode) and the standalone player link. Write a script
# once here and it runs in both -- no duplicated registration.
#
# Editor-created scripts are compiled into a project-owned DLL. This static target
# contains only engine-shipped/manual game scripts.
# ---------------------------------------------------------------------------
add_library(game STATIC
    src/GameModule.cpp
    # add your script .cpp files here (header-only scripts need no entry):
    # src/scripts/MyScript.cpp
)
target_include_directories(game
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/Content
)
target_link_libraries(game PUBLIC engine)

# ---------------------------------------------------------------------------
# Hot-reloadable script DLL (opt-in). The SAME scripts compiled into a shared
# library the editor can load / reload at runtime (see engine ScriptModule),
# registering into the host's registry via the exported RegisterScriptModule.
# It links a private copy of engine; shared mutable state (GameMode, scene-load)
# reaches scripts through ScriptContext, so the two engine copies stay consistent.
# Nothing links this yet — Stage 3 wires the editor's reload path to it.
# ---------------------------------------------------------------------------
set(THREEDG_ACTIVE_PROJECT_ROOT "" CACHE PATH
    "3DG project whose Content/Scripts are compiled into game_scripts")
set(_3dg_project_module "")
if(THREEDG_ACTIVE_PROJECT_ROOT)
    set(_3dg_project_module
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Intermediate/Scripts/ProjectScriptModule.cpp")
endif()
if(THREEDG_ACTIVE_PROJECT_ROOT)
    if(NOT EXISTS "${_3dg_project_module}")
        message(FATAL_ERROR
            "The active project's generated script module is missing: ${_3dg_project_module}. "
            "Regenerate project scripts in the editor before building.")
    endif()
    add_library(game_scripts SHARED "${_3dg_project_module}")
else()
    add_library(game_scripts SHARED src/ScriptModuleExport.cpp)
endif()
if(THREEDG_ACTIVE_PROJECT_ROOT)
    file(GLOB_RECURSE _3dg_project_script_headers CONFIGURE_DEPENDS
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts/*.h"
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts/*.hpp")
    file(GLOB_RECURSE _3dg_project_script_sources CONFIGURE_DEPENDS
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts/*.cpp"
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts/*.cc"
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts/*.cxx")
    if(_3dg_project_script_headers)
        set_source_files_properties(${_3dg_project_script_headers}
            PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
    set(_3dg_project_script_files
        ${_3dg_project_script_headers} ${_3dg_project_script_sources})
    if(_3dg_project_script_files)
        target_sources(game_scripts PRIVATE ${_3dg_project_script_files})
        source_group(TREE "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts"
            PREFIX "Game Scripts" FILES ${_3dg_project_script_files})
    endif()
endif()

# Generated integration files shown in a "Generated" group (auto-generated; do not edit).
if(_3dg_project_module AND EXISTS "${_3dg_project_module}")
    source_group("Generated" FILES "${_3dg_project_module}")
endif()

# ---------------------------------------------------------------------------
# IDE-ONLY: make the REAL engine public headers browsable in the generated script
# solution (Solution Explorer / Go-To-Definition / search) WITHOUT compiling them
# into game_scripts.dll and WITHOUT copying any header. HEADER_FILE_ONLY + a browse
# custom target = visible, navigable, never built. Only added for the script IDE
# generation so the normal engine/editor build is unaffected.
# ---------------------------------------------------------------------------
if(THREEDG_SCRIPT_IDE_ONLY)
    file(GLOB_RECURSE _3dg_engine_public_headers CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/engine/include/engine/*.h"
        "${CMAKE_SOURCE_DIR}/engine/include/engine/*.hpp")
    if(_3dg_engine_public_headers)
        add_custom_target(EngineHeaders SOURCES ${_3dg_engine_public_headers})
        set_source_files_properties(${_3dg_engine_public_headers} PROPERTIES HEADER_FILE_ONLY TRUE)
        source_group(TREE "${CMAKE_SOURCE_DIR}/engine/include/engine"
            PREFIX "Engine" FILES ${_3dg_engine_public_headers})
        set_target_properties(EngineHeaders PROPERTIES FOLDER "Script SDK")
    endif()
endif()
target_include_directories(game_scripts
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/Content
)
if(THREEDG_ACTIVE_PROJECT_ROOT)
    target_include_directories(game_scripts PRIVATE
        "${THREEDG_ACTIVE_PROJECT_ROOT}"
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content"
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Content/Scripts"
        "${THREEDG_ACTIVE_PROJECT_ROOT}/Intermediate/Scripts")
endif()
target_link_libraries(game_scripts PRIVATE engine)
if(THREEDG_ACTIVE_PROJECT_ROOT)
    set_target_properties(game_scripts PROPERTIES
        OUTPUT_NAME "game_scripts"
        DEBUG_POSTFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries/RelWithDebInfo"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries/MinSizeRel")
    # Keep a canonical copy for the cooker, which stages it as game_scripts.dll.
    add_custom_command(TARGET game_scripts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:game_scripts>"
            "${THREEDG_ACTIVE_PROJECT_ROOT}/Binaries/game_scripts.dll")
else()
    set_target_properties(game_scripts PROPERTIES
        DEBUG_POSTFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")
endif()
