#include "engine/graphics/Water.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/VertexLayout.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace engine {
namespace {

const char* kWaterVert = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;   // unused (flat grid); kept for the shared layout
layout(location = 2) in vec2 aUV;

uniform mat4  uViewProj;
uniform vec3  uCenter;       // world centre (xz offset + y = calm level)
uniform float uTime;
uniform int   uWaveCount;
uniform vec2  uWaveDir[4];
uniform float uWaveAmp[4];
uniform float uWaveLen[4];
uniform float uWaveSpeed[4];
uniform float uWaveSteep[4];

out vec3 vWorldPos;
out vec3 vNormal;

const float PI = 3.14159265;

void main() {
    vec3 base = vec3(aPos.x + uCenter.x, uCenter.y, aPos.z + uCenter.z);
    vec3 P = base;
    float nx = 0.0, ny = 0.0, nz = 0.0;   // ny accumulates the crest term
    for (int i = 0; i < uWaveCount; ++i) {
        vec2 d  = normalize(uWaveDir[i]);
        float k = 2.0 * PI / max(uWaveLen[i], 0.0001);
        float a = uWaveAmp[i];
        float Q = uWaveSteep[i] / max(k * a * float(uWaveCount), 0.0001);
        float f = k * dot(d, base.xz) - uWaveSpeed[i] * uTime;
        float c = cos(f), s = sin(f);
        P.x += Q * a * d.x * c;
        P.z += Q * a * d.y * c;
        P.y += a * s;
        float WA = k * a;
        nx += -d.x * WA * c;
        nz += -d.y * WA * c;
        ny +=  Q * WA * s;
    }
    vWorldPos = P;
    vNormal   = normalize(vec3(nx, 1.0 - ny, nz));
    gl_Position = uViewProj * vec4(P, 1.0);
}
)glsl";

const char* kWaterFrag = R"glsl(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3  uCamPos;
uniform vec3  uSunDir;       // travel direction of the sun light
uniform vec3  uSunColor;
uniform vec3  uAmbient;
uniform vec3  uShallow;
uniform vec3  uDeep;
uniform vec3  uReflection;   // sky tint mixed in by Fresnel
uniform float uFresnelPower;
uniform float uSpecStrength;
uniform float uShininess;
uniform float uTransparency;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    float ndv = max(dot(N, V), 0.0);

    float fresnel = pow(1.0 - ndv, uFresnelPower);

    // Water body: shallow->deep gradient, but compress the view-angle range so the
    // surface reads as a smooth body of water instead of hard per-wave-face bands.
    float depthMix = mix(0.45, 1.0, ndv);
    vec3  body     = mix(uShallow, uDeep, depthMix);
    // Reflect the sky at grazing angles, but keep it subtle so crests don't turn pale.
    vec3  color    = mix(body, uReflection, fresnel * 0.55);

    // Sun glint (Blinn-Phong specular), tone-limited so highlights sparkle rather than
    // blowing whole wave faces out to white.
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), uShininess) * uSpecStrength;
    color += uSunColor * min(spec, 1.0);
    color += uAmbient * body * 0.20;

    float alpha = clamp(uTransparency + fresnel * (1.0 - uTransparency), 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
)glsl";

} // namespace

Water::Water(const WaterConfig& config) : m_config(config) {
    m_shader = std::make_unique<Shader>(kWaterVert, kWaterFrag);
    BuildMesh();
}

Water::~Water() = default;

void Water::SetConfig(const WaterConfig& config) {
    const bool rebuild = config.size != m_config.size || config.resolution != m_config.resolution;
    m_config = config;
    if (rebuild || !m_mesh) BuildMesh();
}

