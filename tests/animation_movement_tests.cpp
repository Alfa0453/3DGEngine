#include <engine/animation/AnimatedModel.h>
#include <engine/animation/AnimationGraphDesc.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/PlayerController.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/RagdollSystem.h>
#include <engine/physics/CharacterController.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/physics/PhysicsWorld.h>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "[FAIL] " << message << '\n';
}

bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

void TestNamedSocketWorldTransform() {
    engine::AnimatedModel animated;
    engine::NamedModelSocket socket;
    socket.name = "StaffTip";
    socket.localOffset[3] = glm::vec4(0.25f, 1.5f, -2.0f, 1.0f);
    animated.sockets.push_back(socket);

    engine::ecs::Transform character;
    character.position = {4.0f, 2.0f, 7.0f};

    glm::mat4 world{1.0f};
    Check(animated.SocketWorldTransform(character, "StaffTip", &world),
          "named model socket resolves");
    Check(Near(world[3].x, 4.25f)
          && Near(world[3].y, 3.5f)
          && Near(world[3].z, 5.0f),
          "socket local offset is transformed into world space");
    Check(!animated.SocketWorldTransform(character, "MissingSocket", &world),
          "missing named model socket is reported");
}

void TestFullBodyActionBlocksMovement() {
    engine::AnimatedModel animated;
    animated.PlayAction(2);

    Check(animated.ActionPlaying(), "valid action starts playing");
    Check(animated.BlocksMovement(), "empty action mask blocks movement");

    animated.action.active = false;
    Check(!animated.BlocksMovement(), "completed action releases movement");
}

void TestLayeredActionAllowsMovement() {
    engine::AnimatedModel animated;
    animated.PlayAction(3, std::vector<float>{0.0f, 1.0f, 1.0f});

    Check(animated.ActionPlaying(), "masked action starts playing");
    Check(!animated.BlocksMovement(), "non-empty action mask remains layered");
}

void TestActionEventsAreOrdered() {
    engine::AnimatedModel animated;
    animated.PlayAction(4, {}, {
        {4, 0.75f, "Recover"},
        {4, 0.20f, "Hit"},
        {4, 0.05f, "Windup"}
    });

    Check(animated.action.events.size() == 3
          && animated.action.events[0].name == "Windup"
          && animated.action.events[1].name == "Hit"
          && animated.action.events[2].name == "Recover",
          "action events are ordered before playback");
}

void TestLocomotionBlendSpaceSampling() {
    engine::AnimationController controller;
    engine::AnimationController::State locomotion;
    locomotion.name = "Locomotion";
    locomotion.clip = 0;
    locomotion.blendParameter = "Speed";
    locomotion.blendSamples = {{0, 0.0f}, {1, 2.0f}, {2, 6.0f}};
    controller.AddState(locomotion);

    controller.SetParameter("Speed", 1.0f);
    auto result = controller.CurrentBlendSpace();
    Check(result.active && result.clipA == 0 && result.clipB == 1 && Near(result.alpha, 0.5f),
          "Blend Space interpolates idle and walk at the lower interval midpoint");

    controller.SetParameter("Speed", 4.0f);
    result = controller.CurrentBlendSpace();
    Check(result.clipA == 1 && result.clipB == 2 && Near(result.alpha, 0.5f),
          "Blend Space interpolates walk and run at the upper interval midpoint");

    controller.SetParameter("Speed", 20.0f);
    result = controller.CurrentBlendSpace();
    Check(result.clipA == 2 && result.clipB == 2 && Near(result.alpha, 0.0f),
          "Blend Space clamps values beyond its final sample");
}

