add_executable(particle_tests
    particle_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/assets/ParticleAsset.cpp)

target_include_directories(particle_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(particle_tests PRIVATE engine)

if(MSVC)
    target_compile_options(particle_tests PRIVATE /W4)
else()
    target_compile_options(particle_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME particle_regression COMMAND particle_tests)

add_executable(animation_movement_tests
    animation_movement_tests.cpp)

target_link_libraries(animation_movement_tests PRIVATE engine)

if(MSVC)
    target_compile_options(animation_movement_tests PRIVATE /W4)
else()
    target_compile_options(animation_movement_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME animation_movement_regression COMMAND animation_movement_tests)

add_executable(terrain_tests terrain_tests.cpp)
target_link_libraries(terrain_tests PRIVATE engine)
if(MSVC)
    target_compile_options(terrain_tests PRIVATE /W4)
else()
    target_compile_options(terrain_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME terrain_regression COMMAND terrain_tests)

add_executable(spline_tests spline_tests.cpp)
target_link_libraries(spline_tests PRIVATE engine)
if(MSVC)
    target_compile_options(spline_tests PRIVATE /W4)
else()
    target_compile_options(spline_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME spline_regression COMMAND spline_tests)

add_executable(camera_manager_tests
    camera_manager_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/scene/EditorScene.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/scene/RuntimeSceneExporter.cpp)

target_include_directories(camera_manager_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(camera_manager_tests PRIVATE engine)

if(MSVC)
    target_compile_options(camera_manager_tests PRIVATE /W4)
else()
    target_compile_options(camera_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME camera_manager_regression COMMAND camera_manager_tests)

add_executable(optimization_auditor_tests
    optimization_auditor_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/OptimizationAuditReport.cpp)
target_include_directories(optimization_auditor_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(optimization_auditor_tests PRIVATE engine)
if(MSVC)
    target_compile_options(optimization_auditor_tests PRIVATE /W4)
else()
    target_compile_options(optimization_auditor_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME optimization_auditor_regression COMMAND optimization_auditor_tests)

add_executable(audio_system_tests
    audio_system_tests.cpp)
target_link_libraries(audio_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(audio_system_tests PRIVATE /W4)
else()
    target_compile_options(audio_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME audio_system_regression COMMAND audio_system_tests)

add_executable(shader_asset_tests
    shader_asset_tests.cpp)
target_link_libraries(shader_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(shader_asset_tests PRIVATE /W4)
else()
    target_compile_options(shader_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME shader_asset_regression COMMAND shader_asset_tests)

add_executable(material_shader_tests
    material_shader_tests.cpp)
target_link_libraries(material_shader_tests PRIVATE MaterialMaker engine)
if(MSVC)
    target_compile_options(material_shader_tests PRIVATE /W4)
else()
    target_compile_options(material_shader_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME material_shader_regression COMMAND material_shader_tests)

add_executable(directional_shadow_shader_tests
    directional_shadow_shader_tests.cpp)
target_link_libraries(directional_shadow_shader_tests PRIVATE engine)
if(MSVC)
    target_compile_options(directional_shadow_shader_tests PRIVATE /W4)
else()
    target_compile_options(directional_shadow_shader_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME directional_shadow_shader_regression COMMAND directional_shadow_shader_tests)

add_executable(lighting_build_tests lighting_build_tests.cpp)
target_link_libraries(lighting_build_tests PRIVATE engine)
if(MSVC)
    target_compile_options(lighting_build_tests PRIVATE /W4)
else()
    target_compile_options(lighting_build_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME lighting_build_regression COMMAND lighting_build_tests)

add_executable(script_system_tests
    script_system_tests.cpp)
target_link_libraries(script_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(script_system_tests PRIVATE /W4)
else()
    target_compile_options(script_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME script_system_regression COMMAND script_system_tests)

add_executable(project_script_tools_tests
    project_script_tools_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/scripting/EditorGeneratedScriptTools.cpp)
target_include_directories(project_script_tools_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_compile_definitions(project_script_tools_tests PRIVATE
    THREEDG_TEST_GAME_ROOT="${CMAKE_SOURCE_DIR}/game"
    THREEDG_GAME_MODULE_DIR="${CMAKE_SOURCE_DIR}/game")
target_link_libraries(project_script_tools_tests PRIVATE engine)
if(MSVC)
    target_compile_options(project_script_tools_tests PRIVATE /W4)
else()
    target_compile_options(project_script_tools_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME project_script_tools_regression COMMAND project_script_tools_tests)

add_executable(script_reload_safety_tests
    script_reload_safety_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/scripting/EditorScriptTools.cpp)
target_include_directories(script_reload_safety_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(script_reload_safety_tests PRIVATE engine)
if(WIN32)
    target_link_libraries(script_reload_safety_tests PRIVATE shell32)
endif()
if(MSVC)
    target_compile_options(script_reload_safety_tests PRIVATE /W4)
else()
    target_compile_options(script_reload_safety_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME script_reload_safety_regression COMMAND script_reload_safety_tests)

add_executable(runtime_ai_tests
    runtime_ai_tests.cpp)
target_link_libraries(runtime_ai_tests PRIVATE engine)
if(MSVC)
    target_compile_options(runtime_ai_tests PRIVATE /W4)
else()
    target_compile_options(runtime_ai_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME runtime_ai_regression COMMAND runtime_ai_tests)

add_executable(image_decode_tests
    image_decode_tests.cpp)
target_link_libraries(image_decode_tests PRIVATE engine)
if(MSVC)
    target_compile_options(image_decode_tests PRIVATE /W4)
else()
    target_compile_options(image_decode_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME image_decode_regression COMMAND image_decode_tests)

add_executable(editor_assets_tests
    editor_assets_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/assets/EditorAssets.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/assets/AnimationClipAsset.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/assets/AnimationGraphAsset.cpp)
target_include_directories(editor_assets_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(editor_assets_tests PRIVATE engine)
if(MSVC)
    target_compile_options(editor_assets_tests PRIVATE /W4)
else()
    target_compile_options(editor_assets_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME editor_assets_regression COMMAND editor_assets_tests)

add_executable(asset_registry_tests
    asset_registry_tests.cpp)
target_link_libraries(asset_registry_tests PRIVATE engine)
if(MSVC)
    target_compile_options(asset_registry_tests PRIVATE /W4)
else()
    target_compile_options(asset_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME asset_registry_regression COMMAND asset_registry_tests)

add_executable(asset_cooker_tests
    asset_cooker_tests.cpp)
target_link_libraries(asset_cooker_tests PRIVATE engine)
if(MSVC)
    target_compile_options(asset_cooker_tests PRIVATE /W4)
else()
    target_compile_options(asset_cooker_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME asset_cooker_regression COMMAND asset_cooker_tests)

add_executable(world_streaming_tests
    world_streaming_tests.cpp)
target_link_libraries(world_streaming_tests PRIVATE engine)
if(MSVC)
    target_compile_options(world_streaming_tests PRIVATE /W4)
else()
    target_compile_options(world_streaming_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME world_streaming_regression COMMAND world_streaming_tests)

add_executable(static_mesh_asset_tests
    static_mesh_asset_tests.cpp)
target_link_libraries(static_mesh_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(static_mesh_asset_tests PRIVATE /W4)
else()
    target_compile_options(static_mesh_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME static_mesh_asset_regression COMMAND static_mesh_asset_tests)

add_executable(foliage_asset_tests foliage_asset_tests.cpp)
target_link_libraries(foliage_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(foliage_asset_tests PRIVATE /W4)
else()
    target_compile_options(foliage_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME foliage_asset_regression COMMAND foliage_asset_tests)

add_executable(ragdoll_asset_tests ragdoll_asset_tests.cpp)
target_link_libraries(ragdoll_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(ragdoll_asset_tests PRIVATE /W4)
else()
    target_compile_options(ragdoll_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME ragdoll_asset_regression COMMAND ragdoll_asset_tests)

add_executable(animation_retarget_tests animation_retarget_tests.cpp)
target_link_libraries(animation_retarget_tests PRIVATE engine)
if(MSVC)
    target_compile_options(animation_retarget_tests PRIVATE /W4)
else()
    target_compile_options(animation_retarget_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME animation_retarget_regression COMMAND animation_retarget_tests)

add_executable(ability_system_tests ability_system_tests.cpp)
target_link_libraries(ability_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(ability_system_tests PRIVATE /W4)
else()
    target_compile_options(ability_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME ability_system_regression COMMAND ability_system_tests)

add_executable(runtime_property_snapshot_tests
    runtime_property_snapshot_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/RuntimePropertySnapshot.cpp)
target_include_directories(runtime_property_snapshot_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(runtime_property_snapshot_tests PRIVATE engine)
if(MSVC)
    target_compile_options(runtime_property_snapshot_tests PRIVATE /W4)
else()
    target_compile_options(runtime_property_snapshot_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME runtime_property_snapshot_regression COMMAND runtime_property_snapshot_tests)

add_executable(asset_dependency_graph_tests
    asset_dependency_graph_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/AssetDependencyGraph.cpp)
target_include_directories(asset_dependency_graph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(asset_dependency_graph_tests PRIVATE engine)
if(MSVC)
    target_compile_options(asset_dependency_graph_tests PRIVATE /W4)
else()
    target_compile_options(asset_dependency_graph_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME asset_dependency_graph_regression COMMAND asset_dependency_graph_tests)

add_executable(weather_asset_tests weather_asset_tests.cpp)
target_link_libraries(weather_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(weather_asset_tests PRIVATE /W4)
else()
    target_compile_options(weather_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME weather_asset_regression COMMAND weather_asset_tests)

add_executable(procedural_building_tests
    procedural_building_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/ProceduralBuildingPanel.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/app/EditorPanels.cpp)
target_include_directories(procedural_building_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(procedural_building_tests PRIVATE engine)
if(MSVC)
    target_compile_options(procedural_building_tests PRIVATE /W4)
else()
    target_compile_options(procedural_building_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME procedural_building_regression COMMAND procedural_building_tests)

add_executable(road_generator_tests
    road_generator_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/RoadGeneratorPanel.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/app/EditorPanels.cpp)
target_include_directories(road_generator_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(road_generator_tests PRIVATE engine)
if(MSVC)
    target_compile_options(road_generator_tests PRIVATE /W4)
else()
    target_compile_options(road_generator_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME road_generator_regression COMMAND road_generator_tests)

add_executable(level_instance_tests
    level_instance_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/LevelInstancePanel.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/app/EditorPanels.cpp)
target_include_directories(level_instance_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(level_instance_tests PRIVATE engine)
if(MSVC)
    target_compile_options(level_instance_tests PRIVATE /W4)
else()
    target_compile_options(level_instance_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME level_instance_regression COMMAND level_instance_tests)

add_executable(world_partition_tests
    world_partition_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/WorldPartitionPanel.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/app/EditorPanels.cpp)
target_include_directories(world_partition_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(world_partition_tests PRIVATE engine)
if(MSVC)
    target_compile_options(world_partition_tests PRIVATE /W4)
else()
    target_compile_options(world_partition_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME world_partition_regression COMMAND world_partition_tests)

add_executable(skeletal_asset_tests
    skeletal_asset_tests.cpp)
target_link_libraries(skeletal_asset_tests PRIVATE engine)
target_compile_definitions(skeletal_asset_tests PRIVATE
    THREEDG_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(skeletal_asset_tests PRIVATE /W4)
else()
    target_compile_options(skeletal_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME skeletal_asset_regression COMMAND skeletal_asset_tests)

add_executable(forge_material_importer_tests
    forge_material_importer_tests.cpp)
target_link_libraries(forge_material_importer_tests PRIVATE engine)
if(MSVC)
    target_compile_options(forge_material_importer_tests PRIVATE /W4)
else()
    target_compile_options(forge_material_importer_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME forge_material_importer_regression COMMAND forge_material_importer_tests)

add_executable(mesh_geometry_operations_tests
    mesh_geometry_operations_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/MeshGeometryOperations.cpp)
target_include_directories(mesh_geometry_operations_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(mesh_geometry_operations_tests PRIVATE engine)
if(MSVC)
    target_compile_options(mesh_geometry_operations_tests PRIVATE /W4)
else()
    target_compile_options(mesh_geometry_operations_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME mesh_geometry_operations_regression COMMAND mesh_geometry_operations_tests)

add_executable(scatter_graph_tests scatter_graph_tests.cpp)
target_link_libraries(scatter_graph_tests PRIVATE engine)
if(MSVC)
    target_compile_options(scatter_graph_tests PRIVATE /W4)
else()
    target_compile_options(scatter_graph_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME scatter_graph_regression COMMAND scatter_graph_tests)

add_executable(lighting_analysis_tests
    lighting_analysis_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/panels/LightingAnalysis.cpp)
target_include_directories(lighting_analysis_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(lighting_analysis_tests PRIVATE engine)
if(MSVC)
    target_compile_options(lighting_analysis_tests PRIVATE /W4)
else()
    target_compile_options(lighting_analysis_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME lighting_analysis_regression COMMAND lighting_analysis_tests)

add_executable(biome_asset_tests biome_asset_tests.cpp)
target_link_libraries(biome_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(biome_asset_tests PRIVATE /W4)
else()
    target_compile_options(biome_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME biome_asset_regression COMMAND biome_asset_tests)

add_executable(day_night_timeline_tests day_night_timeline_tests.cpp)
target_link_libraries(day_night_timeline_tests PRIVATE engine)
add_test(NAME day_night_timeline_regression COMMAND day_night_timeline_tests)

add_executable(cave_asset_tests cave_asset_tests.cpp)
target_link_libraries(cave_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(cave_asset_tests PRIVATE /W4)
else()
    target_compile_options(cave_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME cave_asset_regression COMMAND cave_asset_tests)

add_executable(fence_wall_asset_tests fence_wall_asset_tests.cpp)
target_link_libraries(fence_wall_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(fence_wall_asset_tests PRIVATE /W4)
else()
    target_compile_options(fence_wall_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME fence_wall_asset_regression COMMAND fence_wall_asset_tests)

add_executable(destruction_asset_tests destruction_asset_tests.cpp)
target_link_libraries(destruction_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(destruction_asset_tests PRIVATE /W4)
else()
    target_compile_options(destruction_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME destruction_asset_regression COMMAND destruction_asset_tests)

add_executable(interaction_asset_tests interaction_asset_tests.cpp)
target_link_libraries(interaction_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(interaction_asset_tests PRIVATE /W4)
else()
    target_compile_options(interaction_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME interaction_asset_regression COMMAND interaction_asset_tests)

add_executable(ik_rig_tests ik_rig_tests.cpp)
target_link_libraries(ik_rig_tests PRIVATE engine)
if(MSVC)
    target_compile_options(ik_rig_tests PRIVATE /W4)
else()
    target_compile_options(ik_rig_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME ik_rig_regression COMMAND ik_rig_tests)

add_executable(pose_library_tests pose_library_tests.cpp)
target_link_libraries(pose_library_tests PRIVATE engine)
if(MSVC)
    target_compile_options(pose_library_tests PRIVATE /W4)
else()
    target_compile_options(pose_library_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME pose_library_regression COMMAND pose_library_tests)

add_executable(equipment_asset_tests equipment_asset_tests.cpp)
target_link_libraries(equipment_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(equipment_asset_tests PRIVATE /W4)
else()
    target_compile_options(equipment_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME equipment_asset_regression COMMAND equipment_asset_tests)

add_executable(render_debugger_tests
    render_debugger_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/app/EditorPanels.cpp)
target_include_directories(render_debugger_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(render_debugger_tests PRIVATE engine)
if(MSVC)
    target_compile_options(render_debugger_tests PRIVATE /W4)
else()
    target_compile_options(render_debugger_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME render_debugger_regression COMMAND render_debugger_tests)

add_executable(portal_asset_tests portal_asset_tests.cpp)
target_link_libraries(portal_asset_tests PRIVATE engine)
if(MSVC)
    target_compile_options(portal_asset_tests PRIVATE /W4)
else()
    target_compile_options(portal_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME portal_asset_regression COMMAND portal_asset_tests)

add_executable(quest_system_tests quest_system_tests.cpp)
target_link_libraries(quest_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(quest_system_tests PRIVATE /W4)
else()
    target_compile_options(quest_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME quest_system_regression COMMAND quest_system_tests)

add_executable(dialogue_system_tests dialogue_system_tests.cpp)
target_link_libraries(dialogue_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(dialogue_system_tests PRIVATE /W4)
else()
    target_compile_options(dialogue_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME dialogue_system_regression COMMAND dialogue_system_tests)

add_executable(inventory_system_tests inventory_system_tests.cpp)
target_link_libraries(inventory_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(inventory_system_tests PRIVATE /W4)
else()
    target_compile_options(inventory_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME inventory_system_regression COMMAND inventory_system_tests)

add_executable(combat_system_tests combat_system_tests.cpp)
target_link_libraries(combat_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(combat_system_tests PRIVATE /W4)
else()
    target_compile_options(combat_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME combat_system_regression COMMAND combat_system_tests)

add_executable(spawn_system_tests spawn_system_tests.cpp)
target_link_libraries(spawn_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(spawn_system_tests PRIVATE /W4)
else()
    target_compile_options(spawn_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME spawn_system_regression COMMAND spawn_system_tests)

add_executable(save_profile_tests save_profile_tests.cpp)
target_link_libraries(save_profile_tests PRIVATE engine)
if(MSVC)
    target_compile_options(save_profile_tests PRIVATE /W4)
else()
    target_compile_options(save_profile_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME save_profile_regression COMMAND save_profile_tests)

add_executable(editor_scene_hierarchy_tests
    editor_scene_hierarchy_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/scene/EditorScene.cpp)
target_include_directories(editor_scene_hierarchy_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(editor_scene_hierarchy_tests PRIVATE engine)
if(MSVC)
    target_compile_options(editor_scene_hierarchy_tests PRIVATE /W4)
else()
    target_compile_options(editor_scene_hierarchy_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME editor_scene_hierarchy_regression COMMAND editor_scene_hierarchy_tests)

add_executable(editor_dirty_document_tests editor_dirty_document_tests.cpp)
target_include_directories(editor_dirty_document_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
if(MSVC)
    target_compile_options(editor_dirty_document_tests PRIVATE /W4)
else()
    target_compile_options(editor_dirty_document_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME editor_dirty_document_regression COMMAND editor_dirty_document_tests)
