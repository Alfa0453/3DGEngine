# The engine is a STATIC library. The game links it; later, tools or unit
# tests could link it too. Keeping it separate from the game enforces a clean
# boundary: engine code must never depend on game code.
add_library(engine STATIC
    src/core/Window.cpp
    src/core/Application.cpp
    src/core/Config.cpp
    src/core/Paths.cpp
    src/graphics/Shader.cpp
    src/graphics/Camera.cpp
    src/graphics/CameraBlend.cpp
    src/graphics/CameraShake.cpp
    src/graphics/CameraSequence.cpp
    src/graphics/Mesh.cpp
    src/graphics/Renderer.cpp
    src/graphics/Texture.cpp
    src/graphics/TextRenderer.cpp
    src/graphics/ParticleRenderer.cpp
    src/graphics/ParticleSystem.cpp
    src/graphics/GpuParticleSystem.cpp
    src/graphics/RuntimeParticleSystem.cpp
    src/graphics/Primitives.cpp
    src/graphics/ModelDraw.cpp
    src/graphics/Model.cpp
    src/graphics/SkinnedModel.cpp
    src/graphics/SkinnedRenderer.cpp
    src/graphics/PngDecoder.cpp
    src/graphics/JpegDecoder.cpp
    src/graphics/ObjData.cpp
    src/graphics/Cubemap.cpp
    src/graphics/ShadowMap.cpp
    src/graphics/ShadowCasters.cpp
    src/graphics/CascadedShadow.cpp
    src/graphics/DirectionalShadowShader.cpp
    src/graphics/LightingScene.cpp
    src/graphics/DynamicIrradiance.cpp
    src/graphics/EnvironmentLighting.cpp
    src/graphics/LightingScalability.cpp
    src/graphics/SSGI.cpp
    src/graphics/LightingBuildData.cpp
    src/graphics/ClusteredLights.cpp
    src/graphics/Skybox.cpp
    src/graphics/PbrRenderer.cpp
    src/graphics/Framebuffer.cpp
    src/graphics/PostProcess.cpp
    src/graphics/PostProcessVolume.cpp
    src/graphics/ProceduralSky.cpp
    src/graphics/PointShadow.cpp
    src/graphics/SpotShadow.cpp
    src/graphics/IBL.cpp
    src/graphics/ReflectionProbeSystem.cpp
    src/graphics/LtcLut.cpp
    src/graphics/SSAO.cpp
    src/graphics/SSR.cpp
    src/graphics/Terrain.cpp
    src/graphics/Water.cpp
    src/graphics/FoliageRenderer.cpp
    src/math/Spline.cpp
    src/graphics/GpuProfiler.cpp
    src/scene/RuntimeSceneLoader.cpp
    src/assets/RuntimeAssetManager.cpp
    src/assets/AssetIdentity.cpp
    src/assets/AssetCooker.cpp
    src/assets/AssetReference.cpp
    src/assets/AssetRegistry.cpp
    src/assets/StaticMeshAsset.cpp
    src/assets/FoliageAsset.cpp
    src/assets/ScatterGraphAsset.cpp
    src/assets/BiomeAsset.cpp
    src/assets/DayNightTimelineAsset.cpp
    src/assets/CaveAsset.cpp
    src/assets/FenceWallAsset.cpp
    src/assets/DestructionAsset.cpp
    src/assets/TerrainAsset.cpp
    src/assets/TextureAsset.cpp
    src/assets/ForgeMaterialImporter.cpp
    src/assets/SkeletalAsset.cpp
    src/assets/MaterialAssetLoader.cpp
    src/assets/ParticleAsset.cpp
    src/assets/ShaderAsset.cpp
    src/assets/ShaderGraphCompiler.cpp
    src/assets/RagdollAsset.cpp
    src/assets/AnimationRetargetAsset.cpp
    src/assets/AbilityAsset.cpp
    src/assets/WeatherAsset.cpp
    src/assets/RuntimeShaderManager.cpp
    src/ui/ImGuiLayer.cpp
    src/ui/UI.cpp
    src/ui/UiBehavior.cpp
    src/ui/Hud.cpp
    src/ecs/Ecs.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioAsset.cpp
    src/audio/RuntimeAudioSystem.cpp
    src/audio/AudioEditing.cpp
    src/audio/miniaudio_impl.c
    src/physics/PhysicsWorld.cpp
    src/physics/CollisionMesh.cpp
    src/physics/CharacterController.cpp
    src/animation/Animator.cpp
    src/animation/AnimationController.cpp
    src/animation/AnimatedModel.cpp
    src/ai/AStar.cpp
    src/ai/Perception.cpp
    src/ai/Steering.cpp
    src/ai/AiAgent.cpp
    src/ai/AgentCollision.cpp
    src/ai/AiMovement.cpp
    src/ai/NavMesh.cpp
    src/ai/NavMeshBuilder.cpp
    src/ai/BehaviorGraph.cpp
    src/ai/BtScript.cpp
    src/gameplay/PlayerController.cpp
    src/gameplay/CameraDirector.cpp
    src/gameplay/GameplaySystems.cpp
    src/gameplay/RagdollSystem.cpp
    src/gameplay/AbilitySystem.cpp
    src/gameplay/DestructionSystem.cpp
    src/gameplay/GameMode.cpp
    src/gameplay/Script.cpp
    src/gameplay/LuaScript.cpp
    src/gameplay/ScriptModule.cpp
)

