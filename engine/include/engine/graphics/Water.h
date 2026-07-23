#pragma once

#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace engine {

class Shader;
class Camera;
class IBL;

// A stylised water body (Sea-of-Thieves style). The surface is displaced by iterative
// value-noise "sea octaves" (after Alexander Alekseev's Seascape / the gameidea.org
// stylised-water tutorial), shaded with a deep/shallow tint, Fresnel sky reflection, a
// sun glint, and procedural crest foam. Rendered as a transparent forward pass.
struct WaterConfig {
    glm::vec3 center{0.0f, 0.0f, 0.0f};   // world centre of the patch (y = calm surface level)
    float     size = 80.0f;               // square extent (world units)
    int       resolution = 160;           // grid subdivisions per side (mesh detail)

    // Surface shape: iterated value-noise waves. Higher choppy = sharper crests.
    float     seaHeight = 0.55f;          // overall wave height (world units)
    float     seaChoppy = 3.2f;           // crest sharpness
    float     seaSpeed  = 0.8f;           // animation speed
    float     seaFreq   = 0.10f;          // base spatial frequency (larger = smaller waves)

    glm::vec3 shallowColor{0.14f, 0.55f, 0.60f};     // bright shallow / grazing tint
    glm::vec3 deepColor{0.02f, 0.13f, 0.20f};        // deep water tint
    glm::vec3 reflectionColor{0.55f, 0.72f, 0.92f};  // sky tint blended in by Fresnel
    float     transparency   = 0.74f;     // base alpha looking straight down (0..1)
    float     fresnelPower   = 5.0f;      // how quickly reflection takes over at grazing angles
    float     specularStrength = 0.8f;    // sun glint intensity
    float     shininess      = 400.0f;    // sun glint tightness

    // Stylised whitecap foam on wave crests.
    glm::vec3 foamColor{1.0f, 1.0f, 1.0f};
    float     foamAmount = 0.55f;         // 0 = none; higher = more crest foam

    // Directional flow (e.g. a river following a spline). flowDir is a world XZ
    // direction; flowStrength scrolls the wave pattern along it (0 = still water).
    glm::vec2 flowDir{0.0f, 0.0f};
    float     flowStrength = 0.0f;
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
    // `contacts` are objects piercing the surface: each is (worldX, worldZ, radius,
    // strength); a foam ring is drawn where the water meets them (up to kMaxContacts).
    static constexpr int kMaxContacts = 16;
    void Draw(const Camera& camera, float aspect,
              const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& ambient,
              const glm::vec4* contacts = nullptr, int contactCount = 0);

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