void TestDirectionalBlendSpaceSampling() {
    engine::AnimationController controller;
    engine::AnimationController::State locomotion;
    locomotion.name = "Directional";
    locomotion.clip = 0;
    locomotion.blendParameter = "Speed";
    locomotion.blendParameterY = "Direction";
    locomotion.blendSpace2D = true;
    locomotion.blendSamples = {
        {0, 0.0f, 0.0f}, {1, 2.0f, 0.0f}, {2, 2.0f, -90.0f}, {3, 2.0f, 90.0f}
    };
    controller.AddState(locomotion);
    controller.SetParameter("Speed", 2.0f);
    controller.SetParameter("Direction", 90.0f);
    auto result = controller.CurrentBlendSpace();
    Check(result.active && result.samples.size() == 1 && result.samples.front().clip == 3,
          "2D Blend Space selects the exact right-strafe sample");

    controller.SetParameter("Direction", 45.0f);
    result = controller.CurrentBlendSpace();
    float total = 0.0f;
    for (const auto& sample : result.samples) total += sample.weight;
    Check(result.samples.size() >= 2 && Near(total, 1.0f),
          "2D Blend Space normalizes nearby directional sample weights");
}

void TestBlendSpaceInputSmoothing() {
    engine::AnimationController controller;
    engine::AnimationController::State locomotion;
    locomotion.name = "Smooth Locomotion";
    locomotion.clip = 0;
    locomotion.blendParameter = "Speed";
    locomotion.blendSamples = {{0, 0.0f}, {1, 10.0f}};
    controller.AddState(locomotion);

    controller.SetParameter("Speed", 0.0f);
    controller.Update(1.0f / 60.0f);
    controller.SetParameter("Speed", 10.0f);
    controller.Update(1.0f / 60.0f);
    const auto result = controller.CurrentBlendSpace();
    Check(result.active && result.clipA == 0 && result.clipB == 1
              && result.alpha > 0.0f && result.alpha < 1.0f,
          "Blend Space input eases between samples instead of snapping");
}

void TestClipBaseSpeedTimesStateMultiplier() {
    engine::AnimationGraphDesc graph;
    engine::AnimationGraphDesc::StateDesc state;
    state.name = "Walk";
    state.clipIndex = 0;
    state.clipBaseSpeed = 1.5f;
    state.speed = 0.5f;
    graph.states.push_back(state);

    engine::AnimationController controller;
    engine::BuildAnimationController(controller, graph,
        [](int fallback, const std::string&) { return fallback; },
        [](int) { return 2.0f; });
    controller.Update(1.0f);
    Check(Near(controller.CurrentTime(), 0.75f),
          "clip base speed is multiplied by the graph state multiplier");
}

void TestBlendSamplesPreserveIndependentBaseSpeeds() {
    engine::AnimationGraphDesc graph;
    engine::AnimationGraphDesc::StateDesc state;
    state.name = "Locomotion";
    state.clipIndex = 0;
    state.clipName = "Walk";
    state.speed = 0.5f;
    state.clipBaseSpeed = 1.0f;
    state.blendParameter = "Speed";
    state.synchronizeBlendSpace = false;
    state.blendSamples = {
        {0, "Walk", 0.0f, 0.0f, 1.25f, 1.0f},
        {1, "Run", 2.0f, 0.0f, 1.5f, 0.7f}
    };
    graph.states.push_back(state);

    engine::AnimationController controller;
    engine::BuildAnimationController(controller, graph,
        [](int fallback, const std::string&) { return fallback; },
        [](int clip) { return clip == 0 ? 1.0f : 0.7f; });
    controller.SetParameter("Speed", 1.0f);
    controller.Update(1.0f);
    const auto blend = controller.CurrentBlendSpace();
    Check(Near(controller.CurrentTime(), 0.5f),
          "unsynchronized Blend Space keeps a state-multiplier wall clock");
    Check(blend.samples.size() == 2
          && Near(blend.samples[0].basePlaybackSpeed, 1.25f)
          && Near(blend.samples[1].basePlaybackSpeed, 1.5f),
          "each Blend Space sample retains its authoritative clip base speed");
}

