#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

struct CookedAssetEntry {
    AssetHandle id;
    AssetType type = AssetType::Unknown;
    std::string virtualPath;
    std::uint64_t fileSize = 0;
    std::uint64_t contentHash = 0;
};

struct AssetCookResult {
    std::string outputRoot;
    std::string runtimeScenePath;
    std::vector<CookedAssetEntry> assets;
};

// Builds a minimal, relocatable game-content folder from one exported runtime
// scene. Only assets reachable through the scene's stable-ID dependency list
// are copied. The produced Content registry contains the same subset.
class AssetCooker {
public:
    static bool CookRuntimeScene(const std::string& contentRoot,
                                 const std::string& runtimeScenePath,
                                 const std::string& outputRoot,
                                 const AssetRegistry& registry,
                                 AssetCookResult* result = nullptr,
                                 std::string* error = nullptr);

    // Builds a relocatable package for a .3dgworld and every runtime scene it
    // references. Dependencies are unioned across the persistent and streamed
    // levels, so a level activated later never relies on uncooked editor files.
    static bool CookRuntimeWorld(const std::string& contentRoot,
                                 const std::string& runtimeWorldPath,
                                 const std::string& outputRoot,
                                 const AssetRegistry& registry,
                                 AssetCookResult* result = nullptr,
                                 std::string* error = nullptr);
};

} // namespace engine
