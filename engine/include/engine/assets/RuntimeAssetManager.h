#pragma once

#include "engine/ecs/Registry.h"
#include "engine/assets/MaterialAssetLoader.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Texture.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class RuntimeAssetManager {
public:
    struct ResolveReport {
        int modelsLoaded = 0;
        int texturesLoaded = 0;
        int materialsLoaded = 0;
        int modelsAssigned = 0;
        int texturesAssigned = 0;
        int materialsAssigned = 0;
        std::vector<std::string> errors;
    };

    const Model* LoadModel(const std::string& path, std::string* error = nullptr);
    const Texture* LoadTexture(const std::string& path, std::string* error = nullptr);
    const RuntimeMaterialAsset* LoadMaterial(const std::string& path, std::string* error = nullptr);

    const Model* FindModel(const std::string& path) const;
    const Texture* FindTexture(const std::string& path) const;
    const RuntimeMaterialAsset* FindMaterial(const std::string& path) const;

    ResolveReport ResolveRegistryAssets(ecs::Registry& registry);
    void Clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Model>> m_models;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textures;
    std::unordered_map<std::string, std::unique_ptr<RuntimeMaterialAsset>> m_materials;
};

} // namespace engine