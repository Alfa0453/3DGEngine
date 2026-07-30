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

# Preferred: bundle the editor's dependency-pruned Cook Project output beside
# the executable. The cooked folder already contains Content/, player.cfg and
# CookManifest.3dgmanifest:
#     cmake -S . -B build -DPLAYER_COOKED_DIR=E:/Game/Build/Cooked/MyGame
set(PLAYER_COOKED_DIR "" CACHE PATH "Cook Project output bundled beside player.exe")

# Legacy: bundle an arbitrary game folder beneath game/. This remains available
# for projects created before the asset cooker:
#     cmake -S . -B build -DPLAYER_GAME_DIR=E:/path/to/exported_game
set(PLAYER_GAME_DIR "" CACHE PATH "Game content (scene/.hud/images) bundled beside player.exe")
if(PLAYER_COOKED_DIR AND PLAYER_GAME_DIR)
    message(FATAL_ERROR "Set PLAYER_COOKED_DIR or PLAYER_GAME_DIR, not both")
elseif(PLAYER_COOKED_DIR)
    if(NOT EXISTS "${PLAYER_COOKED_DIR}/Content/AssetRegistry.3dgdb"
       OR NOT EXISTS "${PLAYER_COOKED_DIR}/CookManifest.3dgmanifest"
       OR NOT EXISTS "${PLAYER_COOKED_DIR}/player.cfg")
        message(FATAL_ERROR
            "PLAYER_COOKED_DIR is not a complete Cook Project output: ${PLAYER_COOKED_DIR}")
    endif()
    install(DIRECTORY "${PLAYER_COOKED_DIR}/"
            DESTINATION "." COMPONENT player)
elseif(PLAYER_GAME_DIR)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/player.cfg
            DESTINATION "." COMPONENT player OPTIONAL)
    install(DIRECTORY "${PLAYER_GAME_DIR}/" DESTINATION "game" COMPONENT player)
else()
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/player.cfg
            DESTINATION "." COMPONENT player OPTIONAL)
endif()
