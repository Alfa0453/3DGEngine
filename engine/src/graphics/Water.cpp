#include "engine/graphics/Water.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/VertexLayout.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {
namespace {

// Shared value-noise + "sea octave" helpers (Seascape style). Duplicated into both
// stages because GLSL compiles each stage independently.
const char* kWaterNoise = R"glsl(
const mat2 octave_m = mat2(1.6, 1.2, -1.2, 1.6);

float hash12(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334677u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334677u;
    return float(n) * (1.0 / 4294967295.0);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return -1.0 + 2.0 * mix(mix(hash12(i + vec2(0.0, 0.0)), hash12(i + vec2(1.0, 0.0)), u.x),
                            mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
float sea_octave(vec2 uv, float choppy) {
    uv += noise(uv);
    vec2 wv  = 1.0 - abs(sin(uv));
    vec2 swv = abs(cos(uv));
    wv = mix(wv, swv, wv);
    return pow(1.0 - pow(wv.x * wv.y, 0.65), choppy);
}
// Summed sea octaves -> surface height (world units) at an XZ point.
float sea_height(vec2 xz, float time, float seaHeight, float seaChoppy,
                 float seaSpeed, float seaFreq, int iters) {
    float freq   = seaFreq;
    float amp    = seaHeight;
    float choppy = seaChoppy;
    vec2  uv = xz; uv.x *= 0.75;
    float h = 0.0;
    for (int i = 0; i < iters; ++i) {
        float d  = sea_octave((uv + time * seaSpeed) * freq, choppy);
        d       += sea_octave((uv - time * seaSpeed) * freq, choppy);
        h += d * amp;
        uv = uv * octave_m;
        freq *= 1.9;
        amp  *= 0.22;
        choppy = mix(choppy, 1.0, 0.2);
    }
    return h;
}
)glsl";

const char* kWaterVertBody = R"glsl(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;   // unused (flat grid); waves recompute the normal
layout(location = 2) in vec2 aUV;

uniform mat4  uViewProj;
uniform vec3  uCenter;       // world centre (xz offset + y = calm level)
uniform float uTime;
uniform float uSeaHeight;
uniform float uSeaChoppy;
uniform float uSeaSpeed;
uniform float uSeaFreq;
uniform vec2  uFlowDir;
uniform float uFlowStrength;

out vec3 vWorldPos;

void main() {
    vec2 xz = vec2(aPos.x + uCenter.x, aPos.z + uCenter.z);
    // Directional flow scrolls the wave pattern along uFlowDir (rivers).
    vec2 sampleXZ = xz - uFlowDir * uTime * uFlowStrength;
    // 3 octaves for geometry (cheaper); the fragment adds detail via more octaves.
    float h = sea_height(sampleXZ, uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 3);
    vec3 P = vec3(xz.x, uCenter.y + h, xz.y);
    vWorldPos = P;
    gl_Position = uViewProj * vec4(P, 1.0);
}
)glsl";

const char* kWaterFragBody = R"glsl(
in vec3 vWorldPos;

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
uniform float uTime;
uniform float uSeaHeight;
uniform float uSeaChoppy;
uniform float uSeaSpeed;
uniform float uSeaFreq;
uniform vec2  uFlowDir;
uniform float uFlowStrength;
uniform vec3  uFoamColor;
uniform float uFoamAmount;

const int kMaxContacts = 16;
uniform int   uContactCount;
uniform vec4  uContacts[kMaxContacts];   // xy = world XZ centre, z = radius, w = strength

out vec4 FragColor;

// Detailed normal from the height field via central differences of sea_height.
vec3 waterNormal(vec2 xz, float eps) {
    float h  = sea_height(xz,                 uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    float hx = sea_height(xz + vec2(eps, 0.0), uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    float hz = sea_height(xz + vec2(0.0, eps), uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    return normalize(vec3(h - hx, eps, h - hz));
}

void main() {
    vec2 xz = vWorldPos.xz;
    float dist = length(uCamPos - vWorldPos);
    // Distance-based LOD: widen the sampling step far away to kill shimmer/aliasing.
    float eps = max(0.02, dist * 0.004);
    // Match the vertex flow scroll so the shaded normals travel with the current.
    vec3 N = waterNormal(xz - uFlowDir * uTime * uFlowStrength, eps);

    vec3 V = normalize(uCamPos - vWorldPos);
    float ndv = max(dot(N, V), 0.0);

    float fresnel = pow(1.0 - ndv, uFresnelPower);
    // Deep/shallow tint; compress the view-angle range so it reads as a body of water.
    float depthMix = mix(0.4, 1.0, ndv);
    vec3  body  = mix(uShallow, uDeep, depthMix);
    vec3  color = mix(body, uReflection, fresnel * 0.6);

    // Sun glint (Blinn-Phong), tone-limited so it sparkles instead of blowing out.
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), uShininess) * uSpecStrength;
    color += uSunColor * min(spec, 1.0);
    color += uAmbient * body * 0.2;

    // Stylised crest foam: appears where the surface tilts away from vertical (crests),
    // broken up by animated noise into organic clumps.
    float crest = smoothstep(0.78, 0.55, N.y);            // 0 flat .. 1 steep crest
    float foamNoise = noise(xz * 1.1 + uTime * 0.12) * 0.5 + 0.5;
    float foam = clamp(crest * uFoamAmount * smoothstep(0.30, 0.72, foamNoise) * 1.8, 0.0, 1.0);
    color = mix(color, uFoamColor, foam);

    // Contact foam: a bright, animated ring where objects pierce the surface -- the
    // visual cue that the water is touching something.
    float contact = 0.0;
    for (int i = 0; i < uContactCount; ++i) {
        vec2  c = uContacts[i].xy;
        float r = uContacts[i].z;
        float s = uContacts[i].w;
        float d = length(xz - c);
        // Band hugging the object edge (inner fade -> peak at r -> outer fade).
        float ring = smoothstep(r + 1.1, r + 0.15, d) * smoothstep(r - 0.9, r - 0.05, d);
        float n = noise(xz * 2.3 - uTime * 0.5) * 0.5 + 0.5;   // break the ring into clumps
        contact = max(contact, ring * s * smoothstep(0.20, 0.75, n));
    }
    contact = clamp(contact, 0.0, 1.0);
    color = mix(color, uFoamColor, contact);

    float alpha = clamp(uTransparency + fresnel * (1.0 - uTransparency) + foam + contact, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
)glsl";

// Assembled at construction (version + shared noise + stage body).
std::string BuildVert() {
    return std::string("#version 330 core\n") + kWaterNoise + kWaterVertBody;
}
std::string BuildFrag() {
    return std::string("#version 330 core\n") + kWaterNoise + kWaterFragBody;
}

} // namespace

Water::Water(const WaterConfig& config) : m_config(config) {
    const std::string vert = BuildVert();
    const std::string frag = BuildFrag();
    m_shader = std::make_unique<Shader>(vert.c_str(), frag.c_str());
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
                 const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& ambient,
                 const glm::vec4* contacts, int contactCount) {
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
    m_shader->SetFloat("uSeaHeight", m_config.seaHeight);
    m_shader->SetFloat("uSeaChoppy", m_config.seaChoppy);
    m_shader->SetFloat("uSeaSpeed", m_config.seaSpeed);
    m_shader->SetFloat("uSeaFreq", m_config.seaFreq);
    m_shader->SetVec3("uFoamColor", m_config.foamColor);
    m_shader->SetFloat("uFoamAmount", m_config.foamAmount);
    m_shader->SetVec2("uFlowDir", m_config.flowDir);
    m_shader->SetFloat("uFlowStrength", m_config.flowStrength);

    const int n = (contacts && contactCount > 0)
        ? std::min(contactCount, kMaxContacts) : 0;
    m_shader->SetInt("uContactCount", n);
    for (int i = 0; i < n; ++i) {
        m_shader->SetVec4("uContacts[" + std::to_string(i) + "]", contacts[i]);
    }

    m_mesh->Draw();

    glDepthMask(prevDepthMask);
    if (!prevBlend) glDisable(GL_BLEND);
}

namespace {
// CPU mirror of the shader's value noise + sea octaves, so buoyancy matches the
// rendered surface. Uses the geometry octave count (3) like the vertex shader.
float CpuHash12(const glm::vec2& p) {
    const glm::vec2 fp(std::floor(p.x), std::floor(p.y));
    const std::uint32_t qx = static_cast<std::uint32_t>(static_cast<std::int32_t>(fp.x)) * 1597334677u;
    const std::uint32_t qy = static_cast<std::uint32_t>(static_cast<std::int32_t>(fp.y)) * 3812015801u;
    const std::uint32_t n = (qx ^ qy) * 1597334677u;
    return static_cast<float>(n) * (1.0f / 4294967295.0f);
}
float CpuNoise(const glm::vec2& p) {
    const glm::vec2 i(std::floor(p.x), std::floor(p.y));
    const glm::vec2 f = p - i;
    const glm::vec2 u = f * f * (glm::vec2(3.0f) - 2.0f * f);
    const float a = CpuHash12(i + glm::vec2(0.0f, 0.0f));
    const float b = CpuHash12(i + glm::vec2(1.0f, 0.0f));
    const float c = CpuHash12(i + glm::vec2(0.0f, 1.0f));
    const float d = CpuHash12(i + glm::vec2(1.0f, 1.0f));
    return -1.0f + 2.0f * glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}
float CpuSeaOctave(glm::vec2 uv, float choppy) {
    uv += glm::vec2(CpuNoise(uv));
    glm::vec2 wv  = glm::vec2(1.0f) - glm::abs(glm::sin(uv));
    glm::vec2 swv = glm::abs(glm::cos(uv));
    wv = glm::mix(wv, swv, wv);
    return std::pow(1.0f - std::pow(wv.x * wv.y, 0.65f), choppy);
}
} // namespace

float Water::HeightAt(float worldX, float worldZ) const {
    // Matches the vertex shader's sea_height() (3 geometry octaves) so floating objects
    // track the rendered crests.
    float freq   = m_config.seaFreq;
    float amp    = m_config.seaHeight;
    float choppy = m_config.seaChoppy;
    glm::vec2 uv(worldX, worldZ);
    uv.x *= 0.75f;
    const glm::mat2 octave(1.6f, 1.2f, -1.2f, 1.6f);
    float h = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float d  = CpuSeaOctave((uv + m_time * m_config.seaSpeed) * freq, choppy);
        d       += CpuSeaOctave((uv - m_time * m_config.seaSpeed) * freq, choppy);
        h += d * amp;
        uv = uv * octave;
        freq *= 1.9f;
        amp  *= 0.22f;
        choppy = glm::mix(choppy, 1.0f, 0.2f);
    }
    return m_config.center.y + h;
}

} // namespace engine
