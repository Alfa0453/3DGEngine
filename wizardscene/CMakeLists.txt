# Wizard demo: a UE4-style skinned mesh FBX with animation clips loaded from
# separate per-clip FBX files (idle / walk / run) attached to its skeleton, driven
# by the locomotion state machine in a PBR, shadowed scene.
add_executable(wizardscene
    src/main.cpp
    src/WizardSceneApp.cpp
)
target_include_directories(wizardscene PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(wizardscene PRIVATE engine)
target_compile_definitions(wizardscene PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
add_custom_command(TARGET wizardscene POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:wizardscene>/assets"
    COMMENT "Copying wizard assets next to the executable")
