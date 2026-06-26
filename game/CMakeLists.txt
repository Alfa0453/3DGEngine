# The game executable. For now it is a single file; we will add more as the
# milestones progress. It only needs to link `engine` — all of the engine's
# dependencies (GLFW, GLAD, GLM, OpenGL) come through transitively.
add_executable(game
    src/main.cpp
    src/PongApp.cpp
)

# Game-private headers (PongApp.h, Box.h).
target_include_directories(game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(game PRIVATE engine)

# Make the shader/asset folder findable from code regardless of the working
# directory during development. Used as std::string(ASSETS_DIR) + "/shaders/...".
target_compile_definitions(game
    PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

# For a distributable build, also copy the assets next to the executable after
# each build so the game can be shipped as a self-contained folder.
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:game>/assets"
    COMMENT "Copying assets next to the game executable")