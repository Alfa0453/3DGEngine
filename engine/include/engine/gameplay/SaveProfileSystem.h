#pragma once

#include "engine/assets/SaveProfileAsset.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/SaveGame.h"

#include <string>
#include <unordered_set>

namespace engine {

struct SaveProfileRuntimeState {
    SaveProfileAssetData profile;
    std::string assetPath;
    std::string lastCheckpoint;
    int lastSlot = -1;
    float autosaveElapsed = 0.0f;
    bool initialLoadPending = true;
    std::unordered_set<std::string> activated;
};

SaveCapturePolicy SavePolicyFromProfile(const SaveProfileAssetData& profile);
bool ConfigureSaveProfile(const std::string& path, std::string* error = nullptr);
void ClearSaveProfile();
const SaveProfileRuntimeState* ActiveSaveProfile();
bool UpdateSaveProfile(ecs::Registry& registry, ecs::Entity player,
                       const std::string& scenePath, float deltaSeconds,
                       float playtimeSeconds, std::string* event = nullptr);
bool RespawnFromLastCheckpoint(ecs::Registry& registry, ecs::Entity player,
                               std::string* error = nullptr);

} // namespace engine
