#include "engine/graphics/CameraShake.h"

#include "engine/graphics/Camera.h"

#include <algorithm>
#include <cmath>

namespace engine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float NextPhase(std::uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed & 0xffffu) / 65535.0f * (2.0f * kPi);
}

float Envelope(float elapsed, float duration) {
    if (duration <= 0.0f) return 0.0f;
    const float t = std::clamp(elapsed / duration, 0.0f, 1.0f);
    const float remaining = 1.0f - t;
    return remaining * remaining;
}

float Wave(float time, float frequency, float phase, float frequencyScale) {
    return std::sin(time * std::max(frequency, 0.0f) * frequencyScale
                    * (2.0f * kPi) + phase);
}

} // namespace

void CameraShake::Start(const CameraShakeSettings& settings) {
    CameraShakeSettings safe = settings;
    safe.duration = std::max(safe.duration, 0.0f);
    safe.frequency = std::max(safe.frequency, 0.0f);
    safe.translationAmplitude = glm::max(safe.translationAmplitude, glm::vec3(0.0f));
    safe.rotationAmplitudeDegrees = glm::max(
        safe.rotationAmplitudeDegrees, glm::vec2(0.0f));
    safe.fovAmplitude = std::max(safe.fovAmplitude, 0.0f);
    if (safe.duration <= 0.0f) return;

    Instance instance;
    instance.settings = safe;
    instance.translationPhase = {
        NextPhase(m_seed), NextPhase(m_seed), NextPhase(m_seed)};
    instance.rotationPhase = {NextPhase(m_seed), NextPhase(m_seed)};
    instance.fovPhase = NextPhase(m_seed);
    if (m_instances.size() >= 32) {
        m_instances.erase(m_instances.begin());
    }
    m_instances.push_back(instance);
}

void CameraShake::StartImpulse(float intensity, float duration, float frequency) {
    const float strength = std::max(intensity, 0.0f);
    CameraShakeSettings settings;
    settings.duration = duration;
    settings.frequency = frequency;
    settings.translationAmplitude *= strength;
    settings.rotationAmplitudeDegrees *= strength;
    Start(settings);
}

CameraShakeSample CameraShake::Update(float dt) {
    CameraShakeSample sample;
    const float step = std::max(dt, 0.0f);

    for (Instance& instance : m_instances) {
        instance.elapsed = std::min(
            instance.elapsed + step, instance.settings.duration);
        const float envelope = Envelope(instance.elapsed, instance.settings.duration);
        const float frequency = instance.settings.frequency;
        const float time = instance.elapsed;

        sample.translation.x += instance.settings.translationAmplitude.x * envelope
            * Wave(time, frequency, instance.translationPhase.x, 1.00f);
        sample.translation.y += instance.settings.translationAmplitude.y * envelope
            * Wave(time, frequency, instance.translationPhase.y, 1.17f);
        sample.translation.z += instance.settings.translationAmplitude.z * envelope
            * Wave(time, frequency, instance.translationPhase.z, 0.83f);
        sample.rotationDegrees.x += instance.settings.rotationAmplitudeDegrees.x * envelope
            * Wave(time, frequency, instance.rotationPhase.x, 0.91f);
        sample.rotationDegrees.y += instance.settings.rotationAmplitudeDegrees.y * envelope
            * Wave(time, frequency, instance.rotationPhase.y, 1.09f);
        sample.fovOffset += instance.settings.fovAmplitude * envelope
            * Wave(time, frequency, instance.fovPhase, 0.72f);
    }

    m_instances.erase(
        std::remove_if(m_instances.begin(), m_instances.end(), [](const Instance& instance) {
            return instance.elapsed >= instance.settings.duration;
        }),
        m_instances.end());
    return sample;
}

void CameraShake::Clear() {
    m_instances.clear();
}

void CameraShake::Apply(const CameraShakeSample& sample, Camera& camera) {
    const glm::vec3 front = camera.Front();
    glm::vec3 right = glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(right, right) < 0.000001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, front));
    camera.SetPosition(camera.Position()
        + right * sample.translation.x
        + up * sample.translation.y
        + front * sample.translation.z);
    camera.AddYawPitch(sample.rotationDegrees.x, sample.rotationDegrees.y);
    camera.fov = std::clamp(camera.fov + sample.fovOffset, 10.0f, 120.0f);
}

} // namespace engine
