#pragma once

#include "engine/assets/StaticMeshAsset.h"

#include <memory>
#include <string>
#include <vector>

namespace engine {

struct Material;
class Texture;

// Resolves native mesh material slots by stable asset ID first and by the
// serialized path second. The loaded textures are appended to `textures` and
// remain owned by the model for as long as its Material indices are used.
bool ResolveImportedMaterialSlots(
    const std::string& meshAssetPath,
    const std::vector<MeshMaterialSlot>& slots,
    std::vector<Material>* materials,
    std::vector<std::unique_ptr<Texture>>* textures,
    std::string* error = nullptr);

} // namespace engine
