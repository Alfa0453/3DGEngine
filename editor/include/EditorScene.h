#pragma once

#include <engine/ecs/Components.h>
#include <engine/ecs/Entity.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/Script.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <unordered_map>

namespace engine {
class Mesh;
}

class EditorScene {
public:
    enum class Primitive { Plane, Cube, Sphere, Capsule, Cylinder, Cone, Pyramid, Torus, Staircase };

    enum class TriggerActionMode {
        None = 0,
        Enable = 1,
        Disable = 2,
        Toggle = 3,
    };

    enum class CameraSequenceTriggerAction {
        None = 0,
        Play = 1,
        Stop = 2,
        Skip = 3,
    };

    struct PlayerControllerSettings {
        bool firstPerson = false;
        float walkSpeed = 4.0f;
        float runSpeed = 7.0f;
        float jumpSpeed = 5.0f;
        float lookSensitivity = 0.1f;
        float capsuleRadius = 0.4f;
        float capsuleHeight = 1.8f;
        float eyeHeight = 0.6f;
        float cameraDistance = 5.0f;
        float cameraTargetHeight = 1.0f;
        bool cameraCollision = true;
        float cameraProbeRadius = 0.20f;
        float cameraCollisionPadding = 0.08f;
        float cameraReturnSpeed = 8.0f;
        bool shoulderCamera = false;
        float shoulderOffset = 0.65f;
        float shoulderSwitchSpeed = 12.0f;
        bool rightShoulder = true;
        bool lockOnEnabled = false;
        float lockOnRange = 18.0f;
        float lockOnViewAngle = 55.0f;
        float lockOnTargetHeight = 1.0f;
        float lockOnTrackingSpeed = 10.0f;
        float maxSlopeDegrees = 50.0f;
        float stepHeight = 0.35f;
    };

    using ScriptField = engine::ScriptField;

    struct AnimationEvent {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
    };

    struct AnimationActionProfile {
        std::string name = "Action";
        int clipIndex = 0;
        std::string clipName;
        std::string maskRootBone;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f;
    };

    struct AnimationStateNode {
        std::string name = "State";
        int clipIndex = 0;
        std::string clipName;
        bool loop = true;
        float speed = 1.0f;
        int blendClipIndex = -1;
        std::string blendClipName;
        std::string blendParameter;
        float blendMin = 0.0f;
        float blendMax = 1.0f;
        bool rootMotion = false;
    };

    struct AnimationParameter {
        enum class Type { Float = 0, Bool = 1, Trigger = 2 };
        std::string name = "Speed";
        Type type = Type::Float;
        float defaultValue = 0.0f;
    };

    struct AnimationStateTransition {
        enum class Compare {
            GreaterOrEqual = 0,
            Less = 1,
            Equal = 2,
            NotEqual = 3
        };

        std::string fromState;
        std::string toState;
        std::string parameter = "Speed";
        Compare compare = Compare::GreaterOrEqual;
        float threshold = 0.0f;
        float fade = 0.2f;
        float exitTime = 0.0f;
        int priority = 0;
        bool canInterrupt = false;
    };

