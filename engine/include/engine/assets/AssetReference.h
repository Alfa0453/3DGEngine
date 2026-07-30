#pragma once

#include "engine/assets/AssetIdentity.h"

#include <string>

namespace engine {

class AssetRegistry;

// Serialized references keep a stable identity and a readable legacy path.
// The ID is authoritative; fallbackPath keeps older projects loadable and gives
// useful diagnostics when an asset has not yet been imported.
struct AssetReference {
    AssetHandle id;
    std::string fallbackPath;
};

AssetReference MakeAssetReference(const AssetRegistry* registry,
                                  const std::string& contentRoot,
                                  const std::string& path,
                                  AssetType expected = AssetType::Unknown);

// Resolves by ID first, then by fallback path. Returned paths are absolute when
// they belong to Content. An empty result means neither reference was usable.
std::string ResolveAssetReference(const AssetRegistry* registry,
                                  const std::string& contentRoot,
                                  const AssetReference& reference,
                                  AssetType expected = AssetType::Unknown,
                                  std::string* error = nullptr);

// Finds the nearest Content ancestor for an authored asset or scene path.
std::string FindContentRootForAsset(const std::string& path);

} // namespace engine
