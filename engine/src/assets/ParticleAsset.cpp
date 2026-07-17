#include "engine/assets/ParticleAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine {

bool LoadParticleAsset(const std::string& path, ParticleSystemComponent* output, std::string* error) {
    if (!output) { if (error) *error = "Particle asset output is null."; return false; }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DGParticle" || version < 1 || version > 12) {
        if (error) *error = "Unsupported or malformed particle asset: " + path;
        return false;
    }
    ParticleSystemComponent s;
    EmitterConfig& p = s.config;
    int enabled = 1, autoplay = 1, loop = 1, prewarm = 0, localSpace = 1;
    int shape = 0, blend = 0, useSizeCurve = 0, useColorCurve = 0, textureLoop = 1;
    in >> enabled >> autoplay >> loop >> prewarm >> s.duration >> s.startDelay
       >> s.simulationSpeed >> localSpace >> s.burstCount >> s.burstInterval
       >> p.rate >> p.maxParticles >> shape >> p.shapeRadius
       >> p.direction.x >> p.direction.y >> p.direction.z >> p.coneAngleDeg
       >> p.speedMin >> p.speedMax >> p.lifeMin >> p.lifeMax
       >> p.gravity.x >> p.gravity.y >> p.gravity.z >> p.drag
       >> p.startColor.r >> p.startColor.g >> p.startColor.b >> p.startColor.a
       >> p.endColor.r >> p.endColor.g >> p.endColor.b >> p.endColor.a
       >> p.startSize >> p.endSize >> blend
       >> p.rotationMinDeg >> p.rotationMaxDeg
       >> p.angularVelocityMinDeg >> p.angularVelocityMaxDeg
       >> useSizeCurve >> useColorCurve;
    for (float& key : p.sizeCurve) in >> key;
    for (float& key : p.colorCurve) in >> key;
    in >> std::quoted(p.texturePath) >> p.textureColumns >> p.textureRows >> p.textureFps >> textureLoop;
    if (version >= 2) { int value = 1; in >> value >> p.boundsRadius; p.cullingEnabled = value != 0; }
    if (version >= 3) {
        int collision = 0, response = 0;
        in >> collision >> response >> p.collisionRadius >> p.collisionBounce
           >> p.collisionFriction >> p.collisionLifetimeLoss;
        p.collisionEnabled = collision != 0;
        p.collisionResponse = static_cast<ParticleCollisionResponse>(std::clamp(response, 0, 1));
    }
    if (version >= 4) {
        int trails = 0;
        in >> trails >> p.trailSegments >> p.trailLength >> p.trailWidth >> p.trailOpacity;
        p.trailsEnabled = trails != 0;
    }
    if (version >= 5) {
        int renderMode = 0, meshShape = 0, align = 1;
        in >> renderMode >> meshShape >> std::quoted(p.meshPath) >> p.meshScale >> align;
        p.renderMode = static_cast<ParticleRenderMode>(std::clamp(renderMode, 0, 1));
        p.meshShape = static_cast<ParticleMeshShape>(std::clamp(meshShape, 0, 4));
        p.meshAlignToVelocity = align != 0;
        if (p.meshPath == "-") p.meshPath.clear();
        p.meshScale = std::max(p.meshScale, 0.001f);
    }
    if (version >= 6) {
        int backend = 0;
        in >> backend;
        p.simulationBackend = static_cast<ParticleSimulationBackend>(std::clamp(backend, 0, 2));
    }
    if (version >= 7) {
        std::size_t moduleCount = 0;
        in >> moduleCount;
        if (moduleCount > 32) { if (error) *error = "Particle module stack is too large: " + path; return false; }
        p.modules.clear();
        for (std::size_t i = 0; i < moduleCount; ++i) {
            int type = 0, enabledValue = 1;
            in >> type >> enabledValue;
            ParticleModule module;
            module.type = static_cast<ParticleModuleType>(std::clamp(type, 0,
                static_cast<int>(ParticleModuleType::Renderer)));
            module.enabled = enabledValue != 0;
            if (version >= 8) {
                int initialized = 0;
                in >> module.instanceId >> std::quoted(module.name) >> initialized
                   >> module.vectorValue.x >> module.vectorValue.y >> module.vectorValue.z >> module.valueA;
                if (version >= 9) in >> module.valueB >> module.valueC >> module.valueD;
                if (version >= 10) {
                    int curveEnabled = 0;
                    in >> module.colorValueA.r >> module.colorValueA.g
                       >> module.colorValueA.b >> module.colorValueA.a
                       >> module.colorValueB.r >> module.colorValueB.g
                       >> module.colorValueB.b >> module.colorValueB.a;
                    for (float& key : module.curveValues) in >> key;
                    in >> curveEnabled;
                    module.curveEnabled = curveEnabled != 0;
                }
                if (version >= 11) {
                    int stage = 0;
                    in >> stage;
                    module.stage = static_cast<ParticleModuleStage>(std::clamp(stage, 0, 2));
                }
                module.parametersInitialized = initialized != 0;
            }
            if (type >= 0 && type <= static_cast<int>(ParticleModuleType::Renderer))
                p.modules.push_back(std::move(module));
        }
    }
    if (version >= 12) {
        std::size_t parameterCount = 0;
        in >> std::quoted(p.shaderPath) >> parameterCount;
        if (p.shaderPath == "-") p.shaderPath.clear();
        if (parameterCount > 64) {
            if (error) *error = "Particle shader has too many parameters: " + path;
            return false;
        }
        for (std::size_t i = 0; i < parameterCount; ++i) {
            ParticleShaderParameter parameter;
            in >> std::quoted(parameter.name)
               >> parameter.type
               >> std::quoted(parameter.value);
            p.shaderParameters.push_back(std::move(parameter));
        }
    }
    if (!in) { if (error) *error = "Particle asset data is incomplete: " + path; return false; }
    if (p.texturePath == "-") p.texturePath.clear();
    s.enabled = enabled != 0; s.autoplay = autoplay != 0; s.loop = loop != 0;
    s.prewarm = prewarm != 0; s.localSpace = localSpace != 0;
    p.shape = static_cast<EmitShape>(std::clamp(shape, 0, 2));
    p.blend = static_cast<ParticleBlend>(std::clamp(blend, 0, 1));
    p.useSizeCurve = useSizeCurve != 0; p.useColorCurve = useColorCurve != 0;
    p.textureLoop = textureLoop != 0;
    NormalizeParticleModuleStack(p, version >= 10);
    SanitizeParticleConfig(p);
    p.maxParticles = std::max(p.maxParticles, 1);
    p.textureColumns = std::max(p.textureColumns, 1); p.textureRows = std::max(p.textureRows, 1);
    p.textureFps = std::max(p.textureFps, 0.0f); p.boundsRadius = std::max(p.boundsRadius, 0.01f);
    p.collisionRadius = std::max(p.collisionRadius, 0.0f);
    p.collisionBounce = std::max(p.collisionBounce, 0.0f);
    p.collisionFriction = std::clamp(p.collisionFriction, 0.0f, 1.0f);
    p.collisionLifetimeLoss = std::clamp(p.collisionLifetimeLoss, 0.0f, 1.0f);
    p.trailSegments = std::clamp(p.trailSegments, 2, 16);
    p.trailLength = std::max(p.trailLength, 0.001f); p.trailWidth = std::max(p.trailWidth, 0.0f);
    p.trailOpacity = std::clamp(p.trailOpacity, 0.0f, 1.0f);
    p.meshScale = std::max(p.meshScale, 0.001f);
    s.duration = std::max(s.duration, 0.0f); s.startDelay = std::max(s.startDelay, 0.0f);
    s.simulationSpeed = std::max(s.simulationSpeed, 0.0f);
    *output = std::move(s);
    if (error) error->clear();
    return true;
}

