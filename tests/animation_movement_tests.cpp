#include <engine/animation/AnimatedModel.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/PlayerController.h>
#include <engine/physics/CharacterController.h>
#include <engine/physics/PhysicsComponents.h>

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

} // namespace

int main() {
    TestNamedSocketWorldTransform();
    TestFullBodyActionBlocksMovement();
    TestLayeredActionAllowsMovement();
    TestActionEventsAreOrdered();
    TestLocomotionBlendSpaceSampling();
    TestDirectionalBlendSpaceSampling();
    TestBlendSpaceInputSmoothing();
    TestPlayerControllerMovementGate();
    TestCameraRelativeFacingSmoothing();
    TestStairStepDoesNotSnapPresentation();
    TestIsometricCameraMode();
    TestThirdPersonCameraCollisionAndReturn();
    TestCollectiblesDoNotBlockPlayerOrCamera();
    TestShoulderSwitchAndLockOnTracking();
    TestGameplayCannotToggleCameraMode();

    if (g_failures != 0) {
        std::cerr << g_failures << " animation movement test(s) failed\n";
        return 1;
    }
    std::cout << "animation movement tests passed\n";
    return 0;
}