    struct Object {
        engine::ecs::Entity entity = engine::ecs::kNull;
        std::string name;
        Primitive primitive = Primitive::Cube;
        bool light = false;
        bool navMeshBoundsVolume = false;
        engine::ecs::Light lightData;
        bool visible = true;
        bool locked = false;
        std::string modelAssetPath;
        std::string materialAssetPath;
        std::unordered_map<std::string, std::string> materialParameterOverrides;
        bool skeletalModel = false;
        int animationClipIndex = 0;
        std::string animationClipName;
        bool animationAutoplay = true;
        bool animationLoop = true;
        float animationSpeed = 1.0f;
        bool animationLocomotionEnabled = false;
        int animationIdleClipIndex = 0;
        int animationWalkClipIndex = 0;
        int animationRunClipIndex = 0;
        std::string animationIdleClipName;
        std::string animationWalkClipName;
        std::string animationRunClipName;
        float animationWalkAt = 0.15f;
        float animationRunAt = 3.0f;
        std::vector<AnimationEvent> animationEvents;
        std::vector<AnimationActionProfile> animationActionProfiles;
        std::vector<AnimationStateNode> animationStates;
        std::vector<AnimationParameter> animationParameters;
        std::vector<AnimationStateTransition> animationTransitions;
        bool linearVelocityEnabled = false;
        bool angularVelocityEnabled = false;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        bool rigidBodyEnabled = false;
        bool colliderEnabled = false;
        engine::ecs::RigidBody rigidBody;
        engine::ecs::Collider collider;
        bool rotatorEnabled = false;
        engine::ecs::Rotator rotator;
        bool moverEnabled = false;
        engine::ecs::Mover mover;
        std::string triggerTargetName;
        TriggerActionMode triggerEnterMoverAction = TriggerActionMode::None;
        TriggerActionMode triggerEnterRotatorAction = TriggerActionMode::None;
        TriggerActionMode triggerExitMoverAction = TriggerActionMode::None;
        TriggerActionMode triggerExitRotatorAction = TriggerActionMode::None;
        engine::ecs::AudioAction triggerEnterAudioAction = engine::ecs::AudioAction::None;
        engine::ecs::AudioAction triggerExitAudioAction = engine::ecs::AudioAction::None;
        engine::ParticleAction triggerEnterParticleAction = engine::ParticleAction::None;
        engine::ParticleAction triggerExitParticleAction = engine::ParticleAction::None;
        std::string triggerCameraSequenceName;
        CameraSequenceTriggerAction triggerEnterCameraAction = CameraSequenceTriggerAction::None;
        CameraSequenceTriggerAction triggerExitCameraAction = CameraSequenceTriggerAction::None;
        bool triggerCameraLockInput = true;
        bool triggerCameraSkippable = true;
        bool playerControllerEnabled = false;
        PlayerControllerSettings playerController;
        bool cameraZoneEnabled = false;
        std::string cameraZonePresetName;
        bool cameraZoneRestoreOnExit = true;
        int cameraZonePriority = 0;
        float cameraZoneReturnBlend = 0.35f;
        bool healthEnabled = false;
        engine::Health health;
        bool scriptEnabled = false;
        std::string scriptClassName;
        std::string scriptPath;
        std::vector<ScriptField> scriptFields;
        bool audioSourceEnabled = false;
        std::string audioAssetPath;
        engine::AudioBus audioBus = engine::AudioBus::SFX;
        float audioVolume = 1.0f;
        float audioPitch = 1.0f;
        bool audioSpatial = true;
        bool audioLoop = false;
        bool audioAutoplay = false;
        float audioMinDistance = 1.0f;
        float audioMaxDistance = 40.0f;
        float audioRolloff = 1.0f;
        float audioDopplerFactor = 1.0f;
        float audioConeInnerAngle = 360.0f;
        float audioConeOuterAngle = 360.0f;
        float audioConeOuterGain = 1.0f;
        float audioOcclusion = 0.0f;
        int audioPriority = 50;
        bool particleSystemEnabled = false;
        engine::EmitterConfig particleConfig;
        bool particleAutoplay = true;
        bool particleLoop = true;
        bool particlePrewarm = false;
        float particleDuration = 5.0f;
        float particleStartDelay = 0.0f;
        float particleSimulationSpeed = 1.0f;
        bool particleLocalSpace = true;
        int particleBurstCount = 0;
        float particleBurstInterval = 0.0f;
        std::string particleAssetPath;
        bool particleAssetOverride = false;
        std::vector<engine::ParticleEffectLayer> particleEffectLayers;
        // AI NavAgent (patrol/chase/search brain). M1: patrol only.
        bool navAgentEnabled = false;
        std::vector<glm::vec3> patrolPoints;
        float navAgentSpeed = 3.0f;
        float navAgentMaxForce = 20.0f;
        float navAgentReachRadius = 0.6f;
        float navAgentRepathInterval = 0.3f;
        // M2: perception + chase target.
        std::string navAgentTargetName;      // object to chase when seen ("" = patrol only)
        float navAgentVisionRange = 12.0f;
        float navAgentVisionHalfAngle = 45.0f;
        // M7: optional data-driven behaviour-tree asset ("" = built-in patrol/chase brain).
        std::string navAgentBrainAsset;
        // Faction targeting: team id (0 = neutral). With auto-target on, the agent
        // acquires the nearest agent on a different non-zero team as its chase target.
        int  navAgentTeam = 0;
        bool navAgentAutoTarget = false;
        // Procedural terrain (fBm heightmap). When set, the object renders a generated
        // terrain mesh (grass/rock/snow height coloring) instead of its primitive.
        bool  isTerrain = false;
        int   terrainRes = 128;         // vertices per side
        float terrainSize = 64.0f;      // world extent (square)
        float terrainMaxHeight = 8.0f;
        int   terrainSeed = 1337;
        int   terrainOctaves = 5;
        float terrainFrequency = 2.0f;
        std::vector<float> terrainHeights;          // sculpted heights (empty = pure noise)
        std::vector<unsigned char> terrainPaint;    // per-vertex paint layer (0 = auto)

