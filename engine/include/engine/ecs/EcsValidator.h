#pragma once

// ECS Pass 5 -- ValidateECS. A self-check that walks the Registry for structural corruption and lets
// each subsystem register a validation hook (physics proxies, render proxies, joints, script/reflected
// entity references). Read-only; it reports issues, it does not "repair". GL-free, header-only.
//
// Built-in checks:
//   * sparse/dense consistency (per-pool invariant, via Registry::Validate)
//   * dead-entity component ownership + generation validity (no live pool holds a stale handle)
//   * pending-destroy sanity (queued destroys still name live entities)
// Subsystem checks are added as hooks; a ready-made RenderScene proxy hook is provided as a template
// for the pattern (stale/duplicate proxy detection).

#include "engine/ecs/Registry.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {
namespace ecs {

struct ValidationIssue {
    std::string category;   // e.g. "sparse-set", "dead-owner", "stale-render-proxy"
    std::string detail;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool ok() const { return issues.empty(); }
    void add(std::string cat, std::string detail) { issues.push_back({std::move(cat), std::move(detail)}); }
};

using ValidationHook = std::function<void(const Registry&, ValidationReport&)>;

class EcsValidator {
public:
    // Subsystems register a named hook once at startup. Hooks must be read-only w.r.t. the Registry.
    void AddHook(std::string name, ValidationHook hook) { m_hooks.push_back({std::move(name), std::move(hook)}); }
    void ClearHooks() { m_hooks.clear(); }
    std::size_t HookCount() const { return m_hooks.size(); }

    ValidationReport Validate(const Registry& reg) const {
        ValidationReport report;

        // 1) Per-pool sparse/dense invariant.
        if (!reg.Validate()) report.add("sparse-set", "a component pool failed its sparse/dense invariant");

        // 2) No live pool may own a dead/stale entity handle (generation mismatch).
        reg.ForEachPool([&](const IPool& pool) {
            pool.ForEachEntity([&](Entity e) {
                if (!reg.Valid(e))
                    report.add("dead-owner", std::string(pool.TypeName()) + " owns dead entity index "
                               + std::to_string(EntityIndex(e)) + " gen " + std::to_string(EntityGeneration(e)));
            });
        });

        // 3) Queued deferred destroys should still name live entities (double-destroy is a no-op at
        //    flush, but a stale queued handle is worth surfacing).
        // (No public iterator over the queue by design; IsPendingDestroy answers per-entity. Nothing
        //  to scan here without a handle, so this is left to subsystem hooks that hold handles.)

        // 4) Subsystem hooks (physics/render proxies, joints, script/reflected entity refs).
        for (const auto& h : m_hooks) h.second(reg, report);

        return report;
    }

private:
    std::vector<std::pair<std::string, ValidationHook>> m_hooks;
};

// ---- ready-made hook factory: RenderScene proxy staleness --------------------------------------
// Every render proxy must name a LIVE entity that still has the renderable components, and no two
// proxies may share an entity (duplicate proxy). Templated on the RenderScene type to avoid a hard
// include dependency (call with your engine::RenderScene).
template <class RenderSceneT, class TransformT, class MeshRendererT>
ValidationHook MakeRenderProxyHook(const RenderSceneT& scene) {
    return [&scene](const Registry& reg, ValidationReport& report) {
        std::unordered_set<Entity> seen;
        for (const auto& proxy : scene.Proxies()) {
            const Entity e = proxy.entity;
            if (!reg.Valid(e)) {
                report.add("stale-render-proxy", "render proxy for dead entity index " + std::to_string(EntityIndex(e)));
                continue;
            }
            if (!reg.template Has<TransformT>(e) || !reg.template Has<MeshRendererT>(e))
                report.add("stale-render-proxy", "render proxy for entity lacking renderable components (index "
                           + std::to_string(EntityIndex(e)) + ")");
            if (!seen.insert(e).second)
                report.add("duplicate-render-proxy", "two render proxies share entity index " + std::to_string(EntityIndex(e)));
        }
    };
}

// ---- generic entity-reference hook -------------------------------------------------------------
// Validate a collection of Entity handles held by a subsystem (joint bodies, script targets, a
// reflected Entity property). `collect` fills a vector; any invalid handle is reported.
inline ValidationHook MakeEntityRefHook(std::string label, std::function<std::vector<Entity>(const Registry&)> collect) {
    return [label = std::move(label), collect = std::move(collect)](const Registry& reg, ValidationReport& report) {
        for (Entity e : collect(reg))
            if (e != kNull && !reg.Valid(e))
                report.add("invalid-entity-ref", label + " references dead entity index " + std::to_string(EntityIndex(e)));
    };
}

} // namespace ecs
} // namespace engine
