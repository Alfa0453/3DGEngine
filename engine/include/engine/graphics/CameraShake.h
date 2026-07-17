#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace engine {

class Camera;

struct CameraShakeSettings {
    float duration = 0.35f;
    float frequency = 18.0f;
    glm::vec3 translationAmplitude{0.06f, 0.08f, 0.04f};
    glm::vec2 rotationAmplitudeDegrees{0.8f, 1.0f};
    float fovAmplitude = 0.0f;
};

struct CameraShakeSample {
    glm::vec3 translation{0.0f};
    glm::vec2 rotationDegrees{0.0f};
    float fovOffset = 0.0f;
};

// Mixes any number of short camera impulses. Samples are local to the camera:
// translation is right/up/forward and rotation is yaw/pitch in degrees.
class CameraShake {
public:
    void Start(const CameraShakeSettings& settings);
    void StartImpulse(float intensity = 1.0f, float duration = 0.35f,
                      float frequency = 18.0f);
    CameraShakeSample Update(float dt);
    void Clear();

    bool Active() const { return !m_instances.empty(); }
    std::size_t ActiveCount() const { return m_instances.size(); }

    static void Apply(const CameraShakeSample& sample, Camera& camera);

private:
    struct Instance {
        CameraShakeSettings settings;
        float elapsed = 0.0f;
        glm::vec3 translationPhase{0.0f};
        glm::vec2 rotationPhase{0.0f};
        float fovPhase = 0.0f;
    };

    std::vector<Instance> m_instances;
    std::uint32_t m_seed = 1;
};

} // namespace engine
