#include "engine/gameplay/GameplaySystems.h"

#include "engine/gameplay/GameplayComponents.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/animation/AnimatedModel.h"
#include "engine/graphics/SkinnedModel.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace engine {

void UpdateHealth(ecs::Registry& reg) {
    reg.view<Health>().each([](ecs::Entity, Health& h) {
        h.justDied = false;
        if (h.alive && h.hp <= 0.0f) { h.hp = 0.0f; h.alive = false; h.justDied = true; }
    });
}

std::vector<ProjectileHit> UpdateProjectiles(ecs::Registry& reg, float dt) {
    std::vector<ProjectileHit> hits;
    std::vector<ecs::Entity>   spent;

    reg.view<ecs::Transform, Projectile>().each([&](ecs::Entity pe, ecs::Transform& pt, Projectile& pr) {
        // Advance.
        const float step = pr.speed * dt;
        pt.position += pr.dir * step;
        pr.traveled += step;

        // Overlap test against Health targets (nearest within radius wins).
        ecs::Entity best = ecs::kNull; float bestD = pr.radius;
        reg.view<ecs::Transform, Health>().each([&](ecs::Entity te, ecs::Transform& tt, Health& th) {
            if (te == pr.owner || !th.alive) return;
            const float d = glm::length(pt.position - tt.position);
            if (d < bestD) { bestD = d; best = te; }
        });

        if (best != ecs::kNull) {
            Health& th = reg.Get<Health>(best);
            th.Damage(pr.damage);
            ProjectileHit hit;
            hit.projectile = pe; hit.target = best; hit.point = pt.position;
            hit.damage = pr.damage; hit.lethal = (th.hp <= 0.0f);
            hits.push_back(hit);
            spent.push_back(pe);
        } else if (pr.traveled >= pr.range) {
            spent.push_back(pe);
        }
    });

    for (ecs::Entity e : spent) if (reg.Valid(e)) reg.Destroy(e);
    return hits;
}

void UpdateAttachments(ecs::Registry& reg) {
    reg.view<ecs::Transform, Attachment>().each([&](ecs::Entity, ecs::Transform& t, Attachment& a) {
        if (!reg.Valid(a.parent)) return;
        const ecs::Transform* pt = reg.TryGet<ecs::Transform>(a.parent);
        if (!pt) return;

        glm::mat4 parentWorld = pt->Model();
        if (a.boneIndex >= 0) {
            if (const AnimatedModel* pm = reg.TryGet<AnimatedModel>(a.parent)) {
                if (pm->model && a.boneIndex < static_cast<int>(pm->pose.size())) {
                    const glm::mat4 offInv = 
                        glm::inverse(pm->model->GetSkeleton().bones[static_cast<std::size_t>(a.boneIndex)].offset);
                    parentWorld = pt->Model() * pm->pose[static_cast<std::size_t>(a.boneIndex)] * offInv;
                }
            }
        }

        const glm::mat4 w = parentWorld * a.offset;
        // Decompose (rigid + uniform scale) into the child Transform.
        const glm::vec3 T(w[3]);
        const glm::vec3 c0(w[0]), c1(w[1]), c2(w[2]);
        const glm::vec3 S(glm::length(c0), glm::length(c1), glm::length(c2));
        const glm::mat3 R(c0 / std::max(S.x, 1e-8f), c1 / std::max(S.y, 1e-8f), c2 / std::max(S.z, 1e-8f));
        t.position = T;
        t.scale    = S;
        t.rotation = glm::quat_cast(R);
    });
}

} // namespace engine