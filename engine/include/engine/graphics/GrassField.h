#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Terrain.h"   // Heightmap

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace engine {

// Look + motion of a grass field. Colours run root->tip; wind sways the blades. Density
// is roughly blades per square world unit (before the paint mask thins them out).
struct GrassConfig {
    float     density      = 2.0f;
    float     bladeHeight  = 0.6f;
    float     bladeWidth   = 1.0f;                 // multiplier on the base blade width
    glm::vec3 baseColor{0.16f, 0.34f, 0.12f};      // root (darker)
    glm::vec3 tipColor{0.42f, 0.68f, 0.28f};       // tip (lighter)
    float     windStrength = 0.18f;
    float     windSpeed    = 1.4f;
    int       grassLayer   = 1;                    // paint layer that grows grass (1 = grass)
    int       maxBlades    = 200000;               // safety cap
};

// One frozen grass style (baked per-blade so changing the active settings never alters
// grass already on the field). density is per-region via probabilistic thinning.
struct GrassStyle {
    float     density = 2.0f;
    float     bladeHeight = 0.6f;
    glm::vec3 base{0.16f, 0.34f, 0.12f};
    glm::vec3 tip{0.42f, 0.68f, 0.28f};
};

// A field of GPU-instanced procedural grass blades placed on a terrain. Header-only so
// it needs no separate translation unit / CMake entry. Move-only (owns GL objects).
class GrassField {
public:
    GrassField() { InitGpu(); }
    ~GrassField() { Release(); }
    GrassField(const GrassField&) = delete;
    GrassField& operator=(const GrassField&) = delete;
    GrassField(GrassField&& o) noexcept { MoveFrom(o); }
    GrassField& operator=(GrassField&& o) noexcept { if (this != &o) { Release(); MoveFrom(o); } return *this; }

    static constexpr int kFloatsPerInstance = 13;   // pos3,yaw,scale,phase,height,base3,tip3

    // (Re)scatter blades over the terrain, only where paint == cfg.grassLayer. Each blade
    // bakes the appearance of its texel's frozen style (styleIndex -> palette; slot 0 or
    // out of range = the active cfg), so editing the active settings never changes existing
    // grass. Density is per-style (probabilistic thinning). worldOrigin is the terrain's
    // world position (its heightmap is local, origin 0).
    void Build(const Heightmap& hm, const std::vector<std::uint8_t>& paint,
               const std::vector<std::uint8_t>& styleIndex, const std::vector<GrassStyle>& palette,
               const glm::vec3& worldOrigin, const GrassConfig& cfg) {
        m_config = cfg;
        GrassStyle active{cfg.density, cfg.bladeHeight, cfg.baseColor, cfg.tipColor};
        std::vector<float> instances;
        const int res = hm.res;
        if (res >= 2 && !hm.h.empty()) {
            const float size = hm.size;
            const bool havePaint = static_cast<int>(paint.size()) == res * res;
            const bool haveStyle = static_cast<int>(styleIndex.size()) == res * res;
            float maxDensity = std::max(active.density, 0.02f);
            for (const GrassStyle& s : palette) maxDensity = std::max(maxDensity, s.density);
            const float spacing = 1.0f / std::sqrt(maxDensity);   // scatter grid at max density
            std::mt19937 rng(1337u);
            std::uniform_real_distribution<float> u01(0.0f, 1.0f);
            int placed = 0;
            for (float gz = 0.0f; gz < size && placed < cfg.maxBlades; gz += spacing) {
                for (float gx = 0.0f; gx < size && placed < cfg.maxBlades; gx += spacing) {
                    const float jx = gx + (u01(rng) - 0.5f) * spacing;
                    const float jz = gz + (u01(rng) - 0.5f) * spacing;
                    if (jx < 0.0f || jx >= size || jz < 0.0f || jz >= size) continue;
                    if (!havePaint) continue;   // "only painted grass"
                    const int i = std::min(res - 1, std::max(0, static_cast<int>(jx / size * (res - 1) + 0.5f)));
                    const int j = std::min(res - 1, std::max(0, static_cast<int>(jz / size * (res - 1) + 0.5f)));
                    const std::size_t texel = static_cast<std::size_t>(j) * res + i;
                    if (paint[texel] != cfg.grassLayer) continue;

                    GrassStyle st = active;
                    if (haveStyle) {
                        const unsigned slot = styleIndex[texel];
                        if (slot >= 1 && slot <= palette.size()) st = palette[slot - 1];
                    }
                    // Per-style density: thin the max-density grid down to this style's density.
                    if (u01(rng) > st.density / maxDensity) continue;

                    const float y = hm.HeightAt(jx, jz);
                    instances.push_back(worldOrigin.x + jx);
                    instances.push_back(worldOrigin.y + y);
                    instances.push_back(worldOrigin.z + jz);
                    instances.push_back(u01(rng) * 6.2831853f);         // yaw
                    instances.push_back(0.75f + u01(rng) * 0.5f);       // scale variation
                    instances.push_back(u01(rng) * 6.2831853f);         // wind phase
                    instances.push_back(st.bladeHeight);                // baked height
                    instances.push_back(st.base.r); instances.push_back(st.base.g); instances.push_back(st.base.b);
                    instances.push_back(st.tip.r);  instances.push_back(st.tip.g);  instances.push_back(st.tip.b);
                    ++placed;
                }
            }
        }
        m_instanceCount = static_cast<int>(instances.size() / kFloatsPerInstance);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(instances.size() * sizeof(float)),
                     instances.empty() ? nullptr : instances.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void SetConfig(const GrassConfig& cfg) { m_config = cfg; }   // colours/wind/height are uniforms
    const GrassConfig& Config() const { return m_config; }
    void Update(float dt) { m_time += dt; }
    int  InstanceCount() const { return m_instanceCount; }

    // A cheap signature the caller compares to decide whether a rebuild is needed.
    void        SetSignature(std::size_t sig) { m_signature = sig; }
    std::size_t Signature() const { return m_signature; }

    static constexpr int kMaxInteractors = 16;

    // Draw after the opaque terrain (depth test + write; culling off since blades are thin).
    // `interactors` are objects that push/flatten the grass: each is (worldX, worldY,
    // worldZ, radius). Blades within an interactor's radius bend away and press down.
    void Draw(const Camera& camera, float aspect, const glm::vec3& sunDir,
              const glm::vec3& sunColor, const glm::vec3& ambient,
              const glm::vec4* interactors = nullptr, int interactorCount = 0) {
        if (m_instanceCount <= 0 || !m_shader) return;
        const GLboolean cull = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        m_shader->Bind();
        m_shader->SetMat4("uViewProj", camera.ProjectionMatrix(aspect) * camera.ViewMatrix());
        m_shader->SetFloat("uTime", m_time);
        m_shader->SetFloat("uBladeWidth", m_config.bladeWidth);
        m_shader->SetFloat("uWindStrength", m_config.windStrength);
        m_shader->SetFloat("uWindSpeed", m_config.windSpeed);
        m_shader->SetVec3("uSunColor", sunColor);
        m_shader->SetVec3("uSunDir", sunDir);
        m_shader->SetVec3("uAmbient", ambient);
        const int n = (interactors && interactorCount > 0)
            ? std::min(interactorCount, kMaxInteractors) : 0;
        m_shader->SetInt("uInteractorCount", n);
        for (int i = 0; i < n; ++i) {
            m_shader->SetVec4("uInteractors[" + std::to_string(i) + "]", interactors[i]);
        }
        glBindVertexArray(m_vao);
        glDrawElementsInstanced(GL_TRIANGLES, 9, GL_UNSIGNED_INT, nullptr, m_instanceCount);
        glBindVertexArray(0);
        if (cull) glEnable(GL_CULL_FACE);
    }

private:
    void InitGpu() {
        // Blade geometry: root(2) -> mid(2) -> tip(1); heightFrac in .w-ish channel.
        const float verts[] = {
            //  x       y     z    heightFrac
            -0.05f,  0.0f, 0.0f,  0.0f,
             0.05f,  0.0f, 0.0f,  0.0f,
            -0.03f,  0.5f, 0.0f,  0.5f,
             0.03f,  0.5f, 0.0f,  0.5f,
             0.00f,  1.0f, 0.0f,  1.0f,
        };
        const std::uint32_t idx[] = {0, 1, 2, 2, 1, 3, 2, 3, 4};

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        auto offset = [](std::size_t floats) {
            return reinterpret_cast<const void*>(floats * sizeof(float));
        };

        glGenBuffers(1, &m_geoVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_geoVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        const GLsizei geoStride = static_cast<GLsizei>(4 * sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, geoStride, offset(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, geoStride, offset(3));

        glGenBuffers(1, &m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

        glGenBuffers(1, &m_instanceVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        const GLsizei stride = static_cast<GLsizei>(kFloatsPerInstance * sizeof(float));
        glEnableVertexAttribArray(2);   // iPos
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, offset(0));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);   // yaw
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, offset(3));
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);   // scale
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, offset(4));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);   // phase
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, offset(5));
        glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6);   // baked height
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, offset(6));
        glVertexAttribDivisor(6, 1);
        glEnableVertexAttribArray(7);   // baked base colour
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, stride, offset(7));
        glVertexAttribDivisor(7, 1);
        glEnableVertexAttribArray(8);   // baked tip colour
        glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, stride, offset(10));
        glVertexAttribDivisor(8, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        static const char* kVert = R"glsl(
#version 330 core
layout(location=0) in vec3  aPos;
layout(location=1) in float aHeight;
layout(location=2) in vec3  iPos;
layout(location=3) in float iYaw;
layout(location=4) in float iScale;
layout(location=5) in float iPhase;
layout(location=6) in float iBladeHeight;   // baked per blade (frozen style)
layout(location=7) in vec3  iBase;
layout(location=8) in vec3  iTip;
uniform mat4  uViewProj;
uniform float uTime;
uniform float uBladeWidth;
uniform float uWindStrength;
uniform float uWindSpeed;
const int kMaxInteractors = 16;
uniform int   uInteractorCount;
uniform vec4  uInteractors[kMaxInteractors];   // xyz = world pos, w = radius
out float vHeight;
out vec3  vBase;
out vec3  vTip;
void main() {
    vBase = iBase;
    vTip  = iTip;
    vec3 p = aPos;
    p.x *= uBladeWidth;
    p.y *= iBladeHeight;
    p  *= iScale;
    float c = cos(iYaw), s = sin(iYaw);
    vec3 rot = vec3(p.x * c - p.z * s, p.y, p.x * s + p.z * c);
    // Wind bends the blade more toward the tip (heightFrac^2), coherent across the field.
    float w = sin(uTime * uWindSpeed + iPhase + iPos.x * 0.15 + iPos.z * 0.15);
    float bend = uWindStrength * aHeight * aHeight * w;
    rot.x += bend;
    rot.z += bend * 0.5;

    // Object interaction: nearby objects push blades away and flatten them; springs back
    // as the object leaves (there is no persistent state -- it follows the objects live).
    vec2  push = vec2(0.0);
    float flatten = 0.0;
    for (int k = 0; k < uInteractorCount; ++k) {
        vec3  ic = uInteractors[k].xyz;
        float r  = uInteractors[k].w;
        vec2  away = iPos.xz - ic.xz;
        float dist = length(away);
        // Vertical gate so an object above the grass doesn't flatten it.
        float vgate = 1.0 - clamp(abs(iPos.y - ic.y) / max(r * 1.5, 0.5), 0.0, 1.0);
        if (r > 0.0 && dist < r && vgate > 0.0) {
            float f = (1.0 - dist / r) * vgate;
            push    += (dist > 1e-4 ? away / dist : vec2(1.0, 0.0)) * f;
            flatten  = max(flatten, f);
        }
    }
    // Bend the blade outward (more at the tip) and press it down under the object.
    rot.x += push.x * aHeight * 1.4;
    rot.z += push.y * aHeight * 1.4;
    rot.y *= (1.0 - flatten * 0.8 * aHeight);

    vHeight = aHeight;
    gl_Position = uViewProj * vec4(iPos + rot, 1.0);
}
)glsl";
        static const char* kFrag = R"glsl(
