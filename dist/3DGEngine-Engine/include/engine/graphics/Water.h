#pragma once

#include "engine/graphics/Mesh.h"
#include "engine/math/Spline.h"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

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

    // Scene-depth shading. The opaque depth buffer reveals how much geometry lies
    // below the surface, driving shallow/deep absorption and automatic bank foam.
    float     depthFadeDistance = 6.0f;   // metres until the deep colour is reached
    float     shorelineFoamWidth = 0.8f;  // shallow depth band that receives foam
    float     shorelineFoamStrength = 0.75f;

    // Screen-space refraction plus rough environment reflections. Refraction uses
    // the opaque scene colour captured immediately before the transparent pass.
    float     refractionStrength = 0.018f; // screen UV displacement by wave normal
    float     reflectionRoughness = 0.12f; // cubemap mip selection (0 mirror .. 1 rough)
    float     environmentReflectionStrength = 0.85f;
    // Screen-space scene reflection: reflects real scene objects by reusing the opaque
    // colour+depth buffers already bound for refraction (no extra scene pass). Off-screen /
    // missed rays fall back to the environment reflection above.
    bool      reflectScene = true;         // enable SSR of scene objects on the surface
    float     ssrStrength = 0.85f;         // blend of the scene reflection over the sky (0..1)
    float     ssrDistance = 30.0f;         // world-space ray-march length
    float     ssrThickness = 1.2f;         // eye-space depth band counted as a hit
    float     absorptionStrength = 0.75f;  // how strongly depth tints refracted colour
    float     causticsStrength = 0.25f;
    float     causticsScale = 1.5f;
    float     maxRenderDistance = 2500.0f; // footprint distance culling (0 = unlimited)

    // Stylised whitecap foam on wave crests.
    glm::vec3 foamColor{1.0f, 1.0f, 1.0f};
    float     foamAmount = 0.55f;         // 0 = none; higher = more crest foam

    // Directional flow (e.g. a river following a spline). flowDir is a world XZ
    // direction; flowStrength scrolls the wave pattern along it (0 = still water).
    glm::vec2 flowDir{0.0f, 0.0f};
    float     flowStrength = 0.0f;

    // When points are supplied the square patch becomes a ribbon whose centreline
    // follows the spline. Editing the points and reapplying the config rebuilds it.
    std::vector<glm::vec3> splinePoints;
    std::vector<glm::vec3> splinePointRotations;
    bool      splineClosed = false;
    float     riverWidth = 8.0f;

    // Optional custom water shader: the GLSL fragment body (helper functions + main())
    // for the surface. Empty = the built-in look. The engine prepends the version, the
    // shared noise helpers and the full water declaration block, so this source has every
    // water uniform/varying already in scope and only writes helpers + main(). If it fails
    // to compile the water falls back to the built-in shader (see CustomShaderError()).
    std::string customFragmentSource;
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
              const glm::vec4* contacts = nullptr, int contactCount = 0,
              unsigned int sceneColorTexture = 0,
              unsigned int sceneDepthTexture = 0,
              int viewportWidth = 1, int viewportHeight = 1,
              const IBL* ibl = nullptr);

    // Surface height at a world XZ position and the current time -- for buoyancy,
    // splashes, or floating the player. Matches the shader's Gerstner sum.
    float HeightAt(float worldX, float worldZ) const;
    bool ContainsXZ(float worldX, float worldZ, float padding = 0.0f) const;
    float Level() const { return m_config.center.y; }

    // Compile diagnostics for a custom fragment shader. Empty when none is set or it
    // compiled cleanly; otherwise the driver's error (and the built-in shader is used).
    const std::string& CustomShaderError() const { return m_customShaderError; }
    bool UsingCustomShader() const { return m_customShader != nullptr; }

private:
    void BuildMesh();
    // (Re)compile m_customShader from m_config.customFragmentSource. On failure clears it
    // (falling back to the built-in shader) and records the driver message.
    void RebuildCustomShader();

    WaterConfig m_config;
    std::optional<Mesh>     m_mesh;
    std::unique_ptr<Shader> m_shader;         // built-in surface shader (always valid)
    std::unique_ptr<Shader> m_customShader;   // active custom shader, or null
    std::string m_customShaderError;
    // World-space bounds used to reject water before setting up transparent passes.
    glm::vec3 m_boundsMin{0.0f};
    glm::vec3 m_boundsMax{0.0f};
    float m_splineLength = 0.0f;
    Spline m_spline;
    Spline m_flatSpline;
    float m_time = 0.0f;
};

} // namespace engine
