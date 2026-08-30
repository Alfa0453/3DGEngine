#include <unordered_map>
#include "engine/graphics/PbrRenderer.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Camera.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/IBL.h"
#include "engine/graphics/SSAO.h"
#include "engine/graphics/ReflectionProbeSystem.h"
#include "engine/graphics/LtcLut.h"
#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/DirectionalShadowShader.h"
#include "engine/graphics/PbrLightingCommon.h"
#include "engine/graphics/LightingBuildData.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/Frustum.h"
#include "engine/graphics/ShaderParameterBinding.h"
#include "engine/graphics/GpuProfiler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <cmath>
#include <cstdlib>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::Light;
using engine::ecs::PbrMaterial;

namespace engine {
namespace {

void UploadCustomParameters(Shader& shader, const MeshPBR& mesh) {
    if (mesh.shaderParameters.empty() && mesh.shaderTextures.empty()) return;
    int textureUnit = 18;
    for (const auto& entry : mesh.shaderParameters) {
        const auto type = mesh.shaderParameterTypes.find(entry.first);
        const int valueType = type == mesh.shaderParameterTypes.end() ? 0 : type->second;
        const auto texture = mesh.shaderTextures.find(entry.first);
        textureUnit = UploadShaderParameter(
            shader, entry.first, valueType, entry.second,
            texture == mesh.shaderTextures.end() ? nullptr : texture->second,
            textureUnit);
    }
}

int SelectMeshLod(const Mesh& mesh, const Transform& transform,
                  const Camera& camera) {
    if (mesh.MaxLod() <= 0) return 0;
    // Cap exceptionally dense terrain even when the camera is standing on it.
    // Distance then adds normal geometric LOD without any per-frame uploads.
    int level = mesh.LodForTriangleBudget(200000u);
    const glm::mat4 model = transform.Model();
    const glm::vec3 center = glm::vec3(
        model * glm::vec4(mesh.BoundsCenter(), 1.0f));
    const glm::vec3 scale = glm::abs(transform.scale);
    const float radius = std::max(mesh.BoundsRadius()
        * std::max({scale.x, scale.y, scale.z}), 0.01f);
    const float centerDistanceSq = glm::dot(camera.Position() - center,
                                            camera.Position() - center);
    if (centerDistanceSq > (2.0f * radius) * (2.0f * radius)) level = std::max(level, 1);
    if (centerDistanceSq > (4.0f * radius) * (4.0f * radius)) level = std::max(level, 2);
    if (centerDistanceSq > (7.0f * radius) * (7.0f * radius)) level = std::max(level, 3);
    if (centerDistanceSq > (13.0f * radius) * (13.0f * radius)) level = std::max(level, 4);
    return std::min(level, mesh.MaxLod());
}

const char* kPbrVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aIModel0;   // per-instance model matrix (columns)
layout (location = 4) in vec4 aIModel1;
layout (location = 5) in vec4 aIModel2;
layout (location = 6) in vec4 aIModel3;
layout (location = 7) in vec3 aIAlbedo;   // per-instance material
layout (location = 8) in vec3 aIMRA;      // metallic, roughness, ao
layout (location = 9) in vec3 aIEmissive;
uniform int  uInstanced;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
flat out vec3 vIAlbedo;
flat out vec3 vIMRA;
flat out vec3 vIEmissive;
void main() {
    mat4 model; mat3 nrm;
    if (uInstanced == 1) {
        model = mat4(aIModel0, aIModel1, aIModel2, aIModel3);
        nrm   = transpose(inverse(mat3(model)));
        vIAlbedo = aIAlbedo; vIMRA = aIMRA; vIEmissive = aIEmissive;
    } else {
        model = uModel; nrm = uNormalMat;
        vIAlbedo = vec3(0.0); vIMRA = vec3(0.0); vIEmissive = vec3(0.0);
    }
    vec4 world  = model * vec4(aPos, 1.0);
    vWorldPos   = world.xyz;
    vNormal     = normalize(nrm * aNormal);
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
flat in vec3 vIAlbedo;
flat in vec3 vIMRA;
flat in vec3 vIEmissive;
uniform int  uInstanced;
uniform int  uMaterialSlotDebug;
uniform vec3  uViewPos;
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3  uEmissive;
uniform int uBlendMode;
uniform float uOpacity;
uniform float uAlphaCutoff;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform float uUvRotation;
uniform int  uWorldUv;   // 1 = project UVs from world position (scale-independent)
uniform float uNormalStrength;
uniform float uHeightScale;
uniform float uClearcoat;
uniform float uClearcoatRoughness;
uniform float uTransmission;
uniform float uIor;
uniform float uThickness;
uniform float uAnisotropy;
uniform float uAnisotropyRotation;
uniform vec3 uSheenColor;
uniform float uSheenRoughness;
uniform float uSpecularLevel;
uniform float uSubsurface;
uniform vec3 uSubsurfaceColor;
uniform int uHasAlbedoMap;
uniform int uHasNormalMap;
uniform int uHasMetalRoughMap;
uniform int uHasHeightMap;
uniform int uHasEmissiveMap;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMetalRoughMap;
uniform sampler2D uHeightMap;
uniform sampler2D uEmissiveMap;
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
uniform float uSpotRange[MAX_SPOTS];
uniform mat4  uSpotVP[MAX_SPOTS];
uniform sampler2D uSpotMap[MAX_SPOTS];
const int MAX_AREAS = 4;
uniform int   uNumAreas;
uniform vec3  uAreaPos[MAX_AREAS];
uniform vec3  uAreaColor[MAX_AREAS];
uniform float uAreaRadius[MAX_AREAS];
uniform vec3 uAreaRight[MAX_AREAS],uAreaUp[MAX_AREAS];
uniform vec2 uAreaHalfSize[MAX_AREAS];
uniform int uAreaShape[MAX_AREAS],uAreaTwoSided[MAX_AREAS];
uniform sampler2DArray uCascadeMaps;
uniform mat4  uCascadeVP[4];
uniform float uCascadeSplits[4];
uniform float uCascadeWorldTexelSize[4];
uniform float uCascadeDepthRange[4];
uniform mat4  uView;
uniform float uShadowSoftness;
uniform int uShadowBlockerSamples;
uniform int uShadowFilterSamples;
uniform int   uSunShadow;   // 0 disables the directional (sun) shadow
uniform samplerCube uPointCube[4];
uniform int uNumPointShadows;
uniform int uNumPointLights;
uniform int uApplyTonemap;
uniform int uUseIBL;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D uBrdfLUT;
uniform float uMaxReflectionLod;
uniform float uGlobalIblIntensity;
uniform float uGlobalReflectionIntensity;
uniform int   uUseSSAO;
uniform sampler2D uSsaoMap;
uniform sampler2D uRawGtaoMap;
uniform sampler2D uBentNormalMap;
uniform int uUseBentNormal;
uniform mat4 uViewToWorld;
uniform vec2  uScreenSize;
uniform int   uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
uniform float uFogHeightFalloff;
uniform int   uSkylightOcclusion;
uniform float uSkylightOcclusionStrength;
uniform float uMinimumSkylight;
uniform int uUseLightingGrid;
uniform vec3 uLightingGridMin;
uniform vec3 uLightingGridMax;
//__PBR_LIGHTING_COMMON__
uniform int   uCloudShadows;
uniform float uCloudShadowStrength;
uniform float uCloudShadowScale;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudSoftness;
uniform vec2  uCloudWindOffset;

float LocalSkyVisibility(vec3 worldPos, vec3 normal, float sunVisibility) {
    float hemisphere = mix(0.72, 1.0, clamp(normal.y * 0.5 + 0.5, 0.0, 1.0));
    if (uUseLightingGrid == 1) {
        vec3 extent = max(uLightingGridMax - uLightingGridMin, vec3(0.0001));
        vec3 uvw = (worldPos - uLightingGridMin) / extent;
        if (all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0)))) {
            LocalProbeSample probe = SampleLocalProbe(worldPos, normal);
            if (probe.validity > 0.001)
                return clamp(probe.skyVisibility * hemisphere, 0.0, 1.0);
        }
    }
    // No build data: use the directional visibility as a conservative clue.
    // A fully shadowed sealed room can therefore reach zero instead of being
    // forced to at least 82 percent outdoor skylight.
    return clamp(0.25 * sunVisibility * hemisphere, 0.0, 1.0);
}

float CloudHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
float CloudNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(CloudHash(i), CloudHash(i + vec2(1.0, 0.0)), f.x),
               mix(CloudHash(i + vec2(0.0, 1.0)), CloudHash(i + vec2(1.0)), f.x), f.y);
}
float CloudFbm(vec2 p) {
    float value = 0.0, weight = 0.52;
    mat2 octave = mat2(1.7, 1.2, -1.2, 1.7);
    for (int i = 0; i < 5; ++i) {
        value += CloudNoise(p) * weight;
        p = octave * p + vec2(13.7, 9.2);
        weight *= 0.5;
    }
    return value;
}
float CloudSunlight(vec3 worldPos) {
    if (uCloudShadows == 0) return 1.0;
    vec2 p = worldPos.xz * max(uCloudShadowScale, 0.0001) + uCloudWindOffset;
    float shape = mix(CloudFbm(p), CloudFbm(p * 2.7 + vec2(4.2, -3.1)), 0.28);
    float edge = max(uCloudSoftness, 0.005);
    float cover = smoothstep(uCloudCoverage - edge, uCloudCoverage + edge, shape);
    float opacity = clamp(cover * uCloudDensity * uCloudShadowStrength, 0.0, 0.95);
    return 1.0 - opacity;
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
//__DIRECTIONAL_SHADOW_IMPLEMENTATION__
// Filmic ACES tone map (Narkowicz fit) -- punchier + more saturated than Reinhard.
vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
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
vec2 TransformUV(vec2 uv) {
    float a = radians(uUvRotation); float c = cos(a), s = sin(a);
    vec2 p = uv * uUvScale - vec2(0.5);
    return mat2(c, -s, s, c) * p + vec2(0.5) + uUvOffset;
}
// Project a world position onto the plane facing the surface normal, so the texture
// tiles by world size instead of the mesh's authored 0..1 UVs. uUvScale is reused as
// tiles-per-world-unit. Cheap single-tap "triplanar": pick the dominant axis.
vec2 WorldUV(vec3 worldPos, vec3 n) {
    vec3 an = abs(n);
    vec2 wuv = (an.x >= an.y && an.x >= an.z) ? worldPos.zy
             : (an.y >= an.z)                 ? worldPos.xz
                                              : worldPos.xy;
    return wuv * uUvScale + uUvOffset;
}
vec2 ParallaxUV(vec2 uv, vec3 viewTS) {
    if (uHasHeightMap == 0 || uHeightScale <= 0.00001) return uv;
    const float layers = 16.0;
    float layerDepth = 1.0 / layers, depth = 0.0;
    vec2 delta = (viewTS.xy / max(abs(viewTS.z), 0.08)) * uHeightScale / layers;
    vec2 current = uv;
    for (int i = 0; i < 16; ++i) {
        if (depth >= 1.0 - texture(uHeightMap, current).r) break;
        current -= delta; depth += layerDepth;
    }
    return current;
}
vec3 ClearcoatSpecular(vec3 N, vec3 V, vec3 L, vec3 radiance) {
    vec3 H = normalize(V + L);
    float r = max(uClearcoatRoughness, 0.02);
    float D = DistributionGGX(N, H, r);
    float G = GeometrySmith(N, V, L, r);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), vec3(0.04));
    return uClearcoat * D * G * F * radiance * max(dot(N, L), 0.0) /
           (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
}
void main() {
    vec3 baseN = normalize(vNormal);
    vec3 V = normalize(uViewPos-vWorldPos);
    vec2 uv = (uWorldUv == 1) ? WorldUV(vWorldPos, baseN) : TransformUV(vUV);
    mat3 baseTbn = CotangentFrame(baseN, vWorldPos, uv);
    uv = ParallaxUV(uv, transpose(baseTbn) * V);
    vec3 albedo = (uInstanced == 1) ? vIAlbedo : uAlbedo;
    vec4 albedoSample = (uHasAlbedoMap == 1) ? texture(uAlbedoMap, uv) : vec4(1.0);
    albedoSample.rgb = pow(albedoSample.rgb, vec3(2.2));   // sRGB texture -> linear
    albedo *= albedoSample.rgb;
    vec3 emissive = (uInstanced == 1) ? vIEmissive : uEmissive;
    if (uHasEmissiveMap == 1)
        emissive *= pow(texture(uEmissiveMap, uv).rgb, vec3(2.2));
    float opacity = (uInstanced == 1) ? 1.0 : uOpacity * albedoSample.a;
    if (uBlendMode == 1 && opacity < uAlphaCutoff) discard;
    float metallic = (uInstanced == 1) ? vIMRA.x : uMetallic;
    float roughness = (uInstanced == 1) ? vIMRA.y : uRoughness;
    float ao = (uInstanced == 1) ? vIMRA.z : uAO;
    if (uHasMetalRoughMap == 1) {
        vec3 mr = texture(uMetalRoughMap, uv).rgb;
        ao         *= mr.r;
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    vec3 N = baseN;
    if (uHasNormalMap == 1) {
        vec3 tn = texture(uNormalMap, uv).rgb * 2.0 - 1.0;
        tn.xy *= uNormalStrength;
        N = normalize(CotangentFrame(baseN, vWorldPos, uv) * normalize(tn));
    }
    if (uInstanced == 0 && abs(uAnisotropy) > 0.001) {
        float direction = cos(radians(uAnisotropyRotation));
        float alignment = abs(dot(normalize(baseTbn[0] * direction + baseTbn[1] * sqrt(max(0.0, 1.0-direction*direction))), V));
        roughness = clamp(roughness * (1.0 - 0.45 * uAnisotropy * alignment), 0.02, 1.0);
    }
    vec3 F0 = mix(vec3(0.08 * uSpecularLevel), albedo, metallic);
    vec3 Lo = vec3(0.0);
    vec3 Ls = normalize(-uSunDir);
    float sunNdotL = max(dot(N,Ls),0.0);
    float shadow = (uSunShadow == 1) ? ShadowFactor(sunNdotL, N) : 0.0;
    float sunVisibility = (1.0 - shadow) * CloudSunlight(vWorldPos);
    Lo += sunVisibility*Lighting(N,V,Ls,uSunColor,albedo,F0,metallic,roughness);
    if (uInstanced == 0) Lo += sunVisibility*ClearcoatSpecular(N,V,Ls,uSunColor);
    ivec2 tile = ivec2(gl_FragCoord.xy / (uScreenSize / vec2(TILE_COUNT)));
    tile = clamp(tile, ivec2(0), TILE_COUNT - ivec2(1));
    int tbase = (tile.y * TILE_COUNT.x + tile.x) * TILE_STRIDE;
    int nLights = clamp(texelFetch(uTileLights, tbase).r, 0,
                        min(uNumPointLights, TILE_STRIDE - 1));
    for (int k = 0; k < nLights && k < TILE_STRIDE - 1; ++k) {
        int li = texelFetch(uTileLights, tbase + 1 + k).r;
        if (li < 0 || li >= uNumPointLights) continue;
        vec3 lp = uLightPosRadius[li].xyz;
        float lr = uLightPosRadius[li].w;
        vec3 d = lp - vWorldPos;
        float dist2 = dot(d, d);
        if (dist2 > lr * lr) continue;
        vec3 L = d * inversesqrt(max(dist2, 0.0001));
        vec3 radiance = uLightColor[li].rgb * SmoothFiniteAttenuation(dist2, lr);
        vec3 contrib = Lighting(N, V, L, radiance, albedo, F0, metallic, roughness);
        if (li < uNumPointShadows) contrib *= (1.0 - PointShadowFactor(li, -d));
        Lo += contrib;
        if (uInstanced == 0) Lo += ClearcoatSpecular(N, V, L, radiance);
    }
    for (int i = 0; i < min(uNumSpots, MAX_SPOTS); ++i) {
        vec3 d = uSpotPos[i] - vWorldPos;
        float dist2 = dot(d, d);
        vec3 L = d * inversesqrt(max(dist2, 0.0001));
        float cone = smoothstep(uSpotCosOuter[i], uSpotCosInner[i],
                                dot(normalize(-uSpotDir[i]), L));
        vec3 radiance = uSpotColor[i] * SmoothFiniteAttenuation(dist2, uSpotRange[i]) * cone;
        vec3 contribution = Lighting(N, V, L, radiance, albedo, F0, metallic, roughness);
        if (i < uNumSpotShadows) contribution *= (1.0 - SpotShadowFactor(i, vWorldPos));
        Lo += contribution;
    }
    for (int i = 0; i < min(uNumAreas, MAX_AREAS); ++i){
        if(uAreaShape[i]==1)Lo+=LtcRectangleLight(N,V,vWorldPos,uAreaPos[i],uAreaRight[i],uAreaUp[i],uAreaHalfSize[i],uAreaTwoSided[i],uAreaColor[i],albedo,F0,metallic,roughness);
        else Lo += AreaLight(N, V, uAreaPos[i] - vWorldPos, uAreaRadius[i], uAreaColor[i],albedo, F0, metallic, roughness);}
    vec3 ambient;
    vec2 ambientUv = gl_FragCoord.xy / uScreenSize;
    vec3 indirectN = N;
    if (uUseBentNormal == 1) {
        vec3 bentView = normalize(texture(uBentNormalMap, ambientUv).xyz);
        vec3 bentWorld = normalize(mat3(uViewToWorld) * bentView);
        if (dot(bentWorld, bentWorld) > 0.5) indirectN = normalize(mix(N, bentWorld, 0.75));
    }
    LocalProbeSample localProbe = SampleLocalProbe(vWorldPos, indirectN);
    float fallbackSkyVisibility = LocalSkyVisibility(vWorldPos, indirectN, 1.0-shadow);
    float resolvedSkyVisibility = mix(fallbackSkyVisibility, localProbe.skyVisibility,
                                      step(0.001, localProbe.validity));
    float skyVisibility = max(resolvedSkyVisibility, clamp(uMinimumSkylight, 0.0, 1.0));
    float screenAo = (uUseSSAO == 1) ? texture(uSsaoMap, gl_FragCoord.xy / uScreenSize).r : 1.0;
    vec3 diffuseIndirect = vec3(0.0);
    vec3 specularIndirect = vec3(0.0);
    float specularOcclusion = 1.0;
    if (uUseIBL == 1) {
        vec3 F = FresnelSchlickRough(max(dot(N,V),0.0), F0, roughness);
        vec3 kD = (vec3(1.0)-F)*(1.0-metallic);
        vec3 globalIrradiance = texture(uIrradiance, indirectN).rgb * uGlobalIblIntensity;
        // Global sky reaches a surface only where the sky is actually visible. The local
        // probe SH already integrates bounced light AND sky-through-openings (enclosure is
        // baked in by the probe's miss-ray environment sampling), so probe validity is
        // treated as CONFIDENCE in the probe, never as an indoor/outdoor factor. Where the
        // probe is confident we use it directly; elsewhere we fall back to the
        // sky-visibility-gated global term. The sky is thus never counted twice, and
        // shadowed enclosed surfaces stop being dominated by the blue outdoor environment.
        float skyGate = (uSkylightOcclusion == 1)
            ? mix(1.0, skyVisibility, clamp(uSkylightOcclusionStrength, 0.0, 1.0)) : 1.0;
        vec3 skyGatedGlobal = globalIrradiance * skyGate;
        float probeConfidence = localProbe.validity * clamp(uLocalProbeInfluence, 0.0, 1.0);
        vec3 irradiance = mix(skyGatedGlobal, localProbe.irradiance, probeConfidence);
        vec3 diffuse = irradiance * albedo;
        vec3 R = reflect(-V, N);
        vec3 globalPrefiltered = textureLod(uPrefilter, R, roughness*uMaxReflectionLod).rgb
            * uGlobalIblIntensity * uGlobalReflectionIntensity;
        float reflectionProbeWeight=0.0;
        vec3 prefiltered = SampleReflectionEnvironment(vWorldPos,R,roughness,globalPrefiltered,reflectionProbeWeight);
        vec2 brdf = texture(uBrdfLUT, vec2(max(dot(N,V),0.0), roughness)).rg;
        vec3 specular = prefiltered * (F*brdf.x + brdf.y);
        specularOcclusion = PbrSpecularOcclusion(ao * screenAo, max(dot(N,V),0.0), roughness, skyVisibility);
        // Global sky reflections are visibility-limited; valid local probes
        // remain usable as the indoor reflection source.
        specular *= mix(specularOcclusion, 1.0, reflectionProbeWeight);
        // Sky visibility is already applied to the global term (skyGatedGlobal) above and
        // the probe bakes in enclosure, so there is no second occlusion multiply here —
        // that previously double-darkened valid probes and left the blue global term as the
        // dominant fill in shadow.
        vec3 diffuseAmbient = kD * diffuse * ao * screenAo;
        diffuseIndirect = diffuseAmbient;
        specularIndirect = specular;
        ambient = diffuseIndirect + specularIndirect;
        if (uInstanced == 0 && uTransmission > 0.0) {
            vec3 refracted = refract(-V, N, 1.0 / max(uIor, 1.0));
            vec3 transmitted = textureLod(uPrefilter, refracted, roughness*uMaxReflectionLod).rgb;
            transmitted *= specularOcclusion;
            transmitted *= exp(-uThickness * max(vec3(0.02), vec3(1.0) - uSubsurfaceColor));
            ambient = mix(ambient, transmitted, uTransmission);
        }
        if (uInstanced == 0 && uClearcoat > 0.0) {
            vec3 Rc = reflect(-V, N);
            ambient += uClearcoat * textureLod(uPrefilter, Rc, uClearcoatRoughness*uMaxReflectionLod).rgb
                     * 0.04 * specularOcclusion;
        }
    } else {
        ambient = uAmbient*albedo*ao*screenAo;
        diffuseIndirect = ambient;
    }
    if (uSkylightOcclusion == 1 && uUseIBL == 0) {
        ambient *= mix(1.0, skyVisibility, clamp(uSkylightOcclusionStrength, 0.0, 1.0));
    }
    vec3 color = ambient + Lo + emissive;
    if (uInstanced == 0) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), mix(5.0, 2.0, uSheenRoughness));
        color += uSheenColor * rim;
        float back = pow(max(dot(V, -Ls), 0.0), 2.0) * (1.0 - max(dot(N, Ls), 0.0));
        color += uSubsurface * uSubsurfaceColor * albedo * (0.15 + back) * uSunColor;
    }

    if (uLightingDebugMode == 1) color = Lo;
    else if (uLightingDebugMode == 2) color = diffuseIndirect;
    else if (uLightingDebugMode == 3) color = specularIndirect;
    else if (uLightingDebugMode == 4) color = localProbe.irradiance;
    else if (uLightingDebugMode == 5) color = vec3(skyVisibility);
    else if (uLightingDebugMode == 6) color = vec3(ao * screenAo);
    else if (uLightingDebugMode == 7) color = vec3(specularOcclusion);
    else if (uLightingDebugMode == 8) color = vec3(localProbe.validity);
    else if (uLightingDebugMode == 9) color = indirectN * 0.5 + 0.5;
    else if (uLightingDebugMode == 10) color = vec3(texture(uRawGtaoMap, ambientUv).r);
    else if (uLightingDebugMode == 11) color = vec3(screenAo);
    else if (uLightingDebugMode == 12) {
        float reflectionWeight = 0.0;
        if (uReflectionProbeCount > 0) reflectionWeight += ReflectionProbeWeight(0, vWorldPos);
        if (uReflectionProbeCount > 1) reflectionWeight += ReflectionProbeWeight(1, vWorldPos);
        color = vec3(clamp(reflectionWeight, 0.0, 1.0));
    }
    else if ((uLightingDebugMode >= 13 && uLightingDebugMode <= 15) || uLightingDebugMode == 23)
        color = localProbe.irradiance;
    else if (uLightingDebugMode == 16) color = vec3(localProbe.visibility);
    else if (uLightingDebugMode == 17) color = localProbe.irradiance;
    else if (uLightingDebugMode == 18) color = vec3(0.0);
    else if (uLightingDebugMode == 19) color = diffuseIndirect + specularIndirect;
    else if (uLightingDebugMode == 20) color = vec3(1.0 - shadow);
    else if (uLightingDebugMode == 21) color = DirectionalCascadeDebugColor(vWorldPos);
    else if (uLightingDebugMode == 22) {
        vec3 debugF = FresnelSchlickRough(max(dot(N,V),0.0), F0, roughness);
        vec3 debugKd = (vec3(1.0)-debugF)*(1.0-metallic);
        vec3 globalDiffuse = debugKd * texture(uIrradiance, indirectN).rgb * albedo * ao * screenAo;
        if (uSkylightOcclusion == 1)
            globalDiffuse *= mix(1.0, skyVisibility, clamp(uSkylightOcclusionStrength,0.0,1.0));
        vec3 debugR = reflect(-V,N);
        vec2 debugBrdf = texture(uBrdfLUT,vec2(max(dot(N,V),0.0),roughness)).rg;
        vec3 globalSpecular = textureLod(uPrefilter,debugR,roughness*uMaxReflectionLod).rgb
                            * (debugF*debugBrdf.x+debugBrdf.y) * specularOcclusion;
        color = globalDiffuse + globalSpecular;
    }
    else if (uLightingDebugMode == 24) color = albedo;
    else if (uLightingDebugMode == 25) color = normalize(baseN) * 0.5 + 0.5;
    else if (uLightingDebugMode == 26) color = normalize(N) * 0.5 + 0.5;
    else if (uLightingDebugMode == 27) {
        float h = fract(float(max(uMaterialSlotDebug, 0)) * 0.61803398875);
        color = 0.55 + 0.45 * cos(6.2831853 * (h + vec3(0.0, 0.333, 0.667)));
    }
    else if (uLightingDebugMode == 28) {
        // PCSS filter-radius heat map: blue = contact (near-zero penumbra),
        // green = mid, red = wide penumbra. Verifies contact shadows stay tight.
        float fr = (uSunShadow == 1) ? DirectionalFilterRadiusDebug(sunNdotL, N) : 0.0;
        color = vec3(clamp(fr * 2.0, 0.0, 1.0),
                     clamp(1.0 - abs(fr - 0.5) * 2.0, 0.0, 1.0),
                     clamp(1.0 - fr * 2.0, 0.0, 1.0));
    }

    if (uFogEnabled == 1 && uLightingDebugMode == 0) {
        float dist = length(uViewPos - vWorldPos);
        float distFog = 1.0 - exp(-dist * uFogDensity);
        float heightF = clamp(exp(-(vWorldPos.y - uFogHeight) * uFogHeightFalloff), 0.0, 1.0);
        float fog = clamp(distFog * heightF, 0.0, 1.0);
        color = mix(color, uFogColor, fog);
    }

    if (uApplyTonemap == 1 && uLightingDebugMode == 24) {
        color = pow(max(color, vec3(0.0)), vec3(1.0/2.2));
    } else if (uApplyTonemap == 1 && uLightingDebugMode != 20
               && uLightingDebugMode != 21
               && (uLightingDebugMode < 24 || uLightingDebugMode > 27)) {
        color = ACES(color);                     // filmic tone map (was Reinhard)
        color = pow(color, vec3(1.0/2.2));       // linear -> sRGB
    }
    float outputAlpha = opacity * (1.0 - 0.85 * uTransmission);
    FragColor = vec4(color, (uBlendMode == 2) ? outputAlpha : 1.0);
}
)GLSL";

} // namespace

