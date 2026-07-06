#include "engine/gameplay/PlayerController.h"

#include "engine/ecs/Registry.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace engine {
namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};

// Horizontal forward from a yaw angle (degrees). Ignores pitch so movement stays
// on the ground plane regardless of where the camera is looking.
glm::vec3 YawForward(float yawDeg) {
    const float y = glm::radians(yawDeg);
    return glm::vec3(std::cos(y), 0.0f, std::sin(y));   // yaw=90 -> (0, 0, -1)
}
} // namespace

glm::vec3 PlayerController::LookDirection() const {
    const float y = glm::radians(m_yaw), p = glm::radians(m_pitch);
    return glm::normalize(glm::vec3(std::cos(y) * std::cos(p),
                                    std::sin(p), std::sin(y) * std::cos(p)));
}

glm::vec3 PlayerController::EyePosition() const {
    return body.position + glm::vec3(0.0f, eyeHeight, 0.0f);
}

glm::vec3 PlayerController::CameraTarget() const {
    if (view == View::FirstPerson) return EyePosition() + LookDirection();
    return body.position + glm::vec3(0.0f, camTargetHeight, 0.0f);
}

glm::vec3 PlayerController::CameraPosition() const {
    if (view == View::FirstPerson) return EyePosition();
    // Third person: orbit behind the look direction at camDistance.
    const glm::vec3 target = body.position + glm::vec3(0.0f, camTargetHeight, 0.0f);
    return target - LookDirection() * camDistance;
}

glm::mat4 PlayerController::ViewMatrix() const {
    return glm::lookAt(CameraPosition(), CameraTarget(), kWorldUp);
}

glm::quat PlayerController::Facing() const {
    // Yaw-only orientation so an attached capsule/character mesh faces travel.
    return glm::angleAxis(glm::radians(-m_yaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

void PlayerController::Update(ecs::Registry& reg, const PlayerInput& in, float dt) {
    // 1) View toggle (edge-triggered off the held state).
    if (in.toggleView && !m_prevToggle) ToggleView();
    m_prevToggle = in.toggleView;

    // 2) Mouse-look. dy is inverted so pushing the mouse up looks up.
    m_yaw   += in.lookYaw   * lookSensitivity;
    m_pitch -= in.lookPitch * lookSensitivity;
    const float lo = (view == View::FirstPerson) ? fpMinPitch : tpMinPitch;
    const float hi = (view == View::FirstPerson) ? fpMaxPitch : tpMaxPitch;
    m_pitch = std::clamp(m_pitch, lo, hi);
    if (m_yaw > 360.0f) m_yaw -= 360.0f; else if (m_yaw < -360.0f) m_yaw += 360.0f;

    // 3) Camera-relative movement on the ground plane.
    const glm::vec3 fwd  = YawForward(m_yaw);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, kWorldUp));
    glm::vec3 wish = fwd * in.moveForward + right * in.moveRight;
    const float wl = glm::length(wish);
    if (wl > 1.0f) wish /= wl; 
    const float speed = in.sprint ? runSpeed : walkSpeed;                     // no faster on diagonals
    const glm::vec3 wishVel = wish * speed;

    // 4) Jump before the sweep so the upward velocity is integrated this step.
    if (in.jump) body.Jump(jumpSpeed);

    // 5) Move the capsule (gravity + collide-and-slide handled inside).
    body.Move(reg, wishVel, dt);
}
}