#pragma once

#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Entity.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/Texture.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/IBL.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/DayNightCycle.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace material_maker {

// Renders a live PBR sphere / cube / plane, lit by image-based lighting, into an
// off-screen framebuffer, so the Material Maker preview is the engine's ACTUAL
// shader output (WYSIWYG). It can also render debug channel views (albedo /
// metallic / roughness / AO / normals) through a small embedded unlit shader.
//
// GL resources are created lazily on the first Render() call, because a GL
// context is only guaranteed current while the ImGui panel is being drawn.
class MaterialPreview {
public:
    enum class Shape   { Sphere, Cube, Plane };
    enum class Channel { Full, Albedo, Metallic, Roughness, Normal, AO };

    // Everything that controls one preview render.
    struct Settings {
        int       size          = 256;
        float     yawDeg        = 35.0f;   // camera orbit
        float     pitchDeg      = 18.0f;
        Shape     shape         = Shape::Sphere;
        Channel   channel       = Channel::Full;
        // Environment rig.
        float     envTime       = 0.42f;   // DayNightCycle time-of-day [0,1]
        float     envYawDeg     = 0.0f;     // rotate the environment / key light
        float     lightIntensity = 1.0f;    // multiplier on the key light
        bool      groundPlane   = false;    // ground + contact shadow
        glm::vec3 background{0.05f, 0.06f, 0.08f};
        // Texture-map file paths (empty = none). Applied in full and debug views.
        std::string albedoMapPath;
        std::string normalMapPath;
        std::string metalRoughMapPath;
        std::string heightMapPath;
    };

    // Result of loading a texture map, for a thumbnail + status in the panel.
    struct MapInfo {
        unsigned int textureId = 0;
        int          width     = 0;
        int          height    = 0;
        bool         ok        = false;
        std::string  error;
    };

    // Render with the given material + settings; returns the GL colour-texture id
    // to hand to ImGui::Image (0 if the preview could not be created).
    unsigned int Render(const engine::ecs::PbrMaterial& material, const Settings& settings);

    // Load (cached) a texture map and report its GL id / size / status, so the
    // panel can show a thumbnail. Must be called while a GL context is current.
    MapInfo AcquireMap(const std::string& path);
    void Retry();

    bool Available() const { return m_ready; }
    const std::string& LastError() const { return m_error; }

private:
    struct CachedTexture {
        std::optional<engine::Texture> texture;
        MapInfo info;
        std::filesystem::file_time_type writeTime{};
        std::uintmax_t fileSize = 0;
        bool exists = false;
    };

    void EnsureInitialized();
    void RegenerateEnvironment(float envTime, float envYawDeg);
    void RenderChannel(const engine::ecs::PbrMaterial& material,
                       const Settings& settings, const glm::mat4& viewProj);
    unsigned int RenderUnchecked(const engine::ecs::PbrMaterial& material,
                                 const Settings& settings);
    // Resolve a map path to a loaded texture pointer (nullptr if empty/failed).
    const engine::Texture* ResolveMap(const std::string& path);

    bool  m_ready   = false;
    bool  m_failed  = false;
    std::string m_error;
    float m_envTime = -1.0f;   // last environment time baked into the IBL
    float m_envYaw  = -1.0e9f; // last environment rotation baked into the IBL
    int   m_size    = 0;

    std::optional<engine::Mesh>          m_sphere, m_cube, m_plane, m_groundMesh;
    std::optional<engine::Framebuffer>   m_fbo;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::Shader>        m_debug;   // unlit channel-view shader

    engine::DayNightCycle::Sample m_sample{};
    engine::ecs::Registry         m_reg;
    engine::ecs::Entity           m_object = engine::ecs::kNull;
    engine::ecs::Entity           m_sun    = engine::ecs::kNull;
    engine::ecs::Entity           m_ground = engine::ecs::kNull;

    // Loaded texture maps, keyed by path (unique_ptr keeps addresses stable so
    // material pointers into the cache stay valid).
    std::unordered_map<std::string, std::unique_ptr<CachedTexture>> m_textures;
};

} // namespace material_maker
