#pragma once

#include "engine/assets/AssetIdentity.h"
#include "engine/ecs/Entity.h"
#include "engine/audio/AudioTypes.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/ParticleSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

class Model;     // non-owning loaded model pointers
class Texture;   // non-owning material map pointers
class Shader;

namespace ecs{

// Runtime object name exported from the editor. Useful for gameplay scripts,
// trigger wiring, diagnostics, and small-game object lookup.
struct RuntimeName {
    std::string value;
};

// Runtime-editable spline data. Editor-authored spline objects receive this same
// component in Play mode, so native C++ and Lua scripts use one API in editor and
// packaged games. Rotations are Euler degrees; local Z is ribbon roll/banking.
struct SplineComponent {
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> rotations;
    bool closed = false;
    std::uint64_t revision = 1;
};

// Position / rotation / scale, with the model matrix derived on demand. The
// engine's common spatial component — most renderable or moving entities have one.
struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)

    glm::mat4 Model() const {
        return glm::translate(glm::mat4(1.0f), position)
             * glm::mat4_cast(rotation)
             * glm::scale(glm::mat4(1.0f), scale);
    }
};

// Authored local image-based-lighting capture. Box probes are the default because
// they fit rooms and corridors without leaking reflections through nearby walls.
// The stable ID survives object renames; captureSourceHash identifies the exact
// scene state used to produce the baked cubemap.
struct ReflectionProbe {
    enum class Shape : std::uint8_t { Box = 0, Sphere = 1 };

    AssetHandle stableId;
    Shape shape = Shape::Box;
    glm::vec3 boxExtents{5.0f};
    float radius = 5.0f;
    float blendDistance = 1.0f;
    float intensity = 1.0f;
    int priority = 0;
    std::uint32_t captureResolution = 128;
    bool includeSky = true;
    bool enabled = true;
    std::string bakedCubemapPath;
    AssetHandle bakedCubemapId;
    std::uint64_t captureSourceHash = 0;

    bool HasCapture() const {
        return !bakedCubemapPath.empty() || bakedCubemapId.Valid();
    }
    bool CaptureIsStale(std::uint64_t currentSceneHash) const {
        return !HasCapture() || captureSourceHash == 0
            || captureSourceHash != currentSceneHash;
    }
};

// Camera-local presentation override. It changes only the final HDR
// presentation; it never feeds values back into PBR, GI, or reflection probes.
// A stable ID makes overlapping-volume ordering deterministic across saves.
struct PostProcessVolume {
    AssetHandle stableId = AssetHandle::Generate();
    bool enabled = true;
    bool unbound = false;
    int priority = 0;
    float blendDistance = 2.0f;
    float blendWeight = 1.0f;
    glm::vec3 boxExtents{5.0f};

    bool overrideExposure = false;
    float exposureCompensationEV = 0.0f;
    bool overrideBloom = false;
    float bloomStrength = 0.6f;
    bool overrideColorGrading = false;
    float temperature = 6500.0f;
    float tint = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool overrideFogDensity = false;
    float fogDensity = 0.008f;
};

// Local participating medium injected into the existing volumetric integration.
// Density fades at the boundary; this is not a separate per-volume ray marcher.
struct LocalFogVolume {
    enum class Shape : std::uint8_t { Box = 0, Sphere = 1 };
    AssetHandle stableId = AssetHandle::Generate();
    Shape shape = Shape::Box;
    bool enabled = true;
    glm::vec3 boxExtents{3.0f};
    float radius = 3.0f;
    float blendDistance = 1.0f;
    float density = 0.02f;
    glm::vec3 albedo{0.72f, 0.80f, 0.92f};
    float extinction = 1.0f;
    float anisotropy = 0.2f;
};

struct LinearVelocity {
    glm::vec3 velocity{0.0f};
};

struct AngularVelocity {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radiansPerSecond = 0.0f;
};

