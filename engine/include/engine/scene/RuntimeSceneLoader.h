#pragma once

#include "engine/ecs/Entity.h"
#include "engine/ecs/Components.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/Script.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/ai/AiMovement.h"
#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <string>
#include <vector>
#include <unordered_map>

namespace engine {

class Mesh;

namespace ecs {
class Registry;
}

class RuntimeSceneLoader {
public:
    struct AnimationEventDesc {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
        std::string clipName;
    };

    struct AnimationSourceDesc {
        std::string path;
        AssetHandle assetId;
        std::string clipName;
        bool        stripRootMotion = false;
        std::string sourceClipName;
        float       basePlaybackSpeed = 1.0f;
    };

    struct AttachmentDesc {
        std::string path;
        AssetHandle assetId;
        AssetHandle materialAssetId;
        std::string boneName;
        glm::vec3   position{0.0f};
        glm::vec3   eulerDegrees{0.0f};
        glm::vec3   scale{1.0f};
        std::string materialPath;   // optional .3dgmat applied to the attachment model
        std::string socketName;
    };

    struct AnimationActionProfileDesc {
        std::string name;
        int clipIndex = 0;
        std::string clipName;
        std::string maskRootBone;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f;
    };

    struct AnimationStateDesc {
        struct BlendSampleDesc {
            int clipIndex = 0;
            std::string clipName;
            float value = 0.0f;
            float valueY = 0.0f;
        };
        std::string name;
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
        std::vector<BlendSampleDesc> blendSamples;
        std::string blendParameterY;
        bool blendSpace2D = false;
        bool synchronizeBlendSpace = true;
    };

    struct AnimationParameterDesc {
        std::string name;
        int type = 0;
        float defaultValue = 0.0f;
    };

    struct AnimationTransitionDesc {
        struct Condition {
            std::string parameter = "Speed";
            int compare = 0;
            float threshold = 0.0f;
        };
        std::string fromState;
        std::string toState;
        std::string parameter = "Speed";
        int compare = 0;
        float threshold = 0.0f;
        float fade = 0.2f;
        float exitTime = 0.0f;
        int priority = 0;
        bool canInterrupt = false;
        bool useConditions = true;
        bool requireAllConditions = true;
        std::vector<Condition> additionalConditions;
    };

    // Authored first/third-person/isometric player settings (matches the editor's
    // PlayerControllerSettings; the standalone player applies these).
    struct PlayerControllerDesc {
        bool  firstPerson = false;
        int   cameraMode = 0;
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
        float platformerYaw = -90.0f;   // side-view camera axis
        bool  cameraCollision = true;
        float cameraProbeRadius = 0.20f;
        float cameraCollisionPadding = 0.08f;
        float cameraReturnSpeed = 8.0f;
        bool  shoulderCamera = false;
        float shoulderOffset = 0.65f;
        float shoulderSwitchSpeed = 12.0f;
        bool  rightShoulder = true;
        bool  lockOnEnabled = false;
        float lockOnRange = 18.0f;
        float lockOnViewAngle = 55.0f;
        float lockOnTargetHeight = 1.0f;
        float lockOnTrackingSpeed = 10.0f;
        float maxSlopeDegrees = 50.0f;
        float stepHeight = 0.35f;
        int   facingMode = 0;        // 0 = face camera, 1 = face movement
        float turnSpeed = 12.0f;
    };

    struct EntityDesc {
        std::string primitive;
        std::string name;
        bool playerControllerEnabled = false;
        PlayerControllerDesc playerController;
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f};
        bool reflectionProbeEnabled = false;
        ecs::ReflectionProbe reflectionProbe;
        bool postProcessVolumeEnabled = false;
        ecs::PostProcessVolume postProcessVolume;
        bool localFogVolumeEnabled = false;
        ecs::LocalFogVolume localFogVolume;
        std::string modelPath;
        AssetHandle modelAssetId;
        std::string materialPath;
        AssetHandle materialAssetId;
        glm::vec3 modelOrientationEuler{0.0f};   // render-only rotation (deg); collider unaffected
        glm::vec3 modelOffsetPosition{0.0f};     // render-only mesh position offset; collider unaffected
        glm::vec3 modelOffsetScale{1.0f};        // render-only mesh scale (about model centre)
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
        std::vector<AnimationEventDesc> animationEvents;
        std::vector<AnimationActionProfileDesc> animationActionProfiles;
        std::vector<AnimationStateDesc> animationStates;
        std::vector<AnimationParameterDesc> animationParameters;
        std::vector<AnimationTransitionDesc> animationTransitions;
        std::vector<AnimationSourceDesc> animationSources;   // separate FBX clips merged by bone name
        std::vector<AttachmentDesc> attachments;             // static models socketed to bones
        ecs::FootIKSettings footIK;                          // grounded foot placement (opt-in)
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
        struct AdditionalScript {
            bool enabled = true;
            std::string className;
            std::string path;
            std::vector<ScriptField> fields;
            int executionOrder = 0;
            std::vector<std::string> dependencies;
        };
        std::vector<AdditionalScript> additionalScripts;
        bool audioSourceEnabled = false;
        engine::ecs::AudioSource audioSource;
        bool triggerAudioEnabled = false;
        engine::ecs::TriggerAudioAction triggerAudio;
        bool particleSystemEnabled = false;
        engine::ParticleSystemComponent particleSystem;
        bool particleEffectEnabled = false;
        engine::ParticleEffectComponent particleEffect;
        bool triggerParticleEnabled = false;
        engine::TriggerParticleAction triggerParticle;
    };

