# The Breakout game: a second executable on the same engine, built on the ECS.
add_executable(breakout
    src/main.cpp
    src/BreakoutApp.cpp
)

target_include_directories(breakout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(breakout PRIVATE engine)

# Dev-time asset path; the game also resolves assets next to the executable.
target_compile_definitions(breakout
    PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_custom_command(TARGET breakout POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:breakout>/assets"
    COMMENT "Copying Breakout assets next to the executable")
