#include "engine/graphics/PbrRenderer.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/Camera.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/IBL.h"
#include "engine/graphics/SSAO.h"
#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/Texture.h"

#include <algorithm>
#include <cmath>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::Light;

namespace engine {
namespace {

const char* kPbrVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
void main() {
    vec4 world  = uModel * vec4(aPos, 1.0);
    vWorldPos   = world.xyz;
    vNormal     = normalize(uNormalMat * aNormal);
    vUV         = aTexCoord;
    gl_Position = uViewProj * world;
}
)GLSL";

const char* kPbrFrag = R"GLSL(
#version 330 core
const float PI = 3.14159265359;
const int   MAX_SPOTS  = 4;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;
uniform vec3  uViewPos;
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3  uEmissive;
uniform int uHasAlbedoMap;
uniform int uHasNormalMap;
uniform int uHasMetalRoughMap;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMetalRoughMap;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uAmbient;
layout(std140) uniform LightBlock {
    vec4 uLightPosRadius[128];   // world position + influence radius
    vec4 uLightColor[128];       // radiance
};
uniform isamplerBuffer uTileLights;
const ivec2 TILE_COUNT  = ivec2(16, 9);
const int   TILE_STRIDE = 64;
uniform int   uNumSpots;
uniform int   uNumSpotShadows;
uniform vec3  uSpotPos[MAX_SPOTS];
uniform vec3  uSpotDir[MAX_SPOTS];
uniform vec3  uSpotColor[MAX_SPOTS];
uniform float uSpotCosInner[MAX_SPOTS];
uniform float uSpotCosOuter[MAX_SPOTS];
uniform mat4  uSpotVP[MAX_SPOTS];
uniform sampler2D uSpotMap[MAX_SPOTS];
const int MAX_AREAS = 4;
uniform int   uNumAreas;
uniform vec3  uAreaPos[MAX_AREAS];
uniform vec3  uAreaColor[MAX_AREAS];
uniform float uAreaRadius[MAX_AREAS];
uniform sampler2DArray uCascadeMaps;
uniform mat4  uCascadeVP[4];
uniform float uCascadeSplits[4];
uniform mat4  uView;
uniform float uShadowSoftness;
uniform samplerCube uPointCube[4];
uniform int uNumPointShadows;
uniform int uApplyTonemap;
uniform int uUseIBL;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D uBrdfLUT;
uniform float uMaxReflectionLod;
uniform int   uUseSSAO;
uniform sampler2D uSsaoMap;
uniform vec2  uScreenSize;
uniform int   uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
uniform float uFogHeightFalloff;
float DistributionGGX(vec3 N, vec3 H, float r) {
    float a = r*r; float a2 = a*a; float NdotH = max(dot(N,H),0.0);
    float d = (NdotH*NdotH)*(a2-1.0)+1.0; return a2/(PI*d*d);
}
float GeometrySchlickGGX(float NdotV, float r) {
    float k = (r+1.0)*(r+1.0)/8.0; return NdotV/(NdotV*(1.0-k)+k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float r) {
    return GeometrySchlickGGX(max(dot(N,V),0.0),r)*GeometrySchlickGGX(max(dot(N,L),0.0),r);
}
vec3 FresnelSchlick(float c, vec3 F0) { return F0+(1.0-F0)*pow(clamp(1.0-c,0.0,1.0),5.0); }
vec3 FresnelSchlickRough(float c, vec3 F0, float r) {
    return F0 + (max(vec3(1.0-r), F0) - F0) * pow(clamp(1.0-c,0.0,1.0),5.0);
}
float PointShadowFactor(int idx, vec3 fragToLight) {
    float closest;
    if (idx == 0)      closest = texture(uPointCube[0], fragToLight).r;
    else if (idx == 1) closest = texture(uPointCube[1], fragToLight).r;
    else if (idx == 2) closest = texture(uPointCube[2], fragToLight).r;
    else               closest = texture(uPointCube[3], fragToLight).r;
    return (length(fragToLight) - 0.15 > closest) ? 1.0 : 0.0;
}
float SpotShadowFactor(int idx, vec3 worldPos) {
    vec4 lp = uSpotVP[idx] * vec4(worldPos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0) return 0.0;
    float bias = 0.0015;
    float closest;
    if (idx == 0)      closest = texture(uSpotMap[0], p.xy).r;
    else if (idx == 1) closest = texture(uSpotMap[1], p.xy).r;
    else if (idx == 2) closest = texture(uSpotMap[2], p.xy).r;
    else               closest = texture(uSpotMap[3], p.xy).r;
    return (p.z - bias > closest) ? 1.0 : 0.0;
}
float ShadowFactor(float NdotL) {
    float depth = abs((uView * vec4(vWorldPos, 1.0)).z);
    int layer = 3;
    for (int i = 0; i < 4; ++i) { if (depth < uCascadeSplits[i]) { layer = i; break; } }
    vec4 lp = uCascadeVP[layer] * vec4(vWorldPos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0) return 0.0;
    float cur = p.z;
    float bias = max(0.0025 * (1.0 - NdotL), 0.0008);
    vec2 texel = 1.0 / vec2(textureSize(uCascadeMaps, 0).xy);

    // PCSS step 1: blocker search -> average depth of occluders nearer the light.
    float blockerSum = 0.0; int blockers = 0;
    for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) {
        float d = texture(uCascadeMaps, vec3(p.xy + vec2(x, y) * texel * uShadowSoftness, float(layer))).r;
        if (d < cur - bias) { blockerSum += d; ++blockers; }
    }
    if (blockers == 0) return 0.0;                       // no occluder -> lit
    float avgBlocker = blockerSum / float(blockers);

    // PCSS step 2: penumbra grows with receiver-to-blocker distance.
    float penumbra = (cur - avgBlocker) * uShadowSoftness * 300.0;
    float radius = clamp(penumbra, 1.0, 16.0);

    // PCSS step 3: PCF over a kernel sized by the penumbra.
    float s = 0.0;
    for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) {
        float d = texture(uCascadeMaps, vec3(p.xy + vec2(x, y) * texel * radius, float(layer))).r;
        s += (cur - bias > d) ? 1.0 : 0.0;
    }
    return s / 25.0;
}
vec3 Lighting(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float m, float r) {
    vec3 H = normalize(V+L);
    float NDF = DistributionGGX(N,H,r);
    float G = GeometrySmith(N,V,L,r);
    vec3 F = FresnelSchlick(max(dot(H,V),0.0),F0);
    vec3 spec = (NDF*G*F)/(4.0*max(dot(N,V),0.0)*max(dot(N,L),0.0)+0.0001);
    vec3 kD = (vec3(1.0)-F)*(1.0-m);
    return (kD*albedo/PI+spec)*radiance*max(dot(N,L),0.0);
}
// Sphere area light (representative-point specular + centre diffuse).
vec3 AreaLight(vec3 N, vec3 V, vec3 Lvec, float srad, vec3 color,
               vec3 albedo, vec3 F0, float m, float r) {
    float dc2 = dot(Lvec, Lvec);
    vec3 radiance = color / max(dc2, 0.0001);
    // Diffuse from the light centre.
    vec3 Ld = Lvec * inversesqrt(dc2);
    float NdotLd = max(dot(N, Ld), 0.0);
    vec3 kD = (vec3(1.0) - F0) * (1.0 - m);
    vec3 diffuse = kD * albedo / PI * radiance * NdotLd;
    // Specular: closest point on the sphere to the reflection ray.
    vec3 R = reflect(-V, N);
    vec3 centerToRay = dot(Lvec, R) * R - Lvec;
    vec3 closest = Lvec + centerToRay * clamp(srad / max(length(centerToRay), 1e-4), 0.0, 1.0);
    vec3 Ls = normalize(closest);
    float NdotLs = max(dot(N, Ls), 0.0);
    vec3 H = normalize(V + Ls);
    float alpha  = max(r * r, 1e-3);
    float alphaP = clamp(alpha + srad / (2.0 * sqrt(dc2)), 0.0, 1.0);
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH) * (alphaP * alphaP - 1.0) + 1.0;
    float NDF = (alphaP * alphaP) / (PI * d * d);
    float G = GeometrySmith(N, V, Ls, r);
    vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float sphereNorm = (alpha / alphaP) * (alpha / alphaP);     // energy conservation
    vec3 spec = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotLs + 0.0001) * sphereNorm;
    return diffuse + spec * radiance * NdotLs;
}
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);  vec3 dp2 = dFdy(p);
    vec2 du1 = dFdx(uv); vec2 du2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * du1.x + dp1perp * du2.x;
    vec3 B = dp2perp * du1.y + dp1perp * du2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}
