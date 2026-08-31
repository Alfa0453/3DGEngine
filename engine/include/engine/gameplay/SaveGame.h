#pragma once

// -----------------------------------------------------------------------------
// General save/load system.
//
// A save "slot" is a single .3dgsave file capturing runtime WORLD STATE plus
// metadata (display name, source scene, timestamp, playtime) and the flat
// key/value store scripts write via SaveValue. World state is snapshotted per
// NAMED entity (RuntimeName) so it can be re-applied after the scene reloads --
// entity ids are not stable across a load, names are.
//
// Persisted per entity (only components that are present): Transform, Health,
// LinearVelocity, AngularVelocity. This is header-only so it drops in without a
// CMake reconfigure; every free function is inline.
//
// Typical flow (host-driven):
//   save:  SaveGame s = CaptureSaveGame(registry, scenePath, "Slot 1", playSec);
//          s.SaveToFile(SaveSlotPath(1));
//   load:  SaveGame s; SaveGame::LoadFromFile(SaveSlotPath(1), s);
//          <load runtime scene s.scenePath>  then  ApplySaveGame(registry, s);
// -----------------------------------------------------------------------------

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/AbilitySystem.h"
#include "engine/gameplay/InventorySystem.h"
#include "engine/gameplay/QuestSystem.h"
#include "engine/gameplay/Script.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// The flat key/value file scripts use via SaveValue/LoadValue. Captured into each
// slot so hand-saved values ride along; kept in sync on load. (Same path as
// Script.cpp's store.)
inline const char* SaveKeyValuePath() { return "3dg_savegame.dat"; }

struct EntitySaveState {
    std::string name;

    bool      hasTransform = false;
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    bool  hasHealth = false;
    float hp = 0.0f;
    float maxHp = 0.0f;
    bool  alive = true;

    bool      hasLinearVelocity = false;
    glm::vec3 linearVelocity{0.0f};

    bool      hasAngularVelocity = false;
    glm::vec3 angularAxis{0.0f, 1.0f, 0.0f};
    float     angularRadians = 0.0f;

    bool hasAbilityResource = false;
    float mana = 0.0f, maxMana = 0.0f;
    float stamina = 0.0f, maxStamina = 0.0f;
    struct AbilityState {
        std::string path;
        std::string name;
        float cooldown = 0.0f;
        float recharge = 0.0f;
        int charges = 0;
    };
    std::vector<AbilityState> abilities;
    std::string inventoryState;
    std::string questState;
};

struct SaveCapturePolicy {
    bool transforms = true;
    bool health = true;
    bool velocity = true;
    bool abilities = true;
    bool inventory = true;
    bool quests = true;
    bool scriptValues = true;
};

struct SaveGame {
    static constexpr int kVersion = 3;

    int          version = kVersion;
    std::string  displayName;                 // shown in the load menu
    std::string  scenePath;                   // runtime scene (.runtimescene) to load
    std::int64_t timestamp = 0;               // unix seconds when saved
    float        playtimeSeconds = 0.0f;

    std::vector<EntitySaveState> entities;
    std::unordered_map<std::string, std::string> values;   // flat KV (scripts)