// Marks an entity as drawable: which mesh to draw and in what colour. The mesh is
// referenced, not owned — it lives in the game (e.g. a shared cube) so many
// entities can point at the same geometry.
struct MeshRenderer {
    const Mesh* mesh = nullptr;
    glm::vec3   color{1.0f};
};

// A physically-based surface description (metallic / roughness workflow). These
// are the parameters a Cook-Torrance shader needs; the scene demo sets them
// directly (a sphere grid sweeping metallic x roughness), and they could equally
// be filled from a loaded model's maps.
struct PbrMaterial {
    enum class BlendMode { Opaque = 0, Masked = 1, Transparent = 2 };

    glm::vec3 albedo{0.8f, 0.8f, 0.8f}; // base colour
    float     metallic  = 0.0f;         // 0 = dielectric, 1 = metal
    float     roughness = 0.5f;         // 0 = mirror, 1 = fully rough
    float     ao        = 1.0f;         // ambient-occlusion factor
    glm::vec3 emissive{0.0f};           // self-illumination
    float opacity = 1.0f;
    float alphaCutoff = 0.5f;
    BlendMode blendMode = BlendMode::Opaque;
    glm::vec2 uvScale{1.0f};
    glm::vec2 uvOffset{0.0f};
    float uvRotation = 0.0f;            // degrees around UV centre
    // World-space (triplanar-lite) UVs: project the texture from world position so
    // texel density stays constant no matter how the object is scaled — a wall made
    // 4x wider tiles the texture 4x instead of stretching it. uvScale then means
    // "tiles per world unit". Exact on axis-aligned faces (walls/floors); seams on
    // 45-degree blends. Off = classic mesh UVs.
    bool  worldSpaceUv = false;
    float normalStrength = 1.0f;
    float heightScale = 0.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.1f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float thickness = 0.0f;
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;    // degrees in tangent space
    glm::vec3 sheenColor{0.0f};
    float sheenRoughness = 0.5f;
    float specularLevel = 0.5f;
    float subsurface = 0.0f;
    glm::vec3 subsurfaceColor{1.0f};

    // Optional, non-owning texture maps. When set they modulate the values above.
    const Texture* albedoMap     = nullptr;   // RGB base colour
    const Texture* normalMap     = nullptr;   // tangent-space normals
    const Texture* metalRoughMap = nullptr;   // glTF ORM: G = roughness, B = metallic
    const Texture* heightMap     = nullptr;   // grayscale displacement for parallax
};

// Batched foliage placement. A single entity owns many lightweight instance
// transforms instead of creating one ECS entity per tree, bush, or rock.
struct FoliageTypeRuntime {
    std::string name;
    std::string meshPath;
    AssetHandle meshId;
    std::string materialPath;
    AssetHandle materialId;
    const Model* model = nullptr;
    std::string lod1MeshPath;
    AssetHandle lod1MeshId;
    const Model* lod1Model = nullptr;
    std::string lod2MeshPath;
    AssetHandle lod2MeshId;
    const Model* lod2Model = nullptr;
    PbrMaterial material;
    float cullStartDistance = 80.0f;
    float cullEndDistance = 120.0f;
    float lod1Distance = 35.0f;
    float lod2Distance = 75.0f;
    float windStrength = 0.0f;
    bool castShadows = true;
    bool collisionEnabled = false;
};

struct FoliageInstance {
    std::uint32_t id = 0;
    std::uint32_t typeIndex = 0;
    glm::vec3 position{0.0f};       // local to the owning foliage actor
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
    bool enabled = true;
};

struct FoliageComponent {
    std::string assetPath;
    AssetHandle assetId;
    std::vector<FoliageTypeRuntime> types;
    std::vector<FoliageInstance> instances;
    bool visible = true;
    std::uint64_t revision = 1;
};

// Runtime-only marker for a simplified static collider generated from an
// opted-in foliage instance. It is never authored or serialized with a scene.
struct FoliageCollisionProxy {
    Entity foliageOwner = kNull;
    std::uint32_t instanceId = 0;
};

