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
    # app / core shell
    src/app/main.cpp
    src/app/EditorApp.cpp
    src/app/EditorApp_Camera.cpp
    src/app/EditorApp_Prefab.cpp
    src/app/EditorApp_MergeMesh.cpp
    src/app/EditorApp_ScriptDebug.cpp
    src/app/EditorApp_World.cpp
    src/app/EditorBranding.cpp
    src/app/EditorLog.cpp
    src/app/EditorProject.cpp
    src/app/EditorPanels.cpp
    src/app/EditorDockspace.cpp
    # interaction controllers
    src/controllers/EditorGizmo.cpp
    src/controllers/EditorDragDrop.cpp
    src/controllers/EditorContentController.cpp
    src/controllers/EditorCameraController.cpp
    src/controllers/EditorMouseController.cpp
    src/controllers/EditorRuntimeController.cpp
    src/controllers/EditorTransformController.cpp
    # scene + serialization
    src/scene/EditorScene.cpp
    src/scene/RuntimeSceneExporter.cpp
    # asset data + serialization (.3dgcharacter / .3dgclip / .3dggraph / particle)
    src/assets/EditorAssets.cpp
    src/assets/CharacterAsset.cpp
    src/assets/AnimationClipAsset.cpp
    src/assets/AnimationGraphAsset.cpp
    src/assets/ParticleAsset.cpp
    src/assets/PrefabAsset.cpp
    # dockable editor panels
    src/panels/BehaviorGraphPanel.cpp
    src/panels/ParticleEditorPanel.cpp
    src/panels/ShaderEditorPanel.cpp
    src/panels/HudEditorPanel.cpp
    src/panels/CharacterEditorPanel.cpp
    src/panels/ClipEditorPanel.cpp
    src/panels/AnimationGraphEditorPanel.cpp
    src/panels/MeshEditorPanel.cpp
    src/panels/MeshGeometryOperations.cpp
    src/panels/DecalPlacementPanel.cpp
    src/panels/OptimizationAuditReport.cpp
    src/panels/OptimizationAuditorPanel.cpp
    src/panels/LightingAnalysis.cpp
    src/panels/LightingAnalysisPanel.cpp
    src/panels/RagdollPhysicsPanel.cpp
    src/panels/AnimationRetargetingPanel.cpp
    src/panels/AbilityEditorPanel.cpp
    src/panels/RuntimePropertySnapshot.cpp
    src/panels/RuntimePropertyInspectorPanel.cpp
    src/panels/AssetDependencyGraph.cpp
    src/panels/AssetDependencyViewerPanel.cpp
    src/panels/WeatherEditorPanel.cpp
    src/panels/ProceduralBuildingPanel.cpp
    src/panels/RoadGeneratorPanel.cpp
    src/panels/LevelInstancePanel.cpp
    src/panels/WorldPartitionPanel.cpp
    src/panels/ProceduralScatterGraphPanel.cpp
    src/panels/BiomeEditorPanel.cpp
    src/panels/DayNightTimelinePanel.cpp
    src/panels/CaveTunnelPanel.cpp
    src/panels/TerrainCreatorPanel.cpp
    src/panels/ModularPlacementPanel.cpp
    src/panels/PrefabPalettePanel.cpp
    src/panels/RoomBuilderPanel.cpp
    src/panels/ScatterPaintPanel.cpp
    src/panels/ArrayToolPanel.cpp
    src/panels/MeasurementPanel.cpp
    src/panels/LevelValidationPanel.cpp
    src/panels/LevelVariantPanel.cpp
    src/panels/LevelLayersPanel.cpp
    src/panels/ViewportBookmarksPanel.cpp
    src/panels/BlockoutPanel.cpp
    src/panels/AlignmentPanel.cpp
    src/panels/SplineBuilderPanel.cpp
    # viewport rendering
    src/render/EditorViewport.cpp
    src/render/EditorLineRenderer.cpp
    # scripting tools
    src/scripting/EditorScriptTools.cpp
    src/scripting/EditorGeneratedScriptTools.cpp
    src/scripting/GameBtScripts.cpp
    # platform integration
    src/platform/NativeDialog.cpp
)

# Precompiled headers for faster builds
target_precompile_headers(3DGEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include/engine_pch.h")

target_include_directories(3DGEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/btscripts
    ${CMAKE_SOURCE_DIR}/engine/include)
target_compile_definitions(3DGEditor PRIVATE
    THREEDG_BT_SCRIPT_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/btscripts/templates"
    THREEDG_GAME_MODULE_DIR="${CMAKE_SOURCE_DIR}/game"
    THREEDG_ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    THREEDG_ENGINE_BUILD_DIR="${CMAKE_BINARY_DIR}"
    _CRT_SECURE_NO_WARNINGS)   # editor tool code uses sscanf/snprintf; silence MSVC C4996
target_link_libraries(3DGEditor PRIVATE engine MaterialMaker game)
if(WIN32)
    target_sources(3DGEditor PRIVATE assets/branding/3DGEditor.rc)
    # shell32 + ole32: IFileDialog folder picker; comdlg32: GetOpenFileName.
    target_link_libraries(3DGEditor PRIVATE shell32 ole32 comdlg32)
endif()

# Keep the editor in its own bin/editor subfolder, separate from other targets.
set_target_properties(3DGEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")

add_executable(3DGScriptCompiler src/scripting/ScriptCompilerHelper.cpp)
add_dependencies(3DGEditor 3DGScriptCompiler)
target_compile_definitions(3DGScriptCompiler PRIVATE
    THREEDG_ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    THREEDG_ENGINE_BUILD_DIR="${CMAKE_BINARY_DIR}")
set_target_properties(3DGScriptCompiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")
if(MSVC)
    target_compile_options(3DGScriptCompiler PRIVATE /W4)
else()
    target_compile_options(3DGScriptCompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Headless bridge used by 3DG Material Forge's one-click deployment workflow.
add_executable(3DGMaterialCooker src/assets/MaterialCookerMain.cpp)
add_dependencies(3DGEditor 3DGMaterialCooker)
target_link_libraries(3DGMaterialCooker PRIVATE engine)
set_target_properties(3DGMaterialCooker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")
if(MSVC)
    target_compile_options(3DGMaterialCooker PRIVATE /W4)
else()
    target_compile_options(3DGMaterialCooker PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Project launcher. Kept beside the editor executable so it can start the
# matching build without hard-coded installation paths.
add_executable(3DGLauncher
    src/launcher/main.cpp
    src/launcher/LauncherApp.cpp
    src/app/EditorBranding.cpp
    src/app/EditorProject.cpp
    src/platform/NativeDialog.cpp)
add_dependencies(3DGLauncher 3DGEditor)
target_include_directories(3DGLauncher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(3DGLauncher PRIVATE engine)
set_target_properties(3DGLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/editor")
if(WIN32)
    target_sources(3DGLauncher PRIVATE assets/branding/3DGEditor.rc)
    target_link_libraries(3DGLauncher PRIVATE shell32 ole32 comdlg32)
endif()
if(MSVC)
    target_compile_options(3DGLauncher PRIVATE /W4)
else()
    target_compile_options(3DGLauncher PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET 3DGEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:3DGEditor>/assets"
    COMMENT "Copying editor assets next to the executable")

if(MSVC)
    target_compile_options(3DGEditor PRIVATE /W4 /bigobj)
else()
    target_compile_options(3DGEditor PRIVATE -Wall -Wextra -Wpedantic)
endif()
