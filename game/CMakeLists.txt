# ---------------------------------------------------------------------------
# Shared GAME MODULE (like Unreal's game module).
#
# Your gameplay + behaviour-tree scripts, compiled ONCE into a static library
# that BOTH the editor (Play mode) and the standalone player link. Write a script
# once here and it runs in both -- no duplicated registration.
#
# Editor-created header-only scripts live in Content/Scripts and are pulled in by
# EditorGeneratedScripts.h. Manually maintained game-module headers may still live
# under include/game/scripts.
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
add_library(game_scripts SHARED src/ScriptModuleExport.cpp)
target_include_directories(game_scripts
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/Content
)
target_link_libraries(game_scripts PRIVATE engine)
set_target_properties(game_scripts PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")
