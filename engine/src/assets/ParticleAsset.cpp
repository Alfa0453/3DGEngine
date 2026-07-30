#include "engine/assets/ParticleAsset.h"

#include "engine/assets/AssetReference.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/assets/ShaderAsset.h"
#include "engine/assets/TextureAsset.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace engine {
namespace {

std::mutex g_particleAssetRootMutex;
std::filesystem::path g_particleAssetContentRoot;

bool IsContentComponent(const std::filesystem::path& component) {
    std::string value = component.string();
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value == "content";
}

std::filesystem::path WithoutContentPrefix(const std::filesystem::path& path) {
    auto it = path.begin();
    if (it == path.end() || !IsContentComponent(*it)) return path;
    std::filesystem::path relative;
    for (++it; it != path.end(); ++it) relative /= *it;
    return relative;
}

} // namespace

void SetParticleAssetContentRoot(const std::string& contentRoot) {
    std::error_code ec;
    std::filesystem::path root(contentRoot);
    if (!root.empty()) root = std::filesystem::absolute(root, ec).lexically_normal();
    std::lock_guard<std::mutex> lock(g_particleAssetRootMutex);
    g_particleAssetContentRoot = ec ? std::filesystem::path(contentRoot).lexically_normal()
                                    : std::move(root);
}

std::string ParticleAssetContentRoot() {
    std::lock_guard<std::mutex> lock(g_particleAssetRootMutex);
    return g_particleAssetContentRoot.string();
}

std::string ResolveParticleAssetPath(const std::string& path) {
    if (path.empty()) return path;
    std::filesystem::path requested(path);
    std::error_code ec;
    if (std::filesystem::is_regular_file(requested, ec)) {
        return requested.lexically_normal().string();
    }

    std::filesystem::path root;
    {
        std::lock_guard<std::mutex> lock(g_particleAssetRootMutex);
        root = g_particleAssetContentRoot;
    }
    if (root.empty() || requested.is_absolute()) return path;

    const std::filesystem::path relative = WithoutContentPrefix(requested);
    const std::filesystem::path rooted = (root / relative).lexically_normal();
    ec.clear();
    if (std::filesystem::is_regular_file(rooted, ec)) return rooted.string();

    // Script-facing shorthand: "EnemyArcaneBolt" or
    // "EnemyArcaneBolt.particle" resolves in the standard particle folder.
    if (!requested.has_parent_path()) {
        std::filesystem::path filename = requested;
        if (!filename.has_extension()) filename += ".particle";
        const std::filesystem::path particle =
            (root / "Assets" / "Particles" / filename).lexically_normal();
        ec.clear();
        if (std::filesystem::is_regular_file(particle, ec)) return particle.string();
    }
    return path;
}