# Public headers are target sources for IDE browsing only. Consumers still
# compile solely against engine/include through the PUBLIC usage requirement.
file(GLOB_RECURSE THREEDG_ENGINE_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
target_sources(engine PRIVATE ${THREEDG_ENGINE_PUBLIC_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "Public Headers"
             FILES ${THREEDG_ENGINE_PUBLIC_HEADERS})

# Precompiled headers for faster builds (reduces template instantiation + std includes)
# Apply the PCH only for C++ compilation units so C sources (e.g. miniaudio_impl.c)
# are not forced to include C++ headers which would trigger compiler errors.
target_precompile_headers(engine PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_SOURCE_DIR}/include/engine_pch.h">
)

# PUBLIC headers live under include/ so consumers write:  #include <engine/...>
target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/vendor/miniaudio)

# Allow use of GLM experimental extensions (e.g. <glm/gtx/quaternion.hpp>).
# Some GLM GT* headers are marked experimental and require this define.
target_compile_definitions(engine PUBLIC GLM_ENABLE_EXPERIMENTAL)

# OpenGL itself (opengl32 on Windows, libGL on Linux, the framework on macOS).
find_package(OpenGL REQUIRED)

# Dependencies are PUBLIC because they appear in the engine's public headers
# (or are needed transitively by anything that links the engine).
# miniaudio needs the platform's threading + dynamic-loader (and libm on Linux);
# on Windows it loads its backends at runtime and needs no extra link libraries.
find_package(Threads REQUIRED)

target_link_libraries(engine
    PUBLIC
        glfw
        glad
        imgui
        glm::glm
        OpenGL::GL
        Threads::Threads
        ${CMAKE_DL_LIBS}
        lua_static
)

# Link Assimp (built by FetchContent in the root). Newer Assimp also exports the
# aliased target assimp::assimp; older just `assimp` — handle both.
if(TARGET assimp::assimp)
    target_link_libraries(engine PUBLIC assimp::assimp)
else()
    target_link_libraries(engine PUBLIC assimp)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(engine PUBLIC m)
endif()

# The vendored miniaudio implementation is third-party: silence its warnings.
if (MSVC)
    set_source_files_properties(src/audio/miniaudio_impl.c PROPERTIES COMPILE_FLAGS "/w")
else()
    set_source_files_properties(src/audio/miniaudio_impl.c PROPERTIES COMPILE_FLAGS "-w")
endif()

# Treat the engine's own code strictly; third-party warnings are not our problem.
if(MSVC)
    target_compile_options(engine PRIVATE /W4)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)

    # Install rules so the engine can be packaged independently (headers + archive).
    install(TARGETS engine
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)
endif()
