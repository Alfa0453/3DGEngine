#include "engine/assets/ImportedMaterialAssets.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/assets/MaterialAssetLoader.h"
#include "engine/assets/TextureAsset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace engine {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::string Sanitize(std::string value, const std::string& fallback) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '_' && c != '-') c = '_';
    }
    while (!value.empty() && (value.front() == '_' || value.front() == '.'))
        value.erase(value.begin());
    while (!value.empty() && (value.back() == '_' || value.back() == '.'))
        value.pop_back();
    return value.empty() ? fallback : value;
}

std::string AbsoluteSource(const std::string& source) {
    std::error_code ec;
    const auto path = std::filesystem::absolute(source, ec).lexically_normal();
    return ec ? source : path.string();
}

std::uint64_t HashBytes(const std::vector<std::uint8_t>& bytes,
                        std::uint64_t seed = 14695981039346656037ull) {
    for (const std::uint8_t byte : bytes) {
        seed ^= byte;
        seed *= 1099511628211ull;
    }
    return seed;
}

const AssetRegistryEntry* FindGenerated(const AssetRegistry* registry,
                                        AssetType type,
                                        const std::string& identity) {
    if (!registry) return nullptr;
    for (const AssetRegistryEntry& entry : registry->Entries())
        if (entry.type == type && entry.sourcePath == identity) return &entry;
    return nullptr;
}

std::string AbsoluteFromVirtual(const std::string& root,
                                std::string virtualPath) {
    constexpr const char* prefix = "/Game/";
    if (virtualPath.rfind(prefix, 0) == 0) virtualPath.erase(0, 6);
    return (std::filesystem::path(root) / virtualPath).lexically_normal().string();
}

bool VirtualPath(const std::string& path, const std::string& root,
                 std::string* result) {
    std::error_code ec;
    const auto absoluteRoot = std::filesystem::absolute(root, ec).lexically_normal();
    if (ec) return false;
    const auto absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return false;
    const auto relative = absolutePath.lexically_relative(absoluteRoot);
    if (relative.empty() || relative.is_absolute()
        || (relative.begin() != relative.end()
            && relative.begin()->generic_string() == ".."))
        return false;
    *result = AssetRegistry::NormalizeVirtualPath(relative.generic_string());
    return true;
}

AssetHandle ExistingTextureId(const std::string& path) {
    TextureAssetData loaded;
    std::string ignored;
    return LoadTextureAsset(path, &loaded, &ignored) ? loaded.header.id
                                                     : AssetHandle{};
}

AssetHandle ExistingMaterialId(const std::string& path) {
    RuntimeMaterialAsset loaded;
    std::string ignored;
    return LoadMaterialAssetFile(path, &loaded, &ignored) ? loaded.id
                                                          : AssetHandle{};
}

std::filesystem::path PackageFolder(const std::string& meshPath) {
    const std::filesystem::path mesh(meshPath);
    const std::string stem = mesh.stem().string();
    if (mesh.parent_path().filename() == stem) return mesh.parent_path();
    return mesh.parent_path() / (stem + "_Imported");
}

std::vector<std::uint8_t> PackOrm(const StaticMeshMaterialData& material,
                                  const std::vector<StaticMeshTextureData>& textures,
                                  std::uint32_t* width, std::uint32_t* height) {
    auto valid = [&](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < textures.size();
    };
    if (valid(material.metalRoughMap)) {
        const auto& source = textures[static_cast<std::size_t>(material.metalRoughMap)];
        *width = source.width; *height = source.height;
        return source.rgba;
    }
    int reference = valid(material.roughnessMap) ? material.roughnessMap
        : (valid(material.metallicMap) ? material.metallicMap
                                      : (valid(material.aoMap) ? material.aoMap : -1));
    if (reference < 0) return {};
    *width = textures[static_cast<std::size_t>(reference)].width;
    *height = textures[static_cast<std::size_t>(reference)].height;
    std::vector<std::uint8_t> packed(
        static_cast<std::size_t>(*width) * *height * 4u, 255u);
    auto sample = [&](int index, std::uint32_t x, std::uint32_t y,
                      float fallback) {
        if (!valid(index)) return fallback;
        const auto& texture = textures[static_cast<std::size_t>(index)];
        const std::uint32_t sx = std::min(texture.width - 1u,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(x)
                * texture.width) / std::max(1u, *width)));
        const std::uint32_t sy = std::min(texture.height - 1u,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(y)
                * texture.height) / std::max(1u, *height)));
        return texture.rgba[(static_cast<std::size_t>(sy) * texture.width + sx) * 4u]
            / 255.0f;
    };
    for (std::uint32_t y = 0; y < *height; ++y)
        for (std::uint32_t x = 0; x < *width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * *width + x) * 4u;
            packed[at] = static_cast<std::uint8_t>(std::lround(
                std::clamp(sample(material.aoMap, x, y, material.ao), 0.0f, 1.0f) * 255.0f));
            packed[at + 1u] = static_cast<std::uint8_t>(std::lround(
                std::clamp(sample(material.roughnessMap, x, y, material.roughness), 0.0f, 1.0f) * 255.0f));
            packed[at + 2u] = static_cast<std::uint8_t>(std::lround(
                std::clamp(sample(material.metallicMap, x, y, material.metallic), 0.0f, 1.0f) * 255.0f));
        }
    return packed;
}

