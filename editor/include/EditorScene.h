#pragma once

#include <engine/ecs/Components.h>
#include <engine/ecs/Entity.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/Script.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/ai/AiMovement.h>
#include <engine/assets/AssetIdentity.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace engine {
class Mesh;
}

class EditorScene {
public:
    using GroupId = std::uint64_t;
    static constexpr GroupId kRootGroupId = 0;

    enum class HierarchySelectionType { None, Object, Group };

    struct SceneGroup {
        GroupId id = kRootGroupId;
        std::string name;
        GroupId parentId = kRootGroupId;
        bool expanded = true;
    };

    enum class Primitive { Plane, Cube, Sphere, Capsule, Cylinder, Cone, Pyramid, Torus, Staircase, Empty };

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
        int cameraMode = 0;          // 0 = third person, 1 = first person, 2 = isometric
        float walkSpeed = 4.0f;
        float runSpeed = 7.0f;
        float jumpSpeed = 5.0f;
        float crouchSpeed = 2.0f;
        float crouchedHeight = 1.1f;
        float swimSpeed = 3.5f;
        float swimVerticalSpeed = 2.5f;
        float lookSensitivity = 0.1f;
        float capsuleRadius = 0.4f;
        float capsuleHeight = 1.8f;
        float eyeHeight = 0.6f;
        float cameraDistance = 5.0f;
        float cameraTargetHeight = 1.0f;
        float isometricYaw = -45.0f;
        float isometricPitch = -35.0f;
        float isometricDistance = 12.0f;
        float platformerYaw = -90.0f;   // side-view camera axis (-90 => run along world X)
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
        int facingMode = 0;          // 0 = face camera, 1 = face movement (free-orbit camera)
        float turnSpeed = 12.0f;     // how fast the body turns to face travel (face-movement)
    };

    using ScriptField = engine::ScriptField;
    struct ScriptBinding {
        bool enabled = true;
        std::string className;
        std::string path;
        std::vector<ScriptField> fields;
        int executionOrder = 0;
        std::vector<std::string> dependencies;
    };

    struct AnimationEvent {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
        std::string clipName;  // optional stable alias; preferred over clipIndex
    };

    // A separate animation file (e.g. Idle.fbx / Walk.fbx) merged onto the model by
    // bone name. Carried into the runtime scene so clips exist in Play, not just in the
    // Character Editor preview.
    struct AnimationSource {
        std::string file;
        engine::AssetHandle assetId;
        std::string clipName;       // runtime alias used by states
        bool        stripRootMotion = false;
        std::string sourceClipName; // take inside the source file
        float       basePlaybackSpeed = 1.0f; // baked from authoritative .3dgclip
    };

    // A static model socketed to a character bone (weapon, shield, hat...).
    struct ModelAttachment {
        std::string modelPath;
        engine::AssetHandle modelAssetId;
        engine::AssetHandle materialAssetId;
        std::string boneName;
        glm::vec3   position{0.0f};
        glm::vec3   eulerDegrees{0.0f};
        glm::vec3   scale{1.0f};
        std::string materialPath;   // optional .3dgmat applied to the attachment model
        std::string socketName;     // named gameplay point exposed to scripts
    };

    struct AnimationActionProfile {
        std::string name = "Action";
        int clipIndex = 0;
        std::string clipName;
        std::string maskRootBone;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f; // graph-authored speed multiplier (serialized name retained)
    };

    struct AnimationStateNode {
        enum class MotionSourceType {
            Clip = 0,
            BlendSpace1D = 1,
            BlendSpace2D = 2
        };
        struct BlendSample {
            int clipIndex = 0;
            std::string clipName;
            float value = 0.0f;
            float valueY = 0.0f;
        };
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
        std::vector<BlendSample> blendSamples;
        std::string blendParameterY;
        bool blendSpace2D = false;
        bool synchronizeBlendSpace = true;

        // Stable graph identity. Runtime scene serialization does not depend on this;
        // it is used by .3dggraph authoring so renames cannot break transition links.
        engine::AssetHandle graphId;
        MotionSourceType motionSourceType = MotionSourceType::Clip;
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
            NotEqual = 3,
            LessOrEqual = 4,
            Greater = 5
        };
        struct Condition {
            std::string parameter = "Speed";
            Compare compare = Compare::GreaterOrEqual;
            float threshold = 0.0f;
        };

        std::string fromState;
        std::string toState;

        // Parameter-driven transition condition.
        std::string parameter = "Speed";
        Compare compare = Compare::GreaterOrEqual;
        float threshold = 0.0f;
        float fade = 0.2f;
        float exitTime = 0.0f;
        int priority = 0;
        bool canInterrupt = false;

        // false = ignore all conditions and use Exit Time only.
        bool useConditions = true;
        
        bool requireAllConditions = true;
        std::vector<Condition> additionalConditions;

        // Stable authoring references used by Animation Graph assets. The legacy
        // names above remain populated for runtime/scene compatibility.
        engine::AssetHandle graphId;
        engine::AssetHandle fromStateId; // invalid = Any State
        engine::AssetHandle toStateId;
    };

    struct Object {
        engine::ecs::Entity entity = engine::ecs::kNull;
        std::string name;
        Primitive primitive = Primitive::Cube;
        bool light = false;
        bool navMeshBoundsVolume = false;
        bool reflectionProbeEnabled = false;
        engine::ecs::ReflectionProbe reflectionProbe;
        bool postProcessVolumeEnabled = false;
        engine::ecs::PostProcessVolume postProcessVolume;
        bool localFogVolumeEnabled = false;
        engine::ecs::LocalFogVolume localFogVolume;
        bool decal = false;
        float decalOpacity = 1.0f;
        float decalSurfaceOffset = 0.012f;
        engine::ecs::Light lightData;
        bool visible = true;
        bool locked = false;
        // Editor-only organization. Kept in the scene file but ignored by runtime export.
        std::string editorLayer = "Default";
        GroupId editorGroupId = kRootGroupId;
        std::string modelAssetPath;
        engine::AssetHandle modelAssetId;
        std::string materialAssetPath;
        engine::AssetHandle materialAssetId;
        std::unordered_map<std::string, std::string> materialParameterOverrides;
        glm::vec3 modelOrientationEuler{0.0f};   // render-only model rotation (deg); collider unaffected
        glm::vec3 modelOffsetPosition{0.0f};     // render-only model position offset; collider unaffected
        glm::vec3 modelOffsetScale{1.0f};        // render-only model scale (about model centre)
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
        std::vector<AnimationSource> animationSources;   // separate FBX clips merged by bone name
        std::vector<ModelAttachment> modelAttachments;   // static models socketed to bones
        engine::ecs::FootIKSettings footIK;              // grounded foot placement (opt-in)
        std::string characterAssetPath;                  // source .3dgcharacter (for live editor sync)
        engine::AssetHandle characterAssetId;
        std::string prefabAssetPath;                     // source .3dgprefab (editor live-sync link; not scene-serialized yet)
        engine::AssetHandle prefabAssetId;
        bool linearVelocityEnabled = false;
        bool angularVelocityEnabled = false;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        bool rigidBodyEnabled = false;
        bool colliderEnabled = false;
        engine::ecs::RigidBody rigidBody;
        engine::ecs::Collider collider;
        std::vector<engine::ecs::Collider> additionalColliders;
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
        bool ragdollEnabled = false;
        engine::Ragdoll ragdoll;
        bool scriptEnabled = false;
        std::string scriptClassName;
        std::string scriptPath;
        std::vector<ScriptField> scriptFields;
        int scriptExecutionOrder = 0;
        std::vector<std::string> scriptDependencies;
        std::vector<ScriptBinding> additionalScripts;
        bool audioSourceEnabled = false;
        std::string audioAssetPath;
        engine::AssetHandle audioAssetId;
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
        engine::AssetHandle particleAssetId;
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
        float navAgentHearingRange = 12.0f;  // omnidirectional noise perception radius (0 = deaf)
        float navAgentSquadAlertRadius = 18.0f;  // responds to a teammate's alert within this range
        float navAgentSquadForgetTime = 6.0f;    // seconds this agent's sighting keeps its squad alerted
        // M7: optional data-driven behaviour-tree asset ("" = built-in patrol/chase brain).
        std::string navAgentBrainAsset;
        engine::AssetHandle navAgentBrainAssetId;
        // Faction targeting: team id (0 = neutral). With auto-target on, the agent
        // acquires the nearest agent on a different non-zero team as its chase target.
        int  navAgentTeam = 0;
        bool navAgentAutoTarget = false;
        engine::ai::AiMovementMode navMovementMode = engine::ai::AiMovementMode::Grounded;
        float navMovementGravity = -9.81f;
        float navMovementMaxFallSpeed = 35.0f;
        float navMovementGroundProbe = 0.25f;
        float navMovementStepHeight = 0.35f;
        float navMovementMaxSlope = 50.0f;
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
        // Optional material assigned to each paint layer 1..5 (index 0..4). Empty = use
        // the default palette colour for that layer. The painted region takes on the
        // material's representative albedo colour.
        std::array<std::string, 5> terrainLayerMaterials{};

        // Instanced procedural grass, grown only where the Grass layer (1) is painted.
        // The grass* fields below are the "active" settings applied to NEWLY painted grass.
        bool  grassEnabled = false;
        float grassDensity = 2.0f;         // blades per square unit (before the paint mask)
        float grassHeight = 0.6f;
        bool  grassRandomizeHeight = false;
        float grassMinHeightScale = 0.75f;
        float grassMaxHeightScale = 1.25f;
        float grassWindStrength = 0.18f;   // wind is global (shared by all grass)
        float grassWindSpeed = 1.4f;
        glm::vec3 grassBaseColor{0.16f, 0.34f, 0.12f};   // root
        glm::vec3 grassTipColor{0.42f, 0.68f, 0.28f};    // tip
        // Per-region grass: each painted grass vertex records which frozen style it used,
        // so changing the active settings never alters grass already on the field.
        struct GrassStyleEntry {
            float density = 2.0f;
            float height = 0.6f;
            glm::vec3 base{0.16f, 0.34f, 0.12f};
            glm::vec3 tip{0.42f, 0.68f, 0.28f};
        };
        std::vector<GrassStyleEntry> terrainGrassPalette;    // frozen styles (slot 1..N)
        std::vector<unsigned char>   terrainGrassStyle;      // per-vertex slot (0 = active, 1..N)

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
        float waterDepthFadeDistance = 6.0f;
        float waterShoreFoamWidth = 0.8f;
        float waterShoreFoamStrength = 0.75f;
        float waterRefractionStrength = 0.018f;
        float waterReflectionRoughness = 0.12f;
        float waterEnvironmentReflectionStrength = 0.85f;
        float waterAbsorptionStrength = 0.75f;
        float waterCausticsStrength = 0.25f;
        float waterCausticsScale = 1.5f;
        float waterMaxRenderDistance = 2500.0f;
        glm::vec3 waterUnderwaterTint{0.04f, 0.30f, 0.38f};
        float waterUnderwaterFogDensity = 0.16f;
        float waterUnderwaterDistortion = 0.006f;
        float waterUnderwaterTransitionSpeed = 3.5f;
        // Surface motion (fed to engine::WaterConfig). Presets (lake/ocean/river) tune
        // these so each water type moves differently: calm lakes, choppy oceans, flowing
        // rivers. waterType is a label only (0 custom, 1 lake, 2 ocean, 3 river).
        int   waterType = 0;
        float waterSeaHeight = 0.55f;
        float waterSeaChoppy = 3.2f;
        float waterSeaSpeed  = 0.8f;
        float waterSeaFreq   = 0.10f;
        float waterFoam      = 0.55f;
        // A water body may follow a spline (by name) for directional flow -- see below.
        std::string waterFlowSpline;
        float waterRiverWidth = 8.0f;
        // Optional custom water fragment shader (a .glsl file holding helper funcs + main()).
        // Empty = built-in look. The engine prepends the water declaration prelude.
        std::string waterShaderPath;

        // Spline (Catmull-Rom path). General purpose: river flow, motion paths, camera
        // rails, spawn lanes, etc. Control points are world-space. splineType is a label
        // (0 path, 1 river, 2 rail). Rendered as a curve + handles by the editor.
        bool  isSpline = false;
        bool  splineClosed = false;
        int   splineType = 0;
        std::vector<glm::vec3> splinePoints;
        // Per-control-point Euler rotation in degrees. Z is the spline-local roll
        // used by river ribbons; X/Y remain available to other spline consumers.
        std::vector<glm::vec3> splinePointRotations;

        // Foliage actor. Instances are lightweight transforms local to this object and
        // are rendered in batches by FoliageRenderer rather than as hierarchy objects.
        bool isFoliage = false;
        std::string foliageAssetPath;
        engine::AssetHandle foliageAssetId;
        std::vector<engine::ecs::FoliageInstance> foliageInstances;
        std::uint32_t nextFoliageInstanceId = 1;
        // Name of the terrain object this foliage was painted onto (empty = free-standing).
        // Deleting that terrain cascade-deletes this foliage so grass "belongs" to its ground.
        std::string foliageTerrainOwner;
    };

    struct ObjectSnapshot {
        Object object;
        engine::ecs::Transform transform;
        glm::vec3 color{1.0f};
    };

    struct PhysicsJoint {
        enum class Type {
            Distance,
            Spring,
            Ball,     // point-to-point pin (3 translational DOF removed), optional cone limit
            Hinge     // pin + axis alignment (1 rotational DOF), optional angle limit + motor
        };

        Type type = Type::Distance;
        bool enabled = true;
        std::string objectA;
        std::string objectB;
        bool worldAnchor = false;
        glm::vec3 anchor{0.0f};          // Ball/Hinge: the world pivot point; Distance/Spring: world anchor when worldAnchor
        float restLength = 1.0f;
        bool rope = false;
        float stiffness = 100.0f;
        float damping = 1.0f;

        // Ball / Hinge authoring (Pass-3). axis is the world-space hinge axis at bind time; the
        // local anchors/axes for the solver are captured from the bodies' bind poses at play start.
        glm::vec3 axis{0.0f, 1.0f, 0.0f};
        bool  collideConnected = true;    // let the two jointed bodies still collide
        bool  angularLimit = false;       // clamp the swing (Hinge: [minAngle,maxAngle]; Ball: cone maxAngle)
        float minAngle = -180.0f;         // degrees (authoring unit; converted to radians at build)
        float maxAngle =  180.0f;         // degrees
        bool  motorEnabled = false;       // Hinge motor: drive rotation about the axis
        float motorTargetVelocity = 0.0f; // deg/s target (converted to rad/s at build)
        float motorMaxTorque = 0.0f;      // N*m ceiling
        float breakImpulse = 0.0f;        // 0 = unbreakable; else the joint snaps past this impulse
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

    struct ViewportBookmark {
        std::string name = "View";
        glm::vec3 position{0.0f, 3.0f, 8.0f};
        glm::vec3 target{0.0f, 1.0f, 0.0f};
        float fov = 45.0f;
        float blendDuration = 0.3f;
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
            engine::AssetHandle shaderAssetId;
            bool enabled = true;
            std::vector<PostProcessParameter> parameters;
        };

        // Sky source. 0 = procedural atmosphere (day/night, clouds), 1 = imported sky
        // image. An imported sky also lights the scene through IBL. skyRotation spins the
        // sky yaw (degrees); skyIntensity scales its brightness.
        int skyMode = 0;
        std::string skyTexturePath;          // equirectangular panorama (.png/.jpg)
        engine::AssetHandle skyTextureId;
        float skyRotation = 0.0f;
        float skyIntensity = 1.0f;
        float timeOfDay = 0.46f;
        std::string dayNightTimelinePath;
        engine::AssetHandle dayNightTimelineId;
        bool dayNightTimelineAutoplay = true;
        float skyLightIntensity = 1.0f;
        bool skylightOcclusion = true;
        float skylightOcclusionStrength = 1.0f;
        float minimumSkylight = 0.0f;
        float exposureEV = 0.0f;
        float specularOcclusionStrength = 0.85f;
        float localProbeInfluence = 1.0f;
        int lightingDebugMode = 0;
        std::string lightingBuildAsset;
        std::uint64_t lightingBuildHash = 0;
        int lightingBuildQuality = 1;
        float lightingProbeSpacing = 2.0f;
        float lightingRayDistance = 80.0f;
        float lightingIndirectBounceStrength = 1.0f;
        bool lightingIndirectBounceEnabled = true;
        int lightingDiffuseBounces = 2;
        int lightingRaysPerProbe = 0; // 0 uses the quality preset
        bool lightingUseMaterialTextures = true;
        bool lightingIncludeStaticLocalLights = true;
        bool lightingIncludeEmissive = true;
        float lightingEnergyThreshold = 0.01f;
        float lightingEmissiveContribution = 1.0f;
        float lightingIndirectSaturation = 1.0f;
        bool dynamicGiEnabled = false;
        int dynamicGiQuality = 1;
        float dynamicGiProbeSpacing = 3.0f;
        int dynamicGiRaysPerProbe = 24;
        int dynamicGiProbesPerFrame = 6;
        int dynamicGiMaxRaysPerFrame = 192;
        float dynamicGiMaxRayDistance = 60.0f;
        float dynamicGiHysteresis = 0.94f;
        float dynamicGiIntensity = 1.0f;
        bool dynamicGiRelocation = true;
        bool dynamicGiClassification = true;
        bool dynamicGiVisibilityWeighting = true;
        bool dynamicGiMultiBounce = true;
        float dynamicGiMultiBounceStrength = 0.75f;
        bool ssgiEnabled = false;
        float ssgiRayLength = 3.0f;
        int ssgiSteps = 12;
        float ssgiThickness = 0.20f;
        float ssgiIntensity = 0.35f;
        bool driveSunLight = true;
        float sunIntensity = 1.0f;
        bool clouds = true;
        float cloudCoverage = 0.45f;
        float cloudDensity = 0.75f;
        float cloudScale = 1.35f;
        float cloudSoftness = 0.18f;
        float cloudWindSpeed = 0.025f;
        float cloudWindDirection = 25.0f;
        float cloudHorizonHeight = 0.08f;
        glm::vec3 cloudColor{1.0f, 0.98f, 0.94f};
        bool cloudShadows = true;
        float cloudShadowStrength = 0.45f;
        float cloudShadowScale = 0.035f;
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
        engine::AssetHandle hudAssetId;
        std::vector<PostProcessEffect> postProcessEffects;
        bool directionalShadows = true;
        bool pointShadows = true;
        bool spotShadows = true;
        float shadowSoftness = 2.5f;
        float shadowDistance = 300.0f;
        bool  fog = true;
        glm::vec3 fogColor{0.58f, 0.68f, 0.80f};
        float fogDensity = 0.008f;
        float fogHeight = -0.35f;
        float fogHeightFalloff = 0.10f;
        // Lighting Pass 4: authored environment and presentation controls. These
        // resolve into one EnvironmentLightingState at runtime.
        float atmosphereRayleigh = 1.0f;
        float atmosphereRayleighHeight = 8.0f;
        float atmosphereMie = 1.0f;
        float atmosphereMieHeight = 1.2f;
        float atmosphereMieAnisotropy = 0.76f;
        float atmosphereOzone = 1.0f;
        float atmosphereIntensity = 1.0f;
        float sunAngularDiameter = 0.53f;
        float sunDiskIntensity = 10.0f;
        bool stars = true;
        float starIntensity = 0.65f;
        bool moon = true;
        glm::vec3 moonColor{0.50f, 0.62f, 0.90f};
        float moonIntensity = 0.07f;
        float moonAngularDiameter = 0.52f;
        float moonPhase = 1.0f;
        float moonGiContribution = 1.0f;
        float dayEnvironmentIntensity = 1.0f;
        float twilightEnvironmentIntensity = 0.20f;
        float nightEnvironmentIntensity = 0.015f;
        float nightReflectionIntensity = 1.0f;
        float nightFogScattering = 1.0f;
        float nightCloudAmbient = 1.0f;
        bool volumetricFog = false;
        float volumetricScattering = 1.0f;
        float volumetricExtinction = 1.0f;
        float volumetricAnisotropy = 0.55f;
        float volumetricStartDistance = 0.0f;
        float volumetricMaxDistance = 180.0f;
        int environmentQuality = 2;
        bool autoExposure = true;
        float exposureMinEV = -4.0f;
        float exposureMaxEV = 4.0f;
        float exposureCompensationEV = 0.0f;
        float exposureSpeedUp = 3.0f;
        float exposureSpeedDown = 1.0f;
        bool preserveNightDarkness = true;
        float nightExposureLimitEV = 1.0f;
        bool bloom = true;
        float bloomThreshold = 1.0f;
        float bloomKnee = 0.5f;
        float bloomStrength = 0.08f;
        float colorTemperature = 6500.0f;
        float colorTint = 0.0f;
        float colorSaturation = 1.0f;
        float colorContrast = 1.0f;
        glm::vec3 colorLift{0.0f};
        glm::vec3 colorGamma{1.0f};
        glm::vec3 colorGain{1.0f};
        float colorLutIntensity = 1.0f;
        std::string colorLutPath;
        glm::vec3 physicsGravity{0.0f, -9.81f, 0.0f};
        int physicsSolverIterations = 10;   // Pass-3: 4 was too few to converge stacks
        bool physicsBroadPhase = true;
        float physicsCellSize = 2.0f;
        float physicsRestitutionThreshold = 1.0f;   // m/s (was 0.5; too low -> resting jitter)
        bool physicsAllowSleeping = true;
        float physicsSleepLinearVelocity = 0.06f;
        float physicsSleepAngularVelocity = 0.15f;
        float physicsTimeToSleep = 0.5f;
        bool showPhysicsGuides = true;
        bool selectedPhysicsGuideOnly = false;
    };

    struct GameModeSettings {
        std::string playerObjectName;  // empty = first object with Player Controller
        bool playerInputEnabled = true;
        bool startPaused = false;
        bool allowPause = true;
        bool allowRestart = true;
        bool loseOnPlayerDeath = true;
        int initialScore = 0;
        bool cameraOverride = false;
        int cameraMode = 0;            // 0 = third person, 1 = first person, 2 = isometric
    };

    struct Snapshot {
        std::vector<ObjectSnapshot> objects;
        std::vector<SceneGroup> groups;
        std::vector<PhysicsJoint> joints;
        std::vector<CameraPreset> cameraPresets;
        std::vector<CameraSequence> cameraSequences;
        std::vector<ViewportBookmark> viewportBookmarks;
        Environment environment;
        GameModeSettings gameMode;
        int selectedIndex = -1;
        HierarchySelectionType hierarchySelection = HierarchySelectionType::None;
        GroupId selectedGroupId = kRootGroupId;
        GroupId nextGroupId = 1;
        int nextCubeNumber = 1;
    };

    void BuildDefault(const engine::Mesh& Cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    bool Save(const std::string& path, std::string* error, bool markClean = true);
    bool Load(const std::string& path, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase, std::string* error);

    engine::ecs::Registry& Registry() { return m_registry; }
    const engine::ecs::Registry& Registry() const { return m_registry; }
    const std::vector<Object>& Objects() const { return m_objects; }
    const std::vector<SceneGroup>& Groups() const { return m_groups; }
    const std::vector<PhysicsJoint>& PhysicsJoints() const { return m_joints; }
    const std::vector<CameraPreset>& CameraPresets() const { return m_cameraPresets; }
    const std::vector<CameraSequence>& CameraSequences() const { return m_cameraSequences; }
    const std::vector<ViewportBookmark>& ViewportBookmarks() const { return m_viewportBookmarks; }
    const CameraPreset* PrimaryCameraPreset() const;
    bool IsDirty() const { return m_dirty; }
    engine::AssetHandle AssetId() const { return m_assetId; }
    void MarkClean() { m_dirty = false; }
    void MarkDirty() { m_dirty = true; }

    int SelectedIndex() const { return m_selectedIndex; }
    HierarchySelectionType HierarchySelection() const { return m_hierarchySelection; }
    GroupId SelectedGroupId() const { return m_selectedGroupId; }
    const SceneGroup* SelectedGroup() const;
    // While suppressed, edits do not push undo snapshots (used for live drag-sync from
    // the Character Editor, which would otherwise spam the undo stack every frame).
    void SuppressUndo(bool suppress) { m_undoSuppressed = suppress; }
    void PushUndoSnapshot();
    const Object* SelectedObject() const;
    engine::ecs::Transform* SelectedTransform();
    const engine::ecs::Transform* TryGetTransform(engine::ecs::Entity entity) const;
    const engine::ecs::MeshRenderer* TryGetMeshRenderer(engine::ecs::Entity entity) const;
    const engine::ecs::Light* TryGetLight(engine::ecs::Entity entity) const;
    const engine::ecs::ReflectionProbe* TryGetReflectionProbe(engine::ecs::Entity entity) const;
    bool SetSelectedReflectionProbe(bool enabled, const engine::ecs::ReflectionProbe& probe);
    bool SetSelectedPostProcessVolume(bool enabled, const engine::ecs::PostProcessVolume& volume);
    bool SetSelectedLocalFogVolume(bool enabled, const engine::ecs::LocalFogVolume& volume);
    const Environment& GetEnvironment() const { return m_environment; }
    const GameModeSettings& GetGameModeSettings() const { return m_gameMode; }
    void SetGameModeSettings(const GameModeSettings& settings);
    bool IsVisible(engine::ecs::Entity entity) const;
    bool SelectedLocked() const;

    void SelectNext();
    void SelectPrevious();
    void SelectIndex(int index);
    void SelectGroup(GroupId id);
    // Shift+click: add the object to the multi-selection, or remove it if already in.
    // The primary selection (SelectedIndex, used by the inspector/gizmo) follows the
    // most-recently toggled object.
    void ToggleSelection(int index);
    void Deselect();
    const Object* FindObject(engine::ecs::Entity entity) const;
    Object* FindObject(engine::ecs::Entity entity);
    int FindObjectIndex(engine::ecs::Entity entity) const;
    bool SelectEntity(engine::ecs::Entity entity);

    bool IsHierarchyNameAvailable(const std::string& name,
                                  engine::ecs::Entity ignoreObject = engine::ecs::kNull,
                                  GroupId ignoreGroup = kRootGroupId) const;
    std::string MakeUniqueHierarchyName(const std::string& requested,
                                        engine::ecs::Entity ignoreObject = engine::ecs::kNull,
                                        GroupId ignoreGroup = kRootGroupId) const;
    GroupId CreateGroup(const std::string& name = "Group",
                        GroupId parentId = kRootGroupId);
    bool RenameGroup(GroupId id, const std::string& name);
    bool MoveObjectToGroup(int objectIndex, GroupId groupId);
    bool MoveSelectedObjectsToGroup(GroupId groupId);
    bool MoveGroupToGroup(GroupId id, GroupId parentId);
    bool DeleteGroup(GroupId id);
    bool GroupExists(GroupId id) const;
    std::size_t GroupObjectCount(GroupId id, bool recursive = false) const;
    std::size_t ChildGroupCount(GroupId id) const;
    // Every selected object (includes the primary). Single selection = one entry.
    // Self-heals against the many sites that set the primary directly (add/duplicate/undo).
    const std::vector<int>& SelectedIndices() const;
    void SelectIndices(const std::vector<int>& indices);
    bool AssignObjectsToLayer(const std::vector<int>& indices, const std::string& layer);
    bool SetLayerVisible(const std::string& layer, bool visible);
    bool SetLayerLocked(const std::string& layer, bool locked);
    bool RenameLayer(const std::string& oldName, const std::string& newName);
    bool ShowAllLayers();
    void MoveSelected(const glm::vec3& delta);
    void RotateSelected(const glm::vec3& axis, float degrees);
    void RotateSelectedYaw(float degrees);
    void ScaleSelectedAxis(const glm::vec3& axis, float factor);
    void ScaleSelected(float factor);
    bool SetSelectedTransform(const engine::ecs::Transform& transform);
    bool SetObjectTransformsUndoable(
        const std::vector<int>& indices,
        const std::vector<engine::ecs::Transform>& transforms);
    void ResetSelectedTransform();
    void BeginTransformEdit();
    void EndTransformEdit();
    void BeginParticleEdit();
    void EndParticleEdit();
    bool Undo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    bool Redo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    Snapshot CreateSnapshot();
    void RestoreFromSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase);
    void ApplySnapshotUndoable(const Snapshot& snapshot, const engine::Mesh& cube,
                               const engine::Mesh& plane, const engine::Mesh& sphere,
                               const engine::Mesh& capsule, const engine::Mesh& cylinder,
                               const engine::Mesh& cone, const engine::Mesh& pyramid,
                               const engine::Mesh& torus, const engine::Mesh& staircase);
    void AddEmpty(const engine::Mesh& placeholderMesh);
    void AddFoliage(const engine::Mesh& placeholderMesh);
    void AddCube(const engine::Mesh& cube);
    void AddPlane(const engine::Mesh& plane);
    void AddDecal(const engine::Mesh& plane, const glm::vec3& position,
                  const glm::vec3& surfaceNormal, const glm::vec2& size,
                  float rotationDegrees, float surfaceOffset, float opacity,
                  const std::string& materialPath);
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
    bool SetSelectedModelAsset(
        const std::string& path, engine::AssetHandle id = {});
    bool SetSelectedModelOrientation(const glm::vec3& eulerDegrees);
    // Render-only model offset transform (position + Euler rotation + scale). The
    // collider/controller read the object Transform, which is left untouched.
    bool SetSelectedModelOffset(const glm::vec3& position,
                                const glm::vec3& eulerDegrees,
                                const glm::vec3& scale);
    bool SetSelectedMaterialAsset(
        const std::string& path, engine::AssetHandle id = {});
    // Assign one material to every unlocked object in the current selection.
    // Returns the number assigned and records the whole operation as one undo step.
    int SetSelectedMaterialAssetToSelection(
        const std::string& path, engine::AssetHandle id = {});
    bool SetSelectedMaterialParameterOverride(const std::string& name,
                                              const std::string& value);
    bool SetSelectedDecalSettings(float opacity, float surfaceOffset);
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
    // Separate FBX clips merged onto the model by bone name; carried into Play so the
    // character animates instead of falling back to the bind (T-)pose.
    bool SetSelectedAnimationSources(const std::vector<AnimationSource>& sources);
    // Static models socketed to the character's bones (weapons, shields...).
    bool SetSelectedModelAttachments(const std::vector<ModelAttachment>& attachments);
    bool SetSelectedFootIK(const engine::ecs::FootIKSettings& footIK);
    // Record the source .3dgcharacter path so the editor can live-sync edits to it.
    bool SetSelectedCharacterAssetPath(
        const std::string& path, engine::AssetHandle id = {});
    bool SetSelectedPrefabAssetPath(const std::string& path, engine::AssetHandle id);
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
    bool SetSelectedColliderAt(std::size_t index, const engine::ecs::Collider& collider);
    bool AddSelectedCollider(const engine::ecs::Collider& collider);
    bool RemoveSelectedCollider(std::size_t index);
    bool SetSelectedColliders(const std::vector<engine::ecs::Collider>& colliders);
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
    bool SetSelectedRagdollEnabled(bool enabled);
    bool SetSelectedRagdoll(const engine::Ragdoll& ragdoll);
    bool SetSelectedScript(const std::string& className, const std::string& path, bool enabled);
    bool SetSelectedScriptScheduling(int executionOrder,
                                     const std::vector<std::string>& dependencies);
    bool SetSelectedAdditionalScripts(const std::vector<ScriptBinding>& scripts);
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
                             const std::string& targetName, float visionRange, float visionHalfAngle,
                             float hearingRange = 12.0f,
                             float squadAlertRadius = 18.0f, float squadForgetTime = 6.0f);
    bool SetSelectedNavAgentBrain(const std::string& brainAsset);
    bool SetSelectedNavAgentTeam(int team, bool autoTarget);
    bool SetSelectedNavAgentMovement(engine::ai::AiMovementMode mode, float gravity,
                                     float maxFallSpeed, float groundProbe,
                                     float stepHeight, float maxSlopeDegrees);
    bool SetSelectedTerrain(bool enabled, int res, float size, float maxHeight,
                            int seed, int octaves, float frequency);
    bool SetSelectedTerrainHeights(std::vector<float> heights);   // sculpt result (no undo)
    bool UpdateSelectedTerrainHeightRegion(const std::vector<float>& heights,
                                           int resolution,
                                           int minI, int minJ, int maxI, int maxJ);
    bool SetSelectedTerrainPaint(std::vector<unsigned char> paint);   // paint result (no undo)
    bool SetSelectedFoliageAsset(const std::string& path,
                                 engine::AssetHandle id = {});
    bool AddSelectedFoliageInstance(const glm::vec3& worldPosition,
                                    const glm::vec3& rotationDegrees,
                                    const glm::vec3& scale,
                                    std::uint32_t typeIndex = 0);
    std::size_t EraseSelectedFoliageInstances(const glm::vec3& worldPosition,
                                               float radius);
    bool SetSelectedFoliageTerrainOwner(const std::string& terrainName);
    bool SetSelectedFoliageInstance(std::uint32_t id,
                                    const engine::ecs::FoliageInstance& instance);
    bool RemoveSelectedFoliageInstance(std::uint32_t id);
    bool DuplicateSelectedFoliageInstance(std::uint32_t id,
                                          std::uint32_t* newId = nullptr);
    bool ClearSelectedFoliageInstances();
    // Assign (or clear, with an empty path) the material painted for layer 1..5.
    bool SetSelectedTerrainLayerMaterial(int layer, const std::string& materialPath);
    // Grass (instanced blades on the painted grass layer).
    bool SetSelectedTerrainGrass(bool enabled, float density, float height,
                                 float windStrength, float windSpeed,
                                 const glm::vec3& baseColor, const glm::vec3& tipColor,
                                 bool randomizeHeight = false,
                                 float minHeightScale = 0.75f,
                                 float maxHeightScale = 1.25f);
    // Per-region grass style: freeze the active settings into a palette slot (deduped) and
    // read/write the per-vertex slot map so painting new grass never changes old grass.
    int  EnsureActiveGrassStyleSlot();                     // returns 1-based slot
    std::vector<unsigned char> SelectedTerrainGrassStyle();
    bool SetSelectedTerrainGrassStyle(std::vector<unsigned char> style);
    bool SetSelectedWater(float size, int resolution, float level,
                          const glm::vec3& shallow, const glm::vec3& deep,
                          const glm::vec3& reflection, float transparency,
                          float fresnel, float specular, float shininess);
    // Surface motion + foam (lake/ocean/river presets differ here). waterType is a label.
    bool SetSelectedWaterWaves(float seaHeight, float seaChoppy, float seaSpeed,
                               float seaFreq, float foam, int waterType);
    bool SetSelectedWaterDepth(float fadeDistance, float shoreFoamWidth,
                               float shoreFoamStrength);
    bool SetSelectedWaterOptics(float refractionStrength, float reflectionRoughness,
                                float environmentReflectionStrength,
                                float absorptionStrength);
    bool SetSelectedWaterEffects(float causticsStrength, float causticsScale,
                                 float maxRenderDistance, const glm::vec3& underwaterTint,
                                 float underwaterFogDensity, float underwaterDistortion,
                                 float underwaterTransitionSpeed);
    bool SetSelectedWaterFlowSpline(const std::string& splineName);   // river follows a spline
    bool SetSelectedWaterRiverWidth(float width);
    bool SetSelectedWaterShaderPath(const std::string& path);   // custom water fragment shader

    // Spline authoring (Catmull-Rom path).
    bool SetSelectedSpline(bool enabled, bool closed, int type);
    bool AddSelectedSplinePoint(const glm::vec3& point);
    bool InsertSelectedSplinePoint(std::size_t index, const glm::vec3& point);
    bool SetSelectedSplinePoint(std::size_t index, const glm::vec3& point);
    bool SetSelectedSplinePointRotation(std::size_t index, const glm::vec3& degrees);
    bool RemoveSelectedSplinePoint(std::size_t index);
    bool SetSelectedSplinePoints(const std::vector<glm::vec3>& points);
    bool SetSelectedSplinePointRotations(const std::vector<glm::vec3>& rotations);
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
    std::size_t AddViewportBookmark(const ViewportBookmark& bookmark);
    bool SetViewportBookmark(std::size_t index, const ViewportBookmark& bookmark);
    bool RemoveViewportBookmark(std::size_t index);
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
    void SyncSplineComponent(Object& object);
    void SyncFoliageComponent(Object& object);
    void ClearHistory();
    void Clear();

    engine::ecs::Registry m_registry;
    engine::AssetHandle m_assetId;
    std::vector<Object> m_objects;
    std::vector<SceneGroup> m_groups;
    std::vector<PhysicsJoint> m_joints;
    std::vector<CameraPreset> m_cameraPresets;
    std::vector<CameraSequence> m_cameraSequences;
    std::vector<ViewportBookmark> m_viewportBookmarks;
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;
    Environment m_environment;
    GameModeSettings m_gameMode;
    int m_selectedIndex = -1;
    HierarchySelectionType m_hierarchySelection = HierarchySelectionType::None;
    GroupId m_selectedGroupId = kRootGroupId;
    GroupId m_nextGroupId = 1;
    // Multi-selection; includes m_selectedIndex. Mutable so the const accessor can prune
    // out-of-range entries and collapse to single when a direct primary write desynced it.
    mutable std::vector<int> m_selectedIndices;
    void EnsureSelectionValid() const;
    int m_nextCubeNumber = 1;
    bool m_dirty = false;
    bool m_undoSuppressed = false;
    bool m_transformEditOpen = false;
    bool m_particleEditOpen = false;
};
