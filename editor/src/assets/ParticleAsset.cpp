#include "ParticleAsset.h"

#include <engine/assets/ParticleAsset.h>
#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace particle_asset {

bool Save(const std::string& path, engine::ParticleSystemComponent& s, std::string* error) {
    return engine::SaveParticleAsset(path, s, error);
#if 0
    std::error_code ec;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) { if (error) *error = "Could not create particle asset folder: " + ec.message(); return false; }
    std::ofstream out(file);
    if (!out) { if (error) *error = "Could not open particle asset for writing."; return false; }
    const engine::EmitterConfig& p = s.config;
    out << "3DGParticle 12\n"
        << (s.enabled ? 1 : 0) << ' ' << (s.autoplay ? 1 : 0) << ' '
        << (s.loop ? 1 : 0) << ' ' << (s.prewarm ? 1 : 0) << ' '
        << s.duration << ' ' << s.startDelay << ' ' << s.simulationSpeed << ' '
        << (s.localSpace ? 1 : 0) << ' ' << s.burstCount << ' ' << s.burstInterval << '\n'
        << p.rate << ' ' << p.maxParticles << ' ' << static_cast<int>(p.shape) << ' '
        << p.shapeRadius << ' ' << p.direction.x << ' ' << p.direction.y << ' ' << p.direction.z << ' '
        << p.coneAngleDeg << ' ' << p.speedMin << ' ' << p.speedMax << ' '
        << p.lifeMin << ' ' << p.lifeMax << ' ' << p.gravity.x << ' ' << p.gravity.y << ' '
        << p.gravity.z << ' ' << p.drag << '\n'
        << p.startColor.r << ' ' << p.startColor.g << ' ' << p.startColor.b << ' ' << p.startColor.a << ' '
        << p.endColor.r << ' ' << p.endColor.g << ' ' << p.endColor.b << ' ' << p.endColor.a << ' '
        << p.startSize << ' ' << p.endSize << ' ' << static_cast<int>(p.blend) << '\n'
        << p.rotationMinDeg << ' ' << p.rotationMaxDeg << ' '
        << p.angularVelocityMinDeg << ' ' << p.angularVelocityMaxDeg << ' '
        << (p.useSizeCurve ? 1 : 0) << ' ' << (p.useColorCurve ? 1 : 0);
    for (float key : p.sizeCurve) out << ' ' << key;
    for (float key : p.colorCurve) out << ' ' << key;
    out << '\n' << std::quoted(p.texturePath.empty() ? std::string("-") : p.texturePath) << ' '
        << p.textureColumns << ' ' << p.textureRows << ' ' << p.textureFps << ' '
        << (p.textureLoop ? 1 : 0) << ' ' << (p.cullingEnabled ? 1 : 0) << ' '
        << p.boundsRadius << ' ' << (p.collisionEnabled ? 1 : 0) << ' '
        << static_cast<int>(p.collisionResponse) << ' ' << p.collisionRadius << ' '
        << p.collisionBounce << ' ' << p.collisionFriction << ' '
        << p.collisionLifetimeLoss << ' ' << (p.trailsEnabled ? 1 : 0) << ' '
        << p.trailSegments << ' ' << p.trailLength << ' ' << p.trailWidth << ' '
        << p.trailOpacity << ' ' << static_cast<int>(p.renderMode) << ' '
        << static_cast<int>(p.meshShape) << ' ' << std::quoted(p.meshPath.empty() ? std::string("-") : p.meshPath)
        << ' ' << p.meshScale << ' ' << (p.meshAlignToVelocity ? 1 : 0)
        << ' ' << static_cast<int>(p.simulationBackend) << '\n'
        << p.modules.size();
    for (const engine::ParticleModule& module : p.modules) {
        out << ' ' << static_cast<int>(module.type) << ' '
            << (engine::SupportsDuplicateParticleModules(module.type) ? (module.enabled ? 1 : 0)
                : (engine::IsParticleModuleEnabled(p, module.type) ? 1 : 0))
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
        out << ' ' << (module.curveEnabled ? 1 : 0) << ' ' << static_cast<int>(module.stage);
    }
    out << '\n'
        << std::quoted(p.shaderPath.empty() ? std::string("-") : p.shaderPath)
        << ' ' << p.shaderParameters.size();
    for (const engine::ParticleShaderParameter& parameter : p.shaderParameters) {
        out << ' ' << std::quoted(parameter.name)
            << ' ' << parameter.type
            << ' ' << std::quoted(parameter.value);
    }
    out << '\n';
    if (!out) { if (error) *error = "Could not finish writing particle asset."; return false; }
    if (error) error->clear();
    return true;
