#pragma once

#include "engine/assets/AssetIdentity.h"
#include "engine/ecs/Components.h"

#include <string>
#include <vector>

namespace engine {

struct RuntimeShaderParameter {
    std::string name;
    int type = 0;
    std::string value;
    AssetHandle assetId;
};

struct RuntimeMaterialAsset {
    AssetHandle id;
    ecs::PbrMaterial material;
    std::string name;
    std::string albedoMapPath;
    AssetHandle albedoMapAssetId;
    std::string normalMapPath;
    AssetHandle normalMapAssetId;
    std::string metalRoughMapPath;
    AssetHandle metalRoughMapAssetId;
    std::string heightMapPath;
    AssetHandle heightMapAssetId;
    std::string emissiveMapPath;
    AssetHandle emissiveMapAssetId;
    std::string shaderPath;
    AssetHandle shaderAssetId;
    std::vector<RuntimeShaderParameter> shaderParameters;
};

bool LoadMaterialAssetFile(const std::string& path, RuntimeMaterialAsset* material, std::string* error);

// Shared serializer used by Material Forge, model import, and editor-authored
// materials. Existing identity is retained when material.id is valid.
bool SaveMaterialAssetFile(const std::string& path,
                           RuntimeMaterialAsset material,
                           std::string* error = nullptr);

} // namespace engine
