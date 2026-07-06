# Skeletal-animation demo: loads a rigged glTF, plays a clip through the Animator,
# and draws it with GPU skinning (SkinnedRenderer). Ships one asset: the rigged
# character.glb (generated procedurally).
add_executable(character
    src/main.cpp
    src/CharacterApp.cpp
)
target_include_directories(character PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(character PRIVATE engine)
target_compile_definitions(character PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
add_custom_command(TARGET character POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:character>/assets"
    COMMENT "Copying character assets next to the executable")
