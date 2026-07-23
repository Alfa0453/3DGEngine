#include "engine/graphics/ProceduralSky.h"

#include "engine/graphics/Primitives.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>

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
uniform int   uCloudsEnabled;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudScale;
uniform float uCloudSoftness;
uniform float uCloudTime;
uniform vec2  uCloudWind;
uniform float uCloudHorizon;
uniform vec3  uCloudColor;

float hash(vec3 p) {
    return fract(sin(dot(floor(p), vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float z0 = mix(mix(hash(i + vec3(0.0, 0.0, 0.0)),
                       hash(i + vec3(1.0, 0.0, 0.0)), f.x),
                   mix(hash(i + vec3(0.0, 1.0, 0.0)),
                       hash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y);
    float z1 = mix(mix(hash(i + vec3(0.0, 0.0, 1.0)),
                       hash(i + vec3(1.0, 0.0, 1.0)), f.x),
                   mix(hash(i + vec3(0.0, 1.0, 1.0)),
                       hash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y);
    return mix(z0, z1, f.z);
}

float fbm3(vec3 p) {
    float value = 0.0;
    float weight = 0.52;
    for (int i = 0; i < 5; ++i) {
        value += noise3(p) * weight;
        // Permuting the axes between octaves prevents visible directional bands.
        p = p.yzx * 2.03 + vec3(13.7, 9.2, 17.1);
        weight *= 0.5;
    }
    return value;
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

    // Sample a three-dimensional noise field on the sky sphere. Unlike longitude /
    // latitude UVs this has no pole where the pattern collapses into a pinwheel.
    if (uCloudsEnabled == 1 && d.y > -0.02) {
        vec3 windOffset = vec3(uCloudWind.x, 0.0, uCloudWind.y) * uCloudTime;
        vec3 p = d * (4.2 * max(uCloudScale, 0.05)) + windOffset;
        float shape = fbm3(p);
        float detail = fbm3(p * 2.7 + vec3(4.2, -3.1, 6.7));
        shape = mix(shape, shape * 0.72 + detail * 0.38, 0.45);
        float edge = max(uCloudSoftness, 0.005);
        float cloud = smoothstep(uCloudCoverage - edge,
                                 uCloudCoverage + edge, shape);
        cloud *= clamp(uCloudDensity, 0.0, 2.0);
        cloud *= smoothstep(-0.02, max(uCloudHorizon, 0.005), d.y);
        cloud = clamp(cloud, 0.0, 0.96);

        float sunLight = 0.55 + 0.45 * pow(sd, 3.0);
        vec3 dayCloud = uCloudColor * sunLight;
        vec3 nightCloud = mix(uZenith, uMoonDisc, 0.18) * 0.55;
        vec3 cloudColor = mix(nightCloud, dayCloud, uDayFactor);
        col = mix(col, cloudColor, cloud);
    }

    if (uApplyTonemap == 1) col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);
}
)GLSL";

} // namespace

ProceduralSky::ProceduralSky()
    : m_cube(primitives::Cube()), m_shader(kVert, kFrag) {}

void ProceduralSky::Draw(const glm::mat4& view, const glm::mat4& projection,
                         const DayNightCycle::Sample& sky, bool tonemap,
                         const CloudSettings& clouds) {
    GLint previousDepthFunc = GL_LESS;
    GLboolean previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

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
    m_shader.SetInt("uCloudsEnabled", clouds.enabled ? 1 : 0);
    m_shader.SetFloat("uCloudCoverage", clouds.coverage);
    m_shader.SetFloat("uCloudDensity", clouds.density);
    m_shader.SetFloat("uCloudScale", clouds.scale);
    m_shader.SetFloat("uCloudSoftness", clouds.softness);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const float seconds = std::chrono::duration<float>(now).count();
    m_shader.SetFloat("uCloudTime", seconds * clouds.windSpeed);
    const float direction = glm::radians(clouds.windDirectionDegrees);
    m_shader.SetVec2("uCloudWind", glm::vec2(std::cos(direction), std::sin(direction)));
    m_shader.SetFloat("uCloudHorizon", clouds.horizonHeight);
    m_shader.SetVec3("uCloudColor", clouds.color);
    m_cube.Draw();
    glDepthMask(previousDepthMask);
    glDepthFunc(previousDepthFunc);
}

} // namespace engine