#endif
}

bool Load(const std::string& path, engine::ParticleSystemComponent* output, std::string* error) {
    return engine::LoadParticleAsset(path, output, error);
#if 0
    if (!output) { if (error) *error = "Particle asset output is null."; return false; }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DGParticle" || version < 1 || version > 12) {
        if (error) *error = "Unsupported or malformed particle asset.";
        return false;
    }
    engine::ParticleSystemComponent s;
    engine::EmitterConfig& p = s.config;
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
    if (version >= 2) {
        int cullingEnabled = 1;
        in >> cullingEnabled >> p.boundsRadius;
        p.cullingEnabled = cullingEnabled != 0;
    }
    if (version >= 3) {
        int collisionEnabled = 0;
        int collisionResponse = 0;
        in >> collisionEnabled >> collisionResponse >> p.collisionRadius
           >> p.collisionBounce >> p.collisionFriction >> p.collisionLifetimeLoss;
        p.collisionEnabled = collisionEnabled != 0;
        p.collisionResponse = static_cast<engine::ParticleCollisionResponse>(
            std::clamp(collisionResponse, 0, 1));
    }
    if (version >= 4) {
        int trailsEnabled = 0;
        in >> trailsEnabled >> p.trailSegments >> p.trailLength >> p.trailWidth >> p.trailOpacity;
        p.trailsEnabled = trailsEnabled != 0;
    }
    if (version >= 5) {
        int renderMode = 0, meshShape = 0, align = 1;
        in >> renderMode >> meshShape >> std::quoted(p.meshPath) >> p.meshScale >> align;
        p.renderMode = static_cast<engine::ParticleRenderMode>(std::clamp(renderMode, 0, 1));
        p.meshShape = static_cast<engine::ParticleMeshShape>(std::clamp(meshShape, 0, 4));
        p.meshAlignToVelocity = align != 0;
        if (p.meshPath == "-") p.meshPath.clear();
        p.meshScale = std::max(p.meshScale, 0.001f);
    }
    if (version >= 6) {
        int backend = 0;
        in >> backend;
        p.simulationBackend = static_cast<engine::ParticleSimulationBackend>(
            std::clamp(backend, 0, 2));
    }
    if (version >= 7) {
        std::size_t moduleCount = 0;
        in >> moduleCount;
        if (moduleCount > 32) { if (error) *error = "Particle module stack is too large."; return false; }
        p.modules.clear();
        for (std::size_t i = 0; i < moduleCount; ++i) {
            int type = 0, enabledValue = 1;
            in >> type >> enabledValue;
            engine::ParticleModule module;
            module.type = static_cast<engine::ParticleModuleType>(std::clamp(type, 0,
                static_cast<int>(engine::ParticleModuleType::Renderer)));
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
                    module.stage = static_cast<engine::ParticleModuleStage>(std::clamp(stage, 0, 2));
                }
                module.parametersInitialized = initialized != 0;
            }
            if (type >= 0 && type <= static_cast<int>(engine::ParticleModuleType::Renderer))
                p.modules.push_back(std::move(module));
        }
    }
    if (version >= 12) {
        std::size_t parameterCount = 0;
        in >> std::quoted(p.shaderPath) >> parameterCount;
        if (p.shaderPath == "-") p.shaderPath.clear();
        if (parameterCount > 64) {
            if (error) *error = "Particle shader parameter count is too large.";
            return false;
        }
        for (std::size_t i = 0; i < parameterCount; ++i) {
            engine::ParticleShaderParameter parameter;
            in >> std::quoted(parameter.name)
               >> parameter.type
               >> std::quoted(parameter.value);
            p.shaderParameters.push_back(std::move(parameter));
        }
    }
    if (!in) { if (error) *error = "Particle asset data is incomplete."; return false; }
    if (p.texturePath == "-") p.texturePath.clear();
    s.enabled = enabled != 0; s.autoplay = autoplay != 0; s.loop = loop != 0;
    s.prewarm = prewarm != 0; s.localSpace = localSpace != 0;
    p.shape = static_cast<engine::EmitShape>(std::clamp(shape, 0, 2));
    p.blend = static_cast<engine::ParticleBlend>(std::clamp(blend, 0, 1));
    p.useSizeCurve = useSizeCurve != 0; p.useColorCurve = useColorCurve != 0;
    p.textureLoop = textureLoop != 0;
    engine::NormalizeParticleModuleStack(p, version >= 10);
    engine::SanitizeParticleConfig(p);
    p.maxParticles = std::max(p.maxParticles, 1); p.textureColumns = std::max(p.textureColumns, 1);
    p.textureRows = std::max(p.textureRows, 1); p.textureFps = std::max(p.textureFps, 0.0f);
    p.boundsRadius = std::max(p.boundsRadius, 0.01f);
    p.collisionRadius = std::max(p.collisionRadius, 0.0f);
    p.collisionBounce = std::max(p.collisionBounce, 0.0f);
    p.collisionFriction = std::clamp(p.collisionFriction, 0.0f, 1.0f);
    p.collisionLifetimeLoss = std::clamp(p.collisionLifetimeLoss, 0.0f, 1.0f);
    p.trailSegments = std::clamp(p.trailSegments, 2, 16);
    p.trailLength = std::max(p.trailLength, 0.001f);
    p.trailWidth = std::max(p.trailWidth, 0.0f);
    p.trailOpacity = std::clamp(p.trailOpacity, 0.0f, 1.0f);
    p.meshScale = std::max(p.meshScale, 0.001f);
    s.duration = std::max(s.duration, 0.0f); s.startDelay = std::max(s.startDelay, 0.0f);
    s.simulationSpeed = std::max(s.simulationSpeed, 0.0f);
    *output = std::move(s);
    if (error) error->clear();
    return true;
