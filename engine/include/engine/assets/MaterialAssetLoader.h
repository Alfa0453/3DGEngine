#pragma once

#include "engine/ecs/Components.h"

#include <string>

namespace engine {

struct RuntimeMaterialAsset {
    ecs::PbrMaterial material;
    std::string name;
    std::string albedoMapPath;
    std::string normalMapPath;
    std::string metalRoughMapPath;
};

bool LoadMaterialAssetFile(const std::string& path, RuntimeMaterialAsset* material, std::string* error);

} // namespace engine
