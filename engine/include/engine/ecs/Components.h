#pragma once

#include "engine/audio/AudioTypes.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/ParticleSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <unordered_map>

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

struct SkinnedModelAsset {
    struct Notify {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
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
    };

    struct AnimationParameter {
        std::string name;
        int type = 0;
        float defaultValue = 0.0f;
    };

    struct AnimationTransition {
        int from = -1;
        int to = -1;
        std::string parameter = "Speed";
        int compare = 0;
        float threshold = 0.0f;
        float fade = 0.2f;
        float exitTime = 0.0f;
        int priority = 0;
        bool canInterrupt = false;
    };

    std::string path;
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
    std::vector<Notify> notifies;
    std::vector<ActionProfile> actionProfiles;
    std::vector<AnimationState> states;
    std::vector<AnimationParameter> parameters;
    std::vector<AnimationTransition> transitions;
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
    Type      type     = Type::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    glm::vec3 direction{0.0f, -1.0f, 0.0f} ;   // Directional + Spot
    float     innerAngle = 20.0f;             // Spot: degrees, full intensity inside
    float     outerAngle = 30.0f;             // Spot: degrees, fades to zero at the edge
    float     range      = 40.0f;             // Spot: shadow far plane
    float     sourceRadius = 1.0f;            // Area: physical sphere radius
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
