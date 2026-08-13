#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace engine {

// -----------------------------------------------------------------------------
// Level-as-asset streaming data model.
//
// A *world* is an always-resident "persistent level" plus a set of *streamed
// levels*. Each streamed level is a cooked 3DGRuntimeScene placed at a world
// transform; a LevelStreamingManager loads/unloads them around the
// viewer. This header is intentionally data-only and header-only so it can be
// referenced by both the engine runtime and the editor without a link dependency.
// -----------------------------------------------------------------------------

// When a streamed level should be resident.
enum class LevelStreamRule {
    Distance,      // load within loadRadius of the placed bounds, unload past unloadRadius
    AlwaysLoaded,  // resident for the whole session (a second persistent level)
    Manual         // only via explicit LoadLevel/UnloadLevel (doors, transitions, script)
};

// One streamed level reference: which cooked scene, where it sits in the world,
// and how it decides to stream. Bounds are in the level's own local space; the
// streaming manager transforms them by worldTransform for distance tests.
struct LevelRef {
    std::string     scenePath;                 // cooked 3DGRuntimeScene (relative to the world file)
    AssetHandle     sceneId;                   // stable identity (asset registry)
    glm::mat4       worldTransform{1.0f};      // placement applied on instantiation
    LevelStreamRule rule = LevelStreamRule::Distance;
    glm::vec3       boundsMin{0.0f};           // local-space AABB
    glm::vec3       boundsMax{0.0f};
    float           loadRadius   = 60.0f;      // Distance rule: enter to load
    float           unloadRadius = 80.0f;      // Distance rule: exceed to unload (hysteresis)
    bool            enabled = true;            // disabled placements stay authored but never load
    std::string     dataLayer = "Default";     // editor/runtime grouping metadata
    int             partitionX = 0;
    int             partitionZ = 0;
    int             streamingPriority = 0;

    // World-space centre of the placed bounds (for distance tests).
    glm::vec3 WorldBoundsCenter() const {
        const glm::vec3 localCenter = 0.5f * (boundsMin + boundsMax);
        return glm::vec3(worldTransform * glm::vec4(localCenter, 1.0f));
    }
};

struct WorldPartitionSettings {
    bool enabled = false;
    float cellSize = 128.0f;
    glm::vec2 origin{0.0f};
    float defaultLoadRadius = 192.0f;
    float defaultUnloadRadius = 256.0f;
    std::vector<std::string> activeDataLayers;
};

// The authored/cooked world: a persistent level loaded once at boot, plus the
// streamed levels the manager drives.
struct WorldManifest {
    AssetHandle           id;
    std::string           persistentScenePath;  // cooked runtime scene loaded once at boot
    std::vector<LevelRef> levels;
    WorldPartitionSettings partition;

    bool IsValid() const { return !persistentScenePath.empty(); }
};

// ---------------------------- text serialization -----------------------------
//
// Format (whitespace separated), version 3 (versions 1 and 2 remain readable):
//   3DGWorld 3 <assetId|->
//   persistent <quoted path|->
//   levels <count>
//   <quoted scenePath|-> <sceneId|-> <ruleInt> <16 transform floats>
//       <boundsMin xyz> <boundsMax xyz> <loadRadius> <unloadRadius>
//       <enabled> <quoted dataLayer>
//   ... (one line per level)