        // Water body (animated Gerstner-wave surface). Rendered by the water pass,
        // not as a mesh; see EditorApp::DrawWaterBodies. Object.transform.position.xz
        // places the patch centre; waterLevel is its calm surface Y.
        bool  isWater = false;
        float waterSize = 80.0f;
        int   waterResolution = 160;
        float waterLevel = 0.0f;
        glm::vec3 waterShallow{0.10f, 0.42f, 0.50f};
        glm::vec3 waterDeep{0.02f, 0.10f, 0.18f};
        glm::vec3 waterReflection{0.55f, 0.72f, 0.92f};
        float waterTransparency = 0.72f;
        float waterFresnel = 4.0f;
        float waterSpecular = 1.2f;
        float waterShininess = 220.0f;
    };

    struct ObjectSnapshot {
        Object object;
        engine::ecs::Transform transform;
        glm::vec3 color{1.0f};
    };

    struct PhysicsJoint {
        enum class Type {
            Distance,
            Spring
        };

        Type type = Type::Distance;
        bool enabled = true;
        std::string objectA;
        std::string objectB;
        bool worldAnchor = false;
        glm::vec3 anchor{0.0f};
        float restLength = 1.0f;
        bool rope = false;
        float stiffness = 100.0f;
        float damping = 1.0f;
    };

    struct CameraPreset {
        std::string name = "Camera";
        glm::vec3 position{0.0f, 3.0f, 8.0f};
        glm::vec3 target{0.0f, 1.0f, 0.0f};
        float fov = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 3000.0f;
        float blendDuration = 0.35f;
        int blendEasing = 1; // engine::CameraBlend::Easing, kept as editor data
        bool primary = false;
        bool useInPlay = false;
    };

    struct CameraSequenceShot {
        std::string cameraName;
        float travelDuration = 1.0f;
        float holdDuration = 0.25f;
        int easing = 1;
        int pathMode = 0; // engine::CameraSequenceShot::Path
        std::string eventName;
    };

    enum class CinematicCueType { Event = 0, Audio = 1, Animation = 2 };

    struct CinematicCue {
        CinematicCueType type = CinematicCueType::Event;
        float time = 0.0f;
        std::string name;
        std::string assetPath;
        std::string targetObject;
        std::string animationClip;
        float volume = 1.0f;
    };

    struct CameraSequence {
        std::string name = "Camera Sequence";
        bool loop = false;
        std::vector<CameraSequenceShot> shots;
        std::vector<CinematicCue> cues;
    };

    struct Environment {
        struct PostProcessParameter {
            std::string name;
            int type = 0;
            std::string value;
        };

        struct PostProcessEffect {
            std::string shaderPath;
            bool enabled = true;
            std::vector<PostProcessParameter> parameters;
        };

