# Standalone editor target for 3DGEngine.
#
# To wire this into the engine later, copy or keep this folder beside the
# existing engine/game/viewer/scene folders and add:
#
#   add_subdirectory(editor)
#
# to the engine root CMakeLists.txt. This file assumes the `engine` CMake target
# already exists, just like the viewer and scene demos do.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../MaterialMaker" "${CMAKE_CURRENT_BINARY_DIR}/MaterialMaker")

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
    src/EditorPanels.cpp
    src/EditorDockspace.cpp
    src/EditorViewport.cpp
    src/EditorContentController.cpp
    src/EditorScriptTools.cpp
    src/EditorCameraController.cpp
    src/EditorMouseController.cpp
    src/EditorRuntimeController.cpp
    src/EditorTransformController.cpp
    src/BehaviorGraphPanel.cpp
    src/ParticleEditorPanel.cpp
    src/ShaderEditorPanel.cpp
    src/HudEditorPanel.cpp
    src/CharacterAsset.cpp
    src/CharacterEditorPanel.cpp
    src/ParticleAsset.cpp
    src/NativeDialog.cpp
    src/GameBtScripts.cpp
)

target_include_directories(3DGEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/btscripts
    ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(3DGEditor PRIVATE engine MaterialMaker game)
if(WIN32)
    # shell32 + ole32: IFileDialog folder picker; comdlg32: GetOpenFileName.
    target_link_libraries(3DGEditor PRIVATE shell32 ole32 comdlg32)
endif()

# Keep the editor in its own bin/editor subfolder, separate from other targets.
set_target_properties(3DGEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")

add_executable(3DGScriptCompiler src/ScriptCompilerHelper.cpp)
add_dependencies(3DGEditor 3DGScriptCompiler)
set_target_properties(3DGScriptCompiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")
if(MSVC)
    target_compile_options(3DGScriptCompiler PRIVATE /W4)
else()
    target_compile_options(3DGScriptCompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET 3DGEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:3DGEditor>/assets"
    COMMENT "Copying editor assets next to the executable")

if(MSVC)
    target_compile_options(3DGEditor PRIVATE /W4)
else()
    target_compile_options(3DGEditor PRIVATE -Wall -Wextra -Wpedantic)
endif()
