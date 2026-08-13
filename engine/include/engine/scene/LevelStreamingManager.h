#pragma once

#include "engine/scene/WorldManifest.h"
#include "engine/scene/RuntimeSceneLoader.h"
#include "engine/assets/RuntimeAssetManager.h"
#include "engine/ecs/Registry.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <limits>
#include <unordered_set>
#include <string>
#include <utility>
#include <vector>

namespace engine {

// -----------------------------------------------------------------------------
// Level-as-asset streaming runtime.
//
// Drives load/unload of a world's streamed levels around a viewer. Each level is a
// cooked 3DGRuntimeScene instantiated into the shared ECS registry at its world
// offset; the entities it creates are tracked as a group so unload destroys exactly
// that set (colliders/renderers vanish with the entities, since physics is
// registry-driven -- no explicit deregistration).
//
// Assets are load-and-keep (unload frees entities, not the shared GPU asset cache);
// at most one (de)activation runs per Update() to avoid a border-crossing hitch.
// -----------------------------------------------------------------------------
class LevelStreamingManager {
public:
    enum class State { Unloaded, Active };

    struct StreamedLevel {
        LevelRef                 ref;
        State                    state = State::Unloaded;
        RuntimeSceneLoader::Scene scene;       // translated runtime metadata while active
        std::vector<ecs::Entity> created;   // entities this level owns (destroyed on unload)
    };

    // Called after a level's entities are instantiated + resolved, so the host can run
    // its per-entity setup (e.g. MeshPBR conversion, animated-model hookup, per-level
    // nav bake) scoped to just the new entities.
    using ActivateHook = std::function<void(
        std::size_t levelIndex,
        const RuntimeSceneLoader::Scene& scene,
        const std::vector<ecs::Entity>& newEntities)>;
    using BeforeDeactivateHook = std::function<void(
        std::size_t levelIndex,
        const RuntimeSceneLoader::Scene& scene,
        const std::vector<ecs::Entity>& entities)>;
    using DeactivateHook = std::function<void(std::size_t levelIndex)>;

    // 'worldDir' is the directory the level scene paths are resolved against.
    void Configure(const WorldManifest& manifest, std::string worldDir) {
        m_worldDir = std::move(worldDir);
        m_levels.clear();
        m_levels.reserve(manifest.levels.size());
        for (const LevelRef& ref : manifest.levels) {
            StreamedLevel level;
            level.ref = ref;
            m_levels.push_back(std::move(level));
        }
        SetActiveDataLayers(manifest.partition.enabled
            ? manifest.partition.activeDataLayers : std::vector<std::string>{});
    }

    void SetActiveDataLayers(const std::vector<std::string>& layers) {
        m_activeDataLayers.clear();
        m_activeDataLayers.insert(layers.begin(), layers.end());
    }

    void SetActivateHook(ActivateHook hook) { m_onActivate = std::move(hook); }
    void SetBeforeDeactivateHook(BeforeDeactivateHook hook) {
        m_beforeDeactivate = std::move(hook);
    }
    void SetDeactivateHook(DeactivateHook hook) { m_onDeactivate = std::move(hook); }

    // Per-frame streaming. Unloads take priority (free before you load), and at most one
    // (de)activation happens per call so a border crossing never stalls a frame.
    void Update(const glm::vec3& viewerPos, ecs::Registry& registry,
                RuntimeAssetManager& assets, const RuntimeSceneLoader::PrimitiveMeshes& meshes) {
        for (std::size_t i = 0; i < m_levels.size(); ++i) {
            StreamedLevel& level = m_levels[i];
            if (level.state == State::Active && ShouldUnload(level, viewerPos)) {
                Deactivate(i, level, registry);
                return;
            }
        }
        std::size_t candidate = m_levels.size();
        int bestPriority = std::numeric_limits<int>::min();
        for (std::size_t i = 0; i < m_levels.size(); ++i) {
            StreamedLevel& level = m_levels[i];
            if (level.state == State::Unloaded && ShouldLoad(level, viewerPos)
                && level.ref.streamingPriority > bestPriority) {
                candidate = i; bestPriority = level.ref.streamingPriority;
            }
        }
        if (candidate < m_levels.size())
            Activate(candidate, m_levels[candidate], registry, assets, meshes);
    }

    // Explicit control for Manual-rule levels, doors, and script transitions.
    bool LoadLevel(std::size_t index, ecs::Registry& registry, RuntimeAssetManager& assets,
                   const RuntimeSceneLoader::PrimitiveMeshes& meshes) {
        if (index >= m_levels.size()) return false;
        StreamedLevel& level = m_levels[index];
        if (!level.ref.enabled) return false;
        if (level.state == State::Active) return true;
        return Activate(index, level, registry, assets, meshes);
    }
    bool UnloadLevel(std::size_t index, ecs::Registry& registry) {
        if (index >= m_levels.size()) return false;
        StreamedLevel& level = m_levels[index];
        if (level.state != State::Active) return true;
        Deactivate(index, level, registry);
        return true;
    }

