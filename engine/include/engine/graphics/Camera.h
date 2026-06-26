#pragma once

#include <glm/glm.hpp>

namespace engine {

// A first-person "fly" camera. It stores a position and an orientation
// expressed as yaw + pitch (Euler angles), and produces the view and
// projection matrices the renderer needs.
//
// Yaw   = rotation around the vertical (Y) axis — looking left/right.
// Pitch = rotation around the side (X) axis   — looking up/down.
// We keep angles instead of a matrix so mouse-look is just "add to yaw/pitch".
class Camera {
public:
    explicit Camera(const glm::vec3& position = glm::vec3(0.0f, 0.0f, 3.0f));

    // --- Mouse look ------------------------------------------------------
    // Adjust orientation by the given degrees. Pitch is clamped so the camera
    // can never flip over backwards.
    void AddYawPitch(float deltaYawDegrees, float deltaPitchDegrees);

    // --- Movement (amounts already scaled by speed * dt by the caller) ---
    void MoveForward(float amount);  // along the look direction
    void MoveRight(float amount);    // strafe
    void MoveUp(float amount);       // straight up/down (world axis)

    // Aim the camera at a world-space point (sets yaw/pitch). Handy for a
    // fixed camera that should look at a target rather than be flown around.
    void LookAt(const glm::vec3& target);

    // --- Matrices --------------------------------------------------------
    glm::mat4 ViewMatrix() const;        // camera-to-world transform
    glm::mat4 ProjectionMatrix(float aspect) const;  // perspective projection

    const glm::vec3& Position() const { return m_position; }
    const glm::vec3& Front() const { return m_front; }

    // Lens settings, freely adjustable.
    float fov       = 45.0f;   // vertical field of view, in degrees
    float nearPlane = 0.1f;    // near clipping plane
    float farPlane  = 100.0f;  // far clipping plane

private:
    // Recompute the front/right/up basis vectors from yaw and pitch.
    void UpdateVectors();

    glm::vec3 m_position;  // world-space position of the camera
    float m_yaw = -90.0f;   // -90 so the default look direction is -Z
    float m_pitch = 0.0f;   // 0 so the default look direction is horizontal

    glm::vec3 m_front{0.0f, 0.0f, -1.0f};  // direction the camera is looking
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};   // right vector of the camera
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};      // up vector of the camera

    // The fixed "up" of the world, used to derive the camera's right vector.
    // Defined in the .cpp (GLM's vec3 ctor is not guaranteed constexpr).
    static const glm::vec3 kWorldUp;
};

} // namespace engine