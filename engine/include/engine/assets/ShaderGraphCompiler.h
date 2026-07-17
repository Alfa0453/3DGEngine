#pragma once

#include "engine/assets/ShaderAsset.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

struct GeneratedShaderSource {
    bool success = false;
    std::string vertex;
    std::string fragment;
    std::vector<ShaderAssetIssue> issues;
    std::unordered_map<int, std::uint64_t> fragmentLineNodes;
    std::vector<std::uint64_t> reachableNodes;
};

// Deterministic graph-to-GLSL generation. Only nodes feeding the active domain
// output are emitted; invalid and cyclic graphs produce issues, not source.
GeneratedShaderSource GenerateShaderSource(const ShaderAsset& asset, bool skinned = false);

} // namespace engine
