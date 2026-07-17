#pragma once

#include "engine/graphics/CameraBlend.h"

#include <cstddef>
#include <string>
#include <vector>

namespace engine {

struct CameraSequenceShot {
    enum class Path { Linear = 0, CatmullRom = 1 };
    CameraPose pose;
    float travelDuration = 1.0f;
    float holdDuration = 0.25f;
    CameraBlend::Easing easing = CameraBlend::Easing::SmoothStep;
    Path path = Path::Linear;
    std::string eventName;
};

class CameraSequencePlayer {
public:
    void Start(const CameraPose& current, std::vector<CameraSequenceShot> shots,
               bool loop = false);
    CameraPose Update(float dt);
    CameraPose Seek(float seconds);
    CameraPose SkipToEnd();
    void Stop();
    std::vector<std::string> TakeEvents();

    bool Active() const { return m_active; }
    bool Holding() const { return m_holding; }
    std::size_t ShotIndex() const { return m_shotIndex; }
    std::size_t ShotCount() const { return m_shots.size(); }
    const CameraPose& Current() const { return m_current; }
    float Time() const { return m_time; }
    float Duration() const { return m_duration; }
    static glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                const glm::vec3& p2, const glm::vec3& p3, float t);

private:
    void BeginShot(const CameraPose& from, std::size_t index);
    CameraPose SampleTransition(float t) const;
    static float Ease(float t, CameraBlend::Easing easing);

    std::vector<CameraSequenceShot> m_shots;
    std::vector<std::string> m_events;
    CameraPose m_current;
    CameraPose m_initial;
    CameraPose m_transitionFrom;
    std::size_t m_shotIndex = 0;
    float m_transitionElapsed = 0.0f;
    float m_transitionDuration = 0.0f;
    float m_holdElapsed = 0.0f;
    float m_time = 0.0f;
    float m_duration = 0.0f;
    bool m_loop = false;
    bool m_holding = false;
    bool m_active = false;
};

} // namespace engine