bool LoadParticleAsset(const std::string& path, ParticleSystemComponent* output, std::string* error) {
    if (!output) { if (error) *error = "Particle asset output is null."; return false; }
    const std::string resolvedPath = ResolveParticleAssetPath(path);
    std::ifstream in(resolvedPath);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version)
        || (magic != "3DGParticle" && magic != "3DG_PARTICLE")
        || version < 1 || version > 14) {
        if (error) {
            *error = "Unsupported, missing, or malformed particle asset: " + path;
            if (resolvedPath != path) *error += " (resolved to " + resolvedPath + ")";
        }
        return false;
    }
    ParticleSystemComponent s;
    if (magic == "3DG_PARTICLE") {
        std::string assetId;
        if (!(in >> assetId) || !AssetHandle::Parse(assetId, &s.assetId)) {
            if (error) *error = "Particle asset has an invalid stable ID: " + path;
            return false;
        }
    }
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
            if (version >= 14) {
                std::string textureId;
                in >> textureId;
                if (textureId != "-"
                    && !AssetHandle::Parse(textureId, &parameter.assetId)) {
                    if (error) *error =
                        "Particle shader texture reference is invalid: " + path;
                    return false;
                }
            }
            p.shaderParameters.push_back(std::move(parameter));
        }
    }
    if (version >= 13) {
        std::string textureId, meshId, shaderId;
        in >> textureId >> meshId >> shaderId;
        const auto parseOptional = [&](const std::string& text,
                                       AssetHandle* id) {
            return text == "-" || AssetHandle::Parse(text, id);
        };
        if (!parseOptional(textureId, &p.textureAssetId)
            || !parseOptional(meshId, &p.meshAssetId)
            || !parseOptional(shaderId, &p.shaderAssetId)) {
            if (error) *error = "Particle asset reference metadata is invalid: "
                + path;
            return false;
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

    const std::string contentRoot = FindContentRootForAsset(resolvedPath);
    AssetRegistry registry;
    std::string registryError;
    if (!contentRoot.empty()
        && registry.Load(
            AssetRegistry::DefaultRegistryPath(contentRoot), &registryError)) {
        const auto resolve = [&](AssetHandle id, std::string& fallback,
                                 AssetType type) {
            if (!id.Valid()) return;
            const std::string resolved = ResolveAssetReference(
                &registry, contentRoot, {id, fallback}, type);
            if (!resolved.empty()) fallback = resolved;
        };
        resolve(p.textureAssetId, p.texturePath, AssetType::Texture);
        resolve(p.meshAssetId, p.meshPath, AssetType::StaticMesh);
        resolve(p.shaderAssetId, p.shaderPath, AssetType::Shader);
        for (ParticleShaderParameter& parameter : p.shaderParameters)
            if (parameter.type == static_cast<int>(ShaderValueType::Texture2D))
                resolve(parameter.assetId, parameter.value, AssetType::Texture);
    }
    *output = std::move(s);
    if (error) error->clear();
    return true;
}

bool SaveParticleAsset(const std::string& path, ParticleSystemComponent& source,
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
    const std::string contentRoot = FindContentRootForAsset(path);
    AssetRegistry registry;
    std::string registryError;
    const std::string registryPath = contentRoot.empty()
        ? std::string() : AssetRegistry::DefaultRegistryPath(contentRoot);
    if (!registryPath.empty() && std::filesystem::exists(registryPath, ec)
        && !registry.Load(registryPath, &registryError)) {
        if (error) *error = "Could not load the particle asset registry: "
            + registryError;
        return false;
    }
    if (!s.assetId.Valid() && std::filesystem::is_regular_file(file, ec)) {
        ParticleSystemComponent existing;
        std::string ignored;
        if (LoadParticleAsset(path, &existing, &ignored)
            && existing.assetId.Valid())
            s.assetId = existing.assetId;
    }
    if (!s.assetId.Valid()) s.assetId = AssetHandle::Generate();

    auto capture = [&](std::string& assetPath, AssetHandle& id,
                       AssetType type) {
        if (contentRoot.empty() || assetPath.empty()) return true;
        if (type == AssetType::Texture) {
            std::string extension =
                std::filesystem::path(assetPath).extension().string();
            std::transform(extension.begin(), extension.end(),
                extension.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
            if (extension == ".png" || extension == ".jpg"
                || extension == ".jpeg" || extension == ".tga") {
                std::filesystem::path destination = file.parent_path()
                    / std::filesystem::path(assetPath).stem();
                destination.replace_extension(".3dgtex");
                TextureImportResult imported;
                if (!ImportTextureToAsset(
                        assetPath, destination.string(), contentRoot,
                        &registry, &imported, error))
                    return false;
                assetPath = destination.string();
                id = imported.id;
                return true;
            }
        }
        const AssetReference reference = MakeAssetReference(
            &registry, contentRoot, assetPath, type);
        if (reference.id.Valid()) id = reference.id;
        return true;
    };
    if (!capture(s.config.texturePath, s.config.textureAssetId,
                 AssetType::Texture)
        || !capture(s.config.meshPath, s.config.meshAssetId,
                    AssetType::StaticMesh)
        || !capture(s.config.shaderPath, s.config.shaderAssetId,
                    AssetType::Shader))
        return false;
    for (ParticleShaderParameter& parameter : s.config.shaderParameters) {
        if (parameter.type == static_cast<int>(ShaderValueType::Texture2D)
            && !capture(parameter.value, parameter.assetId,
                        AssetType::Texture))
            return false;
    }

    const std::filesystem::path temporary = file.string() + ".tmp";
    std::ofstream out(temporary);
    if (!out) {
        if (error) *error = "Could not open particle asset for writing.";
        return false;
    }
    const EmitterConfig& p = s.config;
    out << "3DG_PARTICLE 14 " << s.assetId.ToString() << '\n'
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
            << ' ' << std::quoted(parameter.value)
            << ' ' << (parameter.assetId.Valid()
                    ? parameter.assetId.ToString() : std::string("-"));
    }
    const auto storedId = [](AssetHandle id) {
        return id.Valid() ? id.ToString() : std::string("-");
    };
    out << '\n' << storedId(p.textureAssetId)
        << ' ' << storedId(p.meshAssetId)
        << ' ' << storedId(p.shaderAssetId) << '\n';
    std::vector<AssetHandle> dependencies;
    for (AssetHandle id :
         {p.textureAssetId, p.meshAssetId, p.shaderAssetId}) {
        if (id.Valid()
            && std::find(dependencies.begin(), dependencies.end(), id)
                   == dependencies.end())
            dependencies.push_back(id);
    }
    for (const ParticleShaderParameter& parameter : p.shaderParameters) {
        if (parameter.assetId.Valid()
            && std::find(dependencies.begin(), dependencies.end(),
                         parameter.assetId) == dependencies.end())
            dependencies.push_back(parameter.assetId);
    }
    out << "ASSET_DEPS " << dependencies.size();
    for (AssetHandle id : dependencies) out << ' ' << id.ToString();
    out << '\n';
    if (!out) {
        out.close();
        std::filesystem::remove(temporary, ec);
        if (error) *error = "Could not finish writing particle asset.";
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
            if (error) *error = "Could not replace particle asset: "
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
        if (error) *error = "Could not commit particle asset: " + ec.message();
        return false;
    }
    std::filesystem::remove(backup, ec);
    source = s;

    if (!contentRoot.empty()) {
        AssetRegistryEntry entry;
        entry.id = s.assetId;
        entry.type = AssetType::Particle;
        entry.virtualPath = AssetRegistry::NormalizeVirtualPath(
            std::filesystem::relative(file, contentRoot, ec).generic_string());
        entry.importerVersion = 1;
        entry.dependencies = dependencies;
        if (ec || !registry.Register(std::move(entry), &registryError)
            || !registry.Save(registryPath, &registryError)) {
            if (error) *error = "Particle saved, but registration failed: "
                + (ec ? ec.message() : registryError);
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

} // namespace engine