// Drawable entity rendered through the PBR pipeline: geometry (referenced, not
// owned) plus its material.
struct MeshPBR {
    const Mesh* mesh = nullptr;
    PbrMaterial material;
    const Shader* customShader = nullptr;
    std::unordered_map<std::string, std::string> shaderParameters;
    std::unordered_map<std::string, int> shaderParameterTypes;
    std::unordered_map<std::string, const Texture*> shaderTextures;
};

// Runtime asset references imported from editor-authored scene files. These are
// path handles, not loaded GPU resources; a runtime asset system can resolve
// them into Model/Texture objects later.
struct ModelAsset {
    std::string path;
};

struct LoadedModelAsset {
    const Model* model = nullptr;
};

// Grounded foot-placement settings authored per character. Plain data so it serializes
// with the scene and travels to packaged builds; the runtime turns it into the AnimatedModel's
// FootIK (the ground raycast callback is supplied by the host, not serialized).
struct FootIKSettings {
    bool  enabled       = false;
    float traceUp       = 0.5f;    // start the ray this far above the animated foot (m)
    float traceDown     = 0.8f;    // max search below that start (m)
    float footHeight    = 0.02f;   // hold the ankle this far above the surface (m)
    float pelvisWeight  = 1.0f;    // 0 = never drop the pelvis, 1 = full drop
    float maxPelvisDrop = 0.5f;    // clamp the pelvis drop (m)
    float weight        = 1.0f;    // overall IK blend 0..1
};

struct SkinnedModelAsset {
    struct Notify {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
        std::string clipName;
    };

    struct ActionProfile {
        std::string name;
        int clipIndex = 0;
        std::string clipName;
        std::string maskRootBone;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f;
    };

    struct AnimationState {
        struct BlendSample {
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
        std::vector<BlendSample> blendSamples;
        std::string blendParameterY;
        bool blendSpace2D = false;
        bool synchronizeBlendSpace = true;
    };

    struct AnimationParameter {
        std::string name;
        int type = 0;
        float defaultValue = 0.0f;
    };

    struct AnimationTransition {
        struct Condition {
            std::string parameter = "Speed";
            int compare = 0;
            float threshold = 0.0f;
        };
        int from = -1;
        int to = -1;
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

    std::string path;
    glm::vec3 modelOrientationEuler{0.0f};   // render-only rotation (deg); collider unaffected
    glm::vec3 modelOffsetPosition{0.0f};     // render-only mesh position offset; collider unaffected
    glm::vec3 modelOffsetScale{1.0f};        // render-only mesh scale (about model centre)
    int clipIndex = 0;
    std::string clipName;
    bool autoplay = true;
    bool loop = true;
    float speed = 1.0f;
    bool locomotionEnabled = false;
    int idleClipIndex = 0;
    int walkClipIndex = 0;
    int runClipIndex = 0;
    std::string idleClipName;
    std::string walkClipName;
    std::string runClipName;
    float walkAt = 0.15f;
    float runAt = 3.0f;
    // A separate animation file merged onto the model by bone name (idle/walk/run
    // exported as their own FBX). Without these the runtime model has no clips and the
    // character falls back to the bind (T-)pose in Play.
    struct AnimationSourceFile {
        std::string path;
        std::string clipName;
        bool        stripRootMotion = false;
        std::string sourceClipName;
        float       basePlaybackSpeed = 1.0f;
    };

    // A static model socketed to a bone (weapon/shield). Resolved to an AnimatedModel
    // attachment (model + bone index) at load time.
    struct Attachment {
        std::string path;
        std::string boneName;
        glm::vec3   position{0.0f};
        glm::vec3   eulerDegrees{0.0f};
        glm::vec3   scale{1.0f};
        std::string materialPath;   // optional .3dgmat applied to the attachment model
        std::string socketName;     // named gameplay point (path may be empty)
    };