    void UnloadAll(ecs::Registry& registry) {
        for (std::size_t i = 0; i < m_levels.size(); ++i) {
            StreamedLevel& level = m_levels[i];
            if (level.state == State::Active) Deactivate(i, level, registry);
        }
    }

    const std::vector<StreamedLevel>& Levels() const { return m_levels; }
    std::size_t ActiveCount() const {
        std::size_t n = 0;
        for (const StreamedLevel& level : m_levels) if (level.state == State::Active) ++n;
        return n;
    }
    const std::string& LastError() const { return m_lastError; }

    // Pure policy helper used by tooling/tests and by the runtime loops below.
    // Manual levels keep their current state until LoadLevel/UnloadLevel is called.
    static bool WantsResident(const LevelRef& ref, bool currentlyActive,
                              const glm::vec3& viewer) {
        if (!ref.enabled) return false;
        switch (ref.rule) {
            case LevelStreamRule::AlwaysLoaded:
                return true;
            case LevelStreamRule::Manual:
                return currentlyActive;
            case LevelStreamRule::Distance:
            default: {
                const float radius = currentlyActive
                    ? std::max(ref.unloadRadius, ref.loadRadius)
                    : std::max(ref.loadRadius, 0.0f);
                return glm::distance(viewer, ref.WorldBoundsCenter()) <= radius;
            }
        }
    }

private:
    bool ShouldLoad(const StreamedLevel& level, const glm::vec3& viewer) const {
        return LayerEnabled(level.ref) && WantsResident(level.ref, false, viewer);
    }
    bool ShouldUnload(const StreamedLevel& level, const glm::vec3& viewer) const {
        return !LayerEnabled(level.ref) || !WantsResident(level.ref, true, viewer);
    }
    bool LayerEnabled(const LevelRef& ref) const {
        return m_activeDataLayers.empty() || ref.dataLayer.empty()
            || m_activeDataLayers.count(ref.dataLayer) != 0;
    }

    struct Placement {
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    static Placement DecomposePlacement(const glm::mat4& transform) {
        Placement result;
        glm::mat3 basis(transform);
        for (int c = 0; c < 3; ++c) {
            result.scale[c] = glm::length(basis[c]);
            if (result.scale[c] > 0.000001f) basis[c] /= result.scale[c];
            else basis[c] = glm::mat3(1.0f)[c];
        }
        if (glm::determinant(basis) < 0.0f) {
            result.scale.x = -result.scale.x;
            basis[0] = -basis[0];
        }
        result.rotation = glm::normalize(glm::quat_cast(basis));
        return result;
    }

    static glm::vec3 TransformPoint(
        const glm::mat4& transform, const glm::vec3& point) {
        return glm::vec3(transform * glm::vec4(point, 1.0f));
    }

    static glm::vec3 TransformDirection(
        const glm::quat& rotation, const glm::vec3& direction) {
        if (glm::dot(direction, direction) <= 0.000001f) return direction;
        return glm::normalize(rotation * direction);
    }

