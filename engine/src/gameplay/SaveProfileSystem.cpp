#include "engine/gameplay/SaveProfileSystem.h"

#include "engine/ecs/Components.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/SaveGame.h"

#include <glm/gtx/norm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <optional>

namespace engine {
namespace {
std::optional<SaveProfileRuntimeState> g_runtime;

const CheckpointDefinition* FindCheckpoint(const SaveProfileRuntimeState& state,
                                           const std::string& name) {
    const auto it = std::find_if(state.profile.checkpoints.begin(), state.profile.checkpoints.end(),
        [&](const CheckpointDefinition& checkpoint) { return checkpoint.name == name; });
    return it == state.profile.checkpoints.end() ? nullptr : &*it;
}

bool WriteSlot(ecs::Registry& registry, const SaveProfileRuntimeState& state,
               int slot, const std::string& label, const std::string& scenePath,
               float playtimeSeconds, std::string* error) {
    SaveGame save = CaptureSaveGame(registry, scenePath, label, playtimeSeconds,
                                    SavePolicyFromProfile(state.profile));
    save.values["3dg.save_profile"] = state.assetPath;
    save.values["3dg.last_checkpoint"] = state.lastCheckpoint;
    save.values["3dg.last_slot"] = std::to_string(slot);
    return save.SaveToFile(SaveSlotPath(slot), error);
}
}

SaveCapturePolicy SavePolicyFromProfile(const SaveProfileAssetData& profile) {
    SaveCapturePolicy policy;
    policy.transforms = profile.persistence.transforms;
    policy.health = profile.persistence.health;
    policy.velocity = profile.persistence.velocity;
    policy.abilities = profile.persistence.abilities;
    policy.inventory = profile.persistence.inventory;
    policy.quests = profile.persistence.quests;
    policy.scriptValues = profile.persistence.scriptValues;
    return policy;
}

bool ConfigureSaveProfile(const std::string& path, std::string* error) {
    SaveProfileAssetData profile;
    if (!LoadSaveProfileAsset(path, &profile, error)) return false;
    g_runtime = SaveProfileRuntimeState{};
    g_runtime->profile = std::move(profile);
    g_runtime->assetPath = path;
    return true;
}

void ClearSaveProfile() { g_runtime.reset(); }
const SaveProfileRuntimeState* ActiveSaveProfile() { return g_runtime ? &*g_runtime : nullptr; }

bool UpdateSaveProfile(ecs::Registry& registry, ecs::Entity player,
                       const std::string& scenePath, float deltaSeconds,
                       float playtimeSeconds, std::string* event) {
    if (!g_runtime || player == ecs::kNull || !registry.Valid(player)) return false;
    auto* transform = registry.TryGet<ecs::Transform>(player);
    if (!transform) return false;
    auto& state = *g_runtime;
    if (state.initialLoadPending) {
        state.initialLoadPending = false;
        if (state.profile.loadLatestOnStart) {
            SaveSlotInfo latest;
            for (const auto& slot : ListSaveSlots(state.profile.maximumSlots))
                if (slot.exists && (!latest.exists || slot.timestamp > latest.timestamp)) latest = slot;
            if (latest.exists) {
                SaveGame save;
                std::string error;
                if (SaveGame::LoadFromFile(SaveSlotPath(latest.slot), save, &error)) {
                    ApplySaveGame(registry, save);
                    state.lastSlot = latest.slot;
                    const auto found = save.values.find("3dg.last_checkpoint");
                    if (found != save.values.end()) state.lastCheckpoint = found->second;
                    if (event) *event = "Loaded latest save slot " + std::to_string(latest.slot);
                    return true;
                }
                if (event) *event = error;
            }
        }
    }
    for (const auto& checkpoint : state.profile.checkpoints) {
        if (!checkpoint.enabled || !checkpoint.saveOnEnter) continue;
        if (checkpoint.oneShot && state.activated.count(checkpoint.name)) continue;
        if (glm::distance2(transform->position, checkpoint.position)
            > checkpoint.activationRadius * checkpoint.activationRadius) continue;
        state.lastCheckpoint = checkpoint.name;
        state.lastSlot = checkpoint.slot;
        state.activated.insert(checkpoint.name);
        std::string error;
        if (!WriteSlot(registry, state, checkpoint.slot, checkpoint.name,
                       scenePath, playtimeSeconds, &error)) {
            if (event) *event = error;
            return false;
        }
        if (event) *event = "Checkpoint saved: " + checkpoint.name;
        return true;
    }
    if (state.profile.autosaveEnabled) {
        state.autosaveElapsed += std::max(deltaSeconds, 0.0f);
        if (state.autosaveElapsed >= state.profile.autosaveIntervalSeconds) {
            state.autosaveElapsed = 0.0f;
            const int slot = state.lastSlot >= 0 ? state.lastSlot : state.profile.defaultSlot;
            std::string error;
            if (!WriteSlot(registry, state, slot, "Autosave", scenePath, playtimeSeconds, &error)) {
                if (event) *event = error;
                return false;
            }
            if (event) *event = "Autosaved slot " + std::to_string(slot);
            return true;
        }
    }
    return false;
}

bool RespawnFromLastCheckpoint(ecs::Registry& registry, ecs::Entity player,
                               std::string* error) {
    if (!g_runtime || g_runtime->lastSlot < 0) {
        if (error) *error = "No checkpoint has been activated.";
        return false;
    }
    SaveGame save;
    if (!SaveGame::LoadFromFile(SaveSlotPath(g_runtime->lastSlot), save, error)) return false;
    ApplySaveGame(registry, save);
    const auto* checkpoint = FindCheckpoint(*g_runtime, g_runtime->lastCheckpoint);
    if (checkpoint && g_runtime->profile.respawn.restoreAtCheckpoint) {
        if (auto* transform = registry.TryGet<ecs::Transform>(player))
        {
            transform->position = checkpoint->position;
            transform->rotation = glm::quat(glm::radians(checkpoint->rotationDegrees));
        }
    }
    if (g_runtime->profile.respawn.restoreHealth) {
        if (auto* health = registry.TryGet<Health>(player)) {
            health->hp = health->maxHp * g_runtime->profile.respawn.healthFraction;
            health->alive = health->hp > 0.0f;
            health->justDied = false;
        }
    }
    if (g_runtime->profile.respawn.clearVelocity) {
        if (auto* linear = registry.TryGet<ecs::LinearVelocity>(player)) linear->velocity = {};
        if (auto* angular = registry.TryGet<ecs::AngularVelocity>(player)) angular->radiansPerSecond = 0.0f;
    }
    return true;
}

} // namespace engine
