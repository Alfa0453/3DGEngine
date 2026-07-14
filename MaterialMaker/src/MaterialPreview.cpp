#include "MaterialMaker/MaterialPreview.h"

// glad must be included before anything that pulls in GL headers.
#include <glad/glad.h>

#include <engine/graphics/Primitives.h>
#include <engine/graphics/Camera.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace material_maker {

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::Light;
using engine::ecs::PbrMaterial;

namespace {

// Unlit shader for the debug channel views. It draws the mesh flat-coloured by the
// selected channel (no lighting), so the user can inspect one property at a time.
const char* kDebugVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() {
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)GLSL";

// uChannel: 0 albedo, 1 metallic, 2 roughness, 3 ao, 4 normal.
const char* kDebugFrag = R"GLSL(
#version 330 core
in vec3 vNormal;
uniform int   uChannel;
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
out vec4 FragColor;
void main() {
    vec3 c;
    if      (uChannel == 0) c = uAlbedo;
    else if (uChannel == 1) c = vec3(uMetallic);
    else if (uChannel == 2) c = vec3(uRoughness);
    else if (uChannel == 3) c = vec3(uAo);
    else                    c = normalize(vNormal) * 0.5 + 0.5;
    FragColor = vec4(c, 1.0);
}
)GLSL";

int ChannelUniform(MaterialPreview::Channel ch) {
    switch (ch) {
        case MaterialPreview::Channel::Albedo:    return 0;
        case MaterialPreview::Channel::Metallic:  return 1;
        case MaterialPreview::Channel::Roughness: return 2;
        case MaterialPreview::Channel::AO:        return 3;
        case MaterialPreview::Channel::Normal:    return 4;
        default:                                  return 0;
    }
}

} // namespace

void MaterialPreview::EnsureInitialized() {
    if (m_ready) {
        return;
    }

    m_sphere.emplace(engine::primitives::Sphere(48));
    m_cube.emplace(engine::primitives::Cube());
    m_plane.emplace(engine::primitives::Plane(1.0f, 1.0f));
    m_groundMesh.emplace(engine::primitives::Plane(1.0f, 1.0f));

    m_fbo.emplace(256, 256, GL_RGBA8, /*depth=*/true);
    m_pbr.emplace(1024);
    m_ibl.emplace(128);
    m_sky.emplace();
    m_debug.emplace(kDebugVert, kDebugFrag);

    // Directional key light (its direction / colour / intensity are driven per
    // frame from the environment sample).
    m_sun = m_reg.Create();
    m_reg.Add<Transform>(m_sun, Transform{});
    Light key;
    key.type = Light::Type::Directional;
    m_reg.Add<Light>(m_sun, key);

    // The previewed object (material + mesh swapped in each frame).
    m_object = m_reg.Create();
    Transform t;
    t.position = glm::vec3(0.0f);
    t.scale    = glm::vec3(1.0f);
    m_reg.Add<Transform>(m_object, t);
    MeshPBR mp;
    mp.mesh = &*m_sphere;
    m_reg.Add<MeshPBR>(m_object, mp);

    // Ground plane (parked hidden by a zero scale until enabled).
    m_ground = m_reg.Create();
    Transform gt;
    gt.scale = glm::vec3(0.0f);
    m_reg.Add<Transform>(m_ground, gt);
    PbrMaterial gm;
    gm.albedo    = glm::vec3(0.18f, 0.19f, 0.21f);
    gm.roughness = 0.9f;
    m_reg.Add<MeshPBR>(m_ground, MeshPBR{&*m_groundMesh, gm});

    m_size  = 256;
    m_ready = true;
}

void MaterialPreview::RegenerateEnvironment(float envTime, float envYawDeg) {
    engine::DayNightCycle::Sample s = engine::DayNightCycle::At(envTime);

    // Rotate the environment about Y: spin the sun/moon/key directions so both the
    // sky and the baked IBL turn with the slider.
    const glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(envYawDeg),
                                                glm::vec3(0.0f, 1.0f, 0.0f)));
    s.keyLightDirection = rot * s.keyLightDirection;
    s.sunToward         = rot * s.sunToward;
    s.moonToward        = rot * s.moonToward;

    m_sample  = s;
    m_envTime = envTime;
    m_envYaw  = envYawDeg;

    m_ibl->Generate([&](const glm::mat4& view, const glm::mat4& proj) {
        m_sky->Draw(view, proj, m_sample, /*tonemap=*/false);
    });
}

