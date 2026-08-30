#pragma once

#include "engine/physics/CharacterController.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

namespace engine {
class PhysicsWorld;
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
    bool  crouch      = false;   // hold to crouch; while swimming, descend
    bool  toggleView  = false;  // deprecated: camera mode is fixed before gameplay
    bool  toggleShoulder = false; // switch left/right third-person shoulder
};

// A ready-made first/third-person player: a kinematic capsule (CharacterController)
// driven by camera-relative movement, plus mouse-look yaw/pitch and a camera rig
// that can sit at the capsule's eye (first person) or orbit behind it (third
// person). One Update() per fixed step; then read ViewMatrix()/CapsuleTransform()
// for rendering. This is the human-player analogue of the AiAgent controller.
class PlayerController {
public:
    // Platformer: a side-on camera locked to a world axis, looking at the character
    // from the side (2.5D). The character moves along the screen axis and faces its
    // travel direction; the camera does not orbit.
    enum class View { FirstPerson, ThirdPerson, Isometric, Platformer };

    // How the character body turns in third person.
    //   CameraRelative  - the body always faces where the camera looks (strafe style);
    //                     rotating the camera rotates the character. (default)
    //   MovementDirection - the body turns to face its travel direction and the camera
    //                     orbits freely around it; it only rotates while moving.
    enum class FacingMode { CameraRelative, MovementDirection };

    CharacterController body;           // the kinematic capsule (position/size live here)
    View view = View::ThirdPerson;
    FacingMode facingMode = FacingMode::CameraRelative;
    float turnSpeed = 12.0f;            // MovementDirection: how fast the body turns to face travel
    float stairSmoothingSpeed = 12.0f;  // visual/camera easing after a physical step-up
    float isometricYaw = -45.0f;
    float isometricPitch = -35.0f;
    float isometricDistance = 12.0f;
    // Platformer side-view: camera looks along platformerYaw (-90 => toward -Z, so the
    // character runs along world X), tilted by platformerPitch, from platformerDistance.
    float platformerYaw = -90.0f;
    float platformerPitch = 0.0f;
    float platformerDistance = 12.0f;

    // Tunables;
    float walkSpeed        = 4.0f;
    float runSpeed         = 7.0f;
    float jumpSpeed        = 5.0f;
    float crouchSpeed      = 2.0f;
    float crouchedHeight   = 1.1f;        // total crouched capsule height
    float swimSpeed        = 3.5f;
    float swimVerticalSpeed = 2.5f;
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
    void SetPosition(const glm::vec3& p) {
        body.position = p;
        m_stepVisualOffset = glm::vec3(0.0f);
    }
    void SetCapsule(float radius, float height) {
        body.radius = glm::max(radius, 0.01f);
        body.height = glm::max(height, body.radius * 2.0f);
        m_standingHeight = body.height;
        crouchedHeight = glm::clamp(crouchedHeight,
            body.radius * 2.0f, m_standingHeight);
    }
    void ToggleView() {
        view = view == View::ThirdPerson ? View::FirstPerson
             : view == View::FirstPerson ? View::Isometric
             : view == View::Isometric   ? View::Platformer
                                         : View::ThirdPerson;
        m_cameraArmInitialized = false;
    }
    void SetPlatformerView(float yawDegrees, float pitchDegrees, float distance) {
        platformerYaw = yawDegrees;
        platformerPitch = glm::clamp(pitchDegrees, -89.0f, 89.0f);
        platformerDistance = glm::max(distance, 0.0f);
        if (view == View::Platformer) {
            m_yaw = platformerYaw;
            m_pitch = platformerPitch;
            m_cameraArmInitialized = false;
        }
    }
    // The authored orbit/side distance for the current view.
    float AuthoredCameraDistance() const {
        return view == View::Isometric  ? isometricDistance
             : view == View::Platformer ? platformerDistance
                                        : camDistance;
    }
    void SetIsometricView(float yawDegrees, float pitchDegrees, float distance) {
        isometricYaw = yawDegrees;
        isometricPitch = glm::clamp(pitchDegrees, -89.0f, 89.0f);
        isometricDistance = glm::max(distance, 0.0f);
        if (view == View::Isometric) {
            m_yaw = isometricYaw;
            m_pitch = isometricPitch;
            m_cameraArmInitialized = false;
        }
    }
    void ToggleShoulder() { rightShoulder = !rightShoulder; }
    void SetLockOnTarget(const glm::vec3& target) { m_lockOnTarget = target; }
    void ClearLockOnTarget() { m_lockOnTarget.reset(); }
    bool LockedOn() const { return m_lockOnTarget.has_value(); }
    void SetWaterSurface(bool overWater, float surfaceY) {
        m_overWater = overWater;
        m_waterSurfaceY = surfaceY;
    }

    // Advance one fixed step: apply look, move camera-relative, jump, and sweep the
    // capsule against the scene colliders in `reg`. Set movementEnabled=false while
    // a full-body animation action is active; look/view controls and gravity remain.
    void Update(ecs::Registry& reg, const PlayerInput& in, float dt,
                bool movementEnabled = true, const PhysicsWorld* physicsWorld = nullptr);

    // --- Queries for rendering -------------------------------------------
    float Yaw()   const { return m_yaw; }
    float Pitch() const { return m_pitch; }
    bool  Grounded() const { return body.grounded; }
    bool  Crouching() const { return m_crouching; }
    bool  Swimming() const { return m_swimming; }
    const glm::vec3& Position() const { return body.position; }   // capsule centre

    glm::vec3 LookDirection() const;      // full forward from yaw+pitch
    glm::vec3 EyePosition() const;        // capsule centre + eyeHeight
    glm::vec3 CameraPosition() const;     // eye (1st) or orbit point (3rd)
    glm::vec3 CameraTarget() const;       // what the camera looks at
    glm::mat4 ViewMatrix() const;         // ready for the renderer
    glm::quat Facing() const;             // yaw-only orientation for the capsule mesh
    float CurrentCameraDistance() const {
        return m_cameraArmInitialized ? m_currentCameraDistance
                                      : AuthoredCameraDistance();
    }

    // A Transform-friendly view: centre position + facing rotation. (Kept as raw
    // members to avoid pulling in the ECS Transform type here.)
    glm::vec3 CapsulePosition() const {
        return body.position + m_stepVisualOffset;
    }
    glm::quat CapsuleRotation() const { return Facing(); }

private:
    float m_yaw   = -90.0f;   // degrees; camera yaw (-90 looks toward -Z, matches Camera)
    float m_facingYaw = -90.0f; // degrees; the body's facing yaw (may lag the camera)
    bool  m_facingInitialized = false;
    float m_pitch =   0.0f;
    bool  m_prevShoulderToggle = false;
    float m_currentCameraDistance = 5.0f;
    bool  m_cameraArmInitialized = false;
    float m_currentShoulderOffset = 0.0f;
    bool m_shoulderInitialized = false;
    std::optional<glm::vec3> m_lockOnTarget;
    glm::vec3 m_stepVisualOffset{0.0f};
    float m_standingHeight = 1.8f;
    float m_waterSurfaceY = 0.0f;
    bool m_overWater = false;
    bool m_crouching = false;
    bool m_swimming = false;

    glm::vec3 ThirdPersonAnchor() const;
    glm::vec3 ThirdPersonOffset(float distance) const;
    float StanceCameraOffset() const;
};

} // namespace engine