void TestSynchronizedBlendSpaceUsesNormalizedCycleRate() {
    engine::AnimationGraphDesc graph;
    engine::AnimationGraphDesc::StateDesc state;
    state.name = "Synchronized";
    state.clipIndex = 0;
    state.clipName = "Walk";
    state.speed = 1.0f;
    state.clipBaseSpeed = 1.0f;
    state.blendParameter = "Speed";
    state.synchronizeBlendSpace = true;
    state.blendSamples = {
        {0, "Walk", 0.0f, 0.0f, 1.0f, 1.0f},
        {1, "Run", 2.0f, 0.0f, 1.3f, 0.7f}
    };
    graph.states.push_back(state);

    engine::AnimationController controller;
    engine::BuildAnimationController(controller, graph,
        [](int fallback, const std::string&) { return fallback; },
        [](int clip) { return clip == 0 ? 1.0f : 0.7f; });
    controller.SetParameter("Speed", 1.0f);
    controller.Update(1.0f);
    const float expected = 0.5f * (1.0f / 1.0f) + 0.5f * (1.3f / 0.7f);
    Check(Near(controller.CurrentTime(), expected, 0.001f),
          "synchronized Blend Space advances one phase from weighted sample cycle rates");
}

void TestExitTimeUsesEffectiveSourceTimeOnce() {
    engine::AnimationGraphDesc graph;
    engine::AnimationGraphDesc::StateDesc start;
    start.name = "Start";
    start.clipBaseSpeed = 2.0f;
    start.speed = 1.0f;
    engine::AnimationGraphDesc::StateDesc end;
    end.name = "End";
    graph.states = {start, end};
    engine::AnimationGraphDesc::TransitionDesc transition;
    transition.fromState = "Start";
    transition.toState = "End";
    transition.useConditions = false;
    transition.exitTime = 1.0f;
    graph.transitions.push_back(transition);

    engine::AnimationController controller;
    engine::BuildAnimationController(controller, graph,
        [](int fallback, const std::string&) { return fallback; },
        [](int) { return 2.0f; });
    controller.Update(1.0f);
    controller.Update(0.001f);
    Check(controller.CurrentStateName() == "End",
          "exit time sees accelerated source time without dividing duration twice");
}

void TestPlayerControllerMovementGate() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));

    engine::PlayerInput input;
    input.moveForward = 1.0f;
    input.sprint = true;
    input.lookYaw = 5.0f;

    const glm::vec3 before = controller.Position();
    const float yawBefore = controller.Yaw();
    controller.Update(registry, input, 1.0f / 60.0f, false);
    const glm::vec3 locked = controller.Position();

    Check(Near(locked.x, before.x) && Near(locked.z, before.z),
          "disabled movement holds horizontal capsule position");
    Check(!Near(controller.Yaw(), yawBefore),
          "disabled movement still permits camera look");

    controller.Update(registry, input, 1.0f / 60.0f, true);
    const glm::vec3 moving = controller.Position();
    Check(!Near(moving.x, locked.x) || !Near(moving.z, locked.z),
          "re-enabled movement advances horizontal capsule position");
}

void TestPlayerControllerCannotSprintInAir() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.walkSpeed = 2.0f;
    controller.runSpeed = 8.0f;
    controller.camCollision = false;
    controller.SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    controller.body.grounded = false;

    engine::PlayerInput input;
    input.moveForward = 1.0f;
    input.sprint = true;

    const glm::vec3 before = controller.Position();
    controller.Update(registry, input, 0.1f);
    const glm::vec2 horizontal(
        controller.Position().x - before.x,
        controller.Position().z - before.z);
    Check(Near(glm::length(horizontal), controller.walkSpeed * 0.1f, 0.01f),
          "airborne sprint input is limited to walk-speed air control");
}

