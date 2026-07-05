#include "engine/graphics/Camera.h"

#include <glm/gtc/matrix_transform.hpp>     // glm::lookAt, glm::perspective

#include <algorithm>     // std::clamp
#include <cmath>         // std::cos, std::sin

namespace engine {

const glm::vec3 Camera::kWorldUp{0.0f, 1.0f, 0.0f};

Camera::Camera(const glm::vec3& position) : m_position(position) {
    UpdateVectors();
}

void Camera::AddYawPitch(float deltaYawDegrees, float deltaPitchDegrees)
{
    m_yaw += deltaYawDegrees;
    m_pitch += deltaPitchDegrees;

    // Stop just short of straight up/down. At exactly +/-90 the camera's
    // direction becomes parallel to world-up and the basis collapses (gimbal
    // lock / a flip), so we clamp to 89 degrees.
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

    UpdateVectors();
}

void Camera::MoveForward(float amount) { m_position += m_front * amount; }

void Camera::MoveRight(float amount) { m_position += m_right * amount; }

void Camera::MoveUp(float amount) { m_position += kWorldUp * amount; }

void Camera::SetPosition(const glm::vec3& position) { m_position = position; }

void Camera::LookAt(const glm::vec3& target) {
    const glm::vec3 dir = glm::normalize(target - m_position);
    // Recover yaw/pitch from the direction (inverse of UpdateVectors).
    m_pitch = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
    m_yaw   = glm::degrees(std::atan2(dir.z, dir.x));
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
    UpdateVectors();
}

glm::mat4 Camera::ViewMatrix() const {
    // lookAt builds a matrix that places the camera at m_position looking
    // toward m_position + m_front, with m_up defining the roll.
    return glm::lookAt(m_position, m_position + m_front, m_up);
}
glm::mat4 Camera::ProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}
void Camera::UpdateVectors() {
    // Convert yaw/pitch (Euler angles) into a unit direction vector. This is
    // the standard spherical-to-Cartesian conversion.
    const float yawRad = glm::radians(m_yaw);
    const float pitchRad = glm::radians(m_pitch);

    glm::vec3 front;
    front.x = std::cos(yawRad) * std::cos(pitchRad);
    front.y = std::sin(pitchRad);
    front.z = std::sin(yawRad) * std::cos(pitchRad);
    m_front = glm::normalize(front);

    // Right is perpendicular to the look direction and world-up; up is then
    // perpendicular to both. Re-deriving up (instead of using world-up) keeps
    // the view correct when looking up or down.
    m_right = glm::normalize(glm::cross(m_front, kWorldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}

} // namespace engine