#version 330 core
in float vHeight;
in vec3  vBase;
in vec3  vTip;
uniform vec3 uSunColor;
uniform vec3 uSunDir;
uniform vec3 uAmbient;
out vec4 FragColor;
void main() {
    vec3 col = mix(vBase, vTip, vHeight);
    // Blades face up; approximate lighting with ambient + a soft sun term, darker at root.
    float sun = clamp(dot(vec3(0.0, 1.0, 0.0), normalize(-uSunDir)), 0.0, 1.0);
    vec3 lit = col * (uAmbient + uSunColor * (0.25 + 0.75 * sun) * (0.4 + 0.6 * vHeight));
    FragColor = vec4(lit, 1.0);
}
)glsl";
        m_shader = std::make_unique<Shader>(kVert, kFrag);
    }

    void Release() {
        if (m_ebo) glDeleteBuffers(1, &m_ebo);
        if (m_instanceVbo) glDeleteBuffers(1, &m_instanceVbo);
        if (m_geoVbo) glDeleteBuffers(1, &m_geoVbo);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        m_vao = m_geoVbo = m_ebo = m_instanceVbo = 0;
    }
    void MoveFrom(GrassField& o) {
        m_vao = o.m_vao; m_geoVbo = o.m_geoVbo; m_ebo = o.m_ebo; m_instanceVbo = o.m_instanceVbo;
        m_instanceCount = o.m_instanceCount; m_shader = std::move(o.m_shader);
        m_config = o.m_config; m_time = o.m_time; m_signature = o.m_signature;
        o.m_vao = o.m_geoVbo = o.m_ebo = o.m_instanceVbo = 0; o.m_instanceCount = 0;
    }

    unsigned int m_vao = 0, m_geoVbo = 0, m_ebo = 0, m_instanceVbo = 0;
    int m_instanceCount = 0;
    std::unique_ptr<Shader> m_shader;
    GrassConfig m_config;
    float m_time = 0.0f;
    std::size_t m_signature = 0;
};

} // namespace engine
