#include "engine/graphics/CameraSequence.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine {

void CameraSequencePlayer::Start(const CameraPose& current,
                                 std::vector<CameraSequenceShot> shots,
                                 bool loop) {
    Stop();
    m_initial = current;
    m_current = current;
    m_shots = std::move(shots);
    m_loop = loop;
    for (const CameraSequenceShot& shot : m_shots) {
        m_duration += std::max(shot.travelDuration, 0.0f)
            + std::max(shot.holdDuration, 0.0f);
    }
    if (m_shots.empty()) return;
    m_active = true;
    BeginShot(current, 0);
}

CameraPose CameraSequencePlayer::Update(float dt) {
    if (!m_active) return m_current;
    float remaining = std::max(dt, 0.0f);
    int guard = 0;
    while (m_active && guard++ < 4096) {
        if (!m_holding) {
            const float available = std::max(m_transitionDuration - m_transitionElapsed, 0.0f);
            const float consumed = std::min(remaining, available);
            m_transitionElapsed += consumed;
            m_time += consumed;
            remaining -= consumed;
            const float t = m_transitionDuration <= 0.0f
                ? 1.0f : m_transitionElapsed / m_transitionDuration;
            m_current = SampleTransition(Ease(t, m_shots[m_shotIndex].easing));
            if (t < 1.0f) break;
            m_current = m_shots[m_shotIndex].pose;
            m_holding = true;
            m_holdElapsed = 0.0f;
            if (!m_shots[m_shotIndex].eventName.empty()) {
                m_events.push_back(m_shots[m_shotIndex].eventName);
            }
            if (remaining <= 0.0f) break;
        } else {
            const float hold = std::max(m_shots[m_shotIndex].holdDuration, 0.0f);
            const float available = std::max(hold - m_holdElapsed, 0.0f);
            const float consumed = std::min(remaining, available);
            m_holdElapsed += consumed;
            m_time += consumed;
            remaining -= consumed;
            if (m_holdElapsed < hold) break;

            std::size_t next = m_shotIndex + 1;
            if (next >= m_shots.size()) {
                if (!m_loop) {
                    m_active = false;
                    m_time = m_duration;
                    break;
                }
                next = 0;
                m_time = 0.0f;
            }
            BeginShot(m_current, next);
            if (remaining <= 0.0f) break;
        }
    }
    return m_current;
}

CameraPose CameraSequencePlayer::Seek(float seconds) {
    if (m_shots.empty()) return m_current;
    const std::vector<CameraSequenceShot> shots = m_shots;
    const bool loop = m_loop;
    const CameraPose initial = m_initial;
    Start(initial, shots, loop);
    const float target = loop && m_duration > 0.0f
        ? std::fmod(std::max(seconds, 0.0f), m_duration)
        : std::clamp(seconds, 0.0f, m_duration);
    Update(target);
    m_events.clear();
    return m_current;
}

CameraPose CameraSequencePlayer::SkipToEnd() {
    if (!m_shots.empty()) {
        m_shotIndex = m_shots.size() - 1;
        m_current = m_shots.back().pose;
    }
    m_time = m_duration;
    m_holding = false;
    m_active = false;
    return m_current;
}

void CameraSequencePlayer::Stop() {
    m_shots.clear();
    m_events.clear();
    m_shotIndex = 0;
    m_transitionElapsed = 0.0f;
    m_transitionDuration = 0.0f;
    m_holdElapsed = 0.0f;
    m_time = 0.0f;
    m_duration = 0.0f;
    m_loop = false;
    m_holding = false;
    m_active = false;
}

std::vector<std::string> CameraSequencePlayer::TakeEvents() {
    std::vector<std::string> events = std::move(m_events);
    m_events.clear();
    return events;
}

void CameraSequencePlayer::BeginShot(const CameraPose& from, std::size_t index) {
    m_shotIndex = index;
    m_transitionFrom = from;
    m_transitionElapsed = 0.0f;
    m_transitionDuration = std::max(m_shots[index].travelDuration, 0.0f);
    m_holding = m_transitionDuration <= 0.0f;
    m_holdElapsed = 0.0f;
    if (m_holding) {
        m_current = m_shots[index].pose;
        if (!m_shots[index].eventName.empty()) m_events.push_back(m_shots[index].eventName);
    }
}

CameraPose CameraSequencePlayer::SampleTransition(float t) const {
    const CameraSequenceShot& shot = m_shots[m_shotIndex];
    CameraPose pose;
    if (shot.path == CameraSequenceShot::Path::CatmullRom) {
        const CameraPose& previous = m_shotIndex > 0
            ? m_shots[m_shotIndex - 1].pose : m_transitionFrom;
        const CameraPose& next = m_shotIndex + 1 < m_shots.size()
            ? m_shots[m_shotIndex + 1].pose : shot.pose;
        pose.position = CatmullRom(
            previous.position, m_transitionFrom.position,
            shot.pose.position, next.position, t);
        pose.target = CatmullRom(
            previous.target, m_transitionFrom.target,
            shot.pose.target, next.target, t);
    } else {
        pose.position = glm::mix(m_transitionFrom.position, shot.pose.position, t);
        pose.target = glm::mix(m_transitionFrom.target, shot.pose.target, t);
    }
    pose.fov = glm::mix(m_transitionFrom.fov, shot.pose.fov, t);
    pose.nearPlane = glm::mix(m_transitionFrom.nearPlane, shot.pose.nearPlane, t);
    pose.farPlane = glm::mix(m_transitionFrom.farPlane, shot.pose.farPlane, t);
    return pose;
}

glm::vec3 CameraSequencePlayer::CatmullRom(
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& p2, const glm::vec3& p3, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
        + (-p0 + p2) * t
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float CameraSequencePlayer::Ease(float t, CameraBlend::Easing easing) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case CameraBlend::Easing::Linear: return t;
    case CameraBlend::Easing::EaseIn: return t * t;
    case CameraBlend::Easing::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
    case CameraBlend::Easing::SmoothStep:
    default: return t * t * (3.0f - 2.0f * t);
    }
}

} // namespace engine
