#pragma once

#include "engine/physics/CharacterController.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

namespace engine {
namespace ecs { class Registry; }

// Per-frame intent for the player, filled by the caller from whatever input
// source it likes (keyboard/gamepad/replay). Keeping input *injected* -- rather
// than reading a Window inside the controller -- means PlayerController carries no
// windowing dependency and is fully unit-testable headless (the same pattern the
// AiAgent uses for perception). moveForward/moveRight are in [-1,1]; lookYaw /
// lookPitch are raw mouse deltas in pixels (scaled by lookSensitivity here).
struct PlayerInput {
    float moveForward = 0.0f;   // +1 = forward (W), -1 = back (S)
    float moveRight   = 0.0f;   // +1 = right (D),  -1 = left (A)
    float lookYaw     = 0.0f;   // mouse dx this frame
    float lookPitch   = 0.0f;   // mouse dy this frame (down = look down)
    bool  jump        = false;
    bool  sprint      = false;
    bool  toggleView  = false;  // held state; the controller edge-detects it
    bool  toggleShoulder = false; // switch left/right third-person shoulder
};

// A ready-made first/third-person player: a kinematic capsule (CharacterController)
// driven by camera-relative movement, plus mouse-look yaw/pitch and a camera rig
// that can sit at the capsule's eye (first person) or orbit behind it (third
// person). One Update() per fixed step; then read ViewMatrix()/CapsuleTransform()
// for rendering. This is the human-player analogue of the AiAgent controller.
class PlayerController {
public:
    enum class View { FirstPerson, ThirdPerson };

    CharacterController body;           // the kinematic capsule (position/size live here)
    View view = View::ThirdPerson;

    // Tunables;
    float walkSpeed        = 4.0f;
    float runSpeed         = 7.0f;
    float jumpSpeed        = 5.0f;
    float lookSensitivity  = 0.1f;        // degrees per pixel of mouse motion
    float eyeHeight        = 0.6f;        // eye offset above the capsule centre (1st person)
    float camDistance      = 5.0f;        // orbit distance (3rd person)
    float camTargetHeight  = 1.0f;        // look-at height above the capsule centre (3rd person)
    bool  camCollision     = true;        // retract the spring arm around solid colliders
    float camProbeRadius   = 0.20f;       // swept camera volume; larger avoids thin-wall clipping
    float camCollisionPadding = 0.08f;    // extra clearance before the obstruction
    float camReturnSpeed   = 8.0f;        // exponential return speed after an obstruction clears
    bool  shoulderCamera   = false;       // offset the third-person arm for aiming
    float shoulderOffset   = 0.65f;       // horizontal distance from the player
    float shoulderSwitchSpeed = 12.0f;    // interpolation speed when changing sides
    bool  rightShoulder    = true;
    bool  lockOnEnabled    = false;
    float lockOnRange      = 18.0f;
    float lockOnViewAngle  = 55.0f;
    float lockOnTargetHeight = 1.0f;
    float lockOnTrackingSpeed = 10.0f;    // yaw/pitch interpolation toward a target
    float fpMinPitch = -89.0f, fpMaxPitch = 89.0f;   // first-person pitch clamp
    float tpMinPitch = -35.0f, tpMaxPitch = 75.0f;   // third-person pitch clamp

    // Place the player (capsule centre) and optionally set the capsule size.
    void SetPosition(const glm::vec3& p) { body.position = p; }
    void SetCapsule(float radius, float height) { body.radius = radius; body.height = height; }
    void ToggleView() {
        view = (view == View::FirstPerson) ? View::ThirdPerson : View::FirstPerson;
        m_cameraArmInitialized = false;
    }
    void ToggleShoulder() { rightShoulder = !rightShoulder; }
    void SetLockOnTarget(const glm::vec3& target) { m_lockOnTarget = target; }
    void ClearLockOnTarget() { m_lockOnTarget.reset(); }
    bool LockedOn() const { return m_lockOnTarget.has_value(); }

    // Advance one fixed step: apply look, move camera-relative, jump, and sweep the
    // capsule against the scene colliders in `reg`. Set movementEnabled=false while
    // a full-body animation action is active; look/view controls and gravity remain.
    void Update(ecs::Registry& reg, const PlayerInput& in, float dt,
                bool movementEnabled = true);

    // --- Queries for rendering -------------------------------------------
    float Yaw()   const { return m_yaw; }
    float Pitch() const { return m_pitch; }
    bool  Grounded() const { return body.grounded; }
    const glm::vec3& Position() const { return body.position; }   // capsule centre

    glm::vec3 LookDirection() const;      // full forward from yaw+pitch
    glm::vec3 EyePosition() const;        // capsule centre + eyeHeight
    glm::vec3 CameraPosition() const;     // eye (1st) or orbit point (3rd)
    glm::vec3 CameraTarget() const;       // what the camera looks at
    glm::mat4 ViewMatrix() const;         // ready for the renderer
    glm::quat Facing() const;             // yaw-only orientation for the capsule mesh
    float CurrentCameraDistance() const {
        return m_cameraArmInitialized ? m_currentCameraDistance : camDistance;
    }

    // A Transform-friendly view: centre position + facing rotation. (Kept as raw
    // members to avoid pulling in the ECS Transform type here.)
    glm::vec3 CapsulePosition() const { return body.position; }
    glm::quat CapsuleRotation() const { return Facing(); }

private:
    float m_yaw   = -90.0f;   // degrees; -90 looks toward -Z (matches Camera)
    float m_pitch =   0.0f;
    bool  m_prevToggle = false;
    bool  m_prevShoulderToggle = false;
    float m_currentCameraDistance = 5.0f;
    bool  m_cameraArmInitialized = false;
    float m_currentShoulderOffset = 0.0f;
    bool m_shoulderInitialized = false;
    std::optional<glm::vec3> m_lockOnTarget;

    glm::vec3 ThirdPersonAnchor() const;
    glm::vec3 ThirdPersonOffset(float distance) const;
};

} // namespace engine