inline bool SaveWorldManifest(const std::string& path, const WorldManifest& manifest,
                              std::string* error = nullptr) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "could not open '" + path + "' for writing.";
        return false;
    }
    const auto id = [](const AssetHandle& h) { return h.Valid() ? h.ToString() : std::string("-"); };
    const auto tok = [](const std::string& s) { return s.empty() ? std::string("-") : s; };
    out << "3DGWorld 3 " << id(manifest.id) << '\n';
    out << "persistent " << std::quoted(tok(manifest.persistentScenePath)) << '\n';
    out << "partition " << manifest.partition.enabled << ' ' << manifest.partition.cellSize
        << ' ' << manifest.partition.origin.x << ' ' << manifest.partition.origin.y
        << ' ' << manifest.partition.defaultLoadRadius
        << ' ' << manifest.partition.defaultUnloadRadius
        << ' ' << manifest.partition.activeDataLayers.size();
    for (const std::string& layer : manifest.partition.activeDataLayers)
        out << ' ' << std::quoted(tok(layer));
    out << '\n';
    out << "levels " << manifest.levels.size() << '\n';
    for (const LevelRef& level : manifest.levels) {
        out << std::quoted(tok(level.scenePath)) << ' ' << id(level.sceneId) << ' '
            << static_cast<int>(level.rule);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) out << ' ' << level.worldTransform[c][r];
        out << ' ' << level.boundsMin.x << ' ' << level.boundsMin.y << ' ' << level.boundsMin.z
            << ' ' << level.boundsMax.x << ' ' << level.boundsMax.y << ' ' << level.boundsMax.z
            << ' ' << level.loadRadius << ' ' << level.unloadRadius
            << ' ' << level.enabled << ' ' << std::quoted(tok(level.dataLayer))
            << ' ' << level.partitionX << ' ' << level.partitionZ
            << ' ' << level.streamingPriority << '\n';
    }
    if (!out) { if (error) *error = "write failed for '" + path + "'."; return false; }
    return true;
}

inline bool LoadWorldManifest(const std::string& path, WorldManifest* outManifest,
                              std::string* error = nullptr) {
    if (!outManifest) {
        if (error) *error = "world manifest output is null.";
        return false;
    }
    std::ifstream in(path);
    if (!in) { if (error) *error = "could not open '" + path + "'."; return false; }
    std::string magic, idText;
    int version = 0;
    in >> magic >> version >> idText;
    if (magic != "3DGWorld" || version < 1 || version > 3) {
        if (error) *error = "not a recognised .3dgworld file.";
        return false;
    }
    WorldManifest manifest;
    if (idText != "-" && !AssetHandle::Parse(idText, &manifest.id)) {
        if (error) *error = "world manifest has an invalid asset ID.";
        return false;
    }

    std::string key, persistent;
    if (!(in >> key >> std::quoted(persistent)) || key != "persistent") {
        if (error) *error = "world manifest has no persistent level record.";
        return false;
    }
    manifest.persistentScenePath = (persistent == "-") ? std::string() : persistent;

    if (version >= 3) {
        std::size_t layerCount = 0;
        if (!(in >> key >> manifest.partition.enabled >> manifest.partition.cellSize
              >> manifest.partition.origin.x >> manifest.partition.origin.y
              >> manifest.partition.defaultLoadRadius
              >> manifest.partition.defaultUnloadRadius >> layerCount)
            || key != "partition" || layerCount > 256) {
            if (error) *error = "world manifest partition settings are invalid.";
            return false;
        }
        for (std::size_t i = 0; i < layerCount; ++i) {
            std::string layer; in >> std::quoted(layer);
            manifest.partition.activeDataLayers.push_back(layer == "-" ? "Default" : layer);
        }
    }

    std::size_t count = 0;
    if (!(in >> key >> count) || key != "levels" || count > 4096) {
        if (error) *error = "world manifest level count is invalid.";
        return false;
    }
    manifest.levels.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        LevelRef level;
        std::string scenePath, sceneIdText;
        int ruleInt = 0;
        in >> std::quoted(scenePath) >> sceneIdText >> ruleInt;
        level.scenePath = (scenePath == "-") ? std::string() : scenePath;
        if (sceneIdText != "-"
            && !AssetHandle::Parse(sceneIdText, &level.sceneId)) {
            if (error) *error = "world level has an invalid scene asset ID.";
            return false;
        }
        level.rule = static_cast<LevelStreamRule>(std::clamp(ruleInt, 0, 2));
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) in >> level.worldTransform[c][r];
        in >> level.boundsMin.x >> level.boundsMin.y >> level.boundsMin.z
           >> level.boundsMax.x >> level.boundsMax.y >> level.boundsMax.z
           >> level.loadRadius >> level.unloadRadius;
        if (version >= 2) {
            std::string layer;
            in >> level.enabled >> std::quoted(layer);
            level.dataLayer = layer == "-" ? "Default" : layer;
        }
        if (version >= 3)
            in >> level.partitionX >> level.partitionZ >> level.streamingPriority;
        manifest.levels.push_back(level);
    }
    if (!in || manifest.persistentScenePath.empty()) {
        if (error) *error = "malformed .3dgworld file.";
        return false;
    }
    *outManifest = std::move(manifest);
    return true;
}

} // namespace engine
