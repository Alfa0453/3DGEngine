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