void TestPlayerControllerCrouchAndStandClearance() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.SetCapsule(0.4f, 1.8f);
    controller.crouchedHeight = 1.0f;
    controller.SetPosition(glm::vec3(0.0f, 0.9f, 0.0f));
    controller.camCollision = false;

    engine::PlayerInput crouch;
    crouch.crouch = true;
    controller.Update(registry, crouch, 0.0f);
    Check(controller.Crouching() && Near(controller.body.height, 1.0f),
          "crouch input reduces the authoritative capsule height");
    Check(Near(controller.body.position.y - controller.body.height * 0.5f, 0.0f),
          "crouching preserves the capsule foot position");

    const engine::ecs::Entity ceiling = registry.Create();
    engine::ecs::Transform ceilingTransform;
    ceilingTransform.position = glm::vec3(0.0f, 1.5f, 0.0f);
    registry.Add<engine::ecs::Transform>(ceiling, ceilingTransform);
    registry.Add<engine::ecs::Collider>(ceiling,
        engine::ecs::Collider::MakeBox(glm::vec3(2.0f, 0.2f, 2.0f)));
    controller.Update(registry, engine::PlayerInput{}, 0.0f);
    Check(controller.Crouching() && Near(controller.body.height, 1.0f),
          "standing remains blocked while head room is occupied");

    registry.Destroy(ceiling);
    controller.Update(registry, engine::PlayerInput{}, 0.0f);
    Check(!controller.Crouching() && Near(controller.body.height, 1.8f),
          "controller returns to standing height once head room is clear");
}

void TestPlayerControllerSwimming() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.SetCapsule(0.4f, 1.8f);
    controller.SetPosition(glm::vec3(0.0f, 0.5f, 0.0f));
    controller.camCollision = false;
    controller.SetWaterSurface(true, 0.0f);

    engine::PlayerInput input;
    input.moveForward = 1.0f;
    input.jump = true;
    const glm::vec3 before = controller.Position();
    controller.Update(registry, input, 0.1f);
    Check(controller.Swimming() && !controller.Grounded(),
          "submerged controller enters non-grounded swimming movement");
    Check(controller.Position().y > before.y,
          "Space moves a swimming controller upward");

    input = {};
    input.crouch = true;
    const float beforeDescend = controller.Position().y;
    controller.Update(registry, input, 0.1f);
    Check(controller.Position().y < beforeDescend,
          "crouch input moves a swimming controller downward");

    controller.SetWaterSurface(false, 0.0f);
    controller.Update(registry, engine::PlayerInput{}, 0.0f);
    Check(!controller.Swimming(), "leaving a water footprint exits swimming mode");
}

void TestCameraRelativeFacingSmoothing() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.camCollision = false;
    controller.Update(registry, engine::PlayerInput{}, 1.0f / 60.0f);
    const glm::quat before = controller.Facing();

    engine::PlayerInput turn;
    turn.lookYaw = 900.0f; // 90 degrees after the default sensitivity.
    controller.Update(registry, turn, 1.0f / 60.0f);
    const glm::quat after = controller.Facing();
    const glm::quat snappedTarget =
        glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Check(std::abs(glm::dot(before, after)) < 0.9999f,
          "camera-relative facing starts turning immediately");
    Check(std::abs(glm::dot(after, snappedTarget)) < 0.9999f,
          "camera-relative facing does not snap to the new camera yaw");
}

