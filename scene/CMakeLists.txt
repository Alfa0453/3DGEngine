# The lighting/scene PBR demo: a fourth executable on the engine.
add_executable(scene
    src/main.cpp
    src/SceneApp.cpp
)
target_include_directories(scene PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(scene PRIVATE engine)
target_compile_definitions(scene
    PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
add_custom_command(TARGET scene POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:scene>/assets"
    COMMENT "Copying scene assets next to the executable")