    struct Scene {
        struct NavBounds {
            glm::vec3 position{0.0f};
            glm::vec3 scale{1.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        };
        struct NavAgentDesc {
            std::string entityName;
            float speed = 3.0f;
            float maxForce = 20.0f;
            float reachRadius = 0.6f;
            float repathInterval = 0.3f;
            std::string targetName;
            float visionRange = 12.0f;
            float visionHalfAngle = 45.0f;
            float hearingRange = 12.0f;
            std::string brainAsset;
            AssetHandle brainAssetId;
            int team = 0;
            bool autoTarget = false;
            engine::ai::AiMovementMode movementMode = engine::ai::AiMovementMode::Grounded;
            float movementGravity = -9.81f;
            float movementMaxFallSpeed = 35.0f;
            float movementGroundProbe = 0.25f;
            float movementStepHeight = 0.35f;
            float movementMaxSlope = 50.0f;
            std::vector<glm::vec3> patrolPoints;
        };
        struct TriggerActionDesc {
            std::string triggerName;
            std::string targetName;
            int enterMover = 0, enterRotator = 0;
            int exitMover = 0, exitRotator = 0;
            std::string cameraSequence;
            int enterCamera = 0, exitCamera = 0;
            bool cameraLockInput = true;
            bool cameraSkippable = true;
        };
        struct CameraZoneDesc {
            std::string triggerName;
            std::string presetName;
            bool restoreOnExit = true;
            int priority = 0;
            float returnBlend = 0.35f;
        };
        struct PhysicsJointDesc {
            int type = 0;
            std::string objectA, objectB;
            bool worldAnchor = false;
            glm::vec3 anchor{0.0f};
            float restLength = 1.0f;
            bool rope = false;
            float stiffness = 100.0f;
            float damping = 1.0f;
        };
        struct TerrainDesc {
            std::string entityName;
            int resolution = 128;
            float size = 64.0f;
            float maxHeight = 8.0f;
            int seed = 1337;
            int octaves = 5;
            float frequency = 2.0f;
            std::vector<float> heights;
            std::vector<unsigned char> paint;
            std::array<std::string, 5> layerMaterials{};
        };
        struct WaterDesc {
            std::string name;
            std::string splineName;
            std::string shaderPath;   // optional custom water fragment shader (.glsl)
            glm::vec3 center{0.0f};
            float size = 80.0f;
            int resolution = 160;
            glm::vec3 shallow{0.14f, 0.55f, 0.60f};
            glm::vec3 deep{0.02f, 0.13f, 0.20f};
            glm::vec3 reflection{0.55f, 0.72f, 0.92f};
            float transparency = 0.74f;
            float fresnel = 5.0f;
            float specular = 0.8f;
            float shininess = 400.0f;
            float seaHeight = 0.55f;
            float seaChoppy = 3.2f;
            float seaSpeed = 0.8f;
            float seaFreq = 0.10f;
            float foam = 0.55f;
            glm::vec2 flowDir{0.0f};
            float flowStrength = 0.0f;
            float riverWidth = 8.0f;
            bool splineClosed = false;
            std::vector<glm::vec3> splinePoints;
            std::vector<glm::vec3> splinePointRotations;
            float depthFadeDistance = 6.0f;
            float shoreFoamWidth = 0.8f;
            float shoreFoamStrength = 0.75f;
            float refractionStrength = 0.018f;
            float reflectionRoughness = 0.12f;
            float environmentReflectionStrength = 0.85f;
            float absorptionStrength = 0.75f;
            float causticsStrength = 0.25f;
            float causticsScale = 1.5f;
            float maxRenderDistance = 2500.0f;
            glm::vec3 underwaterTint{0.04f, 0.30f, 0.38f};
            float underwaterFogDensity = 0.16f;
            float underwaterDistortion = 0.006f;
            float underwaterTransitionSpeed = 3.5f;
        };
        struct SplineDesc {
            std::string name;
            bool closed = false;
            std::vector<glm::vec3> points;
            std::vector<glm::vec3> rotations;
        };
        struct FoliageDesc {
            std::string name;
            std::string assetPath;
            AssetHandle assetId;
            ecs::Transform transform;
            std::vector<ecs::FoliageInstance> instances;
        };
        struct CameraPreset {
            std::string name;
            glm::vec3 position{0.0f, 3.0f, 8.0f};
            glm::vec3 target{0.0f, 1.0f, 0.0f};
            float fov = 45.0f;
            float nearPlane = 0.1f;
            float farPlane = 3000.0f;
            float blendDuration = 0.35f;
            int blendEasing = 1;
            bool primary = false;
            bool useInPlay = false;
        };
        struct CameraSequenceShot {
            std::string cameraName;
            float travelDuration = 1.0f;
            float holdDuration = 0.25f;
            int easing = 1;
            int pathMode = 0;
            std::string eventName;
        };
        struct CinematicCue {
            int type = 0;
            float time = 0.0f;
            std::string name;
            std::string assetPath;
            std::string targetObject;
            std::string animationClip;
            float volume = 1.0f;
        };
        struct CameraSequence {
            std::string name;
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
                AssetHandle shaderAssetId;
                bool enabled = true;
                std::vector<PostProcessParameter> parameters;
            };
            int skyMode = 0;                 // 0 = procedural, 1 = imported sky image
            std::string skyTexturePath;      // equirectangular panorama (.png/.jpg)
            float skyRotation = 0.0f;
            float skyIntensity = 1.0f;
            float timeOfDay = 0.46f;
            std::string dayNightTimelinePath;
            bool dayNightTimelineAutoplay = true;
            float skyLightIntensity = 1.0f;
            bool skylightOcclusion = true;
            float skylightOcclusionStrength = 0.90f;
            float minimumSkylight = 0.06f;
            float exposureEV = 0.0f;
            float specularOcclusionStrength = 0.85f;
            float localProbeInfluence = 1.0f;
            int lightingDebugMode = 0;
            std::string lightingBuildAsset;
            std::uint64_t lightingBuildHash = 0;
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
            float shadowDistance = 300.0f;
            bool fog = true;
            float fogDensity = 0.008f;
            float fogHeight = -0.35f;
            float fogHeightFalloff = 0.10f;
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
            int physicsSolverIterations = 4;
            bool physicsBroadPhase = true;
            float physicsCellSize = 2.0f;
            float physicsRestitutionThreshold = 0.5f;
            bool physicsAllowSleeping = true;
            float physicsSleepLinearVelocity = 0.06f;
            float physicsSleepAngularVelocity = 0.15f;
            float physicsTimeToSleep = 0.5f;
            std::string hudAsset;   // reusable .hud drawn during play (empty = none)
            AssetHandle hudAssetId;
            std::vector<PostProcessEffect> postProcessEffects;
        };