    static void ApplyWorldTransform(RuntimeSceneLoader::Scene& scene,
                                    ecs::Registry& registry,
                                    const std::vector<ecs::Entity>& created,
                                    const glm::mat4& transform) {
        const Placement placement = DecomposePlacement(transform);
        const float uniformScale =
            (glm::abs(placement.scale.x) + glm::abs(placement.scale.y)
             + glm::abs(placement.scale.z)) / 3.0f;
        for (ecs::Entity entity : created) {
            if (!registry.Valid(entity)) continue;
            if (ecs::Transform* t = registry.TryGet<ecs::Transform>(entity)) {
                t->position = TransformPoint(transform, t->position);
                t->rotation = glm::normalize(placement.rotation * t->rotation);
                t->scale *= placement.scale;
            }
            if (ecs::Mover* mover = registry.TryGet<ecs::Mover>(entity)) {
                const glm::vec3 scaledAxis =
                    glm::mat3(transform) * mover->axis;
                const float axisScale = glm::length(scaledAxis);
                mover->axis = axisScale > 0.000001f
                    ? scaledAxis / axisScale : mover->axis;
                mover->origin = TransformPoint(transform, mover->origin);
                mover->distance *= axisScale;
            }
            if (ecs::Rotator* rotator = registry.TryGet<ecs::Rotator>(entity))
                rotator->axis =
                    TransformDirection(placement.rotation, rotator->axis);
            if (ecs::Light* light = registry.TryGet<ecs::Light>(entity))
                light->direction =
                    TransformDirection(placement.rotation, light->direction);
        }

        for (auto& entity : scene.entities) {
            entity.position = TransformPoint(transform, entity.position);
            entity.rotation =
                glm::normalize(placement.rotation * entity.rotation);
            entity.scale *= placement.scale;
            const glm::vec3 scaledAxis =
                glm::mat3(transform) * entity.mover.axis;
            const float axisScale = glm::length(scaledAxis);
            entity.mover.axis = axisScale > 0.000001f
                ? scaledAxis / axisScale : entity.mover.axis;
            entity.mover.origin =
                TransformPoint(transform, entity.mover.origin);
            entity.mover.distance *= axisScale;
        }
        for (auto& light : scene.lights) {
            light.position = TransformPoint(transform, light.position);
            light.light.direction =
                TransformDirection(placement.rotation, light.light.direction);
        }
        for (auto& bounds : scene.navBounds) {
            bounds.position = TransformPoint(transform, bounds.position);
            bounds.rotation =
                glm::normalize(placement.rotation * bounds.rotation);
            bounds.scale *= glm::abs(placement.scale);
        }
        for (auto& water : scene.waters) {
            water.center = TransformPoint(transform, water.center);
            water.size *= std::max(
                (glm::abs(placement.scale.x) + glm::abs(placement.scale.z)) * 0.5f,
                0.0001f);
            const glm::vec3 flow = TransformDirection(
                placement.rotation, glm::vec3(water.flowDir.x, 0.0f, water.flowDir.y));
            water.flowDir = glm::vec2(flow.x, flow.z);
            const float flowLength = glm::length(water.flowDir);
            if (flowLength > 0.0001f) water.flowDir /= flowLength;
        }
        for (auto& agent : scene.navAgents)
            for (glm::vec3& point : agent.patrolPoints)
                point = TransformPoint(transform, point);
        for (auto& joint : scene.physicsJoints) {
            if (joint.worldAnchor)
                joint.anchor = TransformPoint(transform, joint.anchor);
            joint.restLength *= uniformScale;
        }
        for (auto& camera : scene.cameraPresets) {
            camera.position = TransformPoint(transform, camera.position);
            camera.target = TransformPoint(transform, camera.target);
        }
    }

    bool Activate(std::size_t index, StreamedLevel& level,
                  ecs::Registry& registry, RuntimeAssetManager& assets,
                  const RuntimeSceneLoader::PrimitiveMeshes& meshes) {
        const std::string path = ResolvePath(level.ref.scenePath);
        RuntimeSceneLoader::Scene scene;
        std::string err;
        if (!RuntimeSceneLoader::Load(path, &scene, &err)) {
            m_lastError = "level load failed (" + path + "): " + err;
            return false;
        }
        // Environment/game-mode state belongs to the persistent level. Prevent a
        // streamed scene from injecting a second procedural sun while retaining
        // any explicit authored lights in that level.
        scene.environment.driveSunLight = false;
        if (!RuntimeSceneLoader::Instantiate(
                scene, registry, meshes, &level.created, &err)) {
            m_lastError = "level instantiate failed (" + path + "): " + err;
            // Roll back any partial entities so a bad level cannot leak.
            for (ecs::Entity e : level.created) if (registry.Valid(e)) registry.Destroy(e);
            level.created.clear();
            return false;
        }
        ApplyWorldTransform(
            scene, registry, level.created, level.ref.worldTransform);
        level.scene = std::move(scene);
        assets.ResolveRegistryAssets(registry);   // load-and-keep; idempotent on existing entities
        if (m_onActivate) m_onActivate(index, level.scene, level.created);
        level.state = State::Active;
        m_lastError.clear();
        return true;
    }

    void Deactivate(std::size_t index, StreamedLevel& level, ecs::Registry& registry) {
        if (m_beforeDeactivate)
            m_beforeDeactivate(index, level.scene, level.created);
        for (ecs::Entity e : level.created) {
            if (registry.Valid(e)) registry.Destroy(e);
        }
        level.created.clear();
        level.scene = RuntimeSceneLoader::Scene{};
        level.state = State::Unloaded;
        if (m_onDeactivate) m_onDeactivate(index);
    }

    std::string ResolvePath(const std::string& rel) const {
        if (m_worldDir.empty() || rel.empty()) return rel;
        const char back = m_worldDir.back();
        const std::string sep = (back == '/' || back == '\\') ? "" : "/";
        return m_worldDir + sep + rel;
    }

    std::vector<StreamedLevel> m_levels;
    std::string                m_worldDir;
    std::string                m_lastError;
    std::unordered_set<std::string> m_activeDataLayers;
    ActivateHook               m_onActivate;
    BeforeDeactivateHook       m_beforeDeactivate;
    DeactivateHook             m_onDeactivate;
};

} // namespace engine
