#include "MaterialMaker/MaterialPreview.h"

// glad must be included before anything that pulls in GL headers.
#include <glad/glad.h>

#include <engine/graphics/Primitives.h>
#include <engine/graphics/Camera.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <stdexcept>
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
layout(location = 2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vNormal;
out vec3 vPosition;
out vec2 vUV;
void main() {
    vNormal = mat3(uModel) * aNormal;
    vPosition = vec3(uModel * vec4(aPos, 1.0));
    vUV = aUV;
    gl_Position = uViewProj * vec4(vPosition, 1.0);
}
)GLSL";

// uChannel: 0 albedo, 1 metallic, 2 roughness, 3 ao, 4 normal.
const char* kDebugFrag = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vPosition;
in vec2 vUV;
uniform int   uChannel;
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform float uUvRotation;
uniform float uNormalStrength;
uniform int uHasAlbedoMap;
uniform int uHasNormalMap;
uniform int uHasMetalRoughMap;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMetalRoughMap;
out vec4 FragColor;
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}
void main() {
    float angle = radians(uUvRotation);
    vec2 centered = vUV * uUvScale - vec2(0.5);
    vec2 uv = mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * centered + vec2(0.5) + uUvOffset;
    vec3 albedo = uAlbedo;
    if (uHasAlbedoMap == 1) albedo *= texture(uAlbedoMap, uv).rgb;
    vec3 mr = (uHasMetalRoughMap == 1) ? texture(uMetalRoughMap, uv).rgb : vec3(1.0);
    vec3 normal = normalize(vNormal);
    if (uHasNormalMap == 1) {
        vec3 tangentNormal = texture(uNormalMap, uv).rgb * 2.0 - 1.0;
        tangentNormal.xy *= uNormalStrength;
        normal = normalize(cotangentFrame(normal, vPosition, uv) * normalize(tangentNormal));
    }
    vec3 c;
    if      (uChannel == 0) c = albedo;
    else if (uChannel == 1) c = vec3(uMetallic * mr.b);
    else if (uChannel == 2) c = vec3(uRoughness * mr.g);
    else if (uChannel == 3) c = vec3(uAo * mr.r);
    else                    c = normal * 0.5 + 0.5;
    FragColor = vec4(c, 1.0);
}

)GLSL";

struct GLStateGuard {
    GLint framebuffer = 0, viewport[4]{}, program = 0, vao = 0, arrayBuffer = 0, activeTexture = 0;
    GLint depthFunc = GL_LESS;
    GLfloat clearColor[4]{};
    GLboolean depthMask = GL_TRUE;
    GLboolean depth = GL_FALSE, blend = GL_FALSE, cull = GL_FALSE, scissor = GL_FALSE;
    std::array<GLint, 24> texture2D{}, texture2DArray{}, textureCube{};
    GLStateGuard() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer); glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture); glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor); glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        depth = glIsEnabled(GL_DEPTH_TEST); blend = glIsEnabled(GL_BLEND);
        cull = glIsEnabled(GL_CULL_FACE); scissor = glIsEnabled(GL_SCISSOR_TEST);
        for (std::size_t i = 0; i < texture2D.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2D[i]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture2DArray[i]);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &textureCube[i]);
        }
    }
    ~GLStateGuard() {
        for (std::size_t i = 0; i < texture2D.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2D[i]));
            glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(texture2DArray[i]));
            glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(textureCube[i]));
        }
        glActiveTexture(static_cast<GLenum>(activeTexture)); glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glDepthFunc(static_cast<GLenum>(depthFunc)); glDepthMask(depthMask);
        Set(GL_DEPTH_TEST, depth); Set(GL_BLEND, blend); Set(GL_CULL_FACE, cull); Set(GL_SCISSOR_TEST, scissor);
    }
    static void Set(GLenum capability, GLboolean enabled) { enabled ? glEnable(capability) : glDisable(capability); }
};

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
    std::error_code ec;
    const bool exists = std::filesystem::is_regular_file(path, ec);
    const auto writeTime = exists ? std::filesystem::last_write_time(path, ec)
                                  : std::filesystem::file_time_type{};
    ec.clear();
    const std::uintmax_t fileSize = exists ? std::filesystem::file_size(path, ec) : 0;
    auto it = m_textures.find(path);
    const bool stale = it == m_textures.end() || it->second->exists != exists ||
                       it->second->writeTime != writeTime || it->second->fileSize != fileSize;
    if (stale) {
        auto cached = std::make_unique<CachedTexture>();
        cached->exists = exists;
        cached->writeTime = writeTime;
        cached->fileSize = fileSize;
        try {
            if (!exists) throw std::runtime_error("Texture file does not exist: " + path);
            cached->texture.emplace(path, /*smooth=*/true);
            cached->info.textureId = cached->texture->ID();
            cached->info.width     = cached->texture->Width();
            cached->info.height    = cached->texture->Height();
            cached->info.ok        = true;
        } catch (const std::exception& e) {
            cached->info.ok    = false;
            cached->info.error = e.what();
        }
        if (it == m_textures.end()) it = m_textures.emplace(path, std::move(cached)).first;
        else it->second = std::move(cached);
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
    m_debug->SetVec2("uUvScale", material.uvScale); m_debug->SetVec2("uUvOffset", material.uvOffset);
    m_debug->SetFloat("uUvRotation", material.uvRotation);
    m_debug->SetFloat("uNormalStrength", material.normalStrength);
    const engine::Texture* albedo = ResolveMap(settings.albedoMapPath);
    const engine::Texture* normal = ResolveMap(settings.normalMapPath);
    const engine::Texture* metalRough = ResolveMap(settings.metalRoughMapPath);
    m_debug->SetInt("uHasAlbedoMap", albedo ? 1 : 0);
    m_debug->SetInt("uHasNormalMap", normal ? 1 : 0);
    m_debug->SetInt("uHasMetalRoughMap", metalRough ? 1 : 0);
    m_debug->SetInt("uAlbedoMap", 0); m_debug->SetInt("uNormalMap", 1); m_debug->SetInt("uMetalRoughMap", 2);
    if (albedo) albedo->Bind(0);
    if (normal) normal->Bind(1);
    if (metalRough) metalRough->Bind(2);
    mesh->Draw();
}

unsigned int MaterialPreview::Render(const PbrMaterial& material, const Settings& settings) {
    GLStateGuard state;
    if (m_failed) return 0;
    try {
        const unsigned int result = RenderUnchecked(material, settings);
        m_error.clear();
        return result;
    } catch (const std::exception& e) {
        m_error = e.what();
        m_failed = true;
        m_ready = false;
        return 0;
    } catch (...) {
        m_error = "Unknown OpenGL preview error.";
        m_failed = true;
        m_ready = false;
        return 0;
    }
}

unsigned int MaterialPreview::RenderUnchecked(const PbrMaterial& material, const Settings& settings) {
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
    mp.material.heightMap     = ResolveMap(settings.heightMapPath);

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

    return m_fbo->ColorTexture();
}

void MaterialPreview::Retry() {
    m_textures.clear();
    m_debug.reset(); m_sky.reset(); m_ibl.reset(); m_pbr.reset(); m_fbo.reset();
    m_groundMesh.reset(); m_plane.reset(); m_cube.reset(); m_sphere.reset();
    m_reg = engine::ecs::Registry{};
    m_object = m_sun = m_ground = engine::ecs::kNull;
    m_ready = false; m_failed = false; m_error.clear();
    m_envTime = -1.0f; m_envYaw = -1.0e9f; m_size = 0;
}

} // namespace material_maker
