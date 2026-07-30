#include "engine/gameplay/RagdollSystem.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/animation/Animator.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/graphics/SkinnedModel.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsWorld.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace engine {
namespace {

glm::mat4 BoneWorld(const glm::mat4& characterWorld,
                    const AnimatedModel& animated, int bone) {
    const Bone& definition =
        animated.model->GetSkeleton().bones[static_cast<std::size_t>(bone)];
    return characterWorld
        * animated.pose[static_cast<std::size_t>(bone)]
        * glm::inverse(definition.offset);
}

glm::quat RotationOf(const glm::mat4& matrix) {
    glm::vec3 x(matrix[0]), y(matrix[1]), z(matrix[2]);
    const float lx = std::max(glm::length(x), 1.0e-6f);
    const float ly = std::max(glm::length(y), 1.0e-6f);
    const float lz = std::max(glm::length(z), 1.0e-6f);
    return glm::normalize(glm::quat_cast(
        glm::mat3(x / lx, y / ly, z / lz)));
}

glm::quat RotationFromUp(const glm::vec3& direction) {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float cosine = glm::clamp(glm::dot(up, direction), -1.0f, 1.0f);
    if (cosine > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (cosine < -0.9999f)
        return glm::angleAxis(
            std::acos(-1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::normalize(glm::angleAxis(
        std::acos(cosine), glm::normalize(glm::cross(up, direction))));
}

bool ImportantBone(const std::string& source) {
    std::string name = source;
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr const char* words[] = {
        "pelvis", "hip", "spine", "chest", "neck", "head",
        "upperarm", "lowerarm", "forearm", "hand",
        "thigh", "calf", "shin", "leg", "foot"
    };
    for (const char* word : words)
        if (name.find(word) != std::string::npos) return true;
    return false;
}

void ActivateFallback(ecs::Registry& registry, ecs::Entity entity,
                      Ragdoll& ragdoll) {
    ecs::RigidBody body = ecs::RigidBody::Dynamic(
        std::max(ragdoll.totalMass, 1.0f));
    body.linearDamping = ragdoll.linearDamping;
    body.angularDamping = ragdoll.angularDamping;
    body.angularVelocity = glm::vec3(
        ragdoll.deathImpulse * 0.35f, 0.0f, ragdoll.deathImpulse);
    registry.Add<ecs::RigidBody>(entity, body);
    ragdoll.active = true;
}

void Activate(ecs::Registry& registry, PhysicsWorld& physics,
              ecs::Entity owner, Ragdoll& ragdoll) {
    ecs::Transform* character = registry.TryGet<ecs::Transform>(owner);
    AnimatedModel* animated = registry.TryGet<AnimatedModel>(owner);
    if (!character || !animated || !animated->model
        || animated->model->GetSkeleton().bones.empty()) {
        ActivateFallback(registry, owner, ragdoll);
        return;
    }

    const Skeleton& skeleton = animated->model->GetSkeleton();
    if (animated->pose.size() != skeleton.bones.size())
        Animator::ComputeBindPose(skeleton, animated->pose);
    if (animated->pose.size() != skeleton.bones.size()) {
        ActivateFallback(registry, owner, ragdoll);
        return;
    }

    const glm::mat4 characterWorld =
        character->Model() * animated->renderOffset;
    std::vector<glm::mat4> boneWorld(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        boneWorld[i] = BoneWorld(
            characterWorld, *animated, static_cast<int>(i));

    struct Candidate {
        int bone = -1;
        float length = 0.0f;
        bool important = false;
    };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const int parent = skeleton.bones[i].parent;
        if (parent < 0) continue;
        const float length = glm::distance(
            glm::vec3(boneWorld[i][3]),
            glm::vec3(boneWorld[static_cast<std::size_t>(parent)][3]));
        if (length > 0.025f)
            candidates.push_back({
                static_cast<int>(i), length,
                ImportantBone(skeleton.bones[i].name)});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.important != b.important) return a.important > b.important;
            return a.length > b.length;
        });
    const std::size_t limit = static_cast<std::size_t>(
        std::clamp(ragdoll.maxBodies, 4, 32));
    if (candidates.size() > limit) candidates.resize(limit);
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.bone < b.bone;
        });
    if (candidates.size() < 2) {
        ActivateFallback(registry, owner, ragdoll);
        return;
    }