void TestStairStepDoesNotSnapPresentation() {
    engine::ecs::Registry registry;

    const engine::ecs::Entity floor = registry.Create();
    engine::ecs::Transform floorTransform;
    floorTransform.position = glm::vec3(0.0f, -0.25f, 0.0f);
    registry.Add<engine::ecs::Transform>(floor, floorTransform);
    registry.Add<engine::ecs::Collider>(
        floor, engine::ecs::Collider::MakeBox(glm::vec3(4.0f, 0.25f, 4.0f)));

    const engine::ecs::Entity step = registry.Create();
    engine::ecs::Transform stepTransform;
    stepTransform.position = glm::vec3(0.0f, 0.10f, -0.70f);
    registry.Add<engine::ecs::Transform>(step, stepTransform);
    registry.Add<engine::ecs::Collider>(
        step, engine::ecs::Collider::MakeBox(glm::vec3(1.0f, 0.10f, 0.25f)));

    engine::PlayerController controller;
    controller.SetPosition(glm::vec3(0.0f, 0.9f, 0.0f));
    controller.body.grounded = true;
    controller.camCollision = false;

    engine::PlayerInput input;
    input.moveForward = 1.0f;
    const glm::vec3 before = controller.Position();
    glm::vec3 physical = before;
    glm::vec3 presented = before;
    bool climbed = false;
    bool smoothed = false;
    bool presentationTeleported = false;
    glm::vec3 previousPresented = before;
    for (int frame = 0; frame < 30; ++frame) {
        controller.Update(registry, input, 1.0f / 60.0f);
        physical = controller.Position();
        presented = controller.CapsulePosition();
        if (physical.y > before.y + 0.01f) {
            climbed = true;
            smoothed = presented.y > before.y && presented.y < physical.y;
        }
        const float presentedTravel = glm::length(glm::vec2(
            presented.x - previousPresented.x, presented.z - previousPresented.z));
        presentationTeleported |= presentedTravel > controller.body.radius * 0.75f;
        previousPresented = presented;
    }

    Check(climbed,
          "character controller physically climbs a reachable stair");
    Check(smoothed,
          "stair presentation eases upward instead of matching the instant step");
    Check(!presentationTeleported,
          "step-up clearance is not exposed as a presentation teleport");
}

void TestIsometricCameraMode() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.view = engine::PlayerController::View::Isometric;
    controller.camCollision = false;
    controller.SetPosition(glm::vec3(0.0f, 3.0f, 0.0f));
    controller.SetIsometricView(45.0f, -40.0f, 14.0f);
    const glm::vec3 authoredDirection = controller.LookDirection();

    engine::PlayerInput input;
    input.lookYaw = 1000.0f;
    input.lookPitch = -1000.0f;
    input.moveForward = 1.0f;
    controller.Update(registry, input, 1.0f / 60.0f);

    Check(glm::dot(authoredDirection, controller.LookDirection()) > 0.9999f,
          "isometric camera ignores orbit input and keeps its authored angle");
    Check(Near(glm::length(controller.CameraPosition() - controller.CameraTarget()), 14.0f),
          "isometric camera uses its authored distance");
    Check(glm::length(glm::vec2(controller.Position().x, controller.Position().z)) > 0.01f,
          "isometric movement remains relative to the fixed camera direction");
}

void TestThirdPersonCameraCollisionAndReturn() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity wall = registry.Create();
    engine::ecs::Transform wallTransform;
    wallTransform.position = glm::vec3(0.0f, 1.0f, 3.0f);
    registry.Add<engine::ecs::Transform>(wall, wallTransform);
    registry.Add<engine::ecs::Collider>(
        wall, engine::ecs::Collider::MakeBox(glm::vec3(2.0f, 2.0f, 0.25f)));

    engine::PlayerController controller;
    controller.SetPosition(glm::vec3(0.0f));
    controller.camDistance = 5.0f;
    controller.camProbeRadius = 0.2f;
    controller.camCollisionPadding = 0.08f;
    controller.camReturnSpeed = 4.0f;

    controller.Update(registry, engine::PlayerInput{}, 1.0f / 60.0f);
    const float obstructed = controller.CurrentCameraDistance();
    Check(obstructed < 3.0f,
          "third-person sphere cast retracts the camera before a wall");

    registry.Destroy(wall);
    controller.Update(registry, engine::PlayerInput{}, 1.0f / 60.0f);
    const float returning = controller.CurrentCameraDistance();
    Check(returning > obstructed && returning < controller.camDistance,
          "camera returns smoothly instead of snapping after obstruction clears");
}