    std::vector<Notify> notifies;
    std::vector<ActionProfile> actionProfiles;
    std::vector<AnimationState> states;
    std::vector<AnimationParameter> parameters;
    std::vector<AnimationTransition> transitions;
    std::vector<AnimationSourceFile> animationSources;
    std::vector<Attachment> attachments;
    // Grounded foot placement (opt-in). Appended last so the runtime-loader aggregate init
    // stays valid (this trailing member value-initialises to "disabled" when omitted).
    FootIKSettings footIK;
};

struct MaterialAsset {
    std::string path;
    std::string albedoPath;
    std::unordered_map<std::string, std::string> parameterOverrides;
};

// Authored sound attached to a runtime entity. Playback state is deliberately
// kept outside the component so scenes remain serializable and registry clones
// receive an independent voice when a RuntimeAudioSystem observes them.
struct AudioSource {
    std::string path;
    AssetHandle assetId;
    AudioBus bus = AudioBus::SFX;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool spatial = true;
    bool loop = false;
    bool autoplay = true;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    float rolloff = 1.0f;
    float dopplerFactor = 1.0f;
    float coneInnerAngle = 360.0f;
    float coneOuterAngle = 360.0f;
    float coneOuterGain = 1.0f;
    float occlusion = 0.0f;
    int priority = 50;
};

enum class AudioAction {
    None = 0,
    Play = 1,
    Restart = 2,
    Pause = 3,
    Resume = 4,
    Stop = 5
};

// No-code collision event binding. The target is resolved through RuntimeName,
// keeping the authored reference stable across scene serialization.
struct TriggerAudioAction {
    std::string targetName;
    AudioAction onEnter = AudioAction::None;
    AudioAction onExit = AudioAction::None;
};

struct LoadedMaterialAsset {
    PbrMaterial material;
    const Texture* albedoMap = nullptr;
    const Texture* normalMap = nullptr;
    const Texture* metalRoughMap = nullptr;
    const Texture* heightMap = nullptr;
    const Shader* shader = nullptr;
    const Shader* skinnedShader = nullptr;
    std::unordered_map<std::string, std::string> shaderParameters;
    std::unordered_map<std::string, int> shaderParameterTypes;
    std::unordered_map<std::string, const Texture*> shaderTextures;
};

// A light source. Point lights take their position from the entity's Transform;
// directional lights use `direction`. `intensity` scales `color`.
struct Light {
    enum class Type { Directional, Point, Spot, Area };
    enum class AreaShape { Sphere = 0, Rectangle = 1 };
    Type      type     = Type::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    glm::vec3 direction{0.0f, -1.0f, 0.0f} ;   // Directional + Spot
    float     innerAngle = 20.0f;             // Spot: degrees, full intensity inside
    float     outerAngle = 30.0f;             // Spot: degrees, fades to zero at the edge
    float     range      = 40.0f;             // Spot: shadow far plane
    float     sourceRadius = 1.0f;            // Area: physical sphere radius
    AreaShape areaShape = AreaShape::Sphere;
    float areaWidth = 1.0f;
    float areaHeight = 1.0f;
    bool areaTwoSided = false;
    bool affectDynamicGi = true;
    bool affectVolumetricFog = true;
    int volumetricPriority = 0;
};

// Small native gameplay component used by the editor/runtime path. Rotates an
// entity around an axis every gameplay tick.
struct Rotator {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radiansPerSecond = 1.0f;
};

// Small native gameplay component used by the editor/runtime path. Moves an
// entity back and forth around its starting position every gameplay tick.
struct Mover {
    glm::vec3 axis{1.0f, 0.0f, 0.0f};
    float distance = 1.0f;
    float speed = 1.0f;
    float phase = 0.0f;
    glm::vec3 origin{0.0f};
    bool initialized = false;
};

} // namespace ecs
} // namespace engine
