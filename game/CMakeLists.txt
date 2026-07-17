# ---------------------------------------------------------------------------
# Shared GAME MODULE (like Unreal's game module).
#
# Your gameplay + behaviour-tree scripts, compiled ONCE into a static library
# that BOTH the editor (Play mode) and the standalone player link. Write a script
# once here and it runs in both -- no duplicated registration.
#
# To add a script: drop its header in include/game/scripts/, #include it in
# src/GameModule.cpp, and add a Register(...) line. If it has a .cpp, list it below.
# ---------------------------------------------------------------------------
add_library(game STATIC
    src/GameModule.cpp
    # add your script .cpp files here (header-only scripts need no entry):
    # src/scripts/MyScript.cpp
)
target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(game PUBLIC engine)