bool SaveParticleAsset(const std::string& path, const ParticleSystemComponent& source,
                       std::string* error) {
    std::error_code ec;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create particle asset folder: " + ec.message();
        return false;
    }
    ParticleSystemComponent s = source;
    SanitizeParticleConfig(s.config);
    NormalizeParticleModuleStack(s.config, true);
    std::ofstream out(file);
    if (!out) {
        if (error) *error = "Could not open particle asset for writing.";
        return false;
    }
    const EmitterConfig& p = s.config;
    out << "3DGParticle 12\n"
        << (s.enabled ? 1 : 0) << ' ' << (s.autoplay ? 1 : 0) << ' '
        << (s.loop ? 1 : 0) << ' ' << (s.prewarm ? 1 : 0) << ' '
        << s.duration << ' ' << s.startDelay << ' ' << s.simulationSpeed << ' '
        << (s.localSpace ? 1 : 0) << ' ' << s.burstCount << ' ' << s.burstInterval << '\n'
        << p.rate << ' ' << p.maxParticles << ' ' << static_cast<int>(p.shape) << ' '
        << p.shapeRadius << ' ' << p.direction.x << ' ' << p.direction.y << ' '
        << p.direction.z << ' ' << p.coneAngleDeg << ' ' << p.speedMin << ' '
        << p.speedMax << ' ' << p.lifeMin << ' ' << p.lifeMax << ' '
        << p.gravity.x << ' ' << p.gravity.y << ' ' << p.gravity.z << ' ' << p.drag << '\n'
        << p.startColor.r << ' ' << p.startColor.g << ' ' << p.startColor.b << ' '
        << p.startColor.a << ' ' << p.endColor.r << ' ' << p.endColor.g << ' '
        << p.endColor.b << ' ' << p.endColor.a << ' ' << p.startSize << ' '
        << p.endSize << ' ' << static_cast<int>(p.blend) << '\n'
        << p.rotationMinDeg << ' ' << p.rotationMaxDeg << ' '
        << p.angularVelocityMinDeg << ' ' << p.angularVelocityMaxDeg << ' '
        << (p.useSizeCurve ? 1 : 0) << ' ' << (p.useColorCurve ? 1 : 0);
    for (float key : p.sizeCurve) out << ' ' << key;
    for (float key : p.colorCurve) out << ' ' << key;
    out << '\n' << std::quoted(p.texturePath.empty() ? std::string("-") : p.texturePath)
        << ' ' << p.textureColumns << ' ' << p.textureRows << ' ' << p.textureFps
        << ' ' << (p.textureLoop ? 1 : 0) << ' ' << (p.cullingEnabled ? 1 : 0)
        << ' ' << p.boundsRadius << ' ' << (p.collisionEnabled ? 1 : 0)
        << ' ' << static_cast<int>(p.collisionResponse) << ' ' << p.collisionRadius
        << ' ' << p.collisionBounce << ' ' << p.collisionFriction << ' '
        << p.collisionLifetimeLoss << ' ' << (p.trailsEnabled ? 1 : 0)
        << ' ' << p.trailSegments << ' ' << p.trailLength << ' ' << p.trailWidth
        << ' ' << p.trailOpacity << ' ' << static_cast<int>(p.renderMode)
        << ' ' << static_cast<int>(p.meshShape) << ' '
        << std::quoted(p.meshPath.empty() ? std::string("-") : p.meshPath)
        << ' ' << p.meshScale << ' ' << (p.meshAlignToVelocity ? 1 : 0)
        << ' ' << static_cast<int>(p.simulationBackend) << '\n'
        << p.modules.size();
    for (const ParticleModule& module : p.modules) {
        out << ' ' << static_cast<int>(module.type) << ' '
            << (SupportsDuplicateParticleModules(module.type) ? (module.enabled ? 1 : 0)
                : (IsParticleModuleEnabled(p, module.type) ? 1 : 0))
            << ' ' << module.instanceId << ' ' << std::quoted(module.name)
            << ' ' << (module.parametersInitialized ? 1 : 0)
            << ' ' << module.vectorValue.x << ' ' << module.vectorValue.y
            << ' ' << module.vectorValue.z << ' ' << module.valueA
            << ' ' << module.valueB << ' ' << module.valueC << ' ' << module.valueD
            << ' ' << module.colorValueA.r << ' ' << module.colorValueA.g
            << ' ' << module.colorValueA.b << ' ' << module.colorValueA.a
            << ' ' << module.colorValueB.r << ' ' << module.colorValueB.g
            << ' ' << module.colorValueB.b << ' ' << module.colorValueB.a;
        for (float key : module.curveValues) out << ' ' << key;
        out << ' ' << (module.curveEnabled ? 1 : 0)
            << ' ' << static_cast<int>(module.stage);
    }
    out << '\n'
        << std::quoted(p.shaderPath.empty() ? std::string("-") : p.shaderPath)
        << ' ' << p.shaderParameters.size();
    for (const ParticleShaderParameter& parameter : p.shaderParameters) {
        out << ' ' << std::quoted(parameter.name)
            << ' ' << parameter.type
            << ' ' << std::quoted(parameter.value);
    }
    out << '\n';
    if (!out) {
        if (error) *error = "Could not finish writing particle asset.";
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace engine