    glm::vec3 inheritedVelocity(0.0f);
    if (const ecs::RigidBody* source =
            registry.TryGet<ecs::RigidBody>(owner))
        inheritedVelocity = source->velocity;
    if (ecs::Collider* rootCollider =
            registry.TryGet<ecs::Collider>(owner)) {
        ragdoll.rootColliderWasTrigger = rootCollider->isTrigger;
        ragdoll.rootColliderMask = rootCollider->mask;
        rootCollider->isTrigger = true;
        rootCollider->mask = 0;
    }
    if (registry.Has<ecs::RigidBody>(owner))
        registry.Remove<ecs::RigidBody>(owner);

    ragdoll.parts.clear();
    std::vector<glm::mat4> bodyWorld;
    std::vector<int> partForBone(skeleton.bones.size(), -1);
    const float partMass =
        std::max(ragdoll.totalMass, 1.0f)
        / static_cast<float>(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const Candidate& candidate = candidates[index];
        const int parent = skeleton.bones[
            static_cast<std::size_t>(candidate.bone)].parent;
        const glm::vec3 a =
            glm::vec3(boneWorld[static_cast<std::size_t>(parent)][3]);
        const glm::vec3 b =
            glm::vec3(boneWorld[static_cast<std::size_t>(candidate.bone)][3]);
        const glm::vec3 direction = b - a;
        const float length = std::max(glm::length(direction), 0.03f);
        const glm::quat rotation = RotationFromUp(direction / length);

        ecs::Transform transform;
        transform.position = (a + b) * 0.5f;
        transform.rotation = rotation;
        transform.scale = glm::vec3(1.0f);
        const ecs::Entity partEntity = registry.Create();
        registry.Add<ecs::Transform>(partEntity, transform);

        const float radius = std::clamp(
            length * std::max(ragdoll.bodyRadiusScale, 0.04f),
            0.035f, 0.24f);
        ecs::Collider collider = ecs::Collider::MakeCapsule(
            radius, std::max(length * 0.5f - radius, 0.01f));
        collider.layer = ecs::CollisionLayer::WorldDynamic;
        collider.mask = ecs::CollisionLayer::Default
            | ecs::CollisionLayer::WorldStatic
            | ecs::CollisionLayer::CameraBlocker;
        collider.friction = 0.72f;
        collider.restitution = 0.05f;
        registry.Add<ecs::Collider>(partEntity, collider);

        ecs::RigidBody body = ecs::RigidBody::Dynamic(partMass);
        body.velocity = inheritedVelocity;
        body.linearDamping = std::max(ragdoll.linearDamping, 0.0f);
        body.angularDamping = std::max(ragdoll.angularDamping, 0.0f);
        const float sign = (index & 1u) ? -1.0f : 1.0f;
        body.angularVelocity = glm::vec3(
            0.35f * sign, 0.2f,
            sign * std::max(ragdoll.deathImpulse, 0.0f));
        registry.Add<ecs::RigidBody>(partEntity, body);

        partForBone[static_cast<std::size_t>(candidate.bone)] =
            static_cast<int>(ragdoll.parts.size());
        ragdoll.parts.push_back({
            partEntity, candidate.bone, -1});
        bodyWorld.push_back(transform.Model());
    }