        struct GameModeSettings {
            std::string playerObjectName;
            bool playerInputEnabled = true;
            bool startPaused = false;
            bool allowPause = true;
            bool allowRestart = true;
            bool loseOnPlayerDeath = true;
            int initialScore = 0;
            bool cameraOverride = false;
            int cameraMode = 0;
        };

        Environment environment;
        GameModeSettings gameMode;
        std::vector<NavBounds> navBounds;
        std::vector<NavAgentDesc> navAgents;
        std::vector<TriggerActionDesc> triggerActions;
        std::vector<CameraZoneDesc> cameraZones;
        std::vector<PhysicsJointDesc> physicsJoints;
        std::vector<TerrainDesc> terrains;
        std::vector<WaterDesc> waters;
        std::vector<SplineDesc> splines;
        std::vector<FoliageDesc> foliage;
        std::vector<CameraPreset> cameraPresets;
        std::vector<CameraSequence> cameraSequences;
        std::vector<EntityDesc> entities;
        struct LightDesc {
            std::string name;
            engine::ecs::Light light;
            glm::vec3 position{0.0f};
        };
        std::vector<LightDesc> lights;
    };

    struct PrimitiveMeshes {
        const Mesh* cube = nullptr;
        const Mesh* plane = nullptr;
        const Mesh* sphere = nullptr;
        const Mesh* capsule = nullptr;
        const Mesh* cylinder = nullptr;
        const Mesh* cone = nullptr;
        const Mesh* pyramid = nullptr;
        const Mesh* torus = nullptr;
        const Mesh* staircase = nullptr;
    };

    static bool Load(const std::string& path, Scene* scene, std::string* error);
    // Instantiate every entity/light in 'scene' into 'registry', reporting the created
    // entities in 'created' (the per-level group used for streaming unload). worldOffset
    // is retained for callers that only need translated placement; the world streaming
    // manager applies the manifest's complete transform after instantiation.
    static bool Instantiate(const Scene& scene, ecs::Registry& registry,
                            const PrimitiveMeshes& meshes, std::vector<ecs::Entity>* created,
                            std::string* error,
                            const glm::vec3& worldOffset = glm::vec3(0.0f));
};

} // namespace engine