void TestCollectiblesDoNotBlockPlayerOrCamera() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity coin = registry.Create();
    engine::ecs::Transform coinTransform;
    coinTransform.position = glm::vec3(0.0f, 0.9f, 0.0f);
    registry.Add<engine::ecs::Transform>(coin, coinTransform);
    engine::ecs::Collider coinCollider =
        engine::ecs::Collider::MakeBox(glm::vec3(0.25f));
    coinCollider.layer = engine::ecs::CollisionLayer::Collectible;
    registry.Add<engine::ecs::Collider>(coin, coinCollider);

    engine::CharacterController body;
    body.gravity = glm::vec3(0.0f);
    body.position = glm::vec3(-0.8f, 0.9f, 0.0f);
    body.Move(registry, glm::vec3(1.0f, 0.0f, 0.0f), 0.6f);
    Check(Near(body.position.x, -0.2f),
          "collectible channel does not push the character capsule");

    coinTransform.position = glm::vec3(0.0f, 1.0f, 3.0f);
    registry.Get<engine::ecs::Transform>(coin) = coinTransform;
    engine::PlayerController controller;
    controller.SetPosition(glm::vec3(0.0f));
    controller.camDistance = 5.0f;
    controller.Update(registry, engine::PlayerInput{}, 1.0f / 60.0f);
    Check(Near(controller.CurrentCameraDistance(), controller.camDistance),
          "collectible channel does not retract the third-person camera");
}

void TestShoulderSwitchAndLockOnTracking() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
    controller.camCollision = false;
    controller.shoulderCamera = true;
    controller.shoulderOffset = 0.75f;
    controller.shoulderSwitchSpeed = 20.0f;

    controller.Update(registry, engine::PlayerInput{}, 1.0f / 60.0f);
    const float rightShoulderX = controller.CameraPosition().x;
    Check(rightShoulderX > 0.5f, "right shoulder offsets the camera right");

    engine::PlayerInput switchShoulder;
    switchShoulder.toggleShoulder = true;
    controller.Update(registry, switchShoulder, 0.25f);
    Check(controller.CameraPosition().x < 0.0f,
          "shoulder toggle smoothly moves the camera left");

    controller.lockOnTrackingSpeed = 100.0f;
    controller.SetLockOnTarget(glm::vec3(10.0f, 2.0f, 0.0f));
    controller.Update(registry, engine::PlayerInput{}, 0.1f);
    Check(controller.LookDirection().x > 0.9f,
          "lock-on turns the camera rig toward its target");
    Check(controller.CameraTarget().x > 4.0f,
          "lock-on frames both player and target");
}

void TestGameplayCannotToggleCameraMode() {
    engine::ecs::Registry registry;
    engine::PlayerController controller;
    controller.view = engine::PlayerController::View::ThirdPerson;

    engine::PlayerInput input;
    input.toggleView = true;
    controller.Update(registry, input, 1.0f / 60.0f);

    Check(controller.view == engine::PlayerController::View::ThirdPerson,
          "gameplay input cannot change the configured camera mode");
}

void TestRagdollActivatesOnDeathWithoutSkeleton() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity character = registry.Create();
    registry.Add<engine::ecs::Transform>(character, {});
    registry.Add<engine::ecs::Collider>(
        character, engine::ecs::Collider::MakeCapsule(0.4f, 0.5f));
    engine::Health health;
    health.hp = 0.0f;
    health.alive = false;
    health.justDied = true;
    registry.Add<engine::Health>(character, health);
    registry.Add<engine::Ragdoll>(character, {});

    engine::PhysicsWorld physics;
    engine::UpdateRagdollsBeforePhysics(registry, physics);

    const engine::Ragdoll& ragdoll =
        registry.Get<engine::Ragdoll>(character);
    const engine::ecs::RigidBody* body =
        registry.TryGet<engine::ecs::RigidBody>(character);
    Check(ragdoll.active, "death activates the ragdoll component");
    Check(body && body->invMass > 0.0f && !body->kinematic,
          "characters without skeletal data use the dynamic-body fallback");
}

void TestDisabledRagdollDoesNotActivate() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity character = registry.Create();
    registry.Add<engine::ecs::Transform>(character, {});
    engine::Health health;
    health.hp = 0.0f;
    health.alive = false;
    registry.Add<engine::Health>(character, health);
    engine::Ragdoll authored;
    authored.enabled = false;
    registry.Add<engine::Ragdoll>(character, authored);

    engine::PhysicsWorld physics;
    engine::UpdateRagdollsBeforePhysics(registry, physics);

    Check(!registry.Get<engine::Ragdoll>(character).active,
          "disabled ragdoll remains inactive after death");
    Check(!registry.Has<engine::ecs::RigidBody>(character),
          "disabled ragdoll does not add a physics body");
}

