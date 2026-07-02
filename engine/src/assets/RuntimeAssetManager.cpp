#include "engine/assets/RuntimeAssetManager.h"

#include "engine/ecs/Components.h"

#include <exception>
#include <filesystem>
#include <utility>

namespace engine {
namespace {

void SetError(std::string* out, const std::string& message) {
    if (out) {
        *out = message;
    }
}

} // namespace

const Model* RuntimeAssetManager::LoadModel(const std::string &path, std::string *error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: model path is empty");
        return nullptr;
    }

    const auto existing = m_models.find(path);
    if (existing != m_models.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto model = std::make_unique<Model>(Model::FromFile(path));
        const Model* result = model.get();
        m_models.emplace(path, std::move(model));
        SetError(error, std::string{});
        return result;
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const Texture* RuntimeAssetManager::LoadTexture(const std::string &path, std::string *error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: texture path is empty");
        return nullptr;
    }

    const auto existing = m_textures.find(path);
    if (existing != m_textures.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto texture = std::make_unique<Texture>(path);
        const Texture* result = texture.get();
        m_textures.emplace(path, std::move(texture));
        SetError(error, std::string{});
        return result;
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const RuntimeMaterialAsset* RuntimeAssetManager::LoadMaterial(const std::string& path, std::string* error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: material path is empty");
        return nullptr;
    }

    const auto existing = m_materials.find(path);
    if (existing != m_materials.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    auto material = std::make_unique<RuntimeMaterialAsset>();
    if (!LoadMaterialAssetFile(path, material.get(), error)) {
        return nullptr;
    }

    const RuntimeMaterialAsset* result = material.get();
    m_materials.emplace(path, std::move(material));
    SetError(error, std::string{});
    return result;
}
const Model *RuntimeAssetManager::FindModel(const std::string &path) const
{
    const auto found = m_models.find(path);
    return found == m_models.end() ? nullptr : found->second.get();
}
const Texture *RuntimeAssetManager::FindTexture(const std::string &path) const
{
    const auto found = m_textures.find(path);
    return found == m_textures.end() ? nullptr : found->second.get();
}

const RuntimeMaterialAsset* RuntimeAssetManager::FindMaterial(const std::string& path) const
{
    const auto found = m_materials.find(path);
    return found == m_materials.end() ? nullptr : found->second.get();
}
RuntimeAssetManager::ResolveReport RuntimeAssetManager::ResolveRegistryAssets(ecs::Registry &registry)
{
    ResolveReport report;

    registry.view<ecs::ModelAsset>().each([&](ecs::Entity entity, ecs::ModelAsset& asset) {
        const bool wasCached = FindModel(asset.path) != nullptr;
        std::string error;
        const Model* model = LoadModel(asset.path, &error);
        if (!model) {
            report.errors.push_back(error);
            return;
        }

        if (!wasCached) {
            ++report.modelsLoaded;
        }
        registry.Add<ecs::LoadedModelAsset>(entity, ecs::LoadedModelAsset{model});
        ++report.modelsAssigned;
    });

    registry.view<ecs::MaterialAsset>().each([&](ecs::Entity entity, ecs::MaterialAsset& asset) {
        const std::string materialPath = asset.path.empty() ? asset.albedoPath : asset.path;
        if (materialPath.empty()) {
            return;
        }

        std::string error;
        ecs::LoadedMaterialAsset loaded;

        if (std::filesystem::path(materialPath).extension() == ".3dgmat") {
            const bool wasCached = FindMaterial(materialPath) != nullptr;
            const RuntimeMaterialAsset* material = LoadMaterial(materialPath, &error);
            if (!material) {
                report.errors.push_back(error);
                return;
            }
            if (!wasCached) {
                ++report.materialsLoaded;
            }
            loaded.material = material->material;
            if (!material->albedoMapPath.empty()) {
                const bool textureCached = FindTexture(material->albedoMapPath) != nullptr;
                loaded.albedoMap = LoadTexture(material->albedoMapPath, &error);
                if (!loaded.albedoMap) {
                    report.errors.push_back(error);
                    return;
                }
                if (!textureCached) {
                    ++report.texturesLoaded;
                }
            }
            if (!material->normalMapPath.empty()) {
                loaded.normalMap = LoadTexture(material->normalMapPath, &error);
            }
            if (!material->metalRoughMapPath.empty()) {
                loaded.metalRoughMap = LoadTexture(material->metalRoughMapPath, &error);
            }
            ++report.materialsAssigned;
        } else {
            const bool wasCached = FindTexture(materialPath) != nullptr;
            const Texture* texture = LoadTexture(materialPath, &error);
            if (!texture) {
                report.errors.push_back(error);
                return;
            }
            if (!wasCached) {
                ++report.texturesLoaded;
            }
            loaded.albedoMap = texture;
            loaded.material.albedoMap = texture;
            ++report.texturesAssigned;
        }

        loaded.material.albedoMap = loaded.albedoMap;
        loaded.material.normalMap = loaded.normalMap;
        loaded.material.metalRoughMap = loaded.metalRoughMap;
        registry.Add<ecs::LoadedMaterialAsset>(entity, loaded);
    });

    return report;
}
void RuntimeAssetManager::Clear()
{
    m_models.clear();
    m_textures.clear();
    m_materials.clear();
}

} // namespace engine