void main() {
    vec3 albedo = uAlbedo;
    if (uHasAlbedoMap == 1) albedo *= texture(uAlbedoMap, vUV).rgb;
    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = uAO;
    if (uHasMetalRoughMap == 1) {
        vec3 mr = texture(uMetalRoughMap, vUV).rgb;
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    vec3 N = normalize(vNormal);
    if (uHasNormalMap == 1) {
        vec3 tn = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;
        N = normalize(CotangentFrame(normalize(vNormal), vWorldPos, vUV) * tn);
    }
    vec3 V = normalize(uViewPos-vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);
    vec3 Ls = normalize(-uSunDir);
    float sunNdotL = max(dot(N,Ls),0.0);
    float shadow = ShadowFactor(sunNdotL);
    Lo += (1.0-shadow)*Lighting(N,V,Ls,uSunColor,albedo,F0,metallic,roughness);
    ivec2 tile = ivec2(gl_FragCoord.xy / (uScreenSize / vec2(TILE_COUNT)));
    tile = clamp(tile, ivec2(0), TILE_COUNT - ivec2(1));
    int tbase = (tile.y * TILE_COUNT.x + tile.x) * TILE_STRIDE;
    int nLights = texelFetch(uTileLights, tbase).r;
    for (int k = 0; k < nLights && k < TILE_STRIDE - 1; ++k) {
        int li = texelFetch(uTileLights, tbase + 1 + k).r;
        vec3 lp = uLightPosRadius[li].xyz;
        float lr = uLightPosRadius[li].w;
        vec3 d = lp - vWorldPos;
        float dist2 = dot(d, d);
        if (dist2 > lr * lr) continue;
        vec3 L = d / sqrt(dist2);
        vec3 radiance = uLightColor[li].rgb / max(dist2, 0.0001);
        vec3 contrib = Lighting(N, V, L, radiance, albedo, F0, metallic, roughness);
        if (li < uNumPointShadows) contrib *= (1.0 - PointShadowFactor(li, -d));
        Lo += contrib;
    }
    vec3 ambient;
    if (uUseIBL == 1) {
        vec3 F = FresnelSchlickRough(max(dot(N,V),0.0), F0, roughness);
        vec3 kD = (vec3(1.0)-F)*(1.0-metallic);
        vec3 irradiance = texture(uIrradiance, N).rgb;
        vec3 diffuse = irradiance * albedo;
        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(uPrefilter, R, roughness*uMaxReflectionLod).rgb;
        vec2 brdf = texture(uBrdfLUT, vec2(max(dot(N,V),0.0), roughness)).rg;
        vec3 specular = prefiltered * (F*brdf.x + brdf.y);
        ambient = (kD*diffuse + specular) * ao;
    } else {
        ambient = uAmbient*albedo*ao;
    }
    if (uUseSSAO == 1) ambient *= texture(uSsaoMap, gl_FragCoord.xy / uScreenSize).r;
    vec3 color = ambient + Lo + uEmissive;

    if (uFogEnabled == 1) {
        float dist = length(uViewPos - vWorldPos);
        float distFog = 1.0 - exp(-dist * uFogDensity);
        float heightF = clamp(exp(-(vWorldPos.y - uFogHeight) * uFogHeightFalloff), 0.0, 1.0);
        float fog = clamp(distFog * heightF, 0.0, 1.0);
        color = mix(color, uFogColor, fog);
    }

    if (uApplyTonemap == 1) {
        color = color/(color+vec3(1.0));
        color = pow(color, vec3(1.0/2.2));
    }
    FragColor = vec4(color,1.0);
}
)GLSL";

} // namespace

