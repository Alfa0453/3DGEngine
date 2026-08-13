#pragma once

#include "engine/assets/AssetIdentity.h"
#include "engine/gameplay/GameplayComponents.h"

#include <string>

namespace engine {

struct RagdollAssetData {
    AssetHandle assetId;
    std::string name = "Ragdoll";
    std::string skeletonPath;
    float totalMass = 65.0f;
    float linearDamping = 0.25f;
    float angularDamping = 1.4f;
    float deathImpulse = 1.5f;
    float blendInDuration = 0.18f;
    float blendOutDuration = 0.28f;
    bool recoverWhenRevived = true;
    std::vector<RagdollBodyDefinition> bodies;
    std::vector<RagdollConstraintDefinition> constraints;
};

bool SaveRagdollAsset(const std::string& path, RagdollAssetData& asset,
                      std::string* error = nullptr);
bool LoadRagdollAsset(const std::string& path, RagdollAssetData* asset,
                      std::string* error = nullptr);
void ApplyRagdollAsset(const RagdollAssetData& asset, Ragdoll* ragdoll);

} // namespace engine
