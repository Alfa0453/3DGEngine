#pragma once

#include <glm/glm.hpp>

#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {

enum class LightingBuildQuality : std::uint32_t { Preview = 0, Medium = 1, High = 2 };

struct LightingBuildSettings {
    LightingBuildQuality quality = LightingBuildQuality::Medium;
    float probeSpacing = 2.0f;
    float maxRayDistance = 80.0f;
    float boundsPadding = 2.0f;
    // Physical visibility may legitimately be zero in a sealed volume.
    float minimumVisibility = 0.0f;
    std::uint32_t raysPerProbe = 96;
    bool directionalIrradiance = true;
    float indirectBounceStrength = 0.0f;
    bool indirectBounceEnabled = true;
    float emissiveContribution = 1.0f;
    float indirectSaturation = 1.0f;
};

struct LightingBuildLight {
    enum class Type : std::uint32_t { Directional, Point, Spot, Rectangle };
    Type type = Type::Directional;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float range = 20.0f;
    float innerCos = 0.9f;
    float outerCos = 0.75f;
    glm::vec2 halfSize{0.5f};
    glm::vec3 right{1.0f,0.0f,0.0f};
    glm::vec3 up{0.0f,1.0f,0.0f};
    bool twoSided = false;
};

struct LightingTriangle {
    glm::vec3 a{0.0f}, b{0.0f}, c{0.0f};
    glm::vec3 albedo{0.8f};
    glm::vec3 emissive{0.0f};
};

// Rich hit information used by the probe baker. Keeping surface data in the
// query result makes emissive and multi-bounce transport possible without
// replacing the acceleration structure later.
struct LightingRayHit {
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 barycentric{1.0f, 0.0f, 0.0f};
    glm::vec3 albedo{0.8f};
    glm::vec3 emissive{0.0f};
    std::size_t triangleIndex = 0;
};

struct LightingProbe {
    glm::vec3 ambient{1.0f};
    std::array<glm::vec3, 4> irradianceSH{{glm::vec3(0.0f), glm::vec3(0.0f),
                                           glm::vec3(0.0f), glm::vec3(0.0f)}};
    float skyVisibility = 1.0f;
    bool valid = true;
    std::array<glm::vec3, 4> bounceSH{};
    std::array<glm::vec3, 4> emissiveSH{};
    // Runtime dynamic-GI visibility moments. They intentionally are not stored
    // in v3 baked assets; zero means that no directional visibility data exists.
    float depthMean = 0.0f;
    float depthSecondMoment = 0.0f;
};

// Compact directional environment used by the offline probe builder. It is
// value-owned so asynchronous builds never capture renderer resources.
struct DirectionalSkyRadiance {
    glm::vec3 zenith{0.35f, 0.45f, 0.70f};
    glm::vec3 horizon{0.65f, 0.72f, 0.82f};
    glm::vec3 ground{0.03f, 0.035f, 0.04f};
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 sunRadiance{0.0f};
    float sunSharpness = 256.0f;
    glm::vec3 Sample(const glm::vec3& direction) const;
};

struct LightingBuildData {
    static constexpr std::uint32_t kVersion = 4;
    std::uint32_t version = kVersion;
    std::uint64_t sourceHash = 0;
    std::string sourceScene;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
    glm::ivec3 dimensions{0};
    float spacing = 2.0f;
    LightingBuildSettings settings;
    std::vector<LightingProbe> probes;

    bool IsValid() const;
    std::size_t Index(int x, int y, int z) const;
};

struct LightingBuildProgress {
    enum class Phase : std::uint32_t {
        PreparingGeometry, BuildingAcceleration, IrradianceProbes,
        IndirectBounce, CapturingReflectionProbes, FilteringReflectionProbes,
        SavingBuildData, Complete
    };
    std::atomic<std::uint32_t> completed{0};
    std::atomic<std::uint32_t> total{0};
    std::atomic<bool> cancel{false};
    std::atomic<Phase> phase{Phase::PreparingGeometry};
    std::atomic<std::uint64_t> raysCast{0};
};

std::uint64_t HashLightingGeometry(const std::vector<LightingTriangle>& triangles,
                                   const LightingBuildSettings& settings);
bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const glm::vec3& skyColor,
                         std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& settings,
                         LightingBuildData* output,
                         LightingBuildProgress* progress = nullptr,
                         std::string* error = nullptr);
bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const std::vector<LightingBuildLight>& staticLights,
                         const DirectionalSkyRadiance& sky,
                         std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& settings,
                         LightingBuildData* output,
                         LightingBuildProgress* progress = nullptr,
                         std::string* error = nullptr);
bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const DirectionalSkyRadiance& sky,
                         std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& settings,
                         LightingBuildData* output,
                         LightingBuildProgress* progress = nullptr,
                         std::string* error = nullptr);
bool SaveLightingBuildData(const std::string& path, const LightingBuildData& data,
                           std::string* error = nullptr);
bool LoadLightingBuildData(const std::string& path, LightingBuildData* data,
                           std::string* error = nullptr);

// SH4 is packed into three RGBA16F 3D textures plus an RGBA16F metadata texture.
// Coefficients are validity weighted so hardware filtering can interpolate only
// valid neighbours and GLSL can renormalize by the filtered validity channel.
class LightingProbeGrid {
public:
    enum class Contribution { Combined, DirectEnvironment, Bounce, Emissive };
    LightingProbeGrid() = default;
    ~LightingProbeGrid();
    LightingProbeGrid(const LightingProbeGrid&) = delete;
    LightingProbeGrid& operator=(const LightingProbeGrid&) = delete;
    LightingProbeGrid(LightingProbeGrid&& other) noexcept;
    LightingProbeGrid& operator=(LightingProbeGrid&& other) noexcept;

    bool Upload(const LightingBuildData& data, std::string* error = nullptr);
    // Replaces only the combined SH and metadata cells listed in changedIndices.
    // The texture allocation is retained, which keeps temporal GI updates cheap.
    bool UpdateCombined(const LightingBuildData& data,
                        const std::vector<std::size_t>& changedIndices,
                        std::string* error = nullptr);
    void Reset();
    void Bind(unsigned int textureUnit, Contribution contribution = Contribution::Combined) const;
    bool Valid() const { return m_textures[0] != 0; }
    const glm::vec3& BoundsMin() const { return m_boundsMin; }
    const glm::vec3& BoundsMax() const { return m_boundsMax; }
    const glm::ivec3& Dimensions() const { return m_dimensions; }
    std::size_t ProbeCount() const { return m_probeCount; }
    std::uint64_t MemoryBytes() const { return m_memoryBytes; }

private:
    // Combined SH (0..2), metadata (3), bounce (4..6), emissive (7..9),
    // direct environment (10..12). Debug contributions reuse the normal four
    // sampler slots rather than increasing fragment texture-unit pressure.
    std::array<unsigned int, 13> m_textures{};
    glm::vec3 m_boundsMin{0.0f}, m_boundsMax{0.0f};
    glm::ivec3 m_dimensions{0};
    std::size_t m_probeCount = 0;
    std::uint64_t m_memoryBytes = 0;
};

} // namespace engine
