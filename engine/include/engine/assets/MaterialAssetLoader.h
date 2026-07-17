#pragma once

#include "engine/ecs/Components.h"

#include <string>
#include <vector>

namespace engine {

struct RuntimeShaderParameter {
    std::string name;
    int type = 0;
    std::string value;
};

struct RuntimeMaterialAsset {
    ecs::PbrMaterial material;
    std::string name;
    std::string albedoMapPath;
    std::string normalMapPath;
    std::string metalRoughMapPath;
    std::string heightMapPath;
    std::string shaderPath;
    std::vector<RuntimeShaderParameter> shaderParameters;
};

bool LoadMaterialAssetFile(const std::string& path, RuntimeMaterialAsset* material, std::string* error);

} // namespace engine