#endif
}

bool SaveEffect(const std::string& path,
                std::vector<engine::ParticleEffectLayer>& layers,
                engine::AssetHandle* assetId,
                std::string* error) {
    std::error_code ec;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        if (error) *error =
            "Could not create particle effect folder: " + ec.message();
        return false;
    }
    engine::AssetHandle id = assetId ? *assetId : engine::AssetHandle{};
    if (!id.Valid() && std::filesystem::is_regular_file(file, ec)) {
        std::ifstream existing(file);
        std::string magic;
        int version = 0;
        std::string idText;
        if (existing >> magic >> version >> idText
            && magic == "3DG_PARTICLE_EFFECT" && version >= 2)
            engine::AssetHandle::Parse(idText, &id);
    }
    if (!id.Valid()) id = engine::AssetHandle::Generate();

    const std::string contentRoot =
        engine::FindContentRootForAsset(path);
    engine::AssetRegistry registry;
    std::string registryError;
    const std::string registryPath = contentRoot.empty()
        ? std::string()
        : engine::AssetRegistry::DefaultRegistryPath(contentRoot);
    if (!registryPath.empty() && std::filesystem::exists(registryPath, ec)
        && !registry.Load(registryPath, &registryError)) {
        if (error) *error = "Could not load the particle effect registry: "
            + registryError;
        return false;
    }
    std::vector<engine::AssetHandle> dependencies;
    for (engine::ParticleEffectLayer& layer : layers) {
        if (!contentRoot.empty()) {
            const engine::AssetReference reference =
                engine::MakeAssetReference(
                    &registry, contentRoot, layer.assetPath,
                    engine::AssetType::Particle);
            if (reference.id.Valid()) layer.assetId = reference.id;
        }
        if (layer.assetId.Valid()
            && std::find(dependencies.begin(), dependencies.end(),
                         layer.assetId) == dependencies.end())
            dependencies.push_back(layer.assetId);
    }
    const std::filesystem::path temporary = file.string() + ".tmp";
    std::ofstream out(temporary);
    if (!out) { if (error) *error = "Could not open particle effect for writing."; return false; }
    out << "3DG_PARTICLE_EFFECT 2 " << id.ToString() << '\n'
        << layers.size() << '\n';
    for (const engine::ParticleEffectLayer& layer : layers) {
        out << std::quoted(layer.name) << ' ' << std::quoted(layer.assetPath) << ' '
            << (layer.assetId.Valid()
                    ? layer.assetId.ToString() : std::string("-")) << ' '
            << (layer.enabled ? 1 : 0) << ' ' << layer.offset.x << ' '
            << layer.offset.y << ' ' << layer.offset.z << '\n';
    }
    out << "ASSET_DEPS " << dependencies.size();
    for (engine::AssetHandle dependency : dependencies)
        out << ' ' << dependency.ToString();
    out << '\n';
    if (!out) {
        out.close();
        std::filesystem::remove(temporary, ec);
        if (error) *error = "Could not finish writing particle effect.";
        return false;
    }
    out.close();
    const std::filesystem::path backup = file.string() + ".bak";
    std::filesystem::remove(backup, ec);
    ec.clear();
    if (std::filesystem::exists(file, ec)) {
        std::filesystem::rename(file, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            if (error) *error = "Could not replace particle effect: "
                + ec.message();
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        std::error_code rollback;
        if (std::filesystem::exists(backup, rollback))
            std::filesystem::rename(backup, file, rollback);
        if (error) *error = "Could not commit particle effect: "
            + ec.message();
        return false;
    }
    std::filesystem::remove(backup, ec);
    if (!contentRoot.empty()) {
        engine::AssetRegistryEntry entry;
        entry.id = id;
        entry.type = engine::AssetType::ParticleEffect;
        entry.virtualPath = engine::AssetRegistry::NormalizeVirtualPath(
            std::filesystem::relative(file, contentRoot, ec).generic_string());
        entry.importerVersion = 1;
        entry.dependencies = dependencies;
        if (ec || !registry.Register(std::move(entry), &registryError)
            || !registry.Save(registryPath, &registryError)) {
            if (error) *error = "Particle effect saved, but registration failed: "
                + (ec ? ec.message() : registryError);
            return false;
        }
    }
    if (assetId) *assetId = id;
    if (error) error->clear();
    return true;
}

