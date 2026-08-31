#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kSaveProfileAssetVersion = 1;

struct SavePersistenceRules {
    bool transforms = true;
    bool health = true;
    bool velocity = true;
    bool abilities = true;
    bool inventory = true;
    bool quests = true;
    bool scriptValues = true;
    bool streamedLevels = true;
};

struct SaveRespawnRules {
    bool restoreAtCheckpoint = true;
    bool restoreHealth = true;
    float healthFraction = 1.0f;
    bool clearVelocity = true;
};

struct CheckpointDefinition {
    std::string name = "Checkpoint";
    std::string anchorObject;
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    float activationRadius = 2.0f;
    int slot = 0;
    bool saveOnEnter = true;
    bool oneShot = true;
    bool enabled = true;
};

struct SaveProfileAssetData {
    NativeAssetHeader header;
    std::string name = "SaveProfile";
    int maximumSlots = 8;
    int defaultSlot = 0;
    bool loadLatestOnStart = false;
    bool autosaveEnabled = false;
    float autosaveIntervalSeconds = 120.0f;
    SavePersistenceRules persistence;
    SaveRespawnRules respawn;
    std::vector<CheckpointDefinition> checkpoints;
};

void NormalizeSaveProfile(SaveProfileAssetData& asset);
bool ValidateSaveProfile(const SaveProfileAssetData& asset,
                         std::string* error = nullptr);
bool SaveSaveProfileAsset(const std::string& path, SaveProfileAssetData asset,
                          std::string* error = nullptr);
bool LoadSaveProfileAsset(const std::string& path, SaveProfileAssetData* asset,
                          std::string* error = nullptr);

} // namespace engine
