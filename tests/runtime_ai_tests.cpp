#include <engine/scene/RuntimeSceneLoader.h>
#include <engine/ai/AgentCollision.h>
#include <engine/ai/AiAgent.h>
#include <engine/ai/AiMovement.h>
#include <engine/ai/BehaviorGraph.h>
#include <engine/ai/Steering.h>
#include <engine/animation/AnimatedModel.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/physics/PhysicsWorld.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "3dg_runtime_ai_test.3dgscene";
    {
        std::ofstream output(path);
        output << "3DGRuntimeScene 51\n"
               << "nav_bounds 4 1 -3 20 4 12 1 0 0 0\n"
               << "nav_agent \"Enemy Mage\" 4.5 25 0.8 0.2 "
                  "\"PlayerStart\" 18 55 \"AI/Mage.btgraph\" 2 1 2 "
                  "1 0 2 8 0 -5\n"
               << "trigger_action \"GateTrigger\" \"Gate\" 1 0 2 0 "
                  "\"Opening\" 1 0 1 1\n"
               << "camera_zone \"CaveZone\" \"CaveCamera\" 1 7 0.5\n"
               << "physics_joint 1 \"Bridge\" \"\" 1 0 4 0 2 0 80 3\n"
               << "terrain \"Landscape\" 2 10 3 42 4 2 4 0 1 2 3 4 0 1 2 3\n";
    }

    engine::RuntimeSceneLoader::Scene scene;
    std::string error;
    Check(engine::RuntimeSceneLoader::Load(path.string(), &scene, &error),
          "load runtime AI records");
    Check(scene.navBounds.size() == 1, "navigation bounds count");
    Check(Near(scene.navBounds[0].position.x, 4.0f)
          && Near(scene.navBounds[0].scale.z, 12.0f),
          "navigation bounds transform");
    Check(scene.navAgents.size() == 1, "navigation agent count");
    const auto& agent = scene.navAgents[0];
    Check(agent.entityName == "Enemy Mage" && agent.targetName == "PlayerStart",
          "agent entity references");
    Check(Near(agent.speed, 4.5f) && Near(agent.visionRange, 18.0f),
          "agent movement and perception settings");
    Check(agent.brainAsset == "AI/Mage.btgraph"
          && agent.team == 2 && agent.autoTarget,
          "agent behavior and faction settings");
    Check(agent.patrolPoints.size() == 2
          && Near(agent.patrolPoints[1].z, -5.0f),
          "agent patrol route");
    Check(scene.triggerActions.size() == 1
          && scene.triggerActions[0].cameraSequence == "Opening",
          "runtime trigger actions");
    Check(scene.cameraZones.size() == 1
          && scene.cameraZones[0].priority == 7,
          "runtime camera zones");
    Check(scene.physicsJoints.size() == 1
          && scene.physicsJoints[0].worldAnchor,
          "runtime physics joints");
    Check(scene.terrains.size() == 1
          && scene.terrains[0].heights.size() == 4
          && scene.terrains[0].paint.size() == 4,
          "runtime terrain data");

    // Version 52 adds separately authored procedural cloud settings while keeping
    // the version-51 scene above loadable with defaults.
    {
        std::ofstream output(path);
        output << "3DGRuntimeScene 54\n"
               << "clouds 0 0.31 1.2 2.4 0.11 -0.04 145 0.16 0.8 0.85 0.92 1 0.63 0.028\n"
               << "skylight_occlusion 1 0.87 0.04\n";
    }
    scene = {};
    error.clear();
    Check(engine::RuntimeSceneLoader::Load(path.string(), &scene, &error),
          "load runtime cloud settings");
    Check(!scene.environment.clouds, "cloud toggle");
    Check(Near(scene.environment.cloudCoverage, 0.31f)
          && Near(scene.environment.cloudDensity, 1.2f)
          && Near(scene.environment.cloudScale, 2.4f),
          "cloud shape settings");
    Check(Near(scene.environment.cloudWindSpeed, -0.04f)
          && Near(scene.environment.cloudWindDirection, 145.0f),
          "cloud wind settings");
    Check(Near(scene.environment.cloudColor.b, 0.92f), "cloud tint");
    Check(scene.environment.cloudShadows
          && Near(scene.environment.cloudShadowStrength, 0.63f)
          && Near(scene.environment.cloudShadowScale, 0.028f),
          "cloud shadow settings");
    Check(scene.environment.skylightOcclusion
          && Near(scene.environment.skylightOcclusionStrength, 0.87f)
          && Near(scene.environment.minimumSkylight, 0.04f),
          "skylight occlusion settings");

    {
        std::ofstream output(path);
        output << "3DGRuntimeScene 57\n"
               << "nav_agent \"Enemy\" 3 20 0.6 0.3 \"\" 12 45 \"-\" 1 0 0 "
                  "0 -12 40 0.3 0.4 55\n";
    }
    scene = {};
    error.clear();
    Check(engine::RuntimeSceneLoader::Load(path.string(), &scene, &error),
          "load runtime AI movement settings");
    Check(scene.navAgents.size() == 1
          && Near(scene.navAgents[0].movementGravity, -12.0f)
          && Near(scene.navAgents[0].movementStepHeight, 0.4f)
          && Near(scene.navAgents[0].movementMaxSlope, 55.0f),
          "runtime AI movement settings");

    // AI movement is swept through the physics world instead of assigning the
    // requested transform blindly. It must stop at walls, slide along them, and
    // continue through triggers or channels configured to ignore enemies.
    engine::ecs::Registry registry;
    engine::PhysicsWorld physics;
    const engine::ecs::Entity enemy = registry.Create();
    registry.Add<engine::ecs::Transform>(enemy);
    engine::ecs::Collider enemyCollider = engine::ecs::Collider::MakeCapsule(0.4f, 0.5f);
    enemyCollider.layer = engine::ecs::CollisionLayer::Enemy;
    enemyCollider.mask = engine::ecs::CollisionLayer::CharacterBlockers;
    registry.Add<engine::ecs::Collider>(enemy, enemyCollider);

    const engine::ecs::Entity wall = registry.Create();
    engine::ecs::Transform wallTransform;
    wallTransform.position = glm::vec3(2.0f, 0.0f, 0.0f);
    registry.Add<engine::ecs::Transform>(wall, wallTransform);
    engine::ecs::Collider wallCollider =
        engine::ecs::Collider::MakeBox(glm::vec3(0.25f, 2.0f, 5.0f));
    wallCollider.layer = engine::ecs::CollisionLayer::WorldStatic;
    registry.Add<engine::ecs::Collider>(wall, wallCollider);

    glm::vec3 resolved = engine::ai::MoveAgentWithCollision(
        physics, registry, enemy, glm::vec3(0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    Check(resolved.x > 1.2f && resolved.x < 1.35f,
          "AI swept movement stops before a wall");

    resolved = engine::ai::MoveAgentWithCollision(
        physics, registry, enemy, glm::vec3(0.0f), glm::vec3(4.0f, 0.0f, 2.0f));
    Check(resolved.x < 1.35f && resolved.z > 1.9f,
          "AI movement slides along a wall");

    registry.Get<engine::ecs::Collider>(wall).isTrigger = true;
    resolved = engine::ai::MoveAgentWithCollision(
        physics, registry, enemy, glm::vec3(0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    Check(Near(resolved.x, 4.0f), "AI movement does not stop at triggers");

    registry.Get<engine::ecs::Collider>(wall).isTrigger = false;
    registry.Get<engine::ecs::Collider>(wall).mask &= ~engine::ecs::CollisionLayer::Enemy;
    resolved = engine::ai::MoveAgentWithCollision(
        physics, registry, enemy, glm::vec3(0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    Check(Near(resolved.x, 4.0f), "AI movement honors wall channel responses");

    engine::ai::Agent turningAgent;
    turningAgent.maxSpeed = 3.0f;
    turningAgent.maxForce = 25.0f;
    turningAgent.velocity = glm::vec3(3.0f, 0.0f, 0.0f);
    engine::ai::Integrate(turningAgent,
        engine::ai::Seek(turningAgent, glm::vec3(0.0f, 0.0f, 10.0f)), 0.1f);
    Check(turningAgent.velocity.z > std::fabs(turningAgent.velocity.x),
          "AI steering responds promptly when its target changes direction");

    registry.Get<engine::ecs::Collider>(wall).mask = engine::ecs::CollisionLayer::All;
    registry.Get<engine::ecs::Transform>(wall).position = glm::vec3(20.0f, 0.0f, 0.0f);
    const engine::ecs::Entity floor = registry.Create();
    engine::ecs::Transform floorTransform;
    floorTransform.position = glm::vec3(0.0f, -0.25f, 0.0f);
    registry.Add<engine::ecs::Transform>(floor, floorTransform);
    engine::ecs::Collider floorCollider =
        engine::ecs::Collider::MakeBox(glm::vec3(5.0f, 0.25f, 5.0f));
    floorCollider.layer = engine::ecs::CollisionLayer::WorldStatic;
    registry.Add<engine::ecs::Collider>(floor, floorCollider);

    engine::ai::AiMovementComponent movement;
    glm::vec3 position(0.0f, 2.0f, 0.0f);
    position = engine::ai::MoveAiAgent(
        physics, registry, enemy, position, glm::vec3(0.0f, 8.0f, 0.0f),
        0.1f, movement);
    Check(movement.IsFalling() && position.y < 2.0f,
          "ground AI ignores target height and enters falling state");
    for (int i = 0; i < 120; ++i) {
        position = engine::ai::MoveAiAgent(
            physics, registry, enemy, position, position, 1.0f / 60.0f, movement);
    }
    const float capsuleSupportHeight = enemyCollider.halfHeight + enemyCollider.radius;
    Check(movement.IsGrounded()
          && std::fabs(position.y - capsuleSupportHeight) < 0.02f,
          "falling AI lands with the capsule bottom on collider floors");

    movement.mode = engine::ai::AiMovementMode::Falling;
    movement.verticalVelocity = 0.0f;
    position = glm::vec3(0.0f, 2.0f, 0.0f);
    for (int i = 0; i < 120; ++i) {
        position = engine::ai::MoveAiAgent(
            physics, registry, enemy, position, position, 1.0f / 60.0f,
            movement, true, 0.5f);
    }
    Check(movement.IsGrounded()
          && std::fabs(position.y - (0.5f + capsuleSupportHeight)) < 0.02f,
          "falling AI keeps its capsule above terrain ground");

    const engine::ecs::Entity stairs = registry.Create();
    engine::ecs::Transform stairTransform;
    stairTransform.position = glm::vec3(10.0f, 0.0f, 0.0f);
    registry.Add<engine::ecs::Transform>(stairs, stairTransform);
    engine::ecs::Collider stairCollider =
        engine::ecs::Collider::MakeStaircase(glm::vec3(2.0f, 1.0f, 3.0f), 6);
    stairCollider.layer = engine::ecs::CollisionLayer::WorldStatic;
    registry.Add<engine::ecs::Collider>(stairs, stairCollider);
    const engine::RaycastHit lowTread = physics.SphereCast(
        registry, glm::vec3(10.0f, 3.0f, -2.5f),
        glm::vec3(10.0f, -3.0f, -2.5f), 0.4f,
        enemy, engine::ecs::CollisionLayer::CharacterBlockers,
        engine::ecs::CollisionLayer::Enemy);
    const engine::RaycastHit highTread = physics.SphereCast(
        registry, glm::vec3(10.0f, 3.0f, 2.5f),
        glm::vec3(10.0f, -3.0f, 2.5f), 0.4f,
        enemy, engine::ecs::CollisionLayer::CharacterBlockers,
        engine::ecs::CollisionLayer::Enemy);
    Check(lowTread.hit && highTread.hit
          && highTread.point.y - lowTread.point.y > 1.5f,
          "AI sweeps resolve individual staircase treads instead of one tall box");

    engine::ai::AiAgent stopAgent;
    stopAgent.reachRadius = 1.25f;
    stopAgent.agent.maxSpeed = 3.0f;
    stopAgent.agent.maxForce = 25.0f;
    stopAgent.agent.position = glm::vec3(0.0f);
    stopAgent.agent.velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    engine::ai::NavGrid emptyGrid;
    stopAgent.Update(1.0f / 60.0f, glm::vec3(0.5f, 0.0f, 0.0f), true, emptyGrid);
    Check(glm::length(stopAgent.agent.velocity) < 0.0001f
          && glm::length(stopAgent.Position()) < 0.0001f,
          "AI holds position while the player is inside reach radius");
    stopAgent.Update(1.0f / 60.0f, glm::vec3(2.0f, 0.0f, 0.0f), true, emptyGrid);
    Check(stopAgent.Position().x > 0.0f,
          "AI resumes movement when the player leaves reach radius");

    engine::ai::BehaviorGraph focusGraph;
    focusGraph.root = focusGraph.AddNode(
        engine::ai::BtNodeType::FocusTarget, glm::vec2(0.0f));
    engine::ai::BehaviorTree<engine::ai::AgentContext> focusTree =
        engine::ai::BuildBehaviorTree(focusGraph);
    engine::ai::AgentContext focusContext;
    focusContext.self = enemy;
    focusContext.targetEntity = floor;
    focusContext.targetPos = glm::vec3(3.0f, 7.0f, 0.0f);
    focusContext.seesTarget = true;
    Check(focusTree.Tick(focusContext, 1.0f / 60.0f)
              == engine::ai::BtStatus::Success
          && focusContext.focusTarget && focusContext.facing.x > 0.99f,
          "Focus Target task faces a visible target on the movement plane");

    engine::ai::BehaviorGraph clearFocusGraph;
    clearFocusGraph.root = clearFocusGraph.AddNode(
        engine::ai::BtNodeType::ClearFocus, glm::vec2(0.0f));
    engine::ai::BehaviorTree<engine::ai::AgentContext> clearFocusTree =
        engine::ai::BuildBehaviorTree(clearFocusGraph);
    Check(clearFocusTree.Tick(focusContext, 1.0f / 60.0f)
              == engine::ai::BtStatus::Success
          && !focusContext.focusTarget,
          "Clear Focus task releases the target-facing override");

    movement.mode = engine::ai::AiMovementMode::Flying;
    position = engine::ai::MoveAiAgent(
        physics, registry, enemy, position, glm::vec3(0.0f, 3.0f, 0.0f),
        0.1f, movement);
    Check(movement.IsFlying() && Near(position.y, 3.0f),
          "flying AI keeps three-dimensional movement");

    engine::AnimatedModel animated;
    movement.mode = engine::ai::AiMovementMode::Falling;
    movement.verticalVelocity = -6.5f;
    engine::ai::UpdateAiAnimationParameters(
        animated, movement, glm::vec3(3.0f, -6.5f, 4.0f));
    Check(Near(animated.controller.Parameter("Speed"), 5.0f)
          && Near(animated.controller.Parameter("VerticalSpeed"), -6.5f)
          && animated.controller.BoolParameter("IsFalling")
          && !animated.controller.BoolParameter("IsGrounded"),
          "AI movement publishes locomotion flags to the animation graph");

    std::filesystem::remove(path);
    std::cout << "runtime AI tests passed\n";
    return 0;
}
