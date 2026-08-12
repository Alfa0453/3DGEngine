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

// Adapt a Shader-Editor graph (any domain) into a water fragment BODY suitable for
// engine::WaterConfig::customFragmentSource. The graph's generated fragment is rewritten:
// its `#version`, `in` varyings and `out FragColor` are dropped (the water pipeline's
// prelude provides them), uniforms that clash with the water prelude (uTime/uSceneColor/
// uSceneDepth) are removed, and the graph's varying names are #define-aliased to water's
// (vUV->vSurfaceCoord, vWorldPosition->vWorldPos, vNormal->vBaseNormal, ...). The graph's
// main() writes FragColor as usual. This is a best-effort bridge: graph parameters and
// surface-lighting uniforms the water pass does not set read as 0. Returns "" with
// `error` set on failure.
std::string GenerateWaterFragmentBody(const ShaderAsset& asset, std::string* error = nullptr);

} // namespace engine
