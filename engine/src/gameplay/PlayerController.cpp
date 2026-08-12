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
    return CapsulePosition() + glm::vec3(0.0f, eyeHeight - StanceCameraOffset(), 0.0f);
}

glm::vec3 PlayerController::CameraTarget() const {
    if (view == View::FirstPerson) return EyePosition() + LookDirection();
    const glm::vec3 anchor = ThirdPersonAnchor();
    return m_lockOnTarget ? glm::mix(anchor, *m_lockOnTarget, 0.5f) : anchor;
}

glm::vec3 PlayerController::CameraPosition() const {
    if (view == View::FirstPerson) return EyePosition();
    const glm::vec3 target = ThirdPersonAnchor();
    const float authoredDistance = AuthoredCameraDistance();
    const glm::vec3 authoredOffset = ThirdPersonOffset(std::max(authoredDistance, 0.0f));
    const float authoredLength = glm::length(authoredOffset);
    const float distance = m_cameraArmInitialized ? m_currentCameraDistance : authoredLength;
    if (authoredLength <= 0.000001f) return target;
    return target + authoredOffset / authoredLength * distance;
}

glm::mat4 PlayerController::ViewMatrix() const {
    return glm::lookAt(CameraPosition(), CameraTarget(), kWorldUp);
}

glm::quat PlayerController::Facing() const {
    // Yaw-only orientation for the character mesh. Uses the body facing yaw, which
    // tracks the camera in CameraRelative mode and lags toward travel in
    // MovementDirection mode (so the camera can orbit a still-facing character).
    return glm::angleAxis(glm::radians(-m_facingYaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

void PlayerController::Update(ecs::Registry& reg, const PlayerInput& in, float dt,
                              bool movementEnabled) {
    if (in.toggleShoulder && !m_prevShoulderToggle && view == View::ThirdPerson) {
        ToggleShoulder();
    }
    m_prevShoulderToggle = in.toggleShoulder;

    const float safeDt = std::max(dt, 0.0f);

    // Water volumes are supplied by the host before each fixed step. Enter once
    // the capsule's feet cross the surface and remain in swim mode until leaving
    // the water footprint. Swimming uses crouch as descend and Space as ascend.
    const float feetY = body.position.y - body.height * 0.5f;
    const float swimExitHeight = m_waterSurfaceY + body.radius * 0.5f;
    m_swimming = m_overWater && feetY <= (m_swimming
        ? swimExitHeight : m_waterSurfaceY + 0.05f);
    if (m_swimming) {
        if (body.height < m_standingHeight) body.TrySetHeight(reg, m_standingHeight);
        m_crouching = false;
    } else if (in.crouch) {
        const float target = glm::clamp(crouchedHeight,
            body.radius * 2.0f, m_standingHeight);
        body.TrySetHeight(reg, target);
        m_crouching = body.height < m_standingHeight - 0.001f;
    } else if (m_crouching) {
        if (body.TrySetHeight(reg, m_standingHeight)) m_crouching = false;
    }
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

    // 2) Mouse-look. Isometric view keeps an authored fixed angle; the player
    // moves relative to that angle but cannot orbit it accidentally.
    if (view == View::Isometric) {
        m_yaw = isometricYaw;
        m_pitch = glm::clamp(isometricPitch, -89.0f, 89.0f);
    } else if (view == View::Platformer) {
        // Side-on: the camera holds a fixed axis; the player cannot orbit it.
        m_yaw = platformerYaw;
        m_pitch = glm::clamp(platformerPitch, -89.0f, 89.0f);
    } else if (m_lockOnTarget) {
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
    const bool fixedAngleView = view == View::Isometric || view == View::Platformer;
    const float lo = (view == View::FirstPerson) ? fpMinPitch
                   : (fixedAngleView ? -89.0f : tpMinPitch);
    const float hi = (view == View::FirstPerson) ? fpMaxPitch
                   : (fixedAngleView ? 89.0f : tpMaxPitch);
    m_pitch = std::clamp(m_pitch, lo, hi);
    if (m_yaw > 360.0f) m_yaw -= 360.0f; else if (m_yaw < -360.0f) m_yaw += 360.0f;

    // 3) Camera-relative movement. Ground movement stays horizontal; swimming
    // follows the look pitch and supports explicit ascend/descend controls.
    const glm::vec3 fwd  = m_swimming ? LookDirection() : YawForward(m_yaw);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, kWorldUp));
    glm::vec3 wish(0.0f);
    if (movementEnabled) {
        // Platformer: motion is locked to the screen (side) axis only -- left/right
        // input runs the character along it; forward/back is ignored so it stays 2.5D.
        wish = (view == View::Platformer)
            ? right * in.moveRight
            : fwd * in.moveForward + right * in.moveRight;
    }
    const float wl = glm::length(wish);
    if (wl > 1.0f) wish /= wl;
    // Sprint is a grounded movement mode. Air control remains available at walk
    // speed, but holding Shift during a jump/fall cannot accelerate the capsule.
    const bool sprinting = in.sprint && body.grounded && !in.jump
        && !m_crouching && !m_swimming;
    const float speed = m_swimming ? swimSpeed
                      : m_crouching ? crouchSpeed
                      : sprinting ? runSpeed : walkSpeed;
    glm::vec3 wishVel = wish * speed;
    if (m_swimming && movementEnabled) {
        float vertical = (in.jump ? 1.0f : 0.0f) - (in.crouch ? 1.0f : 0.0f);
        wishVel.y += vertical * swimVerticalSpeed;
        const float maxSpeed = std::max(swimSpeed, swimVerticalSpeed);
        const float length = glm::length(wishVel);
        if (length > maxSpeed && length > 0.0001f) wishVel *= maxSpeed / length;
    }

    // Body facing. CameraRelative: the mesh tracks the camera yaw (strafe style).
    // MovementDirection: the mesh turns toward its travel direction while the camera
    // orbits freely; it only rotates while moving, and holds its heading when idle.
    if (!m_facingInitialized) { m_facingYaw = m_yaw; m_facingInitialized = true; }
    const bool orientToMovement =
        (facingMode == FacingMode::MovementDirection
            || view == View::Isometric || view == View::Platformer)
        && view != View::FirstPerson && !m_lockOnTarget;
    bool hasFacingTarget = !orientToMovement;
    float targetFacingYaw = m_yaw;
    if (orientToMovement && wl > 0.1f) {
        targetFacingYaw = glm::degrees(std::atan2(wish.z, wish.x));
        hasFacingTarget = true;
    }
    if (hasFacingTarget) {
        const float delta =
            std::fmod(targetFacingYaw - m_facingYaw + 540.0f, 360.0f) - 180.0f;
        const float response = std::max(turnSpeed, 0.0f);
        const float alpha = response > 0.0f
            ? 1.0f - std::exp(-response * safeDt)
            : 1.0f;
        m_facingYaw += delta * alpha;
    }

    // 4) Jump before the sweep so the upward velocity is integrated this step.
    if (movementEnabled && in.jump && !m_swimming && !m_crouching) body.Jump(jumpSpeed);

    // 5) Move the physical capsule (gravity + collide-and-slide handled inside).
    // Step-up has to place the collision capsule immediately on the higher tread,
    // but retain the old rendered height and release that offset gradually. This
    // keeps collision authoritative while preventing the mesh and camera from
    // inheriting a one-frame vertical pop.
    const glm::vec3 preMovePosition = body.position;
    const bool preMoveGrounded = body.grounded;
    if (m_swimming) {
        body.MoveFree(reg, wishVel, dt);
        // Keep the character close enough to the surface to remain visibly in
        // the water without allowing repeated Space input to fly above it.
        const float maxCentre = m_waterSurfaceY
            + body.height * 0.5f - body.radius * 0.35f;
        if (body.position.y > maxCentre) {
            body.position.y = maxCentre;
            body.velocity.y = std::min(body.velocity.y, 0.0f);
        }
    } else {
        body.Move(reg, wishVel, dt);
    }
    const float stepRise = body.position.y - preMovePosition.y;
    if (preMoveGrounded && body.grounded
        && stepRise > 0.01f
        && stepRise <= std::max(body.stepHeight, 0.0f) + 0.03f) {
        const glm::vec3 requestedPosition =
            preMovePosition + glm::vec3(wishVel.x * safeDt, 0.0f, wishVel.z * safeDt);
        m_stepVisualOffset += requestedPosition - body.position;
        const float maxLag = std::max(body.radius + body.stepHeight * 2.0f, 0.25f);
        const float lagLength = glm::length(m_stepVisualOffset);
        if (lagLength > maxLag) m_stepVisualOffset *= maxLag / lagLength;
    }
    const float stairResponse = std::max(stairSmoothingSpeed, 0.0f);
    const float stairAlpha = stairResponse > 0.0f
        ? 1.0f - std::exp(-stairResponse * safeDt)
        : 1.0f;
    m_stepVisualOffset += (glm::vec3(0.0f) - m_stepVisualOffset) * stairAlpha;

    // 6) Resolve the third-person spring arm. Obstructions retract immediately so
    // the camera never spends a frame inside a wall; returning to the authored
    // distance is exponentially smoothed once the path clears.
    if (view == View::FirstPerson) {
        m_cameraArmInitialized = false;
    } else {
        const float configuredDistance = AuthoredCameraDistance();
        const glm::vec3 authoredOffset =
            ThirdPersonOffset(std::max(configuredDistance, 0.0f));
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
                reg, target, desired, std::max(camProbeRadius, 0.0f),
                ecs::kNull, ecs::CollisionLayer::CameraBlockers);
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
    return CapsulePosition() + glm::vec3(0.0f,
        camTargetHeight - StanceCameraOffset(), 0.0f);
}

float PlayerController::StanceCameraOffset() const {
    return std::max(m_standingHeight - body.height, 0.0f);
}
}