PbrRenderer::PbrRenderer(int shadowSize)
    : m_cascade(shadowSize),
      m_pointShadow(512),
      m_spotShadow(1024),
      m_pbr(std::make_unique<Shader>(kPbrVert, kPbrFrag)) {
    const unsigned int blk = glGetUniformBlockIndex(m_pbr->ID(), "LightBlock");
    if (blk != GL_INVALID_INDEX) glUniformBlockBinding(m_pbr->ID(), blk, 0);
}

PbrRenderer::~PbrRenderer() = default;

void PbrRenderer::Render(ecs::Registry& reg, const Camera& camera, float aspect,
                         int screenWidth, int screenHeight) {
    Render(reg, camera, aspect, screenWidth, screenHeight, Options());
}

void PbrRenderer::Render(ecs::Registry& reg, const Camera& camera, float aspect,
                         int screenWidth, int screenHeight, const Options& opt) {
    // Gather lights.
    glm::vec3 sunDir(0.0f, -1.0f, 0.0f), sunColor(0.0f);
    std::vector<glm::vec3> ppos;
    std::vector<ClusteredLights::PointLight> clusterLights;
    std::vector<glm::vec3> spotPos, spotDir, spotCol;
    std::vector<float> spotCosIn, spotCosOut;
    std::vector<glm::vec3> areaPos, areaCol;
    std::vector<float> areaRad;
    std::vector<SpotShadow::Spot> spotList;
    reg.view<Transform, Light>().each([&](Entity, Transform& t, Light& l) {
        const glm::vec3 c = l.color * l.intensity;
        if (l.type == Light::Type::Directional) { sunDir = glm::normalize(l.direction); sunColor = c; }
        else if (l.type == Light::Type::Point) {
            const float radius = std::sqrt(std::max(std::max(c.r, c.g), c.b) / 0.03f);
            clusterLights.push_back({t.position, c, radius});
            ppos.push_back(t.position);
        }
        else if (l.type == Light::Type::Spot) {
            const glm::vec3 dir = glm::normalize(l.direction);
            spotPos.push_back(t.position);
            spotDir.push_back(dir);
            spotCol.push_back(c);
            spotCosIn.push_back(glm::cos(glm::radians(l.innerAngle)));
            spotCosOut.push_back(glm::cos(glm::radians(l.outerAngle)));
            spotList.push_back(SpotShadow::Spot{t.position, dir, l.outerAngle, l.range});
        }
        else {  // Area (sphere)
            areaPos.push_back(t.position);
            areaCol.push_back(c);
            areaRad.push_back(l.sourceRadius);
        }
    });

    // Cascaded directional (sun) shadow pass.
    m_cascade.Generate(reg, camera, aspect, sunDir, 60.0f);
    glViewport(0, 0, screenWidth, screenHeight);

    int numPointShadows = 0;
    if (opt.pointShadows && !ppos.empty()) {
        m_pointShadow.Generate(reg, ppos, 50.0f);
        glViewport(0, 0, screenWidth, screenHeight);
        numPointShadows = m_pointShadow.Count();
    }
    int numSpotShadows = 0;
    if (opt.spotShadows && !spotList.empty()) {
        m_spotShadow.Generate(reg, spotList);
        glViewport(0, 0, screenWidth, screenHeight);
        numSpotShadows = m_spotShadow.Count();
    }
    m_clustered.Build(camera, aspect, screenWidth, screenHeight, clusterLights);

    // Lit pass.
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    m_pbr->Bind();
    m_pbr->SetMat4("uViewProj", proj * view);
    m_pbr->SetVec3("uViewPos", camera.Position());
    m_pbr->SetVec3("uSunDir", sunDir);
    m_pbr->SetVec3("uSunColor", sunColor);
    m_pbr->SetVec3("uAmbient", opt.ambient);
    m_pbr->SetInt("uApplyTonemap", opt.tonemap ? 1 : 0);
    m_clustered.BindLightUBO(0);
    m_clustered.BindTileBuffer(3);
    m_pbr->SetInt("uTileLights", 3);
    m_pbr->SetVec2("uScreenSize", glm::vec2(screenWidth, screenHeight));
    m_cascade.BindArray(4);
    m_pbr->SetInt("uCascadeMaps", 4);
    m_pbr->SetMat4("uView", view);
    m_pbr->SetFloat("uShadowSoftness", opt.shadowSoftness);
    for (int i = 0; i < CascadedShadow::kCascades; ++i) {
        const std::string ix = "[" + std::to_string(i) + "]";
        m_pbr->SetMat4("uCascadeVP" + ix, m_cascade.CascadeVP(i));
        m_pbr->SetFloat("uCascadeSplits" + ix, m_cascade.SplitDepth(i));
    }
    if (opt.ibl) {
        opt.ibl->Bind(5, 6, 7);
        m_pbr->SetInt("uUseIBL", 1);
        m_pbr->SetInt("uIrradiance", 5);
        m_pbr->SetInt("uPrefilter", 6);
        m_pbr->SetInt("uBrdfLUT", 7);
        m_pbr->SetFloat("uMaxReflectionLod", opt.ibl->MaxReflectionLod());
    } else {
        m_pbr->SetInt("uUseIBL", 0);
    }
    if (opt.ssao) {
        opt.ssao->BindAO(8);
        m_pbr->SetInt("uUseSSAO", 1);
        m_pbr->SetInt("uSsaoMap", 8);
        m_pbr->SetVec2("uScreenSize", glm::vec2(screenWidth, screenHeight));
    } else {
        m_pbr->SetInt("uUseSSAO", 0);
    }
    m_pointShadow.BindCubes(9);
    m_pbr->SetInt("uNumPointShadows", numPointShadows);
    m_pbr->SetInt("uPointCube[0]", 9);
    m_pbr->SetInt("uPointCube[1]", 10);
    m_pbr->SetInt("uPointCube[2]", 11);
    m_pbr->SetInt("uPointCube[3]", 12);
    m_spotShadow.BindMaps(13);
    m_pbr->SetInt("uNumSpots", static_cast<int>(spotPos.size()));
    m_pbr->SetInt("uNumSpotShadows", numSpotShadows);
    for (std::size_t i = 0; i < spotPos.size() && i < static_cast<std::size_t>(SpotShadow::kMax); ++i) {
        const std::string ix = "[" + std::to_string(i) + "]";
        m_pbr->SetVec3("uSpotPos" + ix, spotPos[i]);
        m_pbr->SetVec3("uSpotDir" + ix, spotDir[i]);
        m_pbr->SetVec3("uSpotColor" + ix, spotCol[i]);
        m_pbr->SetFloat("uSpotCosInner" + ix, spotCosIn[i]);
        m_pbr->SetFloat("uSpotCosOuter" + ix, spotCosOut[i]);
        m_pbr->SetMat4("uSpotVP" + ix, m_spotShadow.LightVP(static_cast<int>(i)));
    }
    m_pbr->SetInt("uSpotMap[0]", 13);
    m_pbr->SetInt("uSpotMap[1]", 14);
    m_pbr->SetInt("uSpotMap[2]", 15);
    m_pbr->SetInt("uSpotMap[3]", 16);
    m_pbr->SetInt("uNumAreas", static_cast<int>(areaPos.size()));
    for (std::size_t i = 0; i < areaPos.size() && i < 4; ++i) {
        const std::string ix = "[" + std::to_string(i) + "]";
        m_pbr->SetVec3("uAreaPos" + ix, areaPos[i]);
        m_pbr->SetVec3("uAreaColor" + ix, areaCol[i]);
        m_pbr->SetFloat("uAreaRadius" + ix, areaRad[i]);
    }
    m_pbr->SetInt("uFogEnabled", opt.fog ? 1 : 0);
    m_pbr->SetVec3("uFogColor", opt.fogColor);
    m_pbr->SetFloat("uFogDensity", opt.fogDensity);
    m_pbr->SetFloat("uFogHeight", opt.fogHeight);
    m_pbr->SetFloat("uFogHeightFalloff", opt.fogHeightFalloff);

    reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR& m) {
        if (!m.mesh) return;
        const glm::mat4 model = t.Model();
        m_pbr->SetMat4("uModel", model);
        m_pbr->SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(model))));
        m_pbr->SetVec3("uAlbedo", m.material.albedo);
        m_pbr->SetFloat("uMetallic", m.material.metallic);
        m_pbr->SetFloat("uRoughness", m.material.roughness);
        m_pbr->SetFloat("uAO", m.material.ao);
        m_pbr->SetVec3("uEmissive", m.material.emissive);
        auto bindMap = [&](const Texture* tex, int unit, const char* flag, const char* samp) {
            if (tex) { tex->Bind(static_cast<unsigned int>(unit)); m_pbr->SetInt(samp, unit); m_pbr->SetInt(flag, 1); }
            else m_pbr->SetInt(flag, 0);
        };
        bindMap(m.material.albedoMap,     0, "uHasAlbedoMap",     "uAlbedoMap");
        bindMap(m.material.normalMap,     1, "uHasNormalMap",     "uNormalMap");
        bindMap(m.material.metalRoughMap, 2, "uHasMetalRoughMap", "uMetalRoughMap");
        m.mesh->Draw();
    });
}

} // namespace engine
