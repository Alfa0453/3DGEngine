#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kDestructionAssetVersion = 1;

struct DestructionDamageState {
    std::string name = "Damaged";
    float healthFraction = 0.5f;
    std::string meshPath;
    std::string materialPath;
    std::string particlePath;
    std::string audioPath;
    AssetHandle meshId;
    AssetHandle materialId;
    AssetHandle particleId;
    AssetHandle audioId;
};

struct DestructionAssetData {
    NativeAssetHeader header;
    std::string name = "Destructible";

    std::string sourceMeshPath;
    std::string sourceMaterialPath;
    std::string debrisMeshPath;
    std::string debrisMaterialPath;
    std::string breakParticlePath;
    std::string breakAudioPath;
    AssetHandle sourceMeshId;
    AssetHandle sourceMaterialId;
    AssetHandle debrisMeshId;
    AssetHandle debrisMaterialId;
    AssetHandle breakParticleId;
    AssetHandle breakAudioId;

    glm::vec3 bounds{1.0f};
    int chunksX = 2;
    int chunksY = 2;
    int chunksZ = 2;
    std::uint32_t seed = 1337;
    float maxHealth = 100.0f;
    float minimumDamage = 0.0f;
    float impactThreshold = 2.0f;
    float debrisMass = 1.0f;
    float impulseScale = 1.0f;
    float scatterImpulse = 1.5f;
    float angularImpulse = 2.0f;
    float debrisLifetime = 8.0f;
    float gap = 0.015f;
    bool debrisCollision = true;
    bool removeSourceOnBreak = true;
    std::vector<DestructionDamageState> states;
};

struct DestructionChunk {
    std::uint32_t index = 0;
    glm::vec3 localCenter{0.0f};
    glm::vec3 size{1.0f};
    glm::vec3 impulseDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 angularVelocity{0.0f};
};

void NormalizeDestructionAsset(DestructionAssetData& asset);
bool ValidateDestructionAsset(const DestructionAssetData& asset,
                              std::string* error = nullptr);
std::vector<DestructionChunk> GenerateDestructionChunks(
    const DestructionAssetData& asset,
    const glm::vec3& impactPoint = glm::vec3(0.0f));
int DestructionStateForHealth(const DestructionAssetData& asset, float health);
bool SaveDestructionAsset(const std::string& path, DestructionAssetData asset,
                          std::string* error = nullptr);
bool LoadDestructionAsset(const std::string& path, DestructionAssetData* asset,
                          std::string* error = nullptr);

} // namespace engine