MaterialPreview::MapInfo MaterialPreview::AcquireMap(const std::string& path) {
    if (path.empty()) {
        return MapInfo{};
    }
    auto it = m_textures.find(path);
    if (it == m_textures.end()) {
        auto cached = std::make_unique<CachedTexture>();
        try {
            cached->texture.emplace(path, /*smooth=*/true);
            cached->info.textureId = cached->texture->ID();
            cached->info.width     = cached->texture->Width();
            cached->info.height    = cached->texture->Height();
            cached->info.ok        = true;
        } catch (const std::exception& e) {
            cached->info.ok    = false;
            cached->info.error = e.what();
        }
        it = m_textures.emplace(path, std::move(cached)).first;
    }
    return it->second->info;
}

const engine::Texture* MaterialPreview::ResolveMap(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    if (!AcquireMap(path).ok) {
        return nullptr;
    }
    auto it = m_textures.find(path);
    return (it != m_textures.end() && it->second->texture) ? &*it->second->texture : nullptr;
}

void MaterialPreview::RenderChannel(const PbrMaterial& material, const Settings& settings,
                                    const glm::mat4& viewProj) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    const engine::Mesh* mesh = (settings.shape == Shape::Cube)  ? &*m_cube
                             : (settings.shape == Shape::Plane) ? &*m_plane
                                                                : &*m_sphere;
    m_debug->Bind();
    m_debug->SetMat4("uModel", glm::mat4(1.0f));
    m_debug->SetMat4("uViewProj", viewProj);
    m_debug->SetInt("uChannel", ChannelUniform(settings.channel));
    m_debug->SetVec3("uAlbedo", material.albedo);
    m_debug->SetFloat("uMetallic", material.metallic);
    m_debug->SetFloat("uRoughness", material.roughness);
    m_debug->SetFloat("uAo", material.ao);
    mesh->Draw();
}

unsigned int MaterialPreview::Render(const PbrMaterial& material, const Settings& settings) {
    EnsureInitialized();
    if (!m_ready) {
        return 0;
    }

    const int size = std::max(64, settings.size);
    if (size != m_size) {
        m_fbo->Resize(size, size);
        m_size = size;
    }
    if (std::abs(settings.envTime - m_envTime) > 1.0e-4f ||
        std::abs(settings.envYawDeg - m_envYaw) > 1.0e-3f) {
        RegenerateEnvironment(settings.envTime, settings.envYawDeg);
    }

    // Live material + selected mesh on the preview object.
    MeshPBR& mp = m_reg.Get<MeshPBR>(m_object);
    mp.material = material;
    mp.mesh = (settings.shape == Shape::Cube)  ? &*m_cube
            : (settings.shape == Shape::Plane) ? &*m_plane
                                               : &*m_sphere;

    // Resolve the texture-map paths to loaded textures (nullptr = none/failed).
    mp.material.albedoMap     = ResolveMap(settings.albedoMapPath);
    mp.material.normalMap     = ResolveMap(settings.normalMapPath);
    mp.material.metalRoughMap = ResolveMap(settings.metalRoughMapPath);

    // Drive the key light from the (rotated) environment sample.
    Light& sun = m_reg.Get<Light>(m_sun);
    sun.direction = m_sample.keyLightDirection;
    sun.color     = m_sample.keyLightColor;
    sun.intensity = settings.lightIntensity;

    // Ground plane: place it under the object when enabled, hide it otherwise.
    Transform& ground = m_reg.Get<Transform>(m_ground);
    if (settings.groundPlane) {
        ground.position = glm::vec3(0.0f, -0.5f, 0.0f);
        ground.scale    = glm::vec3(6.0f, 1.0f, 6.0f);
    } else {
        ground.scale = glm::vec3(0.0f);
    }

    // Orbit camera.
    const float yaw   = glm::radians(settings.yawDeg);
    const float pitch = glm::radians(std::max(-89.0f, std::min(89.0f, settings.pitchDeg)));
    const glm::vec3 dir(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
    engine::Camera cam(dir * 2.9f);
    cam.LookAt(glm::vec3(0.0f));
    const float aspect = 1.0f;

    // Save prior framebuffer + viewport, render, restore.
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    m_fbo->Bind();
    glClearColor(settings.background.r, settings.background.g, settings.background.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (settings.channel == Channel::Full) {
        engine::PbrRenderer::Options opt;
        opt.ambient            = m_sample.ambient + glm::vec3(0.04f);
        opt.ibl                = &*m_ibl;
        opt.tonemap            = true;
        opt.directionalShadows = settings.groundPlane;   // contact shadow only when there is a floor
        opt.shadowRadius       = 3.0f;
        opt.pointShadows       = false;
        opt.spotShadows        = false;
        opt.frustumCull        = false;
        m_pbr->Render(m_reg, cam, aspect, size, size, opt);
        m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, /*tonemap=*/true);
    } else {
        const glm::mat4 viewProj = cam.ProjectionMatrix(aspect) * cam.ViewMatrix();
        RenderChannel(material, settings, viewProj);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return m_fbo->ColorTexture();
}

} // namespace material_maker