PbrRenderer::PbrRenderer(int shadowSize)
    : m_cascade(shadowSize),
      m_pointShadow(512),
      m_spotShadow(1024),
      m_pbr(std::make_unique<Shader>(kPbrVert, ComposeDirectionalShadowShader(ComposePbrLightingShader(kPbrFrag).c_str()))),
      m_ltcLut(std::make_unique<LtcLut>()) {
    const unsigned int blk = glGetUniformBlockIndex(m_pbr->ID(), "LightBlock");
    if (blk != GL_INVALID_INDEX) glUniformBlockBinding(m_pbr->ID(), blk, 0);
    glGenBuffers(1, &m_instanceVBO);
}

PbrRenderer::~PbrRenderer() {
    if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);
}

void PbrRenderer::Render(ecs::Registry& reg, const Camera& camera, float aspect,
                         int screenWidth, int screenHeight) {
    Render(reg, camera, aspect, screenWidth, screenHeight, Options());
}

void PbrRenderer::Render(ecs::Registry& reg, const Camera& camera, float aspect,
                         int screenWidth, int screenHeight, const Options& opt) {
    if (opt.shadowFilterSamples <= 6) m_cascade.SetUpdateIntervals({1,4,8,16});
    else if (opt.shadowFilterSamples <= 12) m_cascade.SetUpdateIntervals({1,3,6,12});
    else if (opt.shadowFilterSamples <= 18) m_cascade.SetUpdateIntervals({1,2,4,8});
    else m_cascade.SetUpdateIntervals({1,1,2,4});
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    const glm::mat4 viewProj = proj * view;
    const Frustum frustum = ExtractFrustum(viewProj);

    // Gather lights.
    glm::vec3 sunDir(0.0f, -1.0f, 0.0f), sunColor(0.0f);
    auto& ppos = m_pointPositions;
    auto& clusterLights = m_clusterLights;
    auto& spotPos = m_spotPositions;
    auto& spotDir = m_spotDirections;
    auto& spotCol = m_spotColors;
    auto& spotCosIn = m_spotCosInner;
    auto& spotCosOut = m_spotCosOuter;
    auto& spotRanges = m_spotRanges;
    auto& areaPos = m_areaPositions;
    auto& areaCol = m_areaColors;
    auto& areaRad = m_areaRadii;
    auto& areaRight=m_areaRights;auto& areaUp=m_areaUps;auto& areaHalf=m_areaHalfSizes;auto& areaShape=m_areaShapes;auto& areaTwoSided=m_areaTwoSided;
    auto& spotList = m_spotShadowLights;
    ppos.clear(); clusterLights.clear(); spotPos.clear(); spotDir.clear(); spotCol.clear();
    spotCosIn.clear(); spotCosOut.clear(); spotRanges.clear(); areaPos.clear(); areaCol.clear(); areaRad.clear();areaRight.clear();areaUp.clear();areaHalf.clear();areaShape.clear();areaTwoSided.clear(); spotList.clear();
    auto lightView = reg.view<Transform, Light>();
    if (!lightView.empty()) lightView.each([&](Entity, Transform& t, Light& l) {
        const glm::vec3 c = l.color * l.intensity;
        if (l.type == Light::Type::Directional) { sunDir = glm::normalize(l.direction); sunColor = c; }
        else if (l.type == Light::Type::Point) {
            if (glm::dot(c, c) <= 1.0e-8f) return;
            const float radius = std::sqrt(std::max(std::max(c.r, c.g), c.b) / 0.03f);
            if (!SphereInFrustum(frustum, t.position, std::max(radius, 0.01f))) return;
            clusterLights.push_back({t.position, c, radius});
            if (ppos.size() < static_cast<std::size_t>(std::clamp(opt.maxShadowedLocalLights,0,PointShadow::kMax)))
                ppos.push_back(t.position);
        }
        else if (l.type == Light::Type::Spot) {
            if (glm::dot(c, c) <= 1.0e-8f) return;
            const glm::vec3 dir = glm::normalize(l.direction);
            const float range = std::max(l.range, 0.01f);
            if (!SphereInFrustum(frustum, t.position + dir * (range * 0.5f), range * 0.5f)) return;
            spotPos.push_back(t.position);
            spotDir.push_back(dir);
            spotCol.push_back(c);
            spotCosIn.push_back(glm::cos(glm::radians(l.innerAngle)));
            spotCosOut.push_back(glm::cos(glm::radians(l.outerAngle)));
            spotRanges.push_back(range);
            if (spotList.size() < static_cast<std::size_t>(std::clamp(opt.maxShadowedLocalLights,0,SpotShadow::kMax)))
                spotList.push_back(SpotShadow::Spot{t.position, dir, l.outerAngle, range});
        }
        else {  // Area (sphere)
            if (glm::dot(c, c) <= 1.0e-8f) return;
            if (!SphereInFrustum(frustum, t.position, std::max(l.sourceRadius, 0.01f))) return;
            areaPos.push_back(t.position);
            areaCol.push_back(c);
            areaRad.push_back(l.sourceRadius);
            areaRight.push_back(glm::normalize(t.rotation*glm::vec3(1,0,0)));areaUp.push_back(glm::normalize(t.rotation*glm::vec3(0,1,0)));
            areaHalf.push_back(glm::vec2(std::max(l.areaWidth,0.01f),std::max(l.areaHeight,0.01f))*0.5f);
            areaShape.push_back(l.areaShape==Light::AreaShape::Rectangle?1:0);areaTwoSided.push_back(l.areaTwoSided?1:0);
        }
    });

    // Cascaded directional (sun) shadow pass (skipped when the sun shadow is off).
    const bool sunShadowEnabled = opt.directionalShadows
        && glm::dot(sunColor, sunColor) > 1.0e-8f;
    if (sunShadowEnabled) {
        const float shadowFar = std::max(opt.shadowDistance, camera.nearPlane + 1.0f);
        m_cascade.SetForceUpdateEveryFrame(opt.forceDirectionalShadowUpdate);
        m_cascade.Generate(reg, camera, aspect, sunDir, shadowFar, opt.shadowCasters);
        glViewport(0, 0, screenWidth, screenHeight);
    }

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
    m_pbr->Bind();
    m_pbr->SetMat4("uViewProj", viewProj);
    m_pbr->SetVec3("uViewPos", camera.Position());
    m_pbr->SetVec3("uSunDir", sunDir);
    m_pbr->SetVec3("uSunColor", sunColor);
    m_pbr->SetVec3("uAmbient", opt.ambient);
    m_pbr->SetInt("uApplyTonemap", opt.tonemap ? 1 : 0);
    // Stable sampler layout. Optional samplers must also receive unique units:
    // their uniforms remain active in the linked program even when the feature
    // flag is off, and mixed sampler types may not alias the default unit 0.
    m_pbr->SetInt("uLightingSH0", 18);
    m_pbr->SetInt("uLightingSH1", 19);
    m_pbr->SetInt("uLightingSH2", 20);
    m_pbr->SetInt("uLightingMeta", 21);
    m_pbr->SetInt("uLocalReflection0", 22);
    m_pbr->SetInt("uLocalReflection1", 23);
    m_pbr->SetInt("uLtcMatrixLut", 24);
    m_pbr->SetInt("uLtcAmplitudeLut", 25);
    m_pbr->SetInt("uBentNormalMap", 26);
    m_pbr->SetInt("uRawGtaoMap", 27);
    m_clustered.BindLightUBO(0);
    m_clustered.BindTileBuffer(3);
    m_pbr->SetInt("uTileLights", 3);
    m_pbr->SetVec2("uScreenSize", glm::vec2(screenWidth, screenHeight));
    m_cascade.BindArray(4);
    m_pbr->SetInt("uCascadeMaps", 4);
    m_pbr->SetMat4("uView", view);
    m_pbr->SetFloat("uShadowSoftness", opt.shadowSoftness);
    m_pbr->SetInt("uShadowBlockerSamples", std::clamp(opt.shadowBlockerSamples, 4, 16));
    m_pbr->SetInt("uShadowFilterSamples", std::clamp(opt.shadowFilterSamples, 6, 32));
    m_pbr->SetInt("uSunShadow", sunShadowEnabled ? 1 : 0);
    static constexpr const char* kCascadeVpNames[] = {"uCascadeVP[0]", "uCascadeVP[1]", "uCascadeVP[2]", "uCascadeVP[3]"};
    static constexpr const char* kCascadeSplitNames[] = {"uCascadeSplits[0]", "uCascadeSplits[1]", "uCascadeSplits[2]", "uCascadeSplits[3]"};
    static constexpr const char* kCascadeTexelNames[] = {"uCascadeWorldTexelSize[0]", "uCascadeWorldTexelSize[1]", "uCascadeWorldTexelSize[2]", "uCascadeWorldTexelSize[3]"};
    static constexpr const char* kCascadeDepthRangeNames[] = {"uCascadeDepthRange[0]", "uCascadeDepthRange[1]", "uCascadeDepthRange[2]", "uCascadeDepthRange[3]"};
    for (int i = 0; i < CascadedShadow::kCascades; ++i) {
        m_pbr->SetMat4(kCascadeVpNames[i], m_cascade.CascadeVP(i));
        m_pbr->SetFloat(kCascadeSplitNames[i], m_cascade.SplitDepth(i));
        m_pbr->SetFloat(kCascadeTexelNames[i], m_cascade.WorldTexelSize(i));
        m_pbr->SetFloat(kCascadeDepthRangeNames[i], m_cascade.DepthRange(i));
    }
    if (opt.ibl) {
        opt.ibl->Bind(5, 6, 7);
        m_pbr->SetInt("uUseIBL", 1);
        m_pbr->SetInt("uIrradiance", 5);
        m_pbr->SetInt("uPrefilter", 6);
        m_pbr->SetInt("uBrdfLUT", 7);
        m_pbr->SetFloat("uMaxReflectionLod", opt.ibl->MaxReflectionLod());
        m_pbr->SetFloat("uGlobalIblIntensity", std::max(opt.globalIblIntensity, 0.0f));
        m_pbr->SetFloat("uGlobalReflectionIntensity", std::max(opt.globalReflectionIntensity, 0.0f));
    } else {
        m_pbr->SetInt("uUseIBL", 0);
    }
    if(opt.reflectionProbes){opt.reflectionProbes->Sync(reg,camera.Position());opt.reflectionProbes->BindBest(*m_pbr,camera.Position(),22);}
    else m_pbr->SetInt("uReflectionProbeCount",0);
    if (opt.ssao) {
        opt.ssao->BindAO(8);
        GLint textureUnits=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&textureUnits);
        if(textureUnits>26)opt.ssao->BindBentNormal(26);
        if(textureUnits>27)opt.ssao->BindRawAO(27);
        m_pbr->SetInt("uUseSSAO", 1);
        m_pbr->SetInt("uSsaoMap", 8);
        m_pbr->SetInt("uUseBentNormal", textureUnits>26?1:0);
        m_pbr->SetMat4("uViewToWorld", glm::inverse(camera.ViewMatrix()));
        m_pbr->SetVec2("uScreenSize", glm::vec2(screenWidth, screenHeight));
    } else {
        m_pbr->SetInt("uUseSSAO", 0);
        m_pbr->SetInt("uUseBentNormal", 0);
    }
    m_pointShadow.BindCubes(9);
    m_pbr->SetInt("uNumPointShadows", numPointShadows);
    m_pbr->SetInt("uNumPointLights", std::min(
        static_cast<int>(clusterLights.size()), ClusteredLights::kMaxLights));
    m_pbr->SetInt("uPointCube[0]", 9);
    m_pbr->SetInt("uPointCube[1]", 10);
    m_pbr->SetInt("uPointCube[2]", 11);
    m_pbr->SetInt("uPointCube[3]", 12);
    m_spotShadow.BindMaps(13);
    m_pbr->SetInt("uNumSpots", static_cast<int>(spotPos.size()));
    m_pbr->SetInt("uNumSpotShadows", numSpotShadows);
    static constexpr const char* kSpotPosNames[] = {"uSpotPos[0]", "uSpotPos[1]", "uSpotPos[2]", "uSpotPos[3]"};
    static constexpr const char* kSpotDirNames[] = {"uSpotDir[0]", "uSpotDir[1]", "uSpotDir[2]", "uSpotDir[3]"};
    static constexpr const char* kSpotColorNames[] = {"uSpotColor[0]", "uSpotColor[1]", "uSpotColor[2]", "uSpotColor[3]"};
    static constexpr const char* kSpotInnerNames[] = {"uSpotCosInner[0]", "uSpotCosInner[1]", "uSpotCosInner[2]", "uSpotCosInner[3]"};
    static constexpr const char* kSpotOuterNames[] = {"uSpotCosOuter[0]", "uSpotCosOuter[1]", "uSpotCosOuter[2]", "uSpotCosOuter[3]"};
    static constexpr const char* kSpotRangeNames[] = {"uSpotRange[0]", "uSpotRange[1]", "uSpotRange[2]", "uSpotRange[3]"};
    static constexpr const char* kSpotVpNames[] = {"uSpotVP[0]", "uSpotVP[1]", "uSpotVP[2]", "uSpotVP[3]"};
    for (std::size_t i = 0; i < spotPos.size() && i < static_cast<std::size_t>(SpotShadow::kMax); ++i) {
        m_pbr->SetVec3(kSpotPosNames[i], spotPos[i]);
        m_pbr->SetVec3(kSpotDirNames[i], spotDir[i]);
        m_pbr->SetVec3(kSpotColorNames[i], spotCol[i]);
        m_pbr->SetFloat(kSpotInnerNames[i], spotCosIn[i]);
        m_pbr->SetFloat(kSpotOuterNames[i], spotCosOut[i]);
        m_pbr->SetFloat(kSpotRangeNames[i], spotRanges[i]);
        m_pbr->SetMat4(kSpotVpNames[i], m_spotShadow.LightVP(static_cast<int>(i)));
    }
    m_pbr->SetInt("uSpotMap[0]", 13);
    m_pbr->SetInt("uSpotMap[1]", 14);
    m_pbr->SetInt("uSpotMap[2]", 15);
    m_pbr->SetInt("uSpotMap[3]", 16);
    GLint fragmentTextureUnits=0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragmentTextureUnits);
    const bool ltcAvailable=fragmentTextureUnits>25;
    m_pbr->SetInt("uNumAreas", static_cast<int>(areaPos.size()));
    static constexpr const char* kAreaPosNames[] = {"uAreaPos[0]", "uAreaPos[1]", "uAreaPos[2]", "uAreaPos[3]"};
    static constexpr const char* kAreaColorNames[] = {"uAreaColor[0]", "uAreaColor[1]", "uAreaColor[2]", "uAreaColor[3]"};
    static constexpr const char* kAreaRadiusNames[] = {"uAreaRadius[0]", "uAreaRadius[1]", "uAreaRadius[2]", "uAreaRadius[3]"};
    for (std::size_t i = 0; i < areaPos.size() && i < 4; ++i) {
        m_pbr->SetVec3(kAreaPosNames[i], areaPos[i]);
        m_pbr->SetVec3(kAreaColorNames[i], areaCol[i]);
        m_pbr->SetFloat(kAreaRadiusNames[i], areaRad[i]);
        const std::string index="["+std::to_string(i)+"]";m_pbr->SetVec3("uAreaRight"+index,areaRight[i]);m_pbr->SetVec3("uAreaUp"+index,areaUp[i]);
        m_pbr->SetVec2("uAreaHalfSize"+index,areaHalf[i]);m_pbr->SetInt("uAreaShape"+index,ltcAvailable?areaShape[i]:0);m_pbr->SetInt("uAreaTwoSided"+index,areaTwoSided[i]);
    }
    if(ltcAvailable)m_ltcLut->Bind(24,25);
    m_pbr->SetInt("uFogEnabled", opt.fog ? 1 : 0);
    m_pbr->SetVec3("uFogColor", opt.fogColor);
    m_pbr->SetFloat("uFogDensity", opt.fogDensity);
    m_pbr->SetFloat("uFogHeight", opt.fogHeight);
    m_pbr->SetFloat("uFogHeightFalloff", opt.fogHeightFalloff);
    m_pbr->SetInt("uSkylightOcclusion", opt.skylightOcclusion ? 1 : 0);
    m_pbr->SetFloat("uSkylightOcclusionStrength", opt.skylightOcclusionStrength);
    m_pbr->SetFloat("uMinimumSkylight", opt.minimumSkylight);
    m_pbr->SetFloat("uSpecularOcclusionStrength", opt.specularOcclusionStrength);
    m_pbr->SetFloat("uLocalProbeInfluence", opt.localProbeInfluence);
    m_pbr->SetInt("uLightingDebugMode", opt.lightingDebugMode);
    m_pbr->SetInt("uUseLightingGrid", opt.lightingGrid && opt.lightingGrid->Valid() ? 1 : 0);
    m_pbr->SetInt("uProbeVisibilityWeighting", opt.probeVisibilityWeighting ? 1 : 0);
    m_pbr->SetFloat("uProbeVisibilityMaxDistance", std::max(opt.probeVisibilityMaxDistance, 0.001f));
    if (opt.lightingGrid && opt.lightingGrid->Valid()) {
        engine::LightingProbeGrid::Contribution contribution=engine::LightingProbeGrid::Contribution::Combined;
        if(opt.lightingDebugMode==13)contribution=engine::LightingProbeGrid::Contribution::DirectEnvironment;
        else if(opt.lightingDebugMode==14)contribution=engine::LightingProbeGrid::Contribution::Bounce;
        else if(opt.lightingDebugMode==15)contribution=engine::LightingProbeGrid::Contribution::Emissive;
        else if(opt.lightingDebugMode==23)contribution=engine::LightingProbeGrid::Contribution::HigherBounce;
        opt.lightingGrid->Bind(18,contribution);
        m_pbr->SetVec3("uLightingGridMin", opt.lightingGrid->BoundsMin());
        m_pbr->SetVec3("uLightingGridMax", opt.lightingGrid->BoundsMax());
        const glm::ivec3 dimensions = opt.lightingGrid->Dimensions();
        m_pbr->SetVec3("uLightingGridDimensions", glm::vec3(dimensions));
    }
    m_pbr->SetInt("uCloudShadows", opt.cloudShadows ? 1 : 0);
    m_pbr->SetFloat("uCloudShadowStrength", opt.cloudShadowStrength);
    m_pbr->SetFloat("uCloudShadowScale", opt.cloudShadowScale);
    m_pbr->SetFloat("uCloudCoverage", opt.cloudCoverage);
    m_pbr->SetFloat("uCloudDensity", opt.cloudDensity);
    m_pbr->SetFloat("uCloudSoftness", opt.cloudSoftness);
    const float cloudAngle = glm::radians(opt.cloudWindDirectionDegrees);
    const float cloudSeconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pbr->SetVec2("uCloudWindOffset",
        glm::vec2(std::cos(cloudAngle), std::sin(cloudAngle))
        * (cloudSeconds * opt.cloudWindSpeed));

    // Main lit pass. Untextured MeshPBR entities that share a mesh are drawn in
    // ONE instanced call (per-instance model + material); textured ones fall back
    // to per-object draws. Instancing collapses N draw calls to ~one per mesh.
    auto& batches = m_meshBatches;
    auto& textured = m_texturedObjects;
    auto& custom = m_customObjects;
    for (auto& batch : batches) batch.second.clear();
    textured.clear();
    custom.clear();
    auto meshView = reg.view<Transform, MeshPBR>();
    if (!meshView.empty()) meshView.each([&](Entity, Transform& t, MeshPBR& m) {
        if (!m.mesh) return;
        if (opt.frustumCull) {
            const glm::mat4 model = t.Model();
            const glm::vec3 center = glm::vec3(
                model * glm::vec4(m.mesh->BoundsCenter(), 1.0f));
            const glm::vec3 scale = glm::abs(t.scale);
            const float radius = m.mesh->BoundsRadius()
                * std::max({scale.x, scale.y, scale.z});
            if (!SphereInFrustum(frustum, center, radius))
                return;                               // actual mesh is off-screen
        }
        if (m.customShader) {
            custom.emplace_back(&t, &m);
            return;
        }
        const PbrMaterial defaults;
        const bool advanced = m.material.blendMode != PbrMaterial::BlendMode::Opaque ||
            m.material.opacity != defaults.opacity || m.material.uvScale != defaults.uvScale ||
            m.material.uvOffset != defaults.uvOffset || m.material.uvRotation != defaults.uvRotation ||
            m.material.normalStrength != defaults.normalStrength || m.material.heightScale != defaults.heightScale ||
            m.material.clearcoat != defaults.clearcoat || m.material.transmission != defaults.transmission ||
            m.material.anisotropy != defaults.anisotropy || m.material.sheenColor != defaults.sheenColor ||
            m.material.specularLevel != defaults.specularLevel || m.material.subsurface != defaults.subsurface ||
            m.material.worldSpaceUv != defaults.worldSpaceUv;
        if (m.material.albedoMap || m.material.normalMap || m.material.metalRoughMap ||
            m.material.heightMap || m.material.emissiveMap || advanced || !opt.instancing) {
            textured.emplace_back(&t, &m);   // textured, or instancing disabled -> per-object
            return;
        }
        std::vector<float>& v = batches[m.mesh];
        const glm::mat4 model = t.Model();
        const float* mp = glm::value_ptr(model);
        v.insert(v.end(), mp, mp + 16);
        v.push_back(m.material.albedo.x); v.push_back(m.material.albedo.y); v.push_back(m.material.albedo.z);
        v.push_back(m.material.metallic); v.push_back(m.material.roughness); v.push_back(m.material.ao);
        v.push_back(m.material.emissive.x); v.push_back(m.material.emissive.y); v.push_back(m.material.emissive.z);
    });

    std::stable_sort(textured.begin(), textured.end(), [&](const auto& a, const auto& b) {
        const bool at = a.second->material.blendMode == PbrMaterial::BlendMode::Transparent;
        const bool bt = b.second->material.blendMode == PbrMaterial::BlendMode::Transparent;
        if (at != bt) return !at;
        if (!at) {
            // Opaque objects may be reordered freely. Grouping by geometry and
            // texture set reduces shader/material changes and makes the texture
            // binding cache effective across the whole pass.
            const auto pointerOrder = [](const void* lhs, const void* rhs) {
                return std::less<const void*>{}(lhs, rhs);
            };
            if (a.second->mesh != b.second->mesh)
                return pointerOrder(a.second->mesh, b.second->mesh);
            const auto& am = a.second->material;
            const auto& bm = b.second->material;
            if (am.albedoMap != bm.albedoMap) return pointerOrder(am.albedoMap, bm.albedoMap);
            if (am.normalMap != bm.normalMap) return pointerOrder(am.normalMap, bm.normalMap);
            if (am.metalRoughMap != bm.metalRoughMap) return pointerOrder(am.metalRoughMap, bm.metalRoughMap);
            if (am.heightMap != bm.heightMap) return pointerOrder(am.heightMap, bm.heightMap);
            return pointerOrder(am.emissiveMap, bm.emissiveMap);
        }
        const glm::vec3 da = a.first->position - camera.Position();
        const glm::vec3 db = b.first->position - camera.Position();
        return glm::dot(da, da) > glm::dot(db, db);
    });

    // Backface culling: never rasterize the inside of closed meshes. Front faces are
    // CCW (the mesh convention). Two-sided translucency (glass) turns it off per-object
    // below, and it is restored to the default (off) at the end of Render so later
    // passes (sky / water / particles / foliage) keep their expected state. When
    // opt.backfaceCull is false (e.g. the Material Maker preview) everything draws
    // two-sided so backfaces stay visible.
    const bool cullEnabled = opt.backfaceCull;
    const auto setCull = [cullEnabled](bool want) {
        (want && cullEnabled) ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    };
    setCull(true);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Textured entities: per-object (uInstanced = 0).
    m_pbr->SetInt("uInstanced", 0);
    m_pbr->SetInt("uMaterialSlotDebug", 0);
    std::array<const Texture*, 18> boundMaps{};
    auto bindMap = [&](const Texture* tex, int unit, const char* flag, const char* samp) {
        const std::size_t slot = static_cast<std::size_t>(unit);
        if (slot < boundMaps.size() && boundMaps[slot] == tex) return;
        if (slot < boundMaps.size()) boundMaps[slot] = tex;
        if (tex) {
            tex->Bind(static_cast<unsigned int>(unit));
            m_pbr->SetInt(samp, unit);
            m_pbr->SetInt(flag, 1);
        } else {
            m_pbr->SetInt(flag, 0);
        }
    };
    for (auto& pr : textured) {
        Transform& t = *pr.first; MeshPBR& m = *pr.second;
        const glm::mat4 model = t.Model();
        m_pbr->SetMat4("uModel", model);
        m_pbr->SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(model))));
        m_pbr->SetVec3("uAlbedo", m.material.albedo);
        m_pbr->SetFloat("uMetallic", m.material.metallic);
        m_pbr->SetFloat("uRoughness", m.material.roughness);
        m_pbr->SetFloat("uAO", m.material.ao);
        m_pbr->SetVec3("uEmissive", m.material.emissive);
        m_pbr->SetInt("uBlendMode", static_cast<int>(m.material.blendMode));
        m_pbr->SetFloat("uOpacity", m.material.opacity);
        m_pbr->SetFloat("uAlphaCutoff", m.material.alphaCutoff);
        m_pbr->SetVec2("uUvScale", m.material.uvScale); m_pbr->SetVec2("uUvOffset", m.material.uvOffset);
        m_pbr->SetFloat("uUvRotation", m.material.uvRotation);
        m_pbr->SetInt("uWorldUv", m.material.worldSpaceUv ? 1 : 0);
        m_pbr->SetFloat("uNormalStrength", m.material.normalStrength); m_pbr->SetFloat("uHeightScale", m.material.heightScale);
        m_pbr->SetFloat("uClearcoat", m.material.clearcoat); m_pbr->SetFloat("uClearcoatRoughness", m.material.clearcoatRoughness);
        m_pbr->SetFloat("uTransmission", m.material.transmission); m_pbr->SetFloat("uIor", m.material.ior);
        m_pbr->SetFloat("uThickness", m.material.thickness); m_pbr->SetFloat("uAnisotropy", m.material.anisotropy);
        m_pbr->SetFloat("uAnisotropyRotation", m.material.anisotropyRotation);
        m_pbr->SetVec3("uSheenColor", m.material.sheenColor); m_pbr->SetFloat("uSheenRoughness", m.material.sheenRoughness);
        m_pbr->SetFloat("uSpecularLevel", m.material.specularLevel); m_pbr->SetFloat("uSubsurface", m.material.subsurface);
        m_pbr->SetVec3("uSubsurfaceColor", m.material.subsurfaceColor);
        bindMap(m.material.albedoMap,     0, "uHasAlbedoMap",     "uAlbedoMap");
        bindMap(m.material.normalMap,     1, "uHasNormalMap",     "uNormalMap");
        bindMap(m.material.metalRoughMap, 2, "uHasMetalRoughMap", "uMetalRoughMap");
        bindMap(m.material.heightMap,    17, "uHasHeightMap",     "uHeightMap");
        bindMap(m.material.emissiveMap,  16, "uHasEmissiveMap",   "uEmissiveMap");
        if (m.material.blendMode == PbrMaterial::BlendMode::Transparent) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND); glDepthMask(GL_TRUE);
        }
        // Glass and flat planes render two-sided; closed solids cull their backfaces.
        // A negative object scale reverses winding. Match the front-face rule to
        // the transform so mirrored objects do not lose their visible exterior.
        glFrontFace(glm::determinant(glm::mat3(model)) < 0.0f ? GL_CW : GL_CCW);
        setCull(!(m.material.blendMode == PbrMaterial::BlendMode::Transparent
                  || m.mesh->TwoSided()));
        m.mesh->DrawLod(SelectMeshLod(*m.mesh, t, camera));
    }

    // Imported static models use the very same program and frame-level lighting
    // bindings as native MeshPBR geometry.  Only their geometry/material-slot
    // submission differs: one draw is issued per imported submesh.
    static std::unordered_map<const Model*, std::uint64_t> reportedFallbacks;
    auto importedView = reg.view<Transform, ecs::LoadedModelAsset>();
    if (!importedView.empty()) importedView.each(
        [&](Entity entity, Transform& t, ecs::LoadedModelAsset& loaded) {
            if (!loaded.model || reg.Has<MeshPBR>(entity)) return;
            const glm::mat4 model = t.Model();
            if (opt.frustumCull) {
                const glm::vec3 center = glm::vec3(
                    model * glm::vec4(loaded.model->Center(), 1.0f));
                const glm::vec3 scale = glm::abs(t.scale);
                const float radius = loaded.model->BoundingRadius()
                    * std::max({scale.x, scale.y, scale.z});
                if (!SphereInFrustum(frustum, center, std::max(radius, 0.01f)))
                    return;
            }
            const auto* objectOverride = reg.TryGet<ecs::LoadedMaterialAsset>(entity);
            m_pbr->SetMat4("uModel", model);
            m_pbr->SetMat3("uNormalMat",
                glm::mat3(glm::transpose(glm::inverse(model))));
            glFrontFace(glm::determinant(glm::mat3(model)) < 0.0f ? GL_CW : GL_CCW);
            for (const SubMesh& submesh : loaded.model->SubMeshes()) {
                bool fallback = false;
                const PbrMaterial material = ResolveModelPbrMaterial(
                    *loaded.model, submesh.material, objectOverride, &fallback);
                m_pbr->SetInt("uMaterialSlotDebug", std::max(submesh.material, 0));
                if (fallback) {
                    const std::uint64_t bit = submesh.material >= 0
                        && submesh.material < 63 ? (1ull << submesh.material) : (1ull << 63);
                    std::uint64_t& reported = reportedFallbacks[loaded.model];
                    if ((reported & bit) == 0) {
                        reported |= bit;
                        std::clog << "[Warn] Shared PBR imported-model material fallback: entity="
                                  << static_cast<std::uint32_t>(entity)
                                  << " submeshSlot=" << submesh.material
                                  << " materialCount=" << loaded.model->Materials().size()
                                  << " (default material used)\n";
                    }
                }
                m_pbr->SetVec3("uAlbedo", material.albedo);
                m_pbr->SetFloat("uMetallic", material.metallic);
                m_pbr->SetFloat("uRoughness", material.roughness);
                m_pbr->SetFloat("uAO", material.ao);
                m_pbr->SetVec3("uEmissive", material.emissive);
                m_pbr->SetInt("uBlendMode", static_cast<int>(material.blendMode));
                m_pbr->SetFloat("uOpacity", material.opacity);
                m_pbr->SetFloat("uAlphaCutoff", material.alphaCutoff);
                m_pbr->SetVec2("uUvScale", material.uvScale);
                m_pbr->SetVec2("uUvOffset", material.uvOffset);
                m_pbr->SetFloat("uUvRotation", material.uvRotation);
                m_pbr->SetInt("uWorldUv", material.worldSpaceUv ? 1 : 0);
                m_pbr->SetFloat("uNormalStrength", material.normalStrength);
                m_pbr->SetFloat("uHeightScale", material.heightScale);
                m_pbr->SetFloat("uClearcoat", material.clearcoat);
                m_pbr->SetFloat("uClearcoatRoughness", material.clearcoatRoughness);
                m_pbr->SetFloat("uTransmission", material.transmission);
                m_pbr->SetFloat("uIor", material.ior);
                m_pbr->SetFloat("uThickness", material.thickness);
                m_pbr->SetFloat("uAnisotropy", material.anisotropy);
                m_pbr->SetFloat("uAnisotropyRotation", material.anisotropyRotation);
                m_pbr->SetVec3("uSheenColor", material.sheenColor);
                m_pbr->SetFloat("uSheenRoughness", material.sheenRoughness);
                m_pbr->SetFloat("uSpecularLevel", material.specularLevel);
                m_pbr->SetFloat("uSubsurface", material.subsurface);
                m_pbr->SetVec3("uSubsurfaceColor", material.subsurfaceColor);
                bindMap(material.albedoMap, 0, "uHasAlbedoMap", "uAlbedoMap");
                bindMap(material.normalMap, 1, "uHasNormalMap", "uNormalMap");
                bindMap(material.metalRoughMap, 2, "uHasMetalRoughMap", "uMetalRoughMap");
                bindMap(material.heightMap, 17, "uHasHeightMap", "uHeightMap");
                bindMap(material.emissiveMap, 16, "uHasEmissiveMap", "uEmissiveMap");
                if (material.blendMode == PbrMaterial::BlendMode::Transparent) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glDepthMask(GL_FALSE);
                    setCull(false);
                } else {
                    glDisable(GL_BLEND);
                    glDepthMask(GL_TRUE);
                    setCull(!submesh.mesh.TwoSided());
                }
                submesh.mesh.DrawLod(SelectMeshLod(submesh.mesh, t, camera));
            }
        });
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    m_pbr->SetInt("uMaterialSlotDebug", 0);
    setCull(true);   // batches below are opaque

    // Instanced batches (uInstanced = 1, no textures).
    if (!batches.empty()) {
        m_pbr->SetInt("uInstanced", 1);
        m_pbr->SetInt("uHasAlbedoMap", 0);
        m_pbr->SetInt("uHasNormalMap", 0);
        m_pbr->SetInt("uHasMetalRoughMap", 0);
        m_pbr->SetInt("uHasEmissiveMap", 0);
        m_pbr->SetInt("uHasHeightMap", 0);
        m_pbr->SetInt("uBlendMode", 0); m_pbr->SetFloat("uOpacity", 1.0f);
        m_pbr->SetVec2("uUvScale", glm::vec2(1.0f)); m_pbr->SetVec2("uUvOffset", glm::vec2(0.0f));
        m_pbr->SetInt("uWorldUv", 0);
        m_pbr->SetFloat("uUvRotation", 0.0f); m_pbr->SetFloat("uNormalStrength", 1.0f);
        m_pbr->SetFloat("uHeightScale", 0.0f); m_pbr->SetFloat("uClearcoat", 0.0f);
        m_pbr->SetFloat("uTransmission", 0.0f); m_pbr->SetFloat("uAnisotropy", 0.0f);
        m_pbr->SetVec3("uSheenColor", glm::vec3(0.0f)); m_pbr->SetFloat("uSpecularLevel", 0.5f);
        m_pbr->SetFloat("uSubsurface", 0.0f);
        const GLsizei stride = 25 * static_cast<GLsizei>(sizeof(float));
        for (auto& kv : batches) {
            const Mesh* mesh = kv.first;
            std::vector<float>& data = kv.second;
            if (data.empty()) continue;
            const GLsizei count = static_cast<GLsizei>(data.size() / 25);
            bool mirrored = false;
            for (std::size_t offset = 0; offset + 16 <= data.size(); offset += 25) {
                glm::mat4 instance(1.0f);
                std::memcpy(glm::value_ptr(instance), data.data() + offset,
                            sizeof(glm::mat4));
                mirrored = mirrored || glm::determinant(glm::mat3(instance)) < 0.0f;
            }
            glFrontFace(GL_CCW);
            // Mixed mirrored/non-mirrored instances cannot share one cull winding;
            // render that batch two-sided rather than dropping half the objects.
            setCull(!mesh->TwoSided() && !mirrored);   // flat planes: both sides
            glBindVertexArray(mesh->Vao());
            glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
            const std::size_t instanceBytes = data.size() * sizeof(float);
            if (instanceBytes > m_instanceCapacity) {
                m_instanceCapacity = std::max(instanceBytes,
                    std::max<std::size_t>(m_instanceCapacity * 2, 4096));
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_instanceCapacity),
                             nullptr, GL_DYNAMIC_DRAW);
            }
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instanceBytes), data.data());
            for (int c = 0; c < 4; ++c) {   // mat4 columns -> locations 3..6
                const GLuint loc = static_cast<GLuint>(3 + c);
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void*>(static_cast<std::size_t>(c * 4) * sizeof(float)));
                glVertexAttribDivisor(loc, 1);
            }
            const struct { GLuint loc; int off; } mats[3] = {{7, 16}, {8, 19}, {9, 22}};
            for (auto& a : mats) {
                glEnableVertexAttribArray(a.loc);
                glVertexAttribPointer(a.loc, 3, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void*>(static_cast<std::size_t>(a.off) * sizeof(float)));
                glVertexAttribDivisor(a.loc, 1);
            }
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(mesh->IndexCount()),
                                    GL_UNSIGNED_INT, nullptr, count);
            GpuProfiler::RecordDrawCall();
            for (GLuint loc = 3; loc <= 9; ++loc) {   // leave the mesh VAO as we found it
                glVertexAttribDivisor(loc, 0);
                glDisableVertexAttribArray(loc);
            }
        }
        glBindVertexArray(0);
        m_pbr->SetInt("uInstanced", 0);
    }

    // Custom graph shaders are deliberately per-object. This preserves normal
    // PBR batching and keeps experimental programs isolated from shared state.
    std::stable_sort(custom.begin(), custom.end(), [&](const auto& a, const auto& b) {
        const bool at = a.second->material.blendMode == PbrMaterial::BlendMode::Transparent;
        const bool bt = b.second->material.blendMode == PbrMaterial::BlendMode::Transparent;
        if (at != bt) return !at;
        // Opaque custom materials do not depend on draw order, so group them
        // by shader to avoid switching programs between adjacent objects.
        if (!at && a.second->customShader != b.second->customShader)
            return std::less<const Shader*>{}(a.second->customShader, b.second->customShader);
        const glm::vec3 da = a.first->position - camera.Position();
        const glm::vec3 db = b.first->position - camera.Position();
        return at && glm::dot(da, da) > glm::dot(db, db);
    });
    const Shader* boundCustomShader = nullptr;
    for (const auto& item : custom) {
        const Transform& transform = *item.first;
        const MeshPBR& mesh = *item.second;
        Shader& shader = *const_cast<Shader*>(mesh.customShader);
        if (boundCustomShader != mesh.customShader) {
            shader.Bind();
            boundCustomShader = mesh.customShader;
        }
        shader.SetMat4("uViewProjection", viewProj);
        shader.SetMat4("uModel", transform.Model());
        shader.SetVec3("uCameraPosition", camera.Position());
        shader.SetFloat("uTime", cloudSeconds);
        shader.SetFloat("uDeltaTime", 1.0f / 60.0f);
        shader.SetVec3("uLightDirection", sunDir);
        shader.SetFloat("uLightIntensity", std::max({sunColor.x, sunColor.y, sunColor.z}));
        shader.SetVec3("uLightColor", sunColor);      // key light radiance (for custom lighting)
        shader.SetVec3("uAmbient", opt.ambient);      // sky/ambient term (for custom lighting)
        shader.SetVec4("uObjectColor",
            glm::vec4(mesh.material.albedo, mesh.material.opacity));
        UploadCustomParameters(shader, mesh);
        if (mesh.material.blendMode == PbrMaterial::BlendMode::Transparent) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND); glDepthMask(GL_TRUE);
        }
        glFrontFace(glm::determinant(glm::mat3(transform.Model())) < 0.0f ? GL_CW : GL_CCW);
        setCull(!(mesh.material.blendMode == PbrMaterial::BlendMode::Transparent
                  || mesh.mesh->TwoSided()));
        mesh.mesh->DrawLod(SelectMeshLod(*mesh.mesh, transform, camera));
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);   // restore the default (off) for subsequent passes
}

} // namespace engine
