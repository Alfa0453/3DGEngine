add_executable(particle_tests
    particle_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/ParticleAsset.cpp)

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

add_executable(camera_manager_tests
    camera_manager_tests.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/EditorScene.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/RuntimeSceneExporter.cpp)

target_include_directories(camera_manager_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(camera_manager_tests PRIVATE engine)

if(MSVC)
    target_compile_options(camera_manager_tests PRIVATE /W4)
else()
    target_compile_options(camera_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME camera_manager_regression COMMAND camera_manager_tests)

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

add_executable(script_system_tests
    script_system_tests.cpp)
target_link_libraries(script_system_tests PRIVATE engine)
if(MSVC)
    target_compile_options(script_system_tests PRIVATE /W4)
else()
    target_compile_options(script_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME script_system_regression COMMAND script_system_tests)

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
    ${CMAKE_SOURCE_DIR}/editor/src/EditorAssets.cpp)
target_include_directories(editor_assets_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include)
if(MSVC)
    target_compile_options(editor_assets_tests PRIVATE /W4)
else()
    target_compile_options(editor_assets_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME editor_assets_regression COMMAND editor_assets_tests)