void TestRagdollRecoveryCanBeRequested() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity character = registry.Create();
    registry.Add<engine::ecs::Transform>(character, {});
    registry.Add<engine::ecs::Collider>(
        character, engine::ecs::Collider::MakeCapsule(0.4f, 0.5f));
    registry.Add<engine::Ragdoll>(character, {});

    engine::PhysicsWorld physics;
    Check(engine::ActivateRagdoll(registry, physics, character),
          "scripts can explicitly activate a ragdoll");
    Check(engine::RequestRagdollRecovery(registry, character),
          "scripts can request recovery from an active ragdoll");
    Check(registry.Get<engine::Ragdoll>(character).recovering,
          "recovery request starts animation blending");
}

void TestCompoundColliderQueries() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity object = registry.Create();
    registry.Add<engine::ecs::Transform>(object, {});
    registry.Add<engine::ecs::Collider>(
        object, engine::ecs::Collider::MakeBox(glm::vec3(0.5f)));
    engine::ecs::AdditionalColliders compound;
    engine::ecs::Collider offsetSphere = engine::ecs::Collider::MakeSphere(0.75f);
    offsetSphere.localPosition = glm::vec3(5.0f, 0.0f, 0.0f);
    compound.values.push_back(offsetSphere);
    registry.Add<engine::ecs::AdditionalColliders>(object, compound);

    engine::PhysicsWorld physics;
    engine::Ray ray;
    ray.origin = glm::vec3(5.0f, 0.0f, -4.0f);
    ray.direction = glm::vec3(0.0f, 0.0f, 1.0f);
    const engine::RaycastHit hit = physics.Raycast(registry, ray, 10.0f);
    const auto overlaps = physics.OverlapSphere(
        registry, glm::vec3(5.0f, 0.0f, 0.0f), 0.2f);
    Check(hit.hit && hit.entity == object && hit.distance > 3.0f,
          "raycasts test additional collider shapes");
    Check(overlaps.size() == 1 && overlaps.front() == object,
          "overlap queries report a compound object only once");
}

} // namespace

int main() {
    TestNamedSocketWorldTransform();
    TestFullBodyActionBlocksMovement();
    TestLayeredActionAllowsMovement();
    TestActionEventsAreOrdered();
    TestLocomotionBlendSpaceSampling();
    TestDirectionalBlendSpaceSampling();
    TestBlendSpaceInputSmoothing();
    TestClipBaseSpeedTimesStateMultiplier();
    TestBlendSamplesPreserveIndependentBaseSpeeds();
    TestSynchronizedBlendSpaceUsesNormalizedCycleRate();
    TestExitTimeUsesEffectiveSourceTimeOnce();
    TestPlayerControllerMovementGate();
    TestPlayerControllerCannotSprintInAir();
    TestPlayerControllerCrouchAndStandClearance();
    TestPlayerControllerSwimming();
    TestCameraRelativeFacingSmoothing();
    TestStairStepDoesNotSnapPresentation();
    TestIsometricCameraMode();
    TestThirdPersonCameraCollisionAndReturn();
    TestCollectiblesDoNotBlockPlayerOrCamera();
    TestShoulderSwitchAndLockOnTracking();
    TestGameplayCannotToggleCameraMode();
    TestRagdollActivatesOnDeathWithoutSkeleton();
    TestDisabledRagdollDoesNotActivate();
    TestRagdollRecoveryCanBeRequested();
    TestCompoundColliderQueries();

    if (g_failures != 0) {
        std::cerr << g_failures << " animation movement test(s) failed\n";
        return 1;
    }
    std::cout << "animation movement tests passed\n";
    return 0;
}
