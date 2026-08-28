#pragma once

#include "engine/graphics/LightingBuildData.h"
#include "engine/graphics/LightingScene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

enum class DynamicGiQuality : std::uint8_t { Low = 0, Medium, High, Ultra };
enum class DynamicProbeState : std::uint8_t {
    Active = 0, Sleeping, Invalid, Relocated, OutsideGeometry, InsideGeometry
};

struct DynamicIrradianceSettings {
    bool enabled = false;
    DynamicGiQuality quality = DynamicGiQuality::Medium;
    glm::vec3 boundsMin{-20.0f, -2.0f, -20.0f};
    glm::vec3 boundsMax{20.0f, 12.0f, 20.0f};
    float probeSpacing = 3.0f;
    std::uint32_t raysPerProbe = 48;
    std::uint32_t probesPerFrame = 12;
    std::uint32_t maxGiRaysPerFrame = 768;
    float maxRayDistance = 60.0f;
    float hysteresis = 0.94f;
    float intensity = 1.0f;
    float activeDistance = 120.0f;
    bool relocation = true;
    bool classification = true;
    bool visibilityWeighting = true;
    bool cameraFollowing = false;

    void Normalize();
    void ApplyQualityPreset(DynamicGiQuality preset);
};

struct DynamicGiLight {
    enum class Type : std::uint8_t { Directional = 0, Point, Spot };
    Type type = Type::Point;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 radiance{1.0f};
    float range = 20.0f;
    float innerCos = 0.9f;
    float outerCos = 0.75f;
    bool affectDynamicGi = true;
};

struct DynamicIrradianceProbe {
    std::array<glm::vec3, 4> irradianceSH{};
    glm::vec3 relocationOffset{0.0f};
    float validity = 0.0f;
    float confidence = 0.0f;
    float depthMean = 1.0f;
    float depthSecondMoment = 1.0f;
    float skyVisibility = 1.0f;
    std::uint32_t lastUpdatedFrame = 0;
    std::uint32_t revision = 1;
    DynamicProbeState state = DynamicProbeState::Active;
};

struct DynamicGiStats {
    double updateMilliseconds = 0.0;
    double uploadMilliseconds = 0.0;
    std::uint64_t raysCast = 0;
    std::uint32_t probesUpdated = 0;
    std::uint32_t activeProbes = 0;
    std::uint32_t sleepingProbes = 0;
    std::uint32_t relocatedProbes = 0;
    std::uint32_t invalidProbes = 0;
    std::uint64_t memoryBytes = 0;
};

// Budgeted CPU DDGI-style probe updater. It reuses the baked SH4 convention and
// composes a current-lighting estimate with the baked baseline before updating
// the existing LightingProbeGrid textures on the render thread.
class DynamicIrradianceSystem {
public:
    bool Configure(const DynamicIrradianceSettings& settings,
                   const DirectionalSkyRadiance& initialEnvironment,
                   const LightingBuildData* bakedBaseline = nullptr,
                   std::string* error = nullptr);
    void SetSceneGeometry(const std::vector<LightingTriangle>& triangles);
    void Reset();

    // Must be called with the owning OpenGL context current.
    bool InitializeGpu(std::string* error = nullptr);
    void Update(const glm::vec3& cameraPosition,
                const DirectionalSkyRadiance& environment,
                const std::vector<DynamicGiLight>& lights,
                std::uint32_t frameIndex);

    void InvalidateAll();
    void InvalidateSphere(const glm::vec3& center, float radius);

    bool Ready() const { return !m_probes.empty(); }
    bool GpuReady() const { return m_grid.Valid(); }
    const DynamicIrradianceSettings& Settings() const { return m_settings; }
    const std::vector<DynamicIrradianceProbe>& Probes() const { return m_probes; }
    const LightingBuildData& ComposedData() const { return m_composed; }
    const LightingProbeGrid& Grid() const { return m_grid; }
    LightingProbeGrid& Grid() { return m_grid; }
    const DynamicGiStats& Stats() const { return m_stats; }
    glm::vec3 ProbeGridPosition(std::size_t index) const;
    glm::vec3 ProbeSamplePosition(std::size_t index) const;

private:
    struct ProbeSample {
        std::array<glm::vec3, 4> sh{};
        float skyVisibility = 1.0f;
        float depthMean = 1.0f;
        float depthSecondMoment = 1.0f;
        std::uint32_t rays = 0;
    };

    ProbeSample TraceProbe(std::size_t index,
                           const DirectionalSkyRadiance& environment,
                           const std::vector<DynamicGiLight>& lights,
                           std::uint32_t frameIndex) const;
    void ClassifyAndRelocate(std::size_t index, const glm::vec3& cameraPosition);
    glm::vec3 EvaluateHitRadiance(const LightingRayHit& hit,
                                  const DirectionalSkyRadiance& environment,
                                  const std::vector<DynamicGiLight>& lights) const;
    void ComposeProbe(std::size_t index);
    void RefreshStats();

    DynamicIrradianceSettings m_settings;
    LightingBuildData m_baked;
    LightingBuildData m_composed;
    LightingSceneBvh m_scene;
    LightingProbeGrid m_grid;
    std::vector<DynamicIrradianceProbe> m_probes;
    std::vector<std::uint8_t> m_dirty;
    std::size_t m_scheduleCursor = 0;
    bool m_hasBakedBaseline = false;
    bool m_hasPreviousLighting = false;
    DirectionalSkyRadiance m_previousEnvironment;
    std::vector<DynamicGiLight> m_previousLights;
    DynamicGiStats m_stats;
};

} // namespace engine
