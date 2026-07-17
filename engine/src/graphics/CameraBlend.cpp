#include "engine/graphics/CameraBlend.h"

#include "engine/graphics/Camera.h"

#include <algorithm>

namespace engine {

namespace {

CameraPose Interpolate(const CameraPose& a, const CameraPose& b, float t) {
    CameraPose pose;
    pose.position = glm::mix(a.position, b.position, t);
    pose.target = glm::mix(a.target, b.target, t);
    pose.fov = glm::mix(a.fov, b.fov, t);
    pose.nearPlane = glm::mix(a.nearPlane, b.nearPlane, t);
    pose.farPlane = glm::mix(a.farPlane, b.farPlane, t);
    return pose;
}

} // namespace

void CameraBlend::Start(const CameraPose& from, const CameraPose& to,
                        float durationSeconds, Easing easing) {
    m_from = from;
    m_to = to;
    m_current = from;
    m_duration = std::max(durationSeconds, 0.0f);
    m_elapsed = 0.0f;
    m_easing = easing;
    m_active = m_duration > 0.0f;
    if (!m_active) m_current = to;
}

CameraPose CameraBlend::Update(float dt) {
    if (!m_active) return m_current;
    m_elapsed = std::min(m_elapsed + std::max(dt, 0.0f), m_duration);
    const float t = Progress();
    m_current = Interpolate(m_from, m_to, Ease(t, m_easing));
    if (m_elapsed >= m_duration) {
        m_current = m_to;
        m_active = false;
    }
    return m_current;
}

float CameraBlend::Progress() const {
    if (m_duration <= 0.0f) return 1.0f;
    return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
}

CameraPose CameraBlend::FromCamera(const Camera& camera, float targetDistance) {
    CameraPose pose;
    pose.position = camera.Position();
    pose.target = pose.position + camera.Front() * std::max(targetDistance, 0.01f);
    pose.fov = camera.fov;
    pose.nearPlane = camera.nearPlane;
    pose.farPlane = camera.farPlane;
    return pose;
}

void CameraBlend::Apply(const CameraPose& pose, Camera& camera) {
    camera.SetPosition(pose.position);
    const glm::vec3 delta = pose.target - pose.position;
    camera.LookAt(glm::dot(delta, delta) > 0.000001f
        ? pose.target
        : pose.position + glm::vec3(0.0f, 0.0f, -1.0f));
    camera.fov = std::clamp(pose.fov, 10.0f, 120.0f);
    camera.nearPlane = std::max(pose.nearPlane, 0.001f);
    camera.farPlane = std::max(pose.farPlane, camera.nearPlane + 0.01f);
}

float CameraBlend::Ease(float t, Easing easing) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseIn:
        return t * t;
    case Easing::EaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case Easing::SmoothStep:
    default:
        return t * t * (3.0f - 2.0f * t);
    }
}

} // namespace engine
