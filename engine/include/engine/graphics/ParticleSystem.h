#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// One live particle. Colour and size are interpolated from start->end over its
// life; start/end are cached so per-frame updates are a single lerp.
struct Particle {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec4 color{1.0f};
    float     size = 1.0f;
    float     rotation = 0.0f;          // radians
    float     angularVelocity = 0.0f;   // radians/second
    float     frame = 0.0f;             // texture-sheet frame
    float     age  = 0.0f;
    float     life = 1.0f;
    glm::vec4 startColor{1.0f}, endColor{1.0f};
    float     startSize = 1.0f, endSize = 0.0f;
    std::array<glm::vec3, 16> trailPositions{};
    int trailCount = 0;
    float trailDistanceAccumulator = 0.0f;
};

enum class EmitShape { Point, Sphere, Cone };
enum class ParticleBlend { Additive, Alpha };
enum class ParticleAction { None, Play, Restart, Stop, Burst, Clear };
enum class ParticleCollisionResponse { Bounce, Kill };
enum class ParticleRenderMode { Billboard, Mesh };
enum class ParticleMeshShape { Cube, Sphere, Cone, Cylinder, Model };
enum class ParticleSimulationBackend { Auto, CPU, GPU };

// Authoring modules are deliberately mapped onto the existing emitter pipeline.
// Their order is preserved for editor presentation and future graph compilation;
// the runtime still executes the fixed Spawn -> Update -> Render stages.
enum class ParticleModuleType {
    Spawn, Shape, InitialVelocity, Forces, Rotation,
    ColorOverLife, SizeOverLife, Collision, Trails, Renderer
};
enum class ParticleModuleStage { Spawn, Update, Render };
enum class ParticleModuleDataChannel { SpawnData, ParticleState, RenderData };

struct ParticleModuleConnection {
    std::uint32_t fromInstanceId = 0;
    std::uint32_t toInstanceId = 0;
    ParticleModuleDataChannel channel = ParticleModuleDataChannel::ParticleState;
};

struct ParticleModule {
    ParticleModuleType type = ParticleModuleType::Spawn;
    bool enabled = true;
    ParticleModuleStage stage = ParticleModuleStage::Spawn;
    std::uint32_t instanceId = 0;
    std::string name;
    // First per-instance parameter set. Force instances use vectorValue as
    // gravity and valueA as drag; other module types continue using EmitterConfig.
    bool parametersInitialized = false;
    glm::vec3 vectorValue{0.0f};
    float valueA = 0.0f;
    float valueB = 0.0f;
    float valueC = 0.0f;
    float valueD = 0.0f;
    glm::vec4 colorValueA{1.0f};
    glm::vec4 colorValueB{1.0f};
    std::array<float, 4> curveValues{{1.0f, 1.0f, 1.0f, 1.0f}};
    bool curveEnabled = false;
};

class GpuParticleEmitter;
class Shader;
class Texture;

struct ParticleShaderParameter {
    std::string name;
    int type = 0;
    std::string value;
};

struct ParticleCollisionShape {
    enum class Type { Plane, Sphere, Box };
    Type type = Type::Plane;
    glm::vec3 center{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.5f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float offset = 0.0f;
    float radius = 0.5f;
};

struct TriggerParticleAction {
    std::string targetName;
    ParticleAction onEnter = ParticleAction::None;
    ParticleAction onExit = ParticleAction::None;
};

// How an emitter spawns and evolves particles. Sensible defaults make a warm
// upward "spark" fountain; tweak for fire / smoke / magic / etc.
struct EmitterConfig {
    // Optional graph-authored Particle-domain shader. Runtime pointers are
    // transient and resolved from shaderPath/parameters by the host asset manager.
    std::string shaderPath;
    std::vector<ParticleShaderParameter> shaderParameters;
    const Shader* customShader = nullptr;
    std::unordered_map<std::string, const Texture*> shaderTextures;

    float     rate = 60.0f;                     // continuous spawn (particles/sec); 0 = burst-only
    int       maxParticles = 2000;

    EmitShape shape = EmitShape::Cone;
    float     shapeRadius = 0.1f;               // spawn jitter (Point/Sphere) or cone base radius
    glm::vec3 direction{0.0f, 1.0f, 0.0f};      // cone axis (normalized on use)
    float     coneAngleDeg = 20.0f;

