#pragma once

#include <glm/glm.hpp>

namespace engine {

class Camera;

struct CameraPose {
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

class CameraBlend {
public:
    enum class Easing {
        Linear = 0,
        SmoothStep = 1,
        EaseIn = 2,
        EaseOut = 3,
    };

    void Start(const CameraPose& from, const CameraPose& to,
               float durationSeconds, Easing easing = Easing::SmoothStep);
    CameraPose Update(float dt);
    void Cancel() { m_active = false; }

    bool Active() const { return m_active; }
    float Progress() const;
    const CameraPose& Current() const { return m_current; }

    static CameraPose FromCamera(const Camera& camera, float targetDistance = 10.0f);
    static void Apply(const CameraPose& pose, Camera& camera);

private:
    static float Ease(float t, Easing easing);
    CameraPose m_from;
    CameraPose m_to;
    CameraPose m_current;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    Easing m_easing = Easing::SmoothStep;
    bool m_active = false;
};

} // namespace engine
