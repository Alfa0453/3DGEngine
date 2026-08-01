# Public Source Coverage

This appendix is the coverage checklist for the systems reference. It maps all
public headers currently found under `engine/include/engine` and
`editor/include` to the chapter that explains the system.

Inventory at the time of writing: **109 runtime headers** and **35 editor
headers**.

## Runtime: AI

Chapter: [AI and navigation](08-ai-navigation.md)

- [AgentCollision.h](../../engine/include/engine/ai/AgentCollision.h)
- [AiAgent.h](../../engine/include/engine/ai/AiAgent.h)
- [AiMovement.h](../../engine/include/engine/ai/AiMovement.h)
- [AStar.h](../../engine/include/engine/ai/AStar.h)
- [BehaviorGraph.h](../../engine/include/engine/ai/BehaviorGraph.h)
- [BehaviorTree.h](../../engine/include/engine/ai/BehaviorTree.h)
- [Blackboard.h](../../engine/include/engine/ai/Blackboard.h)
- [BtScript.h](../../engine/include/engine/ai/BtScript.h)
- [NavGrid.h](../../engine/include/engine/ai/NavGrid.h)
- [NavMesh.h](../../engine/include/engine/ai/NavMesh.h)
- [NavMeshBuilder.h](../../engine/include/engine/ai/NavMeshBuilder.h)
- [Perception.h](../../engine/include/engine/ai/Perception.h)
- [StateMachine.h](../../engine/include/engine/ai/StateMachine.h)
- [Steering.h](../../engine/include/engine/ai/Steering.h)

## Runtime: animation

Chapter: [Animation](06-animation.md)

- [AnimatedModel.h](../../engine/include/engine/animation/AnimatedModel.h)
- [AnimationController.h](../../engine/include/engine/animation/AnimationController.h)
- [AnimationGraphDesc.h](../../engine/include/engine/animation/AnimationGraphDesc.h)
- [Animator.h](../../engine/include/engine/animation/Animator.h)
- [Skeleton.h](../../engine/include/engine/animation/Skeleton.h)

## Runtime: assets

Chapter: [Assets](03-assets.md). Material and shader-specific types are also
covered by [Materials and shaders](05-materials-shaders.md).

- [AssetCooker.h](../../engine/include/engine/assets/AssetCooker.h)
- [AssetIdentity.h](../../engine/include/engine/assets/AssetIdentity.h)
- [AssetReference.h](../../engine/include/engine/assets/AssetReference.h)
- [AssetRegistry.h](../../engine/include/engine/assets/AssetRegistry.h)
- [MaterialAssetLoader.h](../../engine/include/engine/assets/MaterialAssetLoader.h)
- [ParticleAsset.h](../../engine/include/engine/assets/ParticleAsset.h)
- [RuntimeAssetManager.h](../../engine/include/engine/assets/RuntimeAssetManager.h)
- [RuntimeShaderManager.h](../../engine/include/engine/assets/RuntimeShaderManager.h)
- [ShaderAsset.h](../../engine/include/engine/assets/ShaderAsset.h)
- [ShaderGraphCompiler.h](../../engine/include/engine/assets/ShaderGraphCompiler.h)
- [SkeletalAsset.h](../../engine/include/engine/assets/SkeletalAsset.h)
- [StaticMeshAsset.h](../../engine/include/engine/assets/StaticMeshAsset.h)
- [TextureAsset.h](../../engine/include/engine/assets/TextureAsset.h)

## Runtime: audio

Chapter: [Audio](10-audio.md)

- [AudioAsset.h](../../engine/include/engine/audio/AudioAsset.h)
- [AudioEditing.h](../../engine/include/engine/audio/AudioEditing.h)
- [AudioEngine.h](../../engine/include/engine/audio/AudioEngine.h)
- [AudioTypes.h](../../engine/include/engine/audio/AudioTypes.h)
- [RuntimeAudioSystem.h](../../engine/include/engine/audio/RuntimeAudioSystem.h)

## Runtime: core

Chapter: [Core architecture](01-core-architecture.md)

- [Application.h](../../engine/include/engine/core/Application.h)
- [Config.h](../../engine/include/engine/core/Config.h)
- [HighPerformanceGPU.h](../../engine/include/engine/core/HighPerformanceGPU.h)
- [Paths.h](../../engine/include/engine/core/Paths.h)
- [Window.h](../../engine/include/engine/core/Window.h)

## Runtime: ECS and scenes

Chapter: [ECS and scenes](02-ecs-scenes.md)