        float timeOfDay = 0.46f;
        float skyLightIntensity = 1.0f;
        bool driveSunLight = true;
        float sunIntensity = 1.0f;
        bool showLightGuides = true;
        bool selectedLightGuideOnly = true;
        bool ibl = true;
        bool ssao = false;
        float ssaoRadius = 0.5f;
        float ssaoBias = 0.025f;
        bool ssr = false;
        float ssrIntensity = 0.5f;
        bool msaa = true;   // 4x MSAA on the default framebuffer (direct render path)
        bool fxaa = true;   // FXAA post pass (SSR/HDR render path)
        float renderScale = 1.0f;   // 3D render resolution fraction (fill-rate control)
        std::string hudAsset;       // reusable .hud file shown during play (empty = none)
        std::vector<PostProcessEffect> postProcessEffects;
        bool directionalShadows = true;
        bool pointShadows = true;
        bool spotShadows = true;
        float shadowSoftness = 2.5f;
        bool  fog = true;
        glm::vec3 fogColor{0.58f, 0.68f, 0.80f};
        float fogDensity = 0.008f;
        float fogHeight = -0.35f;
        float fogHeightFalloff = 0.10f;
        glm::vec3 physicsGravity{0.0f, -9.81f, 0.0f};
        int physicsSolverIterations = 4;
        bool physicsBroadPhase = true;
        float physicsCellSize = 2.0f;
        float physicsRestitutionThreshold = 0.5f;
        bool physicsAllowSleeping = true;
        float physicsSleepLinearVelocity = 0.06f;
        float physicsSleepAngularVelocity = 0.15f;
        float physicsTimeToSleep = 0.5f;
        bool showPhysicsGuides = true;
        bool selectedPhysicsGuideOnly = false;
    };

    struct Snapshot {
        std::vector<ObjectSnapshot> objects;
        std::vector<PhysicsJoint> joints;
        std::vector<CameraPreset> cameraPresets;
        std::vector<CameraSequence> cameraSequences;
        Environment environment;
        int selectedIndex = -1;
        int nextCubeNumber = 1;
    };

