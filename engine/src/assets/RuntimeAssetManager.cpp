#include "engine/assets/RuntimeAssetManager.h"

#include "engine/ecs/Components.h"

#include <exception>
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
        const bool wasCached = FindTexture(asset.albedoPath) != nullptr;
        std::string error;
        const Texture* texture = LoadTexture(asset.albedoPath, &error);
        if (!texture) {
            report.errors.push_back(error);
            return;
        }

        if (!wasCached) {
            ++report.texturesLoaded;
        }
        registry.Add<ecs::LoadedMaterialAsset>(entity, ecs::LoadedMaterialAsset{texture});
        ++report.texturesAssigned;
    });

    return report;
}
void RuntimeAssetManager::Clear()
{
    m_models.clear();
    m_textures.clear();
}

} // namespace engine