    for (std::size_t i = 0; i < ragdoll.parts.size(); ++i) {
        const int bone = ragdoll.parts[i].bone;
        int ancestor = skeleton.bones[static_cast<std::size_t>(bone)].parent;
        while (ancestor >= 0
               && partForBone[static_cast<std::size_t>(ancestor)] < 0)
            ancestor =
                skeleton.bones[static_cast<std::size_t>(ancestor)].parent;
        if (ancestor < 0) continue;
        const int parentPart =
            partForBone[static_cast<std::size_t>(ancestor)];
        ragdoll.parts[i].parentPart = parentPart;
        const glm::vec3 joint =
            glm::vec3(boneWorld[static_cast<std::size_t>(
                skeleton.bones[static_cast<std::size_t>(bone)].parent)][3]);
        const glm::vec3 localParent =
            glm::vec3(glm::inverse(bodyWorld[
                static_cast<std::size_t>(parentPart)])
                * glm::vec4(joint, 1.0f));
        const glm::vec3 localChild =
            glm::vec3(glm::inverse(bodyWorld[i])
                * glm::vec4(joint, 1.0f));
        physics.AddBallJoint(
            ragdoll.parts[static_cast<std::size_t>(parentPart)].entity,
            ragdoll.parts[i].entity, localParent, localChild);
    }

    ragdoll.boneDrivers.assign(skeleton.bones.size(), -1);
    ragdoll.boneFromBody.assign(
        skeleton.bones.size(), glm::mat4(1.0f));
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
        int cursor = static_cast<int>(bone);
        int driver = -1;
        while (cursor >= 0) {
            driver = partForBone[static_cast<std::size_t>(cursor)];
            if (driver >= 0) break;
            cursor = skeleton.bones[
                static_cast<std::size_t>(cursor)].parent;
        }
        if (driver < 0) {
            // Root/helper bones use the closest first body.
            driver = 0;
        }
        ragdoll.boneDrivers[bone] = driver;
        ragdoll.boneFromBody[bone] =
            glm::inverse(bodyWorld[static_cast<std::size_t>(driver)])
            * boneWorld[bone];
    }
    ragdoll.active = true;
}

} // namespace

void UpdateRagdollsBeforePhysics(
    ecs::Registry& registry, PhysicsWorld& physics) {
    registry.view<Health, Ragdoll>().each(
        [&](ecs::Entity entity, Health& health, Ragdoll& ragdoll) {
            if (!ragdoll.enabled || ragdoll.active
                || !ragdoll.activateOnDeath)
                return;
            if (health.justDied || !health.alive || health.hp <= 0.0f)
                Activate(registry, physics, entity, ragdoll);
        });
}

void UpdateRagdollsAfterPhysics(ecs::Registry& registry) {
    registry.view<ecs::Transform, AnimatedModel, Ragdoll>().each(
        [&](ecs::Entity, ecs::Transform& character,
            AnimatedModel& animated, Ragdoll& ragdoll) {
            if (!ragdoll.active || ragdoll.parts.empty()
                || !animated.model)
                return;
            const Skeleton& skeleton =
                animated.model->GetSkeleton();
            if (ragdoll.boneDrivers.size() != skeleton.bones.size()
                || ragdoll.boneFromBody.size() != skeleton.bones.size())
                return;
            const glm::mat4 inverseCharacter = glm::inverse(
                character.Model() * animated.renderOffset);
            animated.pose.resize(skeleton.bones.size());
            for (std::size_t bone = 0;
                 bone < skeleton.bones.size(); ++bone) {
                const int driver = ragdoll.boneDrivers[bone];
                if (driver < 0
                    || driver >= static_cast<int>(ragdoll.parts.size()))
                    continue;
                const ecs::Transform* body =
                    registry.TryGet<ecs::Transform>(
                        ragdoll.parts[static_cast<std::size_t>(driver)].entity);
                if (!body) continue;
                const glm::mat4 boneWorld =
                    body->Model() * ragdoll.boneFromBody[bone];
                animated.pose[bone] = inverseCharacter * boneWorld
                    * skeleton.bones[bone].offset;
            }
        });
}

} // namespace engine
