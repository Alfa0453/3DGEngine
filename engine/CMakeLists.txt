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
    src/graphics/ClusteredLights.cpp
    src/graphics/Skybox.cpp
    src/graphics/PbrRenderer.cpp
    src/graphics/Framebuffer.cpp
    src/graphics/PostProcess.cpp
    src/graphics/ProceduralSky.cpp
    src/graphics/PointShadow.cpp
    src/graphics/SpotShadow.cpp
    src/graphics/IBL.cpp
    src/graphics/SSAO.cpp
    src/graphics/SSR.cpp
    src/graphics/Terrain.cpp
    src/graphics/Water.cpp
    src/math/Spline.cpp
    src/graphics/GpuProfiler.cpp
    src/scene/RuntimeSceneLoader.cpp
    src/assets/RuntimeAssetManager.cpp
    src/assets/MaterialAssetLoader.cpp
    src/assets/ParticleAsset.cpp
    src/assets/ShaderAsset.cpp
    src/assets/ShaderGraphCompiler.cpp
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
    src/gameplay/GameMode.cpp
    src/gameplay/Script.cpp
)

# PUBLIC headers live under include/ so consumers write:  #include <engine/...>
target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/vendor/miniaudio)

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
endif()