- [Components.h](../../engine/include/engine/ecs/Components.h)
- [Entity.h](../../engine/include/engine/ecs/Entity.h)
- [Prefab.h](../../engine/include/engine/ecs/Prefab.h)
- [Registry.h](../../engine/include/engine/ecs/Registry.h)
- [RuntimeSystems.h](../../engine/include/engine/ecs/RuntimeSystems.h)
- [Systems.h](../../engine/include/engine/ecs/Systems.h)
- [LevelStreamingManager.h](../../engine/include/engine/scene/LevelStreamingManager.h)
- [RuntimeSceneLoader.h](../../engine/include/engine/scene/RuntimeSceneLoader.h)
- [WorldManifest.h](../../engine/include/engine/scene/WorldManifest.h)

## Runtime: gameplay and scripting

Chapter: [Gameplay and scripting](09-gameplay-scripting.md)

- [CameraDirector.h](../../engine/include/engine/gameplay/CameraDirector.h)
- [GameMode.h](../../engine/include/engine/gameplay/GameMode.h)
- [GameplayComponents.h](../../engine/include/engine/gameplay/GameplayComponents.h)
- [GameplaySystems.h](../../engine/include/engine/gameplay/GameplaySystems.h)
- [LuaScript.h](../../engine/include/engine/gameplay/LuaScript.h)
- [PlayerController.h](../../engine/include/engine/gameplay/PlayerController.h)
- [RagdollSystem.h](../../engine/include/engine/gameplay/RagdollSystem.h)
- [Script.h](../../engine/include/engine/gameplay/Script.h)
- [ScriptModule.h](../../engine/include/engine/gameplay/ScriptModule.h)

## Runtime: rendering foundations

Chapter: [Rendering](04-rendering.md)

- [Camera.h](../../engine/include/engine/graphics/Camera.h)
- [CameraBlend.h](../../engine/include/engine/graphics/CameraBlend.h)
- [CameraSequence.h](../../engine/include/engine/graphics/CameraSequence.h)
- [CameraShake.h](../../engine/include/engine/graphics/CameraShake.h)
- [Cubemap.h](../../engine/include/engine/graphics/Cubemap.h)
- [Framebuffer.h](../../engine/include/engine/graphics/Framebuffer.h)
- [Frustum.h](../../engine/include/engine/graphics/Frustum.h)
- [GpuProfiler.h](../../engine/include/engine/graphics/GpuProfiler.h)
- [ImageDecode.h](../../engine/include/engine/graphics/ImageDecode.h)
- [Mesh.h](../../engine/include/engine/graphics/Mesh.h)
- [Model.h](../../engine/include/engine/graphics/Model.h)
- [ObjData.h](../../engine/include/engine/graphics/ObjData.h)
- [Primitives.h](../../engine/include/engine/graphics/Primitives.h)
- [Renderer.h](../../engine/include/engine/graphics/Renderer.h)
- [Shader.h](../../engine/include/engine/graphics/Shader.h)
- [SkinnedModel.h](../../engine/include/engine/graphics/SkinnedModel.h)
- [SkinnedRenderer.h](../../engine/include/engine/graphics/SkinnedRenderer.h)
- [Texture.h](../../engine/include/engine/graphics/Texture.h)
- [VertexLayout.h](../../engine/include/engine/graphics/VertexLayout.h)

## Runtime: lighting, environment, and post-processing

Chapter: [Rendering](04-rendering.md)

- [CascadedShadow.h](../../engine/include/engine/graphics/CascadedShadow.h)
- [ClusteredLight.h](../../engine/include/engine/graphics/ClusteredLight.h)
- [DayNightCycle.h](../../engine/include/engine/graphics/DayNightCycle.h)
- [GrassField.h](../../engine/include/engine/graphics/GrassField.h)
- [IBL.h](../../engine/include/engine/graphics/IBL.h)
- [PbrRenderer.h](../../engine/include/engine/graphics/PbrRenderer.h)
- [PointShadow.h](../../engine/include/engine/graphics/PointShadow.h)
- [PostProcess.h](../../engine/include/engine/graphics/PostProcess.h)
- [ProceduralSky.h](../../engine/include/engine/graphics/ProceduralSky.h)
- [ShadowCasters.h](../../engine/include/engine/graphics/ShadowCasters.h)
- [ShadowMap.h](../../engine/include/engine/graphics/ShadowMap.h)
- [Skybox.h](../../engine/include/engine/graphics/Skybox.h)
- [SpotShadow.h](../../engine/include/engine/graphics/SpotShadow.h)
- [SSAO.h](../../engine/include/engine/graphics/SSAO.h)
- [SSR.h](../../engine/include/engine/graphics/SSR.h)
- [Terrain.h](../../engine/include/engine/graphics/Terrain.h)
- [Water.h](../../engine/include/engine/graphics/Water.h)

## Runtime: particles

Chapter: [Particles](11-particles.md)