    float     speedMin = 1.5f, speedMax = 3.0f;
    float     lifeMin  = 0.7f, lifeMax   = 1.3f;

    glm::vec3 gravity{0.0f, -2.0f, 0.0f};
    float     drag = 0.0f;                      // velocity damping (per second)

    glm::vec4 startColor{2.0f, 1.2f, 0.4f, 1.0f};   // HDR (>1 to bloom)
    glm::vec4 endColor  {1.5f, 0.1f, 0.0f, 0.0f};    // fades to transparent
    float     startSize = 0.30f, endSize = 0.02f;

    float     rotationMinDeg = 0.0f, rotationMaxDeg = 0.0f;
    float     angularVelocityMinDeg = 0.0f, angularVelocityMaxDeg = 0.0f;

    // Optional four-key easing curves. Values are interpolation weights between
    // the existing start/end values, preserving legacy effects when disabled.
    bool useSizeCurve = false;
    bool useColorCurve = false;
    std::array<float, 4> sizeCurve{{0.0f, 0.333333f, 0.666667f, 1.0f}};
    std::array<float, 4> colorCurve{{0.0f, 0.333333f, 0.666667f, 1.0f}};

    std::string texturePath;
    int textureColumns = 1;
    int textureRows = 1;
    float textureFps = 0.0f;
    bool textureLoop = true;
    bool cullingEnabled = true;
    float boundsRadius = 5.0f;

    bool collisionEnabled = false;
    ParticleCollisionResponse collisionResponse = ParticleCollisionResponse::Bounce;
    float collisionRadius = 0.02f;
    float collisionBounce = 0.45f;
    float collisionFriction = 0.15f;
    float collisionLifetimeLoss = 0.0f;

    bool trailsEnabled = false;
    int trailSegments = 8;
    float trailLength = 1.5f;
    float trailWidth = 0.12f;
    float trailOpacity = 0.7f;

    ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
    ParticleMeshShape meshShape = ParticleMeshShape::Cube;
    std::string meshPath;
    float meshScale = 1.0f;
    bool meshAlignToVelocity = true;

    // Auto uses GPU compute when the context and selected features support it,
    // otherwise it transparently retains the deterministic CPU emitter.
    ParticleSimulationBackend simulationBackend = ParticleSimulationBackend::Auto;

    ParticleBlend blend = ParticleBlend::Additive;