    void BuildDefault(const engine::Mesh& Cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    bool Save(const std::string& path, std::string* error, bool markClean = true);
    bool Load(const std::string& path, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase, std::string* error);

    engine::ecs::Registry& Registry() { return m_registry; }
    const std::vector<Object>& Objects() const { return m_objects; }
    const std::vector<PhysicsJoint>& PhysicsJoints() const { return m_joints; }
    const std::vector<CameraPreset>& CameraPresets() const { return m_cameraPresets; }
    const std::vector<CameraSequence>& CameraSequences() const { return m_cameraSequences; }
    const CameraPreset* PrimaryCameraPreset() const;
    bool IsDirty() const { return m_dirty; }
    void MarkClean() { m_dirty = false; }
    void MarkDirty() { m_dirty = true; }

    int SelectedIndex() const { return m_selectedIndex; }
    const Object* SelectedObject() const;
    engine::ecs::Transform* SelectedTransform();
    const engine::ecs::Transform* TryGetTransform(engine::ecs::Entity entity) const;
    const engine::ecs::MeshRenderer* TryGetMeshRenderer(engine::ecs::Entity entity) const;
    const engine::ecs::Light* TryGetLight(engine::ecs::Entity entity) const;
    const Environment& GetEnvironment() const { return m_environment; }
    bool IsVisible(engine::ecs::Entity entity) const;
    bool SelectedLocked() const;

    void SelectNext();
    void SelectPrevious();
    void SelectIndex(int index);
    void Deselect();
    void MoveSelected(const glm::vec3& delta);
    void RotateSelected(const glm::vec3& axis, float degrees);
    void RotateSelectedYaw(float degrees);
    void ScaleSelectedAxis(const glm::vec3& axis, float factor);
    void ScaleSelected(float factor);
    bool SetSelectedTransform(const engine::ecs::Transform& transform);
    void ResetSelectedTransform();
    void BeginTransformEdit();
    void EndTransformEdit();
    void BeginParticleEdit();
    void EndParticleEdit();
    bool Undo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    bool Redo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    Snapshot CreateSnapshot();
    void RestoreFromSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    void AddCube(const engine::Mesh& cube);
    void AddPlane(const engine::Mesh& plane);
    void AddSphere(const engine::Mesh& sphere);
    void AddCapsule(const engine::Mesh& capsule);
    void AddCylinder(const engine::Mesh& cylinder);
    void AddCone(const engine::Mesh& cone);
    void AddParticleSystem(const engine::Mesh& placeholderMesh,
                           const engine::ecs::Transform& transform,
                           const std::string& assetPath,
                           const engine::ParticleSystemComponent& settings);
    void AddNavMeshBoundsVolume(const engine::Mesh& cube);
    void AddConfiguredPrimitive(Primitive primitive, const engine::Mesh& mesh,
                                const engine::ecs::Transform& transform,
                                const engine::ecs::Collider* collider,
                                const std::string& name = {});
    void AddDirectionalLight(const engine::Mesh& placeholderMesh);
    void AddPointLight(const engine::Mesh& placeholderMesh);
    void AddSpotLight(const engine::Mesh& placeholderMesh);
    void AddAreaLight(const engine::Mesh& placeholderMesh);
    bool AddModel(const std::string& path, const engine::Mesh& placeholderMesh, const engine::ecs::Transform& transform);
    bool CycleSelectedColor();
    bool SetSelectedName(const std::string& name);
    bool SetSelectedColor(const glm::vec3& color);
    bool SetSelectedPrimitive(Primitive primitive, const engine::Mesh& mesh);
    bool SetSelectedModelAsset(const std::string& path);
    bool SetSelectedMaterialAsset(const std::string& path);
    bool SetSelectedMaterialParameterOverride(const std::string& name,
                                              const std::string& value);
    bool SetSelectedAnimationSettings(bool skeletalModel,
                                      int clipIndex,
                                      const std::string& clipName,
                                      bool autoplay,
                                      bool loop,
                                      float speed);
    bool SetSelectedAnimationLocomotion(bool enabled,
                                    int idleClipIndex,
                                    const std::string& idleClipName,
                                    int walkClipIndex,
                                    const std::string& walkClipName,
                                    int runClipIndex,
                                    const std::string& runClipName,
                                    float walkAt,
                                    float runAt);
    bool SetSelectedAnimationEvents(const std::vector<AnimationEvent>& events);
    bool SetSelectedAnimationActionProfiles(const std::vector<AnimationActionProfile>& profiles);
    bool SetSelectedAnimationStateGraph(const std::vector<AnimationStateNode>& states,
                                        const std::vector<AnimationStateTransition>& transitions,
                                        const std::vector<AnimationParameter>& parameters = {});
    bool SetSelectedLight(const engine::ecs::Light& light);
    void SetEnvironment(const Environment& environment);
    bool SetSelectedLinearVelocityEnabled(bool enabled);
    bool SetSelectedAngularVelocityEnabled(bool enabled);
    bool SetSelectedLinearVelocity(const glm::vec3& velocity);
    bool SetSelectedAngularVelocity(const glm::vec3& axis, float radiansPerSecond);
    bool SetSelectedRigidBodyEnabled(bool enabled);
    bool SetSelectedRigidBody(const engine::ecs::RigidBody& rigidBody);
    bool SetSelectedColliderEnabled(bool enabled);
    bool SetSelectedCollider(const engine::ecs::Collider& collider);
    bool SetSelectedRotatorEnabled(bool enabled);
    bool SetSelectedRotator(const engine::ecs::Rotator& rotator);
    bool SetSelectedMoverEnabled(bool enabled);
    bool SetSelectedMover(const engine::ecs::Mover& mover);
    bool SetSelectedTriggerAction(const std::string& targetName,
                              TriggerActionMode enterMoverAction,
                              TriggerActionMode enterRotatorAction,
                              TriggerActionMode exitMoverAction,
                              TriggerActionMode exitRotatorAction,
                              engine::ecs::AudioAction enterAudioAction = engine::ecs::AudioAction::None,
                              engine::ecs::AudioAction exitAudioAction = engine::ecs::AudioAction::None,
                              engine::ParticleAction enterParticleAction = engine::ParticleAction::None,
                              engine::ParticleAction exitParticleAction = engine::ParticleAction::None);
    bool SetSelectedTriggerCameraSequence(
        const std::string& sequenceName,
        CameraSequenceTriggerAction enterAction,
        CameraSequenceTriggerAction exitAction,
        bool lockInput, bool skippable);
    bool SetSelectedPlayerControllerEnabled(bool enabled);
    bool SetSelectedPlayerController(const PlayerControllerSettings& settings);
    bool SetSelectedCameraZone(bool enabled, const std::string& presetName,
                               bool restoreOnExit, int priority, float returnBlend);
    bool SetSelectedHealthEnabled(bool enabled);
    bool SetSelectedHealth(const engine::Health& health);
    bool SetSelectedScript(const std::string& className, const std::string& path, bool enabled);
    bool SetSelectedAudioSource(bool enabled, const std::string& path,
                                float volume, float pitch, bool spatial,
                                bool loop, bool autoplay, float minDistance,
                                float maxDistance, float rolloff,
                                engine::AudioBus bus = engine::AudioBus::SFX,
                                float dopplerFactor = 1.0f,
                                float coneInnerAngle = 360.0f,
                                float coneOuterAngle = 360.0f,
                                float coneOuterGain = 1.0f,
                                float occlusion = 0.0f, int priority = 50);
    bool SetSelectedParticleSystem(bool enabled, const engine::ParticleSystemComponent& settings);
    bool SetSelectedParticleAsset(const std::string& path,
                                  const engine::ParticleSystemComponent& settings,
                                  bool instanceOverride = false);
    int RefreshParticleAssetInstances(const std::string& path,
                                      const engine::ParticleSystemComponent& settings);
    bool SetSelectedParticleEffectLayers(const std::vector<engine::ParticleEffectLayer>& layers);
    bool SetSelectedScriptEnabled(bool enabled);
    bool SetSelectedNavAgent(bool enabled, float speed, float maxForce,
                             float reachRadius, float repathInterval,
                             const std::string& targetName, float visionRange, float visionHalfAngle);
    bool SetSelectedNavAgentBrain(const std::string& brainAsset);
    bool SetSelectedNavAgentTeam(int team, bool autoTarget);
    bool SetSelectedTerrain(bool enabled, int res, float size, float maxHeight,
                            int seed, int octaves, float frequency);
    bool SetSelectedTerrainHeights(std::vector<float> heights);   // sculpt result (no undo)
    bool SetSelectedTerrainPaint(std::vector<unsigned char> paint);   // paint result (no undo)
    bool SetSelectedWater(float size, int resolution, float level,
                          const glm::vec3& shallow, const glm::vec3& deep,
                          const glm::vec3& reflection, float transparency,
                          float fresnel, float specular, float shininess);
    bool AddSelectedPatrolPoint(const glm::vec3& point);
    bool ClearSelectedPatrolPoints();
    bool SetSelectedScriptFields(const std::vector<ScriptField>& fields);
    bool AddSelectedScriptField();
    bool SetSelectedScriptField(std::size_t index, const ScriptField& field);
    bool RemoveSelectedScriptField(std::size_t index);
    bool AddPhysicsJoint(const PhysicsJoint& joint);
    bool SetPhysicsJoint(std::size_t index, const PhysicsJoint& joint);
    bool RemovePhysicsJoint(std::size_t index);
    std::size_t AddCameraPreset(const CameraPreset& preset);
    bool SetCameraPreset(std::size_t index, const CameraPreset& preset);
    bool RemoveCameraPreset(std::size_t index);
    std::size_t DuplicateCameraPreset(std::size_t index);
    bool SetPrimaryCameraPreset(std::size_t index);
    std::size_t AddCameraSequence(const CameraSequence& sequence);
    bool SetCameraSequence(std::size_t index, const CameraSequence& sequence);
    bool RemoveCameraSequence(std::size_t index);
    bool ToggleSelectVisible();
    bool ToggleSelectedLocked();
    bool DuplicateSelected(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    bool DeleteSelected();

private:
    engine::ecs::Entity CreateObject(const std::string& name, Primitive primitive,
                                     const engine::Mesh& mesh, const engine::ecs::Transform& transform,
                                     const glm::vec3& color);
    Snapshot CaptureSnapshot();
    void RestoreSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    void PushUndoSnapshot();
    void ClearHistory();
    void Clear();

    engine::ecs::Registry m_registry;
    std::vector<Object> m_objects;
    std::vector<PhysicsJoint> m_joints;
    std::vector<CameraPreset> m_cameraPresets;
    std::vector<CameraSequence> m_cameraSequences;
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;
    Environment m_environment;
    int m_selectedIndex = -1;
    int m_nextCubeNumber = 1;
    bool m_dirty = false;
    bool m_transformEditOpen = false;
    bool m_particleEditOpen = false;
};
