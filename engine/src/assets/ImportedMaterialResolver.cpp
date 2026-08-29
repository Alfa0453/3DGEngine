#include "engine/assets/ImportedMaterialResolver.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/assets/MaterialAssetLoader.h"
#include "engine/assets/TextureAsset.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Texture.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace engine {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::filesystem::path FindContentRoot(const std::filesystem::path& asset) {
    std::filesystem::path cursor = std::filesystem::absolute(asset).parent_path();
    for (std::filesystem::path current = cursor; !current.empty();
         current = current.parent_path()) {
        std::string name = current.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "content") return current;
        if (current == current.root_path()) break;
    }
    return cursor;
}

std::filesystem::path FromVirtual(const std::filesystem::path& content,
                                  std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.rfind("/Game/", 0) == 0) path.erase(0, 6);
    else if (path.rfind("Game/", 0) == 0) path.erase(0, 5);
    return (content / std::filesystem::path(path)).lexically_normal();
}

std::filesystem::path ResolveReference(
    const std::filesystem::path& content,
    const std::filesystem::path& owner,
    const AssetRegistry* registry, AssetHandle id, const std::string& fallback) {
    if (registry && id.Valid()) {
        if (const AssetRegistryEntry* entry = registry->Find(id))
            return FromVirtual(content, entry->virtualPath);
    }
    if (fallback.empty()) return {};
    const std::filesystem::path path(fallback);
    if (path.is_absolute()) return path.lexically_normal();
    if (fallback.rfind("/Game/", 0) == 0
        || fallback.rfind("Game/", 0) == 0)
        return FromVirtual(content, fallback);
    std::error_code ec;
    const std::filesystem::path beside = (owner.parent_path() / path).lexically_normal();
    if (std::filesystem::exists(beside, ec)) return beside;
    return FromVirtual(content, fallback);
}

} // namespace

bool ResolveImportedMaterialSlots(
    const std::string& meshAssetPath,
    const std::vector<MeshMaterialSlot>& slots,
    std::vector<Material>* materials,
    std::vector<std::unique_ptr<Texture>>* textures,
    std::string* error) {
    if (!materials || !textures) {
        SetError(error, "Material-slot output is null.");
        return false;
    }
    const std::filesystem::path mesh = std::filesystem::absolute(meshAssetPath);
    const std::filesystem::path content = FindContentRoot(mesh);
    AssetRegistry registry;
    std::string ignored;
    const bool hasRegistry = registry.Load(
        AssetRegistry::DefaultRegistryPath(content.string()), &ignored);
    const AssetRegistry* registryPtr = hasRegistry ? &registry : nullptr;
    std::unordered_map<std::string, int> textureCache;

    auto loadTexture = [&](const std::filesystem::path& materialPath,
                           AssetHandle id, const std::string& fallback) -> int {
        const std::filesystem::path path = ResolveReference(
            content, materialPath, registryPtr, id, fallback);
        if (path.empty()) return -1;
        const std::string key = path.lexically_normal().generic_string();
        if (const auto found = textureCache.find(key); found != textureCache.end())
            return found->second;
        TextureAssetData asset;
        std::string loadError;
        if (!LoadTextureAsset(path.string(), &asset, &loadError)) {
            textureCache.emplace(key, -1);
            return -1;
        }
        const int index = static_cast<int>(textures->size());
        textures->push_back(std::make_unique<Texture>(asset.rgba.data(),
            static_cast<int>(asset.width), static_cast<int>(asset.height)));
        textureCache.emplace(key, index);
        return index;
    };

    materials->clear();
    materials->reserve(slots.size());
    for (const MeshMaterialSlot& slot : slots) {
        const std::filesystem::path materialPath = ResolveReference(
            content, mesh, registryPtr, slot.materialId, slot.materialPath);
        RuntimeMaterialAsset source;
        std::string loadError;
        if (materialPath.empty()
            || !LoadMaterialAssetFile(materialPath.string(), &source, &loadError)) {
            Material missing;
            missing.name = slot.name;
            materials->push_back(std::move(missing));
            continue;
        }
        Material material;
        material.name = slot.name.empty() ? source.name : slot.name;
        material.diffuse = source.material.albedo;
        material.emissive = source.material.emissive;
        material.metallic = source.material.metallic;
        material.roughness = source.material.roughness;
        material.ao = source.material.ao;
        material.opacity = source.material.opacity;
        material.specular = glm::mix(glm::vec3(0.04f), material.diffuse,
                                    std::clamp(material.metallic, 0.0f, 1.0f));
        const float roughness = std::clamp(material.roughness, 0.04f, 1.0f);
        material.shininess = std::clamp(2.0f / (roughness * roughness) - 2.0f,
                                        1.0f, 1024.0f);
        material.diffuseMap = loadTexture(materialPath,
            source.albedoMapAssetId, source.albedoMapPath);
        material.normalMap = loadTexture(materialPath,
            source.normalMapAssetId, source.normalMapPath);
        material.metalRoughMap = loadTexture(materialPath,
            source.metalRoughMapAssetId, source.metalRoughMapPath);
        material.heightMap = loadTexture(materialPath,
            source.heightMapAssetId, source.heightMapPath);
        material.emissiveMap = loadTexture(materialPath,
            source.emissiveMapAssetId, source.emissiveMapPath);
        materials->push_back(std::move(material));
    }
    SetError(error, {});
    return true;
}

} // namespace engine