    // Stable authoring order. Required pipeline modules cannot be disabled in
    // the editor; optional module state is compiled into the legacy booleans.
    std::vector<ParticleModule> modules{
        {ParticleModuleType::Spawn, true},
        {ParticleModuleType::Shape, true},
        {ParticleModuleType::InitialVelocity, true},
        {ParticleModuleType::Forces, true},
        {ParticleModuleType::Rotation, true},
        {ParticleModuleType::ColorOverLife, false},
        {ParticleModuleType::SizeOverLife, false},
        {ParticleModuleType::Collision, false},
        {ParticleModuleType::Trails, false},
        {ParticleModuleType::Renderer, true}
    };
};

inline float FiniteParticleValue(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

// Shared safety boundary for editor assets, runtime scenes, scripts, and direct
// C++ construction. It intentionally preserves artistic HDR colour values.
inline void SanitizeParticleConfig(EmitterConfig& config) {
    config.rate = std::clamp(FiniteParticleValue(config.rate, 0.0f), 0.0f, 1000000.0f);
    config.maxParticles = std::clamp(config.maxParticles, 1, 1000000);
    config.shapeRadius = std::clamp(FiniteParticleValue(config.shapeRadius, 0.0f), 0.0f, 1000000.0f);
    for (int i = 0; i < 3; ++i)
        config.direction[i] = FiniteParticleValue(config.direction[i], i == 1 ? 1.0f : 0.0f);
    config.coneAngleDeg = std::clamp(FiniteParticleValue(config.coneAngleDeg, 0.0f), 0.0f, 180.0f);
    config.speedMin = FiniteParticleValue(config.speedMin, 0.0f);
    config.speedMax = FiniteParticleValue(config.speedMax, config.speedMin);
    if (config.speedMin > config.speedMax) std::swap(config.speedMin, config.speedMax);
    config.lifeMin = std::max(FiniteParticleValue(config.lifeMin, 1.0f), 0.001f);
    config.lifeMax = std::max(FiniteParticleValue(config.lifeMax, config.lifeMin), 0.001f);
    if (config.lifeMin > config.lifeMax) std::swap(config.lifeMin, config.lifeMax);
    for (int i = 0; i < 3; ++i)
        config.gravity[i] = FiniteParticleValue(config.gravity[i], 0.0f);
    config.drag = std::clamp(FiniteParticleValue(config.drag, 0.0f), 0.0f, 100000.0f);
    for (int i = 0; i < 4; ++i) {
        config.startColor[i] = FiniteParticleValue(config.startColor[i], 1.0f);
        config.endColor[i] = FiniteParticleValue(config.endColor[i], i == 3 ? 0.0f : 1.0f);
        config.sizeCurve[static_cast<std::size_t>(i)] =
            FiniteParticleValue(config.sizeCurve[static_cast<std::size_t>(i)],
                                static_cast<float>(i) / 3.0f);
        config.colorCurve[static_cast<std::size_t>(i)] =
            FiniteParticleValue(config.colorCurve[static_cast<std::size_t>(i)],
                                static_cast<float>(i) / 3.0f);
    }
    config.startSize = std::max(FiniteParticleValue(config.startSize, 0.1f), 0.0f);
    config.endSize = std::max(FiniteParticleValue(config.endSize, 0.0f), 0.0f);
    config.rotationMinDeg = FiniteParticleValue(config.rotationMinDeg, 0.0f);
    config.rotationMaxDeg = FiniteParticleValue(config.rotationMaxDeg, config.rotationMinDeg);
    if (config.rotationMinDeg > config.rotationMaxDeg)
        std::swap(config.rotationMinDeg, config.rotationMaxDeg);
    config.angularVelocityMinDeg = FiniteParticleValue(config.angularVelocityMinDeg, 0.0f);
    config.angularVelocityMaxDeg =
        FiniteParticleValue(config.angularVelocityMaxDeg, config.angularVelocityMinDeg);
    if (config.angularVelocityMinDeg > config.angularVelocityMaxDeg)
        std::swap(config.angularVelocityMinDeg, config.angularVelocityMaxDeg);
    config.textureColumns = std::clamp(config.textureColumns, 1, 256);
    config.textureRows = std::clamp(config.textureRows, 1, 256);
    config.textureFps = std::clamp(FiniteParticleValue(config.textureFps, 0.0f), 0.0f, 10000.0f);
    config.boundsRadius = std::clamp(FiniteParticleValue(config.boundsRadius, 5.0f),
                                     0.01f, 1000000.0f);
    config.collisionRadius = std::max(FiniteParticleValue(config.collisionRadius, 0.0f), 0.0f);
    config.collisionBounce = std::clamp(FiniteParticleValue(config.collisionBounce, 0.45f),
                                        0.0f, 10.0f);
    config.collisionFriction = std::clamp(FiniteParticleValue(config.collisionFriction, 0.15f),
                                          0.0f, 1.0f);
    config.collisionLifetimeLoss =
        std::clamp(FiniteParticleValue(config.collisionLifetimeLoss, 0.0f), 0.0f, 1.0f);
    config.trailSegments = std::clamp(config.trailSegments, 2, 16);
    config.trailLength = std::max(FiniteParticleValue(config.trailLength, 1.5f), 0.001f);
    config.trailWidth = std::max(FiniteParticleValue(config.trailWidth, 0.12f), 0.0f);
    config.trailOpacity = std::clamp(FiniteParticleValue(config.trailOpacity, 0.7f), 0.0f, 1.0f);
    config.meshScale = std::max(FiniteParticleValue(config.meshScale, 1.0f), 0.001f);
}

inline const char* ParticleModuleName(ParticleModuleType type) {
    switch (type) {
    case ParticleModuleType::Spawn: return "Spawn";
    case ParticleModuleType::Shape: return "Shape";
    case ParticleModuleType::InitialVelocity: return "Initial Velocity";
    case ParticleModuleType::Forces: return "Forces";
    case ParticleModuleType::Rotation: return "Rotation";
    case ParticleModuleType::ColorOverLife: return "Color Over Life";
    case ParticleModuleType::SizeOverLife: return "Size Over Life";
    case ParticleModuleType::Collision: return "Collision";
    case ParticleModuleType::Trails: return "Trails / Ribbons";
    case ParticleModuleType::Renderer: return "Renderer";
    }
    return "Module";
}

inline const char* ParticleModuleStageName(ParticleModuleStage stage) {
    switch (stage) {
    case ParticleModuleStage::Spawn: return "Spawn Stage";
    case ParticleModuleStage::Update: return "Update Stage";
    case ParticleModuleStage::Render: return "Render Stage";
    }
    return "Stage";
}

inline const char* ParticleModuleDataChannelName(ParticleModuleDataChannel channel) {
    switch (channel) {
    case ParticleModuleDataChannel::SpawnData: return "Spawn Data";
    case ParticleModuleDataChannel::ParticleState: return "Particle State";
    case ParticleModuleDataChannel::RenderData: return "Render Data";
    }
    return "Data";
}

inline ParticleModuleStage ParticleModuleDefaultStage(ParticleModuleType type) {
    switch (type) {
    case ParticleModuleType::Spawn:
    case ParticleModuleType::Shape:
    case ParticleModuleType::InitialVelocity:
        return ParticleModuleStage::Spawn;
    case ParticleModuleType::Forces:
    case ParticleModuleType::Rotation:
    case ParticleModuleType::ColorOverLife:
    case ParticleModuleType::SizeOverLife:
    case ParticleModuleType::Collision:
        return ParticleModuleStage::Update;
    case ParticleModuleType::Trails:
    case ParticleModuleType::Renderer:
        return ParticleModuleStage::Render;
    }
    return ParticleModuleStage::Update;
}

inline bool IsOptionalParticleModule(ParticleModuleType type) {
    return type == ParticleModuleType::Collision
        || type == ParticleModuleType::Trails;
}

inline bool SupportsDuplicateParticleModules(ParticleModuleType type) {
    return type == ParticleModuleType::Forces
        || type == ParticleModuleType::InitialVelocity
        || type == ParticleModuleType::Rotation
        || type == ParticleModuleType::ColorOverLife
        || type == ParticleModuleType::SizeOverLife;
}

inline bool IsParticleModuleEnabled(const EmitterConfig& config, ParticleModuleType type) {
    if (type == ParticleModuleType::ColorOverLife) return config.useColorCurve;
    if (type == ParticleModuleType::SizeOverLife) return config.useSizeCurve;
    if (type == ParticleModuleType::Collision) return config.collisionEnabled;
    if (type == ParticleModuleType::Trails) return config.trailsEnabled;
    return true;
}

inline void CompileParticleModuleStack(EmitterConfig& config) {
    config.useColorCurve = false;
    config.useSizeCurve = false;
    config.collisionEnabled = false;
    config.trailsEnabled = false;
    bool hasForceParameters = false;
    glm::vec3 combinedGravity{0.0f};
    float combinedDrag = 0.0f;
    bool hasVelocityParameters = false;
    float combinedSpeedMin = 0.0f, combinedSpeedMax = 0.0f;
    bool hasRotationParameters = false;
    float combinedRotationMin = 0.0f, combinedRotationMax = 0.0f;
    float combinedAngularMin = 0.0f, combinedAngularMax = 0.0f;
    bool hasColorParameters = false, hasSizeParameters = false;
    glm::vec4 combinedStartColor{1.0f}, combinedEndColor{1.0f};
    float combinedStartSize = 1.0f, combinedEndSize = 1.0f;
    std::array<float, 4> combinedColorCurve{{1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<float, 4> combinedSizeCurve{{1.0f, 1.0f, 1.0f, 1.0f}};
    for (const ParticleModule& module : config.modules) {
        if (module.type == ParticleModuleType::Forces && module.enabled
            && module.parametersInitialized) {
            hasForceParameters = true;
            combinedGravity += module.vectorValue;
            combinedDrag += std::max(module.valueA, 0.0f);
        }
        if (module.type == ParticleModuleType::InitialVelocity && module.enabled
            && module.parametersInitialized) {
            hasVelocityParameters = true;
            combinedSpeedMin += module.valueA;
            combinedSpeedMax += module.valueB;
        }
        if (module.type == ParticleModuleType::Rotation && module.enabled
            && module.parametersInitialized) {
            hasRotationParameters = true;
            combinedRotationMin += module.valueA;
            combinedRotationMax += module.valueB;
            combinedAngularMin += module.valueC;
            combinedAngularMax += module.valueD;
        }
        if (module.type == ParticleModuleType::ColorOverLife && module.enabled
            && module.parametersInitialized) {
            hasColorParameters = true;
            combinedStartColor *= module.colorValueA;
            combinedEndColor *= module.colorValueB;
            if (module.curveEnabled) {
                config.useColorCurve = true;
                for (std::size_t i = 0; i < combinedColorCurve.size(); ++i)
                    combinedColorCurve[i] *= module.curveValues[i];
            }
        }
        else if (module.type == ParticleModuleType::SizeOverLife && module.enabled
                 && module.parametersInitialized) {
            hasSizeParameters = true;
            combinedStartSize *= module.valueA;
            combinedEndSize *= module.valueB;
            if (module.curveEnabled) {
                config.useSizeCurve = true;
                for (std::size_t i = 0; i < combinedSizeCurve.size(); ++i)
                    combinedSizeCurve[i] *= module.curveValues[i];
            }
        }
        else if (module.type == ParticleModuleType::Collision) config.collisionEnabled = module.enabled;
        else if (module.type == ParticleModuleType::Trails) config.trailsEnabled = module.enabled;
    }
    if (hasForceParameters) {
        config.gravity = combinedGravity;
        config.drag = combinedDrag;
    }
    if (hasVelocityParameters) {
        config.speedMin = std::min(combinedSpeedMin, combinedSpeedMax);
        config.speedMax = std::max(combinedSpeedMin, combinedSpeedMax);
    }
    if (hasRotationParameters) {
        config.rotationMinDeg = std::min(combinedRotationMin, combinedRotationMax);
        config.rotationMaxDeg = std::max(combinedRotationMin, combinedRotationMax);
        config.angularVelocityMinDeg = std::min(combinedAngularMin, combinedAngularMax);
        config.angularVelocityMaxDeg = std::max(combinedAngularMin, combinedAngularMax);
    }
    if (hasColorParameters) {
        config.startColor = combinedStartColor;
        config.endColor = combinedEndColor;
        if (config.useColorCurve) config.colorCurve = combinedColorCurve;
    }
    if (hasSizeParameters) {
        config.startSize = combinedStartSize;
        config.endSize = combinedEndSize;
        if (config.useSizeCurve) config.sizeCurve = combinedSizeCurve;
    }
}

inline void SyncParticleModuleStack(EmitterConfig& config) {
    bool firstForce = true;
    bool firstVelocity = true;
    bool firstRotation = true;
    bool firstColor = true;
    bool firstSize = true;
    for (ParticleModule& module : config.modules) {
        const bool firstDuplicateCapable =
            (module.type == ParticleModuleType::Forces && firstForce)
            || (module.type == ParticleModuleType::InitialVelocity && firstVelocity)
            || (module.type == ParticleModuleType::Rotation && firstRotation)
            || (module.type == ParticleModuleType::ColorOverLife && firstColor)
            || (module.type == ParticleModuleType::SizeOverLife && firstSize);
        if (module.type == ParticleModuleType::Collision) module.enabled = config.collisionEnabled;
        else if (module.type == ParticleModuleType::Trails) module.enabled = config.trailsEnabled;
        else if (!SupportsDuplicateParticleModules(module.type) || firstDuplicateCapable)
            module.enabled = true;
        if (module.type == ParticleModuleType::Forces && !module.parametersInitialized) {
            module.vectorValue = firstForce ? config.gravity : glm::vec3(0.0f);
            module.valueA = firstForce ? config.drag : 0.0f;
            module.parametersInitialized = true;
        }
        if (module.type == ParticleModuleType::InitialVelocity && !module.parametersInitialized) {
            module.valueA = firstVelocity ? config.speedMin : 0.0f;
            module.valueB = firstVelocity ? config.speedMax : 0.0f;
            module.parametersInitialized = true;
        }
        if (module.type == ParticleModuleType::Rotation && !module.parametersInitialized) {
            module.valueA = firstRotation ? config.rotationMinDeg : 0.0f;
            module.valueB = firstRotation ? config.rotationMaxDeg : 0.0f;
            module.valueC = firstRotation ? config.angularVelocityMinDeg : 0.0f;
            module.valueD = firstRotation ? config.angularVelocityMaxDeg : 0.0f;
            module.parametersInitialized = true;
        }
        if (module.type == ParticleModuleType::ColorOverLife && !module.parametersInitialized) {
            module.colorValueA = firstColor ? config.startColor : glm::vec4(1.0f);
            module.colorValueB = firstColor ? config.endColor : glm::vec4(1.0f);
            module.curveValues = firstColor ? config.colorCurve
                                            : std::array<float, 4>{{1.0f, 1.0f, 1.0f, 1.0f}};
            module.curveEnabled = firstColor && config.useColorCurve;
            module.parametersInitialized = true;
        }
        if (module.type == ParticleModuleType::SizeOverLife && !module.parametersInitialized) {
            module.valueA = firstSize ? config.startSize : 1.0f;
            module.valueB = firstSize ? config.endSize : 1.0f;
            module.curveValues = firstSize ? config.sizeCurve
                                           : std::array<float, 4>{{1.0f, 1.0f, 1.0f, 1.0f}};
            module.curveEnabled = firstSize && config.useSizeCurve;
            module.parametersInitialized = true;
        }
        if (module.type == ParticleModuleType::Forces) firstForce = false;
        if (module.type == ParticleModuleType::InitialVelocity) firstVelocity = false;
        if (module.type == ParticleModuleType::Rotation) firstRotation = false;
        if (module.type == ParticleModuleType::ColorOverLife) firstColor = false;
        if (module.type == ParticleModuleType::SizeOverLife) firstSize = false;
    }
}

inline void NormalizeParticleModuleStack(EmitterConfig& config, bool compileOptionalState = false) {
    constexpr int moduleCount = static_cast<int>(ParticleModuleType::Renderer) + 1;
    std::array<bool, moduleCount> seen{};
    std::vector<std::uint32_t> usedIds;
    std::uint32_t nextId = 1;
    std::vector<ParticleModule> normalized;
    normalized.reserve(moduleCount);
    for (ParticleModule module : config.modules) {
        const int index = static_cast<int>(module.type);
        if (index < 0 || index >= moduleCount
            || (seen[index] && !SupportsDuplicateParticleModules(module.type))) continue;
        const bool duplicateInstance = SupportsDuplicateParticleModules(module.type) && seen[index];
        seen[index] = true;
        if (module.instanceId == 0
            || std::find(usedIds.begin(), usedIds.end(), module.instanceId) != usedIds.end())
            module.instanceId = nextId;
        usedIds.push_back(module.instanceId);
        nextId = std::max(nextId, module.instanceId + 1);
        if (module.name.empty()) module.name = ParticleModuleName(module.type);
        module.stage = ParticleModuleDefaultStage(module.type);
        if (!IsOptionalParticleModule(module.type) && !duplicateInstance) module.enabled = true;
        normalized.push_back(module);
    }
    for (int index = 0; index < moduleCount; ++index) {
        if (seen[index]) continue;
        const auto type = static_cast<ParticleModuleType>(index);
        if (!IsOptionalParticleModule(type)) {
            ParticleModule module{type, true};
            module.instanceId = nextId++;
            module.name = ParticleModuleName(type);
            module.stage = ParticleModuleDefaultStage(type);
            normalized.push_back(std::move(module));
        }
    }
    std::stable_sort(normalized.begin(), normalized.end(),
        [](const ParticleModule& a, const ParticleModule& b) {
            return static_cast<int>(a.stage) < static_cast<int>(b.stage);
        });
    config.modules = std::move(normalized);
    if (compileOptionalState) CompileParticleModuleStack(config);
    else SyncParticleModuleStack(config);
}

inline std::vector<ParticleModuleConnection> BuildParticleModuleConnections(
    const EmitterConfig& config) {
    std::vector<ParticleModuleConnection> connections;
    const ParticleModule* previous = nullptr;
    for (const ParticleModule& module : config.modules) {
        if (!module.enabled) continue;
        if (previous) {
            ParticleModuleDataChannel channel = ParticleModuleDataChannel::ParticleState;
            if (module.stage == ParticleModuleStage::Spawn)
                channel = ParticleModuleDataChannel::SpawnData;
            else if (module.stage == ParticleModuleStage::Render)
                channel = ParticleModuleDataChannel::RenderData;
            connections.push_back({previous->instanceId, module.instanceId, channel});
        }
        previous = &module;
    }
    return connections;
}

inline std::vector<std::string> ValidateParticleModulePipeline(const EmitterConfig& config) {
    std::vector<std::string> errors;
    std::array<bool, static_cast<int>(ParticleModuleType::Renderer) + 1> required{};
    ParticleModuleStage previousStage = ParticleModuleStage::Spawn;
    bool first = true;
    std::vector<std::uint32_t> ids;
    for (const ParticleModule& module : config.modules) {
        if (module.instanceId == 0
            || std::find(ids.begin(), ids.end(), module.instanceId) != ids.end())
            errors.emplace_back("Module instance IDs must be non-zero and unique.");
        ids.push_back(module.instanceId);
        if (!first && static_cast<int>(module.stage) < static_cast<int>(previousStage))
            errors.emplace_back("Module stages are not in Spawn, Update, Render order.");
        first = false;
        previousStage = module.stage;
        const int type = static_cast<int>(module.type);
        if (type >= 0 && type < static_cast<int>(required.size()) && module.enabled)
            required[type] = true;
    }
    constexpr ParticleModuleType mandatory[] = {
        ParticleModuleType::Spawn, ParticleModuleType::Shape,
        ParticleModuleType::InitialVelocity, ParticleModuleType::Renderer
    };
    for (const ParticleModuleType type : mandatory) {
        if (!required[static_cast<int>(type)])
            errors.emplace_back(std::string(ParticleModuleName(type)) + " module is required.");
    }
    return errors;
}

// A CPU particle emitter: owns a pool, spawns (continuous rate and/or bursts),
// integrates and ages them each Update. Rendering is separate (ParticleRenderer),
// so this is pure logic and unit-testable headless.
class ParticleEmitter {
public:
    EmitterConfig cfg;
    glm::vec3     position{0.0f};       // world spawn origin
    bool          emitting = true;      // continuous emission on/off (bursts always work)

    void Burst(int count);              // spawn `count` particles immediately
    void Update(float dt);              // spawn (rate) + integrate + age + remove dead
    void Update(float dt, const std::vector<ParticleCollisionShape>& collisionShapes);
    void Clear();
    void Translate(const glm::vec3& delta);
    const std::vector<Particle>& Particles() const { return m_particles; }
    std::size_t Alive() const { return m_particles.size(); }
    int LastCollisionCount() const { return m_lastCollisionCount; }

private:
    std::vector<Particle> m_particles;
    float       m_accum = 0.0f;
    std::uint32_t m_rng = 0x9E3779B9u;
    int m_lastCollisionCount = 0;

    void SpawnOne();
    float Rand01();                     // xorshift32 in [0,1)
    glm::vec3 SampleDirection();        // within the cone (or the config direction)
    glm::vec3 SampleOffset();           // spawn position jitter by shape
};

// Serializable playback wrapper used by runtime ECS scenes. Transient fields
// are reset when a scene is instantiated and are not written to disk.
struct ParticleSystemComponent {
    EmitterConfig config;
    bool enabled = true;
    bool autoplay = true;
    bool loop = true;
    bool prewarm = false;
    float duration = 5.0f;
    float startDelay = 0.0f;
    float simulationSpeed = 1.0f;
    bool localSpace = true;
    int burstCount = 0;
    float burstInterval = 0.0f;

    ParticleEmitter emitter;
    bool initialized = false;
    bool playing = false;
    bool initialBurstFired = false;
    float elapsed = 0.0f;
    float delayElapsed = 0.0f;
    float burstElapsed = 0.0f;
    glm::vec3 lastPosition{0.0f};

    // Transient GPU state. It is never serialized and is recreated after load.
    std::shared_ptr<GpuParticleEmitter> gpuEmitter;
    float gpuSpawnAccumulator = 0.0f;
    int gpuPendingBurst = 0;
    bool gpuBackendActive = false;
};

struct ParticleEffectLayer {
    std::string name{"Layer"};
    std::string assetPath;
    glm::vec3 offset{0.0f};
    bool enabled = true;
    ParticleSystemComponent system;
};

struct ParticleEffectComponent {
    bool enabled = true;
    std::vector<ParticleEffectLayer> layers;
};

} // namespace engine
