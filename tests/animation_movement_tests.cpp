#include <engine/animation/AnimatedModel.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/PlayerController.h>
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

} // namespace

int main() {
    TestFullBodyActionBlocksMovement();
    TestLayeredActionAllowsMovement();
    TestPlayerControllerMovementGate();
    TestThirdPersonCameraCollisionAndReturn();
    TestShoulderSwitchAndLockOnTracking();

    if (g_failures != 0) {
        std::cerr << g_failures << " animation movement test(s) failed\n";
        return 1;
    }
    std::cout << "animation movement tests passed\n";
    return 0;
}
