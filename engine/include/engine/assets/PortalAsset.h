#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace engine {

inline constexpr std::uint32_t kPortalAssetVersion = 1;

enum class PortalMode : std::uint32_t {
    SameLevel = 0,
    LevelTransition,
    SeamlessDoor
};

struct PortalAssetData {
    NativeAssetHeader header;
    std::string name = "Portal";
    PortalMode mode = PortalMode::SameLevel;
    std::string destinationObject;
    std::string destinationLevel;
    AssetHandle destinationLevelId;
    glm::vec3 arrivalOffset{0.0f, 0.1f, 1.0f};
    glm::vec3 arrivalRotationDegrees{0.0f};
    float activationRadius = 1.5f;
    float cooldown = 0.75f;
    bool automatic = false;
    bool preserveVelocity = false;
    bool alignFacing = true;
    bool oneWay = false;
    bool safeArrival = true;
    std::string requiredAccessTag;
    std::string prompt = "Enter";
    std::string enterAudioPath;
    std::string exitAudioPath;
    std::string transitionEffectPath;
    AssetHandle enterAudioId;
    AssetHandle exitAudioId;
    AssetHandle transitionEffectId;
};

void NormalizePortalAsset(PortalAssetData& asset);
bool ValidatePortalAsset(const PortalAssetData& asset, std::string* error = nullptr);
bool SavePortalAsset(const std::string& path, PortalAssetData asset,
                     std::string* error = nullptr);
bool LoadPortalAsset(const std::string& path, PortalAssetData* asset,
                     std::string* error = nullptr);
const char* PortalModeName(PortalMode mode);

} // namespace engine
