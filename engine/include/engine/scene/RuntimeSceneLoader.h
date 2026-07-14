#pragma once

#include "engine/ecs/Entity.h"
#include "engine/ecs/Components.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/Script.h"
#include "engine/physics/PhysicsComponents.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace engine {

class Mesh;

namespace ecs {
class Registry;
}

class RuntimeSceneLoader {
public:
    struct AnimationEventDesc {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
    };

    struct AnimationActionProfileDesc {
        std::string name;
        int clipIndex = 0;
        std::string clipName;
        std::string maskRootBone;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f;
    };

    struct AnimationStateDesc {
        std::string name;
        int clipIndex = 0;
        std::string clipName;
        bool loop = true;
        float speed = 1.0f;
    };

    struct AnimationTransitionDesc {
        std::string fromState;
        std::string toState;
        std::string parameter = "Speed";
        int compare = 0;
        float threshold = 0.0f;
        float fade = 0.2f;
    };

    struct EntityDesc {
        std::string primitive;
        std::string name;
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f};
        std::string modelPath;
        std::string materialPath;
        bool skeletalModel = false;
        int animationClipIndex = 0;
        std::string animationClipName;
        bool animationAutoplay = true;
        bool animationLoop = true;
        float animationSpeed = 1.0f;
        bool animationLocomotionEnabled = false;
        int animationIdleClipIndex = 0;
        int animationWalkClipIndex = 0;
        int animationRunClipIndex = 0;
        std::string animationIdleClipName;
        std::string animationWalkClipName;
        std::string animationRunClipName;
        float animationWalkAt = 0.15f;
        float animationRunAt = 3.0f;
        std::vector<AnimationEventDesc> animationEvents;
        std::vector<AnimationActionProfileDesc> animationActionProfiles;
        std::vector<AnimationStateDesc> animationStates;
        std::vector<AnimationTransitionDesc> animationTransitions;
        bool linearVelocityEnabled = false;
        bool angularVelocityEnabled = false;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        bool rigidBodyEnabled = false;
        bool colliderEnabled = false;
        engine::ecs::RigidBody rigidBody;
        engine::ecs::Collider collider;
        bool rotatorEnabled = false;
        engine::ecs::Rotator rotator;
        bool moverEnabled = false;
        engine::ecs::Mover mover;
        bool healthEnabled = false;
        engine::Health health;
        bool scriptEnabled = false;
        std::string scriptClassName;
        std::string scriptPath;
        std::vector<ScriptField> scriptFields;
    };

    struct Scene {
        struct Environment {
            float timeOfDay = 0.46f;
            float skyLightIntensity = 1.0f;
            bool driveSunLight = true;
            float sunIntensity = 1.0f;
            bool fog = true;
            float fogDensity = 0.008f;
            float fogHeight = -0.35f;
            float fogHeightFalloff = 0.10f;
            glm::vec3 physicsGravity{0.0f, -9.81f, 0.0f};
            int physicsSolverIterations = 4;
            bool physicsBroadPhase = true;
            float physicsCellSize = 2.0f;
            float physicsRestitutionThreshold = 0.5f;
            bool physicsAllowSleeping = true;
            float physicsSleepLinearVelocity = 0.06f;
            float physicsSleepAngularVelocity = 0.15f;
            float physicsTimeToSleep = 0.5f;
        };

        Environment environment;
        std::vector<EntityDesc> entities;
        struct LightDesc {
            std::string name;
            engine::ecs::Light light;
            glm::vec3 position{0.0f};
        };
        std::vector<LightDesc> lights;
    };

    struct PrimitiveMeshes {
        const Mesh* cube = nullptr;
        const Mesh* plane = nullptr;
        const Mesh* sphere = nullptr;
    };

    static bool Load(const std::string& path, Scene* scene, std::string* error);
    static bool Instantiate(const Scene& scene, ecs::Registry& registry,
                            const PrimitiveMeshes& meshes, std::vector<ecs::Entity>* created,
                            std::string* error);
};

} // namespace engine