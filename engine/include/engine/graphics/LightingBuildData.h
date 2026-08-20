#pragma once

#include <glm/glm.hpp>

#include <atomic>
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
    float minimumVisibility = 0.02f;
    std::uint32_t raysPerProbe = 96;
};

struct LightingTriangle {
    glm::vec3 a{0.0f}, b{0.0f}, c{0.0f};
};

struct LightingProbe {
    glm::vec3 ambient{1.0f};
    float skyVisibility = 1.0f;
    bool valid = true;
};

struct LightingBuildData {
    static constexpr std::uint32_t kVersion = 1;
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
    std::atomic<std::uint32_t> completed{0};
    std::atomic<std::uint32_t> total{0};
    std::atomic<bool> cancel{false};
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
bool SaveLightingBuildData(const std::string& path, const LightingBuildData& data,
                           std::string* error = nullptr);
bool LoadLightingBuildData(const std::string& path, LightingBuildData* data,
                           std::string* error = nullptr);

// GPU representation used by static and skinned PBR paths. RGB stores local
// ambient tint and A stores sky visibility; hardware filtering provides smooth
// trilinear interpolation between probes.
class LightingProbeGrid {
public:
    LightingProbeGrid() = default;
    ~LightingProbeGrid();
    LightingProbeGrid(const LightingProbeGrid&) = delete;
    LightingProbeGrid& operator=(const LightingProbeGrid&) = delete;
    LightingProbeGrid(LightingProbeGrid&& other) noexcept;
    LightingProbeGrid& operator=(LightingProbeGrid&& other) noexcept;

    bool Upload(const LightingBuildData& data, std::string* error = nullptr);
    void Reset();
    void Bind(unsigned int textureUnit) const;
    bool Valid() const { return m_texture != 0; }
    const glm::vec3& BoundsMin() const { return m_boundsMin; }
    const glm::vec3& BoundsMax() const { return m_boundsMax; }

private:
    unsigned int m_texture = 0;
    glm::vec3 m_boundsMin{0.0f}, m_boundsMax{0.0f};
};

} // namespace engine