bool LoadEffect(const std::string& path, std::vector<engine::ParticleEffectLayer>* layers,
                engine::AssetHandle* assetId, std::string* error) {
    if (!layers) { if (error) *error = "Particle effect output is null."; return false; }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    std::size_t count = 0;
    engine::AssetHandle id;
    if (!(in >> magic >> version)
        || (magic != "3DGParticleEffect"
            && magic != "3DG_PARTICLE_EFFECT")
        || version < 1 || version > 2) {
        if (error) *error = "Unsupported or malformed particle effect.";
        return false;
    }
    if (magic == "3DG_PARTICLE_EFFECT") {
        std::string idText;
        if (!(in >> idText) || !engine::AssetHandle::Parse(idText, &id)) {
            if (error) *error = "Particle effect has an invalid stable ID.";
            return false;
        }
    }
    if (!(in >> count) || count > 64) {
        if (error) *error = "Particle effect layer count is invalid.";
        return false;
    }
    const std::string contentRoot =
        engine::FindContentRootForAsset(path);
    engine::AssetRegistry registry;
    std::string registryError;
    const bool haveRegistry = !contentRoot.empty()
        && registry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot),
            &registryError);
    std::vector<engine::ParticleEffectLayer> loaded;
    loaded.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        engine::ParticleEffectLayer layer;
        int enabled = 1;
        in >> std::quoted(layer.name) >> std::quoted(layer.assetPath);
        if (version >= 2) {
            std::string layerId;
            in >> layerId;
            if (layerId != "-"
                && !engine::AssetHandle::Parse(layerId, &layer.assetId)) {
                if (error) *error =
                    "Particle effect layer has an invalid asset ID.";
                return false;
            }
        }
        in >> enabled
           >> layer.offset.x >> layer.offset.y >> layer.offset.z;
        if (!in) { if (error) *error = "Particle effect layer data is incomplete."; return false; }
        layer.enabled = enabled != 0;
        if (haveRegistry && layer.assetId.Valid()) {
            const std::string resolved = engine::ResolveAssetReference(
                &registry, contentRoot,
                {layer.assetId, layer.assetPath},
                engine::AssetType::Particle);
            if (!resolved.empty()) layer.assetPath = resolved;
        }
        std::string loadError;
        if (!Load(layer.assetPath, &layer.system, &loadError)) {
            if (error) *error = "Layer " + layer.name + ": " + loadError;
            return false;
        }
        loaded.push_back(std::move(layer));
    }
    *layers = std::move(loaded);
    if (assetId) *assetId = id;
    if (error) error->clear();
    return true;
}

} // namespace particle_asset