- [GpuParticleSystem.h](../../engine/include/engine/graphics/GpuParticleSystem.h)
- [ParticleRenderer.h](../../engine/include/engine/graphics/ParticleRenderer.h)
- [ParticleSystem.h](../../engine/include/engine/graphics/ParticleSystem.h)
- [RuntimeParticleSystem.h](../../engine/include/engine/graphics/RuntimeParticleSystem.h)

## Runtime: text and UI

Chapter: [UI and HUD](12-ui-hud.md)

- [TextRenderer.h](../../engine/include/engine/graphics/TextRenderer.h)
- [TrueType.h](../../engine/include/engine/graphics/TrueType.h)
- [Hud.h](../../engine/include/engine/ui/Hud.h)
- [ImGuiLayer.h](../../engine/include/engine/ui/ImGuiLayer.h)
- [UI.h](../../engine/include/engine/ui/UI.h)

## Runtime: math

Spline evaluation is used by camera and cinematic paths and is documented in
[Rendering](04-rendering.md).

- [Spline.h](../../engine/include/engine/math/Spline.h)

## Runtime: physics

Chapter: [Physics](07-physics.md)

- [CharacterController.h](../../engine/include/engine/physics/CharacterController.h)
- [PhysicsComponents.h](../../engine/include/engine/physics/PhysicsComponents.h)
- [PhysicsWorld.h](../../engine/include/engine/physics/PhysicsWorld.h)

## Editor: animation, characters, behavior, particles, shaders, and HUD

Chapters: [Editor](13-editor.md), [Animation](06-animation.md),
[AI](08-ai-navigation.md), [Particles](11-particles.md),
[Materials and shaders](05-materials-shaders.md), and [UI/HUD](12-ui-hud.md).

- [AnimationClipAsset.h](../../editor/include/AnimationClipAsset.h)
- [AnimationGraphAsset.h](../../editor/include/AnimationGraphAsset.h)
- [AnimationGraphBuilder.h](../../editor/include/AnimationGraphBuilder.h)
- [AnimationGraphEditorPanel.h](../../editor/include/AnimationGraphEditorPanel.h)
- [BehaviorGraphPanel.h](../../editor/include/BehaviorGraphPanel.h)
- [CharacterAsset.h](../../editor/include/CharacterAsset.h)
- [CharacterEditorPanel.h](../../editor/include/CharacterEditorPanel.h)
- [ClipEditorPanel.h](../../editor/include/ClipEditorPanel.h)
- [GameBtScripts.h](../../editor/include/GameBtScripts.h)
- [HudEditorPanel.h](../../editor/include/HudEditorPanel.h)
- [ParticleAsset.h](../../editor/include/ParticleAsset.h)
- [ParticleEditorPanel.h](../../editor/include/ParticleEditorPanel.h)
- [ParticlePresets.h](../../editor/include/ParticlePresets.h)
- [ShaderEditorPanel.h](../../editor/include/ShaderEditorPanel.h)

## Editor: application, project, scene, content, and interaction

Chapter: [Editor](13-editor.md)

- [EditorApp.h](../../editor/include/EditorApp.h)
- [EditorAssets.h](../../editor/include/EditorAssets.h)
- [EditorCameraController.h](../../editor/include/EditorCameraController.h)
- [EditorContentController.h](../../editor/include/EditorContentController.h)
- [EditorDockspace.h](../../editor/include/EditorDockspace.h)
- [EditorDragDrop.h](../../editor/include/EditorDragDrop.h)
- [EditorGeneratedScriptTools.h](../../editor/include/EditorGeneratedScriptTools.h)
- [EditorGizmo.h](../../editor/include/EditorGizmo.h)
- [EditorLineRenderer.h](../../editor/include/EditorLineRenderer.h)
- [EditorLog.h](../../editor/include/EditorLog.h)
- [EditorMouseController.h](../../editor/include/EditorMouseController.h)
- [EditorPanels.h](../../editor/include/EditorPanels.h)
- [EditorProject.h](../../editor/include/EditorProject.h)
- [EditorRuntimeController.h](../../editor/include/EditorRuntimeController.h)
- [EditorScene.h](../../editor/include/EditorScene.h)
- [EditorScriptTools.h](../../editor/include/EditorScriptTools.h)
- [EditorTransformController.h](../../editor/include/EditorTransformController.h)
- [EditorViewport.h](../../editor/include/EditorViewport.h)
- [NativeDialog.h](../../editor/include/NativeDialog.h)
- [PrefabAsset.h](../../editor/include/PrefabAsset.h)
- [RuntimeSceneExporter.h](../../editor/include/RuntimeSceneExporter.h)

## Coverage rule

When a public header is added, its owning system should update the appropriate
chapter and add the header to this appendix. Internal `.cpp` files implement
these APIs; they are deliberately not duplicated here as separate “systems.”
