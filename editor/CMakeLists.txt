# Standalone editor target for 3DGEngine.
#
# To wire this into the engine later, copy or keep this folder beside the
# existing engine/game/viewer/scene folders and add:
#
#   add_subdirectory(editor)
#
# to the engine root CMakeLists.txt. This file assumes the `engine` CMake target
# already exists, just like the viewer and scene demos do.
add_executable(3DGEditor
    src/main.cpp
    src/EditorApp.cpp
    src/EditorLog.cpp
    src/EditorAssets.cpp
    src/EditorProject.cpp
    src/EditorScene.cpp
    src/RuntimeSceneExporter.cpp
    src/EditorGizmo.cpp
    src/EditorDragDrop.cpp
)

target_include_directories(3DGEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(3DGEditor PRIVATE engine)

add_custom_command(TARGET 3DGEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:3DGEditor>/assets"
    COMMENT "Copying editor assets next to the executable")

if(MSVC)
    target_compile_options(3DGEditor PRIVATE /W4)
else()
    target_compile_options(3DGEditor PRIVATE -Wall -Wextra -Wpedantic)
endif()