    bool SaveToFile(const std::string& path, std::string* error = nullptr) const {
        std::error_code ec;
        const std::filesystem::path fs(path);
        if (fs.has_parent_path())
            std::filesystem::create_directories(fs.parent_path(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (!out) { if (error) *error = "Could not write save: " + path; return false; }
        out << "3DGSAVE " << version << '\n'
            << "DISPLAY " << std::quoted(displayName.empty() ? std::string("-") : displayName) << '\n'
            << "SCENE " << std::quoted(scenePath.empty() ? std::string("-") : scenePath) << '\n'
            << "TIME " << timestamp << '\n'
            << "PLAY " << playtimeSeconds << '\n';
        out << "VALUES " << values.size() << '\n';
        for (const auto& kv : values)
            out << std::quoted(kv.first) << ' ' << std::quoted(kv.second) << '\n';
        out << "ENTITIES " << entities.size() << '\n';
        for (const EntitySaveState& e : entities) {
            out << std::quoted(e.name.empty() ? std::string("-") : e.name) << ' '
                << (e.hasTransform ? 1 : 0) << ' '
                << e.position.x << ' ' << e.position.y << ' ' << e.position.z << ' '
                << e.scale.x << ' ' << e.scale.y << ' ' << e.scale.z << ' '
                << e.rotation.w << ' ' << e.rotation.x << ' ' << e.rotation.y << ' ' << e.rotation.z << ' '
                << (e.hasHealth ? 1 : 0) << ' '
                << e.hp << ' ' << e.maxHp << ' ' << (e.alive ? 1 : 0) << ' '
                << (e.hasLinearVelocity ? 1 : 0) << ' '
                << e.linearVelocity.x << ' ' << e.linearVelocity.y << ' ' << e.linearVelocity.z << ' '
                << (e.hasAngularVelocity ? 1 : 0) << ' '
                << e.angularAxis.x << ' ' << e.angularAxis.y << ' ' << e.angularAxis.z << ' '
                << e.angularRadians << ' '
                << (e.hasAbilityResource ? 1 : 0) << ' '
                << e.mana << ' ' << e.maxMana << ' '
                << e.stamina << ' ' << e.maxStamina << ' '
                << e.abilities.size();
            for (const EntitySaveState::AbilityState& ability : e.abilities)
                out << ' ' << std::quoted(ability.path) << ' '
                    << std::quoted(ability.name) << ' ' << ability.cooldown << ' '
                    << ability.recharge << ' ' << ability.charges;
            out << ' ' << std::quoted(e.inventoryState) << ' ' << std::quoted(e.questState);
            out << '\n';
        }
        return static_cast<bool>(out);
    }

    static bool LoadFromFile(const std::string& path, SaveGame& out, std::string* error = nullptr) {
        std::ifstream in(path);
        if (!in) { if (error) *error = "Save not found: " + path; return false; }
        std::string tag;
        int fileVersion = 0;
        if (!(in >> tag >> fileVersion) || tag != "3DGSAVE" || fileVersion < 1
            || fileVersion > kVersion) {
            if (error) *error = "Unrecognised save format: " + path;
            return false;
        }
        out = SaveGame{};
        out.version = fileVersion;
        std::string display, scene;
        in >> tag >> std::quoted(display);
        in >> tag >> std::quoted(scene);
        in >> tag >> out.timestamp;
        in >> tag >> out.playtimeSeconds;
        out.displayName = (display == "-") ? std::string() : display;
        out.scenePath = (scene == "-") ? std::string() : scene;

        std::size_t valueCount = 0;
        in >> tag >> valueCount;                    // VALUES
        for (std::size_t i = 0; i < valueCount; ++i) {
            std::string k, v;
            in >> std::quoted(k) >> std::quoted(v);
            out.values[std::move(k)] = std::move(v);
        }
        std::size_t entityCount = 0;
        in >> tag >> entityCount;                   // ENTITIES
        out.entities.reserve(entityCount);
        for (std::size_t i = 0; i < entityCount; ++i) {
            EntitySaveState e;
            std::string name;
            int hasT = 0, hasH = 0, alive = 0, hasLV = 0, hasAV = 0;
            in >> std::quoted(name) >> hasT
               >> e.position.x >> e.position.y >> e.position.z
               >> e.scale.x >> e.scale.y >> e.scale.z
               >> e.rotation.w >> e.rotation.x >> e.rotation.y >> e.rotation.z
               >> hasH >> e.hp >> e.maxHp >> alive
               >> hasLV >> e.linearVelocity.x >> e.linearVelocity.y >> e.linearVelocity.z
               >> hasAV >> e.angularAxis.x >> e.angularAxis.y >> e.angularAxis.z
               >> e.angularRadians;
            if (!in) break;
            e.name = (name == "-") ? std::string() : name;
            e.hasTransform = hasT != 0;
            e.hasHealth = hasH != 0;
            e.alive = alive != 0;
            e.hasLinearVelocity = hasLV != 0;
            e.hasAngularVelocity = hasAV != 0;
            if (fileVersion >= 2) {
                int hasResource = 0;
                std::size_t abilityCount = 0;
                in >> hasResource >> e.mana >> e.maxMana >> e.stamina >> e.maxStamina
                   >> abilityCount;
                if (!in || abilityCount > 256) break;
                e.hasAbilityResource = hasResource != 0;
                e.abilities.resize(abilityCount);
                for (EntitySaveState::AbilityState& ability : e.abilities)
                    in >> std::quoted(ability.path) >> std::quoted(ability.name)
                       >> ability.cooldown >> ability.recharge >> ability.charges;
                if (!in) break;
            }
            if (fileVersion >= 3)
                in >> std::quoted(e.inventoryState) >> std::quoted(e.questState);
            if (!in) break;
            out.entities.push_back(std::move(e));
        }
        return true;
    }
};

// Read/write the flat key/value store scripts use, so it round-trips through slots.
inline std::unordered_map<std::string, std::string> ReadSaveKeyValues() {
    std::unordered_map<std::string, std::string> values;
    std::ifstream in(SaveKeyValuePath());
    std::string k, v;
    while (in >> std::quoted(k) >> std::quoted(v)) values[std::move(k)] = std::move(v);
    return values;
}

inline void WriteSaveKeyValues(const std::unordered_map<std::string, std::string>& values) {
    std::ofstream out(SaveKeyValuePath(), std::ios::trunc);
    for (const auto& kv : values)
        out << std::quoted(kv.first) << ' ' << std::quoted(kv.second) << '\n';
}

// Snapshot the persistent runtime state of every NAMED entity plus metadata + KV store.
inline SaveGame CaptureSaveGame(ecs::Registry& registry, const std::string& scenePath,
                                const std::string& displayName, float playtimeSeconds,
                                const SaveCapturePolicy& policy = {}) {
    SaveGame save;
    save.displayName = displayName;
    save.scenePath = scenePath;
    save.playtimeSeconds = playtimeSeconds;
    save.timestamp = static_cast<std::int64_t>(std::time(nullptr));
    if (policy.scriptValues) {
        save.values = ReadSaveKeyValues();
        CaptureScriptPersistentStates(registry, save.values);
    }

    registry.view<ecs::RuntimeName>().each(
        [&](ecs::Entity entity, ecs::RuntimeName& runtimeName) {
            if (runtimeName.value.empty()) return;
            EntitySaveState e;
            e.name = runtimeName.value;
            if (policy.transforms) if (const ecs::Transform* t = registry.TryGet<ecs::Transform>(entity)) {
                e.hasTransform = true;
                e.position = t->position; e.scale = t->scale; e.rotation = t->rotation;
            }
            if (policy.health) if (const Health* h = registry.TryGet<Health>(entity)) {
                e.hasHealth = true; e.hp = h->hp; e.maxHp = h->maxHp; e.alive = h->alive;
            }
            if (policy.velocity) if (const ecs::LinearVelocity* lv = registry.TryGet<ecs::LinearVelocity>(entity)) {
                e.hasLinearVelocity = true; e.linearVelocity = lv->velocity;
            }
            if (policy.velocity) if (const ecs::AngularVelocity* av = registry.TryGet<ecs::AngularVelocity>(entity)) {
                e.hasAngularVelocity = true; e.angularAxis = av->axis; e.angularRadians = av->radiansPerSecond;
            }
            if (policy.abilities) if (const AbilityResource* resource = registry.TryGet<AbilityResource>(entity)) {
                e.hasAbilityResource = true;
                e.mana = resource->mana; e.maxMana = resource->maxMana;
                e.stamina = resource->stamina; e.maxStamina = resource->maxStamina;
            }
            if (policy.abilities) if (const AbilityComponent* abilities = registry.TryGet<AbilityComponent>(entity)) {
                for (const AbilitySlot& slot : abilities->abilities)
                    e.abilities.push_back({slot.assetPath, slot.asset.name,
                        slot.cooldownRemaining, slot.rechargeRemaining, slot.charges});
            }
            if (policy.inventory) e.inventoryState = SerializeInventory(registry, entity);
            if (policy.quests) e.questState = SerializeQuestState(registry, entity);
            save.entities.push_back(std::move(e));
        });
    return save;
}

// Re-apply a snapshot onto a freshly loaded scene: matches entities by RuntimeName and
// overwrites the components that were captured. Also restores the flat KV store.
inline void ApplySaveGame(ecs::Registry& registry, const SaveGame& save) {
    WriteSaveKeyValues(save.values);

    std::unordered_map<std::string, const EntitySaveState*> byName;
    byName.reserve(save.entities.size());
    for (const EntitySaveState& e : save.entities)
        if (!e.name.empty()) byName[e.name] = &e;

    registry.view<ecs::RuntimeName>().each(
        [&](ecs::Entity entity, ecs::RuntimeName& runtimeName) {
            const auto it = byName.find(runtimeName.value);
            if (it == byName.end()) return;
            const EntitySaveState& e = *it->second;
            if (e.hasTransform) {
                if (ecs::Transform* t = registry.TryGet<ecs::Transform>(entity)) {
                    t->position = e.position; t->scale = e.scale; t->rotation = e.rotation;
                }
            }
            if (e.hasHealth) {
                if (Health* h = registry.TryGet<Health>(entity)) {
                    h->hp = e.hp; h->maxHp = e.maxHp; h->alive = e.alive; h->justDied = false;
                }
            }
            if (e.hasLinearVelocity) {
                if (ecs::LinearVelocity* lv = registry.TryGet<ecs::LinearVelocity>(entity))
                    lv->velocity = e.linearVelocity;
            }
            if (e.hasAngularVelocity) {
                if (ecs::AngularVelocity* av = registry.TryGet<ecs::AngularVelocity>(entity)) {
                    av->axis = e.angularAxis; av->radiansPerSecond = e.angularRadians;
                }
            }
            if (e.hasAbilityResource) {
                AbilityResource* resource = registry.TryGet<AbilityResource>(entity);
                if (!resource) resource = &registry.Add<AbilityResource>(entity, {});
                resource->mana = e.mana; resource->maxMana = e.maxMana;
                resource->stamina = e.stamina; resource->maxStamina = e.maxStamina;
            }
            for (const EntitySaveState::AbilityState& savedAbility : e.abilities) {
                AbilityComponent* component = registry.TryGet<AbilityComponent>(entity);
                AbilitySlot* slot = nullptr;
                if (component) for (AbilitySlot& candidate : component->abilities)
                    if (candidate.asset.name == savedAbility.name) { slot = &candidate; break; }
                if (!slot && !savedAbility.path.empty()
                    && GrantAbility(registry, entity, savedAbility.path)) {
                    component = registry.TryGet<AbilityComponent>(entity);
                    if (component) for (AbilitySlot& candidate : component->abilities)
                        if (candidate.asset.name == savedAbility.name) { slot = &candidate; break; }
                }
                if (slot) {
                    slot->cooldownRemaining = savedAbility.cooldown;
                    slot->rechargeRemaining = savedAbility.recharge;
                    slot->charges = savedAbility.charges;
                }
            }
            if (!e.inventoryState.empty()) RestoreInventory(registry, entity, e.inventoryState);
            if (!e.questState.empty()) RestoreQuestState(registry, entity, e.questState);
        });
    RestoreScriptPersistentStates(registry, save.values);
}

// ---- Slots ----------------------------------------------------------------------
inline std::string SaveSlotPath(int slot) {
    return "saves/save_" + std::to_string(slot) + ".3dgsave";
}

struct SaveSlotInfo {
    int          slot = 0;
    bool         exists = false;
    std::string  displayName;
    std::string  scenePath;
    std::int64_t timestamp = 0;
    float        playtimeSeconds = 0.0f;
};

// Header-only peek at a slot (does not parse world state).
inline SaveSlotInfo ReadSaveSlotInfo(int slot) {
    SaveSlotInfo info;
    info.slot = slot;
    SaveGame save;
    if (SaveGame::LoadFromFile(SaveSlotPath(slot), save)) {
        info.exists = true;
        info.displayName = save.displayName;
        info.scenePath = save.scenePath;
        info.timestamp = save.timestamp;
        info.playtimeSeconds = save.playtimeSeconds;
    }
    return info;
}

inline std::vector<SaveSlotInfo> ListSaveSlots(int maxSlots = 8) {
    std::vector<SaveSlotInfo> slots;
    slots.reserve(static_cast<std::size_t>(maxSlots));
    for (int i = 0; i < maxSlots; ++i) slots.push_back(ReadSaveSlotInfo(i));
    return slots;
}

inline bool DeleteSaveSlot(int slot) {
    std::error_code ec;
    return std::filesystem::remove(SaveSlotPath(slot), ec);
}

} // namespace engine