void Water::BuildMesh() {
    const int res = (m_config.resolution < 1) ? 1 : m_config.resolution;
    const float half = m_config.size * 0.5f;
    const float step = m_config.size / static_cast<float>(res);

    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>(res + 1) * (res + 1) * 8);
    for (int z = 0; z <= res; ++z) {
        for (int x = 0; x <= res; ++x) {
            const float px = -half + static_cast<float>(x) * step;
            const float pz = -half + static_cast<float>(z) * step;
            verts.push_back(px);  verts.push_back(0.0f); verts.push_back(pz);   // position (local; centre added in shader)
            verts.push_back(0.0f); verts.push_back(1.0f); verts.push_back(0.0f); // normal (waves recompute it)
            verts.push_back(static_cast<float>(x) / res);
            verts.push_back(static_cast<float>(z) / res);
        }
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(res) * res * 6);
    const int rowStride = res + 1;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const std::uint32_t i0 = static_cast<std::uint32_t>(z * rowStride + x);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = static_cast<std::uint32_t>((z + 1) * rowStride + x);
            const std::uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    m_mesh.emplace(verts, indices, VertexLayout{{3}, {3}, {2}});
}

void Water::Draw(const Camera& camera, float aspect,
                 const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& ambient) {
    if (!m_mesh || !m_shader) return;

    // Transparent surface: blend over the opaque scene, keep depth testing but don't
    // write depth (so particles / other transparents still composite correctly).
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", camera.ProjectionMatrix(aspect) * camera.ViewMatrix());
    m_shader->SetVec3("uCenter", m_config.center);
    m_shader->SetFloat("uTime", m_time);
    m_shader->SetVec3("uCamPos", camera.Position());
    m_shader->SetVec3("uSunDir", sunDir);
    m_shader->SetVec3("uSunColor", sunColor);
    m_shader->SetVec3("uAmbient", ambient);
    m_shader->SetVec3("uShallow", m_config.shallowColor);
    m_shader->SetVec3("uDeep", m_config.deepColor);
    m_shader->SetVec3("uReflection", m_config.reflectionColor);
    m_shader->SetFloat("uFresnelPower", m_config.fresnelPower);
    m_shader->SetFloat("uSpecStrength", m_config.specularStrength);
    m_shader->SetFloat("uShininess", m_config.shininess);
    m_shader->SetFloat("uTransparency", m_config.transparency);

    const int n = (m_config.waveCount < 1) ? 1 : (m_config.waveCount > 4 ? 4 : m_config.waveCount);
    m_shader->SetInt("uWaveCount", n);
    for (int i = 0; i < n; ++i) {
        const std::string s = "[" + std::to_string(i) + "]";
        m_shader->SetVec2("uWaveDir" + s, m_config.waves[i].direction);
        m_shader->SetFloat("uWaveAmp" + s, m_config.waves[i].amplitude);
        m_shader->SetFloat("uWaveLen" + s, m_config.waves[i].wavelength);
        m_shader->SetFloat("uWaveSpeed" + s, m_config.waves[i].speed);
        m_shader->SetFloat("uWaveSteep" + s, m_config.waves[i].steepness);
    }

    m_mesh->Draw();

    glDepthMask(prevDepthMask);
    if (!prevBlend) glDisable(GL_BLEND);
}

float Water::HeightAt(float worldX, float worldZ) const {
    // Vertical height of the surface at a base XZ (ignores the small horizontal
    // Gerstner displacement -- a good approximation for buoyancy / floating).
    float y = m_config.center.y;
    const glm::vec2 base(worldX, worldZ);
    const int n = (m_config.waveCount < 1) ? 1 : (m_config.waveCount > 4 ? 4 : m_config.waveCount);
    for (int i = 0; i < n; ++i) {
        const glm::vec2 d = glm::normalize(m_config.waves[i].direction);
        const float k = 6.2831853f / std::max(m_config.waves[i].wavelength, 0.0001f);
        const float f = k * glm::dot(d, base) - m_config.waves[i].speed * m_time;
        y += m_config.waves[i].amplitude * std::sin(f);
    }
    return y;
}

} // namespace engine