// Generated sub-assets are written before the owning mesh so that its slot
// table can contain final handles. Keep those writes transactional: any fatal
// error restores artist-owned files and the in-memory registry. Individual
// missing source maps remain non-fatal and are handled by the caller.
class GeneratedAssetTransaction {
public:
    explicit GeneratedAssetTransaction(AssetRegistry* registry)
        : m_registry(registry) {
        if (registry) m_registryBefore = *registry;
    }

    void Track(const std::filesystem::path& path) {
        const std::string key = path.lexically_normal().string();
        if (!m_tracked.insert(key).second) return;
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            m_newFiles.insert(key);
            return;
        }
        m_originalFiles[key] = std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    void Commit() { m_committed = true; }

    ~GeneratedAssetTransaction() {
        if (m_committed) return;
        std::error_code ec;
        for (const std::string& path : m_newFiles)
            std::filesystem::remove(path, ec);
        for (const auto& [path, bytes] : m_originalFiles) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!bytes.empty())
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
        }
        if (m_registry) *m_registry = std::move(m_registryBefore);
    }

private:
    AssetRegistry* m_registry = nullptr;
    AssetRegistry m_registryBefore;
    std::unordered_set<std::string> m_tracked;
    std::unordered_set<std::string> m_newFiles;
    std::unordered_map<std::string, std::vector<std::uint8_t>> m_originalFiles;
    bool m_committed = false;
};

} // namespace

