# Standalone runtime player: boots an editor-exported scene and runs it with no
# editor and no ImGui — links only the engine. See RUNTIME_ROADMAP.md.
add_executable(player
    src/main.cpp
    src/RuntimePlayerApp.cpp
    src/GameScripts.cpp
    # Add your game's script source files here so they compile into the player,
    # then register them in src/GameScripts.cpp. e.g.:
    # src/MyPlayerScript.cpp
)
target_include_directories(player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(player PRIVATE engine game)

# --- Packaging -------------------------------------------------------------
# `cmake --install build --prefix dist --component player` stages a runnable folder:
#     dist/player[.exe]      the game
#     dist/player.cfg        window + startup-scene settings
#     dist/game/...          your exported scene + its .hud/images (see below)
# The player finds player.cfg and relative scenes next to the executable, so the
# folder runs from anywhere. Zip it with cpack (see the root CMakeLists).
# All player install rules use the "player" component so CPack (and
# `cmake --install --component player`) package ONLY the game — not the
# FetchContent dependencies' own install rules.
install(TARGETS player RUNTIME DESTINATION "." COMPONENT player)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/player.cfg DESTINATION "." COMPONENT player OPTIONAL)

# Bundle a game-content folder (an exported <scene>.3dgscene plus its .hud and any
# HUD images) beside the executable. Point this at your exported game folder:
#     cmake -S . -B build -DPLAYER_GAME_DIR=E:/path/to/exported_game
set(PLAYER_GAME_DIR "" CACHE PATH "Game content (scene/.hud/images) bundled beside player.exe")
if(PLAYER_GAME_DIR)
    install(DIRECTORY "${PLAYER_GAME_DIR}/" DESTINATION "game" COMPONENT player)
endif()
