#include "engine/graphics/ProceduralSky.h"

#include "engine/graphics/Primitives.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {
namespace {

const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vDir;
void main() {
    vDir = aPos;
    vec4 p = uViewProj * vec4(aPos, 1.0);
    gl_Position = p.xyww;
}
)GLSL";

const char* kFrag = R"GLSL(
#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform vec3  uSunToward;
uniform vec3  uMoonToward;
uniform vec3  uHorizon;
uniform vec3  uZenith;
uniform vec3  uSunDisc;
uniform vec3  uMoonDisc;
uniform float uDayFactor;
uniform int   uApplyTonemap;

float hash(vec3 p) {
    return fract(sin(dot(floor(p), vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

void main() {
    vec3 d = normalize(vDir);
    float night = 1.0 - uDayFactor;

    // Vertical gradient (ground tint below the horizon).
    float t = clamp(d.y, 0.0, 1.0);
    vec3 col = (d.y < 0.0) ? uHorizon * 0.4 * (1.0 + d.y * 0.3)
                           : mix(uHorizon, uZenith, t);

    // Sun: a tight disc plus a daytime halo.
    float sd = clamp(dot(d, uSunToward), 0.0, 1.0);
    col += uSunDisc * pow(sd, 260.0) * 1.8;
    col += uSunDisc * pow(sd, 8.0) * 0.15 * uDayFactor;

    // Moon: disc + soft halo, visible at night.
    float md = clamp(dot(d, uMoonToward), 0.0, 1.0);
    col += uMoonDisc * pow(md, 500.0) * 1.2 * night;
    col += uMoonDisc * pow(md, 40.0) * 0.05 * night;

    // Stars: sparse, above the horizon, only at night.
    if (d.y > 0.02) {
        float h = hash(d * 220.0);
        float star = step(0.9986, h);
        col += vec3(star) * night * (0.6 + 0.6 * t);
    }

    if (uApplyTonemap == 1) col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);
}
)GLSL";

} // namespace

ProceduralSky::ProceduralSky()
    : m_cube(primitives::Cube()), m_shader(kVert, kFrag) {}

void ProceduralSky::Draw(const glm::mat4& view, const glm::mat4& projection,
                         const DayNightCycle::Sample& sky, bool tonemap) {
    glDepthFunc(GL_LEQUAL);
    m_shader.Bind();
    m_shader.SetMat4("uViewProj", projection * glm::mat4(glm::mat3(view)));
    m_shader.SetVec3("uSunToward",  sky.sunToward);
    m_shader.SetVec3("uMoonToward", sky.moonToward);
    m_shader.SetVec3("uHorizon",    sky.horizon);
    m_shader.SetVec3("uZenith",     sky.zenith);
    m_shader.SetVec3("uSunDisc",    sky.sunDisc);
    m_shader.SetVec3("uMoonDisc",   sky.moonDisc);
    m_shader.SetFloat("uDayFactor", sky.dayFactor);
    m_shader.SetInt("uApplyTonemap", tonemap ? 1 : 0);
    m_cube.Draw();
    glDepthFunc(GL_LESS);
}

} // namespace engine