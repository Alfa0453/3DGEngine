#pragma once

#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace engine {

class Shader;
class Camera;
class IBL;

// One Gerstner wave that contributes to the water surface. Several are summed for a
// natural, non-repeating look. `steepness` (0..1) sharpens crests; keep the total
// across waves <= 1 to avoid the surface folding over itself.
struct WaterWave {
    glm::vec2 direction{1.0f, 0.0f};   // travel direction on the XZ plane (normalized on use)
    float     amplitude  = 0.11f;      // wave height (world units)
    float     wavelength = 9.0f;       // crest-to-crest distance
    float     speed      = 1.0f;       // phase speed
    float     steepness  = 0.30f;      // 0 = round sine, 1 = sharp Gerstner crest
};

// A water body: a subdivided plane animated by summed Gerstner waves, shaded with a
// Fresnel sky reflection, a sun glint, and depth-tinted transparency.
struct WaterConfig {
    glm::vec3 center{0.0f, 0.0f, 0.0f};   // world centre of the patch (y = calm surface level)
    float     size = 80.0f;               // square extent (world units)
    int       resolution = 160;           // grid subdivisions per side (mesh detail)

    glm::vec3 shallowColor{0.10f, 0.42f, 0.50f};
    glm::vec3 deepColor{0.02f, 0.10f, 0.18f};
    glm::vec3 reflectionColor{0.55f, 0.72f, 0.92f};  // sky tint blended in by Fresnel
    float     transparency   = 0.72f;     // base alpha looking straight down (0..1)
    float     fresnelPower   = 4.0f;      // how quickly reflection takes over at grazing angles
    float     specularStrength = 1.2f;    // sun glint intensity
    float     shininess      = 220.0f;    // sun glint tightness

    int       waveCount = 4;              // 1..4 waves used
    // Long, low, gently-sloped swells layered with progressively smaller ripples in
    // varied directions. Low steepness keeps crests rounded (no razor bands); the
    // spread of wavelengths/directions breaks up any obvious tiling.
    WaterWave waves[4] = {
        {{ 1.0f,  0.15f}, 0.13f, 17.0f, 0.85f, 0.32f},
        {{ 0.5f,  0.85f}, 0.08f, 10.0f, 1.05f, 0.26f},
        {{-0.8f,  0.35f}, 0.045f, 5.5f, 1.35f, 0.20f},
        {{ 0.25f,-1.0f},  0.025f, 3.0f, 1.65f, 0.16f},
    };
};

class Water {
public:
    explicit Water(const WaterConfig& config = {});
    ~Water();
    Water(const Water&)            = delete;
    Water& operator=(const Water&) = delete;
    Water(Water&&) noexcept        = default;
    Water& operator=(Water&&) noexcept = default;

    // Re-apply config; rebuilds the mesh only if size/resolution changed.
    void SetConfig(const WaterConfig& config);
    const WaterConfig& Config() const { return m_config; }

    void Update(float dt) { m_time += dt; }

    // Draw the transparent water surface. Call AFTER the opaque scene (it reads the
    // depth buffer and blends). Restores GL blend/depth state afterward.
    void Draw(const Camera& camera, float aspect,
              const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& ambient);

    // Surface height at a world XZ position and the current time -- for buoyancy,
    // splashes, or floating the player. Matches the shader's Gerstner sum.
    float HeightAt(float worldX, float worldZ) const;
    float Level() const { return m_config.center.y; }

private:
    void BuildMesh();

    WaterConfig m_config;
    std::optional<Mesh>     m_mesh;
    std::unique_ptr<Shader> m_shader;
    float m_time = 0.0f;
};

} // namespace engine
