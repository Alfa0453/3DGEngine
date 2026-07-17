#include "engine/gameplay/PlayerController.h"

#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsWorld.h"

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
    const glm::vec3 anchor = ThirdPersonAnchor();
    return m_lockOnTarget ? glm::mix(anchor, *m_lockOnTarget, 0.5f) : anchor;
}

glm::vec3 PlayerController::CameraPosition() const {
    if (view == View::FirstPerson) return EyePosition();
    const glm::vec3 target = ThirdPersonAnchor();
    const glm::vec3 authoredOffset = ThirdPersonOffset(std::max(camDistance, 0.0f));
    const float authoredLength = glm::length(authoredOffset);
    const float distance = m_cameraArmInitialized ? m_currentCameraDistance : authoredLength;
    if (authoredLength <= 0.000001f) return target;
    return target + authoredOffset / authoredLength * distance;
}

glm::mat4 PlayerController::ViewMatrix() const {
    return glm::lookAt(CameraPosition(), CameraTarget(), kWorldUp);
}

glm::quat PlayerController::Facing() const {
    // Yaw-only orientation so an attached capsule/character mesh faces travel.
    return glm::angleAxis(glm::radians(-m_yaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

void PlayerController::Update(ecs::Registry& reg, const PlayerInput& in, float dt,
                              bool movementEnabled) {
    // 1) View toggle (edge-triggered off the held state).
    if (in.toggleView && !m_prevToggle) ToggleView();
    m_prevToggle = in.toggleView;
    if (in.toggleShoulder && !m_prevShoulderToggle && view == View::ThirdPerson) {
        ToggleShoulder();
    }
    m_prevShoulderToggle = in.toggleShoulder;

    const float safeDt = std::max(dt, 0.0f);
    const float desiredShoulder = shoulderCamera
        ? (rightShoulder ? 1.0f : -1.0f) * std::max(shoulderOffset, 0.0f)
        : 0.0f;
    if (!m_shoulderInitialized) {
        m_currentShoulderOffset = desiredShoulder;
        m_shoulderInitialized = true;
    } else {
        const float speed = std::max(shoulderSwitchSpeed, 0.0f);
        const float alpha = speed > 0.0f ? 1.0f - std::exp(-speed * safeDt) : 1.0f;
        m_currentShoulderOffset += (desiredShoulder - m_currentShoulderOffset) * alpha;
    }

    // 2) Mouse-look. dy is inverted so pushing the mouse up looks up.
    if (m_lockOnTarget) {
        const glm::vec3 origin = ThirdPersonAnchor();
        const glm::vec3 delta = *m_lockOnTarget - origin;
        if (glm::dot(delta, delta) > 0.000001f) {
            const glm::vec3 direction = glm::normalize(delta);
            const float desiredYaw = glm::degrees(std::atan2(direction.z, direction.x));
            const float desiredPitch = glm::degrees(std::asin(
                std::clamp(direction.y, -1.0f, 1.0f)));
            float yawDelta = std::fmod(desiredYaw - m_yaw + 540.0f, 360.0f) - 180.0f;
            const float speed = std::max(lockOnTrackingSpeed, 0.0f);
            const float alpha = speed > 0.0f ? 1.0f - std::exp(-speed * safeDt) : 1.0f;
            m_yaw += yawDelta * alpha;
            m_pitch += (desiredPitch - m_pitch) * alpha;
        }
    } else {
        m_yaw   += in.lookYaw   * lookSensitivity;
        m_pitch -= in.lookPitch * lookSensitivity;
    }
    const float lo = (view == View::FirstPerson) ? fpMinPitch : tpMinPitch;
    const float hi = (view == View::FirstPerson) ? fpMaxPitch : tpMaxPitch;
    m_pitch = std::clamp(m_pitch, lo, hi);
    if (m_yaw > 360.0f) m_yaw -= 360.0f; else if (m_yaw < -360.0f) m_yaw += 360.0f;

    // 3) Camera-relative movement on the ground plane.
    const glm::vec3 fwd  = YawForward(m_yaw);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, kWorldUp));
    glm::vec3 wish(0.0f);
    if (movementEnabled) {
        wish = fwd * in.moveForward + right * in.moveRight;
    }
    const float wl = glm::length(wish);
    if (wl > 1.0f) wish /= wl; 
    const float speed = in.sprint ? runSpeed : walkSpeed;                     // no faster on diagonals
    const glm::vec3 wishVel = wish * speed;

    // 4) Jump before the sweep so the upward velocity is integrated this step.
    if (movementEnabled && in.jump) body.Jump(jumpSpeed);

    // 5) Move the capsule (gravity + collide-and-slide handled inside).
    body.Move(reg, wishVel, dt);

    // 6) Resolve the third-person spring arm. Obstructions retract immediately so
    // the camera never spends a frame inside a wall; returning to the authored
    // distance is exponentially smoothed once the path clears.
    if (view == View::FirstPerson) {
        m_cameraArmInitialized = false;
    } else {
        const glm::vec3 authoredOffset = ThirdPersonOffset(std::max(camDistance, 0.0f));
        const float authoredDistance = glm::length(authoredOffset);
        if (!m_cameraArmInitialized) {
            m_currentCameraDistance = authoredDistance;
            m_cameraArmInitialized = true;
        }

        float safeDistance = authoredDistance;
        if (camCollision && authoredDistance > 0.0f) {
            const glm::vec3 target = ThirdPersonAnchor();
            const glm::vec3 desired = target + authoredOffset;
            PhysicsWorld query;
            const RaycastHit hit = query.SphereCast(
                reg, target, desired, std::max(camProbeRadius, 0.0f));
            if (hit.hit) {
                safeDistance = std::clamp(
                    hit.distance - std::max(camCollisionPadding, 0.0f),
                    0.0f, authoredDistance);
            }
        }

        if (safeDistance < m_currentCameraDistance) {
            m_currentCameraDistance = safeDistance;
        } else {
            const float returnSpeed = std::max(camReturnSpeed, 0.0f);
            const float alpha = returnSpeed > 0.0f
                ? 1.0f - std::exp(-returnSpeed * std::max(dt, 0.0f))
                : 1.0f;
            m_currentCameraDistance +=
                (safeDistance - m_currentCameraDistance) * alpha;
        }
    }
}

glm::vec3 PlayerController::ThirdPersonOffset(float distance) const {
    const glm::vec3 forward = LookDirection();
    glm::vec3 right = glm::cross(forward, kWorldUp);
    if (glm::dot(right, right) <= 0.000001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);
    return -forward * std::max(distance, 0.0f) + right * m_currentShoulderOffset;
}

glm::vec3 PlayerController::ThirdPersonAnchor() const {
    return body.position + glm::vec3(0.0f, camTargetHeight, 0.0f);
}
}