bool CreateImportedMaterialAssets(
    const std::string& sourceModelPath, const std::string& meshAssetPath,
    const std::string& contentRoot,
    const ImportedMaterialGenerationOptions& options,
    const std::vector<StaticMeshMaterialData>& sourceMaterials,
    const std::vector<StaticMeshTextureData>& sourceTextures,
    AssetRegistry* registry, std::vector<MeshMaterialSlot>* materialSlots,
    std::vector<AssetHandle>* dependencies,
    ImportedMaterialGenerationStats* stats, std::string* error) {
    if (!materialSlots || !dependencies) {
        SetError(error, "Imported material output is null.");
        return false;
    }
    materialSlots->clear();
    ImportedMaterialGenerationStats generated;
    if (!options.importMaterials || sourceMaterials.empty()) {
        if (stats) *stats = generated;
        SetError(error, {});
        return true;
    }
    GeneratedAssetTransaction transaction(registry);

    const std::string source = AbsoluteSource(sourceModelPath);
    const std::filesystem::path package = PackageFolder(meshAssetPath);
    const std::filesystem::path materialFolder = options.createMaterialFolder
        ? package / "Materials" : package;
    const std::filesystem::path textureFolder = options.createTextureFolder
        ? package / "Textures" : package;
    std::error_code ec;
    std::filesystem::create_directories(materialFolder, ec);
    if (ec) {
        SetError(error, "Could not create imported material folder: " + ec.message());
        return false;
    }
    if (options.importTextures) std::filesystem::create_directories(textureFolder, ec);
    if (ec) {
        SetError(error, "Could not create imported texture folder: " + ec.message());
        return false;
    }

    std::unordered_map<std::string, int> usedNames;
    std::unordered_map<std::string, std::pair<std::string, AssetHandle>> textureCache;
    auto uniqueMaterialName = [&](const std::string& sourceName) {
        const std::string base = Sanitize(sourceName, "Material");
        const int occurrence = usedNames[base]++;
        return occurrence == 0 ? base : base + "_" + std::to_string(occurrence);
    };
    auto createTexture = [&](int sourceIndex, const std::string& semantic,
                             const std::string& materialName, bool srgb,
                             const std::vector<std::uint8_t>* replacement,
                             std::uint32_t replacementWidth,
                             std::uint32_t replacementHeight,
                             std::string* pathOut, AssetHandle* idOut) {
        pathOut->clear(); *idOut = {};
        if (!options.importTextures) return true;
        if (!replacement && (sourceIndex < 0
            || static_cast<std::size_t>(sourceIndex) >= sourceTextures.size()))
            return true;
        const std::string cacheKey = (replacement ? "packed:" : "source:")
            + std::to_string(sourceIndex) + ":" + semantic;
        if (const auto found = textureCache.find(cacheKey);
            found != textureCache.end()) {
            *pathOut = found->second.first; *idOut = found->second.second;
            return true;
        }
        const std::string identity = source + "#texture:" + cacheKey;
        const AssetRegistryEntry* existing = options.reuseExistingTextures
            ? FindGenerated(registry, AssetType::Texture, identity) : nullptr;
        const bool reused = existing != nullptr;
        const AssetHandle existingId = existing ? existing->id : AssetHandle{};
        const std::string existingVirtualPath = existing
            ? existing->virtualPath : std::string{};
        std::filesystem::path path = existing
            ? std::filesystem::path(AbsoluteFromVirtual(contentRoot, existingVirtualPath))
            : textureFolder / (materialName + "_" + semantic + ".3dgtex");
        AssetHandle id = reused ? existingId : ExistingTextureId(path.string());
        if (!id.Valid()) id = AssetHandle::Generate();
        TextureAssetData texture;
        texture.header.type = AssetType::Texture;
        texture.header.id = id;
        texture.header.importerVersion = kStaticMeshImporterVersion;
        texture.smooth = true; texture.srgb = srgb;
        if (replacement) {
            texture.width = replacementWidth; texture.height = replacementHeight;
            texture.rgba = *replacement;
        } else {
            const auto& sourceTexture = sourceTextures[static_cast<std::size_t>(sourceIndex)];
            texture.width = sourceTexture.width; texture.height = sourceTexture.height;
            texture.rgba = sourceTexture.rgba;
        }
        texture.header.sourceHash = HashBytes(texture.rgba);
        transaction.Track(path);
        if (!SaveTextureAsset(path.string(), std::move(texture), error)) {
            ++generated.failedTextures;
            return true; // missing/bad individual maps are non-fatal
        }
        std::string virtualPath;
        if (!VirtualPath(path.string(), contentRoot, &virtualPath)) {
            SetError(error, "Generated texture destination is outside Content.");
            return false;
        }
        if (registry) {
            AssetRegistryEntry entry;
            entry.id = id; entry.type = AssetType::Texture;
            entry.virtualPath = virtualPath; entry.sourcePath = identity;
            entry.sourceHash = HashBytes(replacement ? *replacement
                : sourceTextures[static_cast<std::size_t>(sourceIndex)].rgba);
            entry.importerVersion = kStaticMeshImporterVersion;
            if (!registry->Register(std::move(entry), error)) return false;
        }
        reused ? ++generated.reusedTextures : ++generated.importedTextures;
        *pathOut = path.string(); *idOut = id;
        textureCache.emplace(cacheKey, std::make_pair(*pathOut, *idOut));
        return true;
    };

    for (std::size_t i = 0; i < sourceMaterials.size(); ++i) {
        const StaticMeshMaterialData& sourceMaterial = sourceMaterials[i];
        const std::string name = uniqueMaterialName(sourceMaterial.name);
        const std::string identity = source + "#material:" + std::to_string(i);
        const AssetRegistryEntry* existing = options.reuseExistingMaterials
            ? FindGenerated(registry, AssetType::Material, identity) : nullptr;
        const bool reused = existing != nullptr;
        const AssetHandle existingId = existing ? existing->id : AssetHandle{};
        const std::string existingVirtualPath = existing
            ? existing->virtualPath : std::string{};
        std::filesystem::path path = existing
            ? std::filesystem::path(AbsoluteFromVirtual(contentRoot, existingVirtualPath))
            : materialFolder / (name + ".3dgmat");
        AssetHandle id = reused ? existingId : ExistingMaterialId(path.string());
        if (!id.Valid()) id = AssetHandle::Generate();
        RuntimeMaterialAsset previousMaterial;
        const bool materialAlreadyExists =
            LoadMaterialAssetFile(path.string(), &previousMaterial, nullptr);

        RuntimeMaterialAsset material;
        material.id = id; material.name = sourceMaterial.name.empty()
            ? name : sourceMaterial.name;
        material.material.albedo = {sourceMaterial.diffuse[0],
            sourceMaterial.diffuse[1], sourceMaterial.diffuse[2]};
        material.material.metallic = std::clamp(sourceMaterial.metallic, 0.0f, 1.0f);
        material.material.roughness = std::clamp(sourceMaterial.roughness, 0.04f, 1.0f);
        material.material.ao = std::clamp(sourceMaterial.ao, 0.0f, 1.0f);
        material.material.emissive = {sourceMaterial.emissive[0],
            sourceMaterial.emissive[1], sourceMaterial.emissive[2]};
        material.material.opacity = std::clamp(sourceMaterial.opacity, 0.0f, 1.0f);
        material.material.blendMode = static_cast<ecs::PbrMaterial::BlendMode>(
            std::clamp(sourceMaterial.alphaMode, 0, 2));

        if (!createTexture(sourceMaterial.diffuseMap, "BaseColor", name, true,
                nullptr, 0, 0, &material.albedoMapPath,
                &material.albedoMapAssetId)
            || !createTexture(sourceMaterial.normalMap, "Normal", name, false,
                nullptr, 0, 0, &material.normalMapPath,
                &material.normalMapAssetId)
            || !createTexture(sourceMaterial.heightMap, "Height", name, false,
                nullptr, 0, 0, &material.heightMapPath,
                &material.heightMapAssetId)
            || !createTexture(sourceMaterial.emissiveMap, "Emissive", name, true,
                nullptr, 0, 0, &material.emissiveMapPath,
                &material.emissiveMapAssetId))
            return false;
        std::uint32_t ormWidth = 0, ormHeight = 0;
        const std::vector<std::uint8_t> orm = PackOrm(
            sourceMaterial, sourceTextures, &ormWidth, &ormHeight);
        if (!orm.empty() && !createTexture(sourceMaterial.metalRoughMap,
                "MetalRough", name, false, &orm, ormWidth, ormHeight,
                &material.metalRoughMapPath, &material.metalRoughMapAssetId))
            return false;

        const bool preserve = materialAlreadyExists
            && options.materialReimportPolicy
                == StaticMeshImportOptions::MaterialReimportPolicy::PreserveExisting;
        if (!preserve) {
            transaction.Track(path);
            if (!SaveMaterialAssetFile(path.string(), material, error)) return false;
        }
        std::vector<AssetHandle> materialDependencies;
        const RuntimeMaterialAsset& dependencySource = preserve
            ? previousMaterial : material;
        for (AssetHandle dependency : {dependencySource.albedoMapAssetId,
                dependencySource.normalMapAssetId,
                dependencySource.metalRoughMapAssetId,
                dependencySource.heightMapAssetId,
                dependencySource.emissiveMapAssetId})
            if (dependency.Valid()) materialDependencies.push_back(dependency);
        std::string virtualPath;
        if (!VirtualPath(path.string(), contentRoot, &virtualPath)) {
            SetError(error, "Generated material destination is outside Content.");
            return false;
        }
        if (registry) {
            AssetRegistryEntry entry;
            entry.id = id; entry.type = AssetType::Material;
            entry.virtualPath = virtualPath; entry.sourcePath = identity;
            entry.sourceHash = HashAssetSourceFile(sourceModelPath, nullptr);
            entry.importerVersion = kStaticMeshImporterVersion;
            entry.dependencies = materialDependencies;
            if (!registry->Register(std::move(entry), error)) return false;
        }
        if (options.applyImportedMaterials) {
            MeshMaterialSlot slot;
            slot.name = sourceMaterial.name.empty() ? name : sourceMaterial.name;
            slot.materialId = id;
            slot.materialPath = virtualPath;
            materialSlots->push_back(std::move(slot));
            dependencies->push_back(id);
            ++generated.assignedSlots;
        }
        (reused || materialAlreadyExists)
            ? ++generated.reusedMaterials : ++generated.importedMaterials;
    }
    std::sort(dependencies->begin(), dependencies->end());
    dependencies->erase(std::unique(dependencies->begin(), dependencies->end()),
                        dependencies->end());
    if (stats) *stats = generated;
    transaction.Commit();
    SetError(error, {});
    return true;
}

} // namespace engine
