#include "engine/graphics/SkinnedRenderer.h"

#include "engine/graphics/SkinnedModel.h"
#include "engine/graphics/Frustum.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/DirectionalShadowShader.h"
#include "engine/graphics/PbrLightingCommon.h"
#include "engine/graphics/LightingBuildData.h"
#include "engine/graphics/IBL.h"
#include "engine/graphics/SSAO.h"
#include "engine/graphics/ReflectionProbeSystem.h"
#include "engine/graphics/LtcLut.h"
#include "engine/animation/AnimatedModel.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/ShaderParameterBinding.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace engine {
namespace {

void UploadMaterialShaderParameters(Shader& shader, const ecs::LoadedMaterialAsset& material) {
    int textureUnit = 18;
    for (const auto& entry : material.shaderParameters) {
        const auto type = material.shaderParameterTypes.find(entry.first);
        const int valueType = type == material.shaderParameterTypes.end() ? 0 : type->second;
        const auto texture = material.shaderTextures.find(entry.first);
        textureUnit = UploadShaderParameter(
            shader, entry.first, valueType, entry.second,
            texture == material.shaderTextures.end() ? nullptr : texture->second,
            textureUnit);
    }
}

// Shared skinned vertex stage: blend up to four bone matrices, then model+VP.
const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aBoneIds;   // packed as floats
layout (location = 4) in vec4 aWeights;
const int MAX_BONES = 128;
uniform mat4 uBones[MAX_BONES];
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
void main() {
    mat4 skin = aWeights.x * uBones[int(aBoneIds.x)]
              + aWeights.y * uBones[int(aBoneIds.y)]
              + aWeights.z * uBones[int(aBoneIds.z)]
              + aWeights.w * uBones[int(aBoneIds.w)];
    vec4 local = skin * vec4(aPos, 1.0);
    vec4 world = uModel * local;
    vWorldPos  = world.xyz;
    mat3 nrm   = mat3(transpose(inverse(uModel * skin)));
    vNormal    = normalize(nrm * aNormal);
    vUV        = aUV;
    gl_Position = uViewProj * world;
}
)GLSL";

// Phong fragment (standalone character demo).
const char* kFrag = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;
uniform vec3  uColor;
uniform vec3  uSpecular;
uniform vec3  uEmissive;
uniform float uShininess;
uniform int   uHasDiffuse;
uniform sampler2D uDiffuseTex;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uAmbient;
uniform vec3  uViewPos;
void main() {
    vec3 base = uColor;
    if (uHasDiffuse == 1) base *= texture(uDiffuseTex, vUV).rgb;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uSunDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), max(uShininess, 1.0)) : 0.0;
    vec3 color = uAmbient * base + uSunColor * (diff * base + spec * uSpecular) + uEmissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)GLSL";

// Cook-Torrance fragment matching the world renderer. Shared probe,
// attenuation, and specular-occlusion code is injected from PbrLightingCommon.
const char* kPbrFrag = R"GLSL(
#version 330 core
const float PI = 3.14159265359;
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
uniform int   uHasAlbedoMap;
uniform sampler2D uAlbedoMap;
uniform int   uHasEmissiveMap;
uniform sampler2D uEmissiveMap;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uAmbient;
const int MAX_POINTS = 32;
const int MAX_SPOTS = 4;
const int MAX_AREAS = 4;
uniform int uNumPoints;
uniform vec4 uPointPosRadius[MAX_POINTS];
uniform vec3 uPointColor[MAX_POINTS];
uniform int uNumSpots;
uniform vec3 uSpotPos[MAX_SPOTS], uSpotDir[MAX_SPOTS], uSpotColor[MAX_SPOTS];
uniform float uSpotCosInner[MAX_SPOTS], uSpotCosOuter[MAX_SPOTS], uSpotRange[MAX_SPOTS];
uniform int uNumAreas;
uniform vec3 uAreaPos[MAX_AREAS], uAreaColor[MAX_AREAS];
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
uniform int   uHasShadow;
uniform int   uUseIBL;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D   uBrdfLUT;
uniform float uMaxReflectionLod;
uniform float uGlobalIblIntensity;
uniform float uGlobalReflectionIntensity;
uniform int   uApplyTonemap;
uniform int   uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
uniform float uFogHeightFalloff;
uniform int uSkylightOcclusion;
uniform float uSkylightOcclusionStrength, uMinimumSkylight;
uniform int uUseLightingGrid;
uniform vec3 uLightingGridMin, uLightingGridMax;
//__PBR_LIGHTING_COMMON__
uniform int uUseSSAO;
uniform sampler2D uSsaoMap;
uniform sampler2D uRawGtaoMap;
uniform sampler2D uBentNormalMap;
uniform int uUseBentNormal;
uniform mat4 uViewToWorld;
uniform vec2 uScreenSize;
uniform int uCloudShadows;
uniform float uCloudShadowStrength, uCloudShadowScale;
uniform float uCloudCoverage, uCloudDensity, uCloudSoftness;
uniform vec2 uCloudWindOffset;
float LocalSkyVisibility(vec3 worldPos, vec3 normal, float sunVisibility) {
    float hemisphere=mix(0.72,1.0,clamp(normal.y*0.5+0.5,0.0,1.0));
    if(uUseLightingGrid==1){vec3 extent=max(uLightingGridMax-uLightingGridMin,vec3(0.0001));vec3 uvw=(worldPos-uLightingGridMin)/extent;
        if(all(greaterThanEqual(uvw,vec3(0)))&&all(lessThanEqual(uvw,vec3(1)))){LocalProbeSample probe=SampleLocalProbe(worldPos,normal);
            if(probe.validity>0.001)return clamp(probe.skyVisibility*hemisphere,0.0,1.0);}}
    return clamp(0.25*sunVisibility*hemisphere,0.0,1.0);
}
float CloudHash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453123); }
float CloudNoise(vec2 p) {
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(CloudHash(i),CloudHash(i+vec2(1,0)),f.x),
               mix(CloudHash(i+vec2(0,1)),CloudHash(i+vec2(1)),f.x),f.y);
}
float CloudFbm(vec2 p) {
    float v=0.0,w=0.52; mat2 o=mat2(1.7,1.2,-1.2,1.7);
    for(int i=0;i<5;++i){v+=CloudNoise(p)*w;p=o*p+vec2(13.7,9.2);w*=0.5;} return v;
}
float CloudSunlight(vec3 wp) {
    if(uCloudShadows==0)return 1.0;
    vec2 p=wp.xz*max(uCloudShadowScale,0.0001)+uCloudWindOffset;
    float s=mix(CloudFbm(p),CloudFbm(p*2.7+vec2(4.2,-3.1)),0.28);
    float c=smoothstep(uCloudCoverage-max(uCloudSoftness,0.005),
                       uCloudCoverage+max(uCloudSoftness,0.005),s);
    return 1.0-clamp(c*uCloudDensity*uCloudShadowStrength,0.0,0.95);
}
//__DIRECTIONAL_SHADOW_IMPLEMENTATION__
vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 SphereAreaLight(vec3 N, vec3 V, vec3 Lvec, float sourceRadius, vec3 color,
                     vec3 albedo, vec3 F0, float metallic, float roughness) {
    float distanceSquared = max(dot(Lvec, Lvec), 0.0001);
    vec3 L = Lvec * inversesqrt(distanceSquared);
    float apparentRadius = clamp(sourceRadius / sqrt(distanceSquared), 0.0, 1.0);
    vec3 radiance = color / distanceSquared;
    return Lighting(N, V, L, radiance * (1.0 + apparentRadius),
                    albedo, F0, metallic, roughness);
}
void main() {
    vec3 albedo = uAlbedo;
    if (uHasAlbedoMap == 1) albedo *= pow(texture(uAlbedoMap, vUV).rgb, vec3(2.2));  // sRGB -> linear
    vec3 emissive = uEmissive;
    if (uHasEmissiveMap == 1)
        emissive *= pow(texture(uEmissiveMap, vUV).rgb, vec3(2.2));
    float metallic = uMetallic, roughness = uRoughness, ao = uAO;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Ls = normalize(-uSunDir);
    float sunNdotL = max(dot(N, Ls), 0.0);
    float shadow = (uHasShadow == 1) ? ShadowFactor(sunNdotL, N) : 0.0;
    vec3 Lo = (1.0 - shadow) * CloudSunlight(vWorldPos)
            * Lighting(N, V, Ls, uSunColor, albedo, F0, metallic, roughness);
    for (int i=0; i<min(uNumPoints,MAX_POINTS); ++i) {
        vec3 d=uPointPosRadius[i].xyz-vWorldPos; float distanceSquared=dot(d,d);
        vec3 L=d*inversesqrt(max(distanceSquared,0.0001));
        Lo += Lighting(N,V,L,uPointColor[i]*SmoothFiniteAttenuation(distanceSquared,uPointPosRadius[i].w),albedo,F0,metallic,roughness);
    }
    for (int i=0; i<min(uNumSpots,MAX_SPOTS); ++i) {
        vec3 d=uSpotPos[i]-vWorldPos; float distanceSquared=dot(d,d);
        vec3 L=d*inversesqrt(max(distanceSquared,0.0001));
        float cone=smoothstep(uSpotCosOuter[i],uSpotCosInner[i],dot(normalize(-uSpotDir[i]),L));
        Lo += Lighting(N,V,L,uSpotColor[i]*SmoothFiniteAttenuation(distanceSquared,uSpotRange[i])*cone,albedo,F0,metallic,roughness);
    }
    for (int i=0; i<min(uNumAreas,MAX_AREAS); ++i){if(uAreaShape[i]==1)
        Lo+=LtcRectangleLight(N,V,vWorldPos,uAreaPos[i],uAreaRight[i],uAreaUp[i],uAreaHalfSize[i],uAreaTwoSided[i],uAreaColor[i],albedo,F0,metallic,roughness);
        else Lo += SphereAreaLight(N,V,uAreaPos[i]-vWorldPos,uAreaRadius[i],uAreaColor[i],albedo,F0,metallic,roughness);}
    vec3 ambient;
    vec2 ambientUv=gl_FragCoord.xy/uScreenSize;
    vec3 indirectN=N;
    if(uUseBentNormal==1){vec3 bentView=normalize(texture(uBentNormalMap,ambientUv).xyz);
        vec3 bentWorld=normalize(mat3(uViewToWorld)*bentView);
        if(dot(bentWorld,bentWorld)>0.5)indirectN=normalize(mix(N,bentWorld,0.75));}
    LocalProbeSample localProbe=SampleLocalProbe(vWorldPos,indirectN);
    float fallbackSkyVisibility=LocalSkyVisibility(vWorldPos,indirectN,1.0-shadow);
    float resolvedSkyVisibility=mix(fallbackSkyVisibility,localProbe.skyVisibility,step(0.001,localProbe.validity));
    float skyVisibility=max(resolvedSkyVisibility,clamp(uMinimumSkylight,0.0,1.0));
    float screenAo=(uUseSSAO==1)?texture(uSsaoMap,gl_FragCoord.xy/uScreenSize).r:1.0;
    vec3 diffuseIndirect=vec3(0.0),specularIndirect=vec3(0.0);
    float specularOcclusion=1.0;
    if (uUseIBL == 1) {
        vec3 F = FresnelSchlickRough(max(dot(N,V),0.0), F0, roughness);
        vec3 kD = (vec3(1.0)-F)*(1.0-metallic);
        vec3 irradiance = texture(uIrradiance, indirectN).rgb * uGlobalIblIntensity;
        irradiance=mix(irradiance,localProbe.irradiance,localProbe.validity*clamp(uLocalProbeInfluence,0.0,1.0));
        vec3 diffuse = irradiance * albedo;
        vec3 R = reflect(-V, N);
        vec3 globalPrefiltered=textureLod(uPrefilter,R,roughness*uMaxReflectionLod).rgb
            *uGlobalIblIntensity*uGlobalReflectionIntensity;
        float reflectionProbeWeight=0.0;
        vec3 prefiltered=SampleReflectionEnvironment(vWorldPos,R,roughness,globalPrefiltered,reflectionProbeWeight);
        vec2 brdf = texture(uBrdfLUT, vec2(max(dot(N,V),0.0), roughness)).rg;
        vec3 specular = prefiltered * (F*brdf.x + brdf.y);
        specularOcclusion=PbrSpecularOcclusion(ao*screenAo,max(dot(N,V),0.0),roughness,skyVisibility);
        specular*=mix(specularOcclusion,1.0,reflectionProbeWeight);
        vec3 diffuseAmbient=kD*diffuse*ao*screenAo;
        if(uSkylightOcclusion==1)diffuseAmbient*=mix(1.0,skyVisibility,clamp(uSkylightOcclusionStrength,0.0,1.0));
        diffuseIndirect=diffuseAmbient; specularIndirect=specular;
        ambient = diffuseIndirect + specularIndirect;
    } else {
        ambient = uAmbient*albedo*ao*screenAo;
        diffuseIndirect=ambient;
    }
    if (uSkylightOcclusion == 1 && uUseIBL == 0) {
        ambient *= mix(1.0, skyVisibility, clamp(uSkylightOcclusionStrength, 0.0, 1.0));
    }
    vec3 color = ambient + Lo + emissive;
    if(uLightingDebugMode==1)color=Lo;
    else if(uLightingDebugMode==2)color=diffuseIndirect;
    else if(uLightingDebugMode==3)color=specularIndirect;
    else if(uLightingDebugMode==4)color=localProbe.irradiance;
    else if(uLightingDebugMode==5)color=vec3(skyVisibility);
    else if(uLightingDebugMode==6)color=vec3(ao*screenAo);
    else if(uLightingDebugMode==7)color=vec3(specularOcclusion);
    else if(uLightingDebugMode==8)color=vec3(localProbe.validity);
    else if(uLightingDebugMode==9)color=indirectN*0.5+0.5;
    else if(uLightingDebugMode==10)color=vec3(texture(uRawGtaoMap,ambientUv).r);
    else if(uLightingDebugMode==11)color=vec3(screenAo);
    else if(uLightingDebugMode==12){float reflectionWeight=0.0;
        if(uReflectionProbeCount>0)reflectionWeight+=ReflectionProbeWeight(0,vWorldPos);
        if(uReflectionProbeCount>1)reflectionWeight+=ReflectionProbeWeight(1,vWorldPos);
        color=vec3(clamp(reflectionWeight,0.0,1.0));}
    else if((uLightingDebugMode>=13&&uLightingDebugMode<=15)||uLightingDebugMode==23)color=localProbe.irradiance;
    else if(uLightingDebugMode==16)color=vec3(localProbe.visibility);
    else if(uLightingDebugMode==17)color=localProbe.irradiance;
    else if(uLightingDebugMode==18)color=vec3(0.0);
    else if(uLightingDebugMode==19)color=diffuseIndirect+specularIndirect;
    else if(uLightingDebugMode==20)color=vec3(1.0-shadow);
    else if(uLightingDebugMode==21)color=DirectionalCascadeDebugColor(vWorldPos);
    else if(uLightingDebugMode==22){vec3 debugF=FresnelSchlickRough(max(dot(N,V),0.0),F0,roughness);
        vec3 debugKd=(vec3(1.0)-debugF)*(1.0-metallic);
        vec3 globalDiffuse=debugKd*texture(uIrradiance,indirectN).rgb*albedo*ao*screenAo;
        if(uSkylightOcclusion==1)globalDiffuse*=mix(1.0,skyVisibility,clamp(uSkylightOcclusionStrength,0.0,1.0));
        vec3 debugR=reflect(-V,N);vec2 debugBrdf=texture(uBrdfLUT,vec2(max(dot(N,V),0.0),roughness)).rg;
        vec3 globalSpecular=textureLod(uPrefilter,debugR,roughness*uMaxReflectionLod).rgb*(debugF*debugBrdf.x+debugBrdf.y)*specularOcclusion;
        color=globalDiffuse+globalSpecular;}
    if (uFogEnabled == 1 && uLightingDebugMode == 0) {
        float dist = length(uViewPos - vWorldPos);
        float distFog = 1.0 - exp(-dist * uFogDensity);
        float heightF = clamp(exp(-(vWorldPos.y - uFogHeight) * uFogHeightFalloff), 0.0, 1.0);
        float fog = clamp(distFog * heightF, 0.0, 1.0);
        color = mix(color, uFogColor, fog);
    }
    if (uApplyTonemap == 1 && uLightingDebugMode != 20 && uLightingDebugMode != 21) {
        color = ACES(color);                 // filmic tone map (was Reinhard)
        color = pow(color, vec3(1.0/2.2));   // linear -> sRGB
    }
    FragColor = vec4(color, 1.0);
}
)GLSL";

const char* kDepthVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 3) in vec4 aBoneIds;
layout (location = 4) in vec4 aWeights;
const int MAX_BONES = 128;
uniform mat4 uBones[MAX_BONES];
uniform mat4 uModel;
uniform mat4 uLightVP;
void main() {
    mat4 skin = aWeights.x * uBones[int(aBoneIds.x)]
              + aWeights.y * uBones[int(aBoneIds.y)]
              + aWeights.z * uBones[int(aBoneIds.z)]
              + aWeights.w * uBones[int(aBoneIds.w)];
    gl_Position = uLightVP * uModel * skin * vec4(aPos, 1.0);
}
)GLSL";
const char* kDepthFrag = R"GLSL(
#version 330 core
void main() {}
)GLSL";

void UploadBones(Shader& sh, const std::vector<glm::mat4>& bones) {
    const std::size_t n = (bones.size() < static_cast<std::size_t>(SkinnedRenderer::kMaxBones))
                          ? bones.size() : static_cast<std::size_t>(SkinnedRenderer::kMaxBones);
    if (n > 0) sh.SetMat4Array("uBones[0]", bones.data(), static_cast<int>(n));
}

} // namespace

SkinnedRenderer::SkinnedRenderer()
    : m_shader(std::make_unique<Shader>(kVert, kFrag)),
      m_pbr(std::make_unique<Shader>(kVert, ComposeDirectionalShadowShader(ComposePbrLightingShader(kPbrFrag).c_str()))),
      m_depth(std::make_unique<Shader>(kDepthVert, kDepthFrag)),m_ltcLut(std::make_unique<LtcLut>()) {}

SkinnedRenderer::~SkinnedRenderer() = default;

void SkinnedRenderer::Draw(const SkinnedModel& model,
                           const std::vector<glm::mat4>& bones,
                           const glm::mat4& modelMatrix,
                           const Camera& camera, float aspect,
                           const glm::vec3& sunDir, const glm::vec3& sunColor,
                           const glm::vec3& ambient,
                           const glm::vec3& tint,
                           const Texture* albedoOverride) {
    const glm::mat4 viewProj = camera.ProjectionMatrix(aspect) * camera.ViewMatrix();
    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetMat4("uModel", modelMatrix);
    m_shader->SetVec3("uViewPos", camera.Position());
    m_shader->SetVec3("uSunDir", sunDir);
    m_shader->SetVec3("uSunColor", sunColor);
    m_shader->SetVec3("uAmbient", ambient);
    UploadBones(*m_shader, bones);

    const auto& mats = model.Materials();
    const auto& texs = model.Textures();
    const Texture* boundDiffuse = nullptr;
    for (const SubMesh& sm : model.SubMeshes()) {
        glm::vec3 color(0.8f), specular(0.2f), emissive(0.0f);
        float shininess = 32.0f;
        int diffuseMap = -1;
        if (sm.material >= 0 && sm.material < static_cast<int>(mats.size())) {
            const Material& m = mats[static_cast<std::size_t>(sm.material)];
            color = m.diffuse; specular = m.specular; emissive = m.emissive;
            shininess = m.shininess; diffuseMap = m.diffuseMap;
        }
        // A material override (from a .3dgmat) tints the base colour and can replace the
        // albedo map, so the character preview reflects an assigned material.
        m_shader->SetVec3("uColor", albedoOverride ? tint : (color * tint));
        m_shader->SetVec3("uSpecular", specular);
        m_shader->SetVec3("uEmissive", emissive);
        m_shader->SetFloat("uShininess", shininess);
        if (albedoOverride) {
            if (boundDiffuse != albedoOverride) {
                albedoOverride->Bind(0);
                boundDiffuse = albedoOverride;
            }
            m_shader->SetInt("uDiffuseTex", 0);
            m_shader->SetInt("uHasDiffuse", 1);
        } else if (diffuseMap >= 0 && diffuseMap < static_cast<int>(texs.size()) && texs[static_cast<std::size_t>(diffuseMap)]) {
            const Texture* diffuse = texs[static_cast<std::size_t>(diffuseMap)].get();
            if (boundDiffuse != diffuse) {
                diffuse->Bind(0);
                boundDiffuse = diffuse;
            }
            m_shader->SetInt("uDiffuseTex", 0);
            m_shader->SetInt("uHasDiffuse", 1);
        } else {
            m_shader->SetInt("uHasDiffuse", 0);
        }
        sm.mesh.Draw();
    }
}

void SkinnedRenderer::DrawScene(ecs::Registry& reg, const Camera& camera, float aspect,
                                const SkinnedLighting& lit) {
    if (!lit.cascade) return;

    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    const glm::mat4 viewProj = proj * view;
    m_pbr->Bind();
    m_pbr->SetMat4("uViewProj", viewProj);
    m_pbr->SetMat4("uView", view);
    m_pbr->SetVec3("uViewPos", camera.Position());
    m_pbr->SetVec3("uSunDir", lit.sunDir);
    m_pbr->SetVec3("uSunColor", lit.sunColor);
    m_pbr->SetVec3("uAmbient", lit.ambient);
    m_pbr->SetInt("uApplyTonemap", lit.tonemap ? 1 : 0);
    m_pbr->SetFloat("uShadowSoftness", lit.shadowSoftness);
    m_pbr->SetInt("uShadowBlockerSamples", std::clamp(lit.shadowBlockerSamples, 4, 16));
    m_pbr->SetInt("uShadowFilterSamples", std::clamp(lit.shadowFilterSamples, 6, 24));
    // Keep optional sampler types off unit 0 even when their feature is disabled.
    m_pbr->SetInt("uLightingSH0",18); m_pbr->SetInt("uLightingSH1",19);
    m_pbr->SetInt("uLightingSH2",20); m_pbr->SetInt("uLightingMeta",21);
    m_pbr->SetInt("uLocalReflection0",22); m_pbr->SetInt("uLocalReflection1",23);
    m_pbr->SetInt("uLtcMatrixLut",24); m_pbr->SetInt("uLtcAmplitudeLut",25);
    m_pbr->SetInt("uBentNormalMap",26); m_pbr->SetInt("uRawGtaoMap",27);

    // Sun (cascade) shadows -- same texture array + matrices the static PBR pass built.
    lit.cascade->BindArray(4);
    m_pbr->SetInt("uCascadeMaps", 4);
    m_pbr->SetInt("uHasShadow", 1);
    static constexpr const char* kCascadeVpNames[] = {"uCascadeVP[0]", "uCascadeVP[1]", "uCascadeVP[2]", "uCascadeVP[3]"};
    static constexpr const char* kCascadeSplitNames[] = {"uCascadeSplits[0]", "uCascadeSplits[1]", "uCascadeSplits[2]", "uCascadeSplits[3]"};
    static constexpr const char* kCascadeTexelNames[] = {"uCascadeWorldTexelSize[0]", "uCascadeWorldTexelSize[1]", "uCascadeWorldTexelSize[2]", "uCascadeWorldTexelSize[3]"};
    static constexpr const char* kCascadeDepthRangeNames[] = {"uCascadeDepthRange[0]", "uCascadeDepthRange[1]", "uCascadeDepthRange[2]", "uCascadeDepthRange[3]"};
    for (int i = 0; i < CascadedShadow::kCascades; ++i) {
        m_pbr->SetMat4(kCascadeVpNames[i], lit.cascade->CascadeVP(i));
        m_pbr->SetFloat(kCascadeSplitNames[i], lit.cascade->SplitDepth(i));
        m_pbr->SetFloat(kCascadeTexelNames[i], lit.cascade->WorldTexelSize(i));
        m_pbr->SetFloat(kCascadeDepthRangeNames[i], lit.cascade->DepthRange(i));
    }
    // Image-based ambient is optional. The ambient term remains available when
    // an editor environment deliberately disables IBL.
    m_pbr->SetInt("uUseIBL", lit.ibl ? 1 : 0);
    if (lit.ibl) {
        lit.ibl->Bind(5, 6, 7);
        m_pbr->SetInt("uIrradiance", 5);
        m_pbr->SetInt("uPrefilter", 6);
        m_pbr->SetInt("uBrdfLUT", 7);
        m_pbr->SetFloat("uMaxReflectionLod", lit.ibl->MaxReflectionLod());
        m_pbr->SetFloat("uGlobalIblIntensity", std::max(lit.globalIblIntensity, 0.0f));
        m_pbr->SetFloat("uGlobalReflectionIntensity", std::max(lit.globalReflectionIntensity, 0.0f));
    }
    // Fog.
    m_pbr->SetInt("uFogEnabled", lit.fog ? 1 : 0);
    m_pbr->SetVec3("uFogColor", lit.fogColor);
    m_pbr->SetFloat("uFogDensity", lit.fogDensity);
    m_pbr->SetFloat("uFogHeight", lit.fogHeight);
    m_pbr->SetFloat("uFogHeightFalloff", lit.fogHeightFalloff);
    m_pbr->SetInt("uSkylightOcclusion", lit.skylightOcclusion ? 1 : 0);
    m_pbr->SetFloat("uSkylightOcclusionStrength", lit.skylightOcclusionStrength);
    m_pbr->SetFloat("uMinimumSkylight", lit.minimumSkylight);
    m_pbr->SetInt("uUseLightingGrid", lit.lightingGrid && lit.lightingGrid->Valid() ? 1 : 0);
    m_pbr->SetInt("uProbeVisibilityWeighting",lit.probeVisibilityWeighting?1:0);
    m_pbr->SetFloat("uProbeVisibilityMaxDistance",std::max(lit.probeVisibilityMaxDistance,0.001f));
    if (lit.lightingGrid && lit.lightingGrid->Valid()) {
        LightingProbeGrid::Contribution contribution=LightingProbeGrid::Contribution::Combined;
        if(lit.lightingDebugMode==13)contribution=LightingProbeGrid::Contribution::DirectEnvironment;
        else if(lit.lightingDebugMode==14)contribution=LightingProbeGrid::Contribution::Bounce;
        else if(lit.lightingDebugMode==15)contribution=LightingProbeGrid::Contribution::Emissive;
        else if(lit.lightingDebugMode==23)contribution=LightingProbeGrid::Contribution::HigherBounce;
        lit.lightingGrid->Bind(18,contribution);
        m_pbr->SetVec3("uLightingGridMin",lit.lightingGrid->BoundsMin());
        m_pbr->SetVec3("uLightingGridMax",lit.lightingGrid->BoundsMax());
        const glm::ivec3 dimensions=lit.lightingGrid->Dimensions();
        m_pbr->SetVec3("uLightingGridDimensions",glm::vec3(dimensions));
    }
    m_pbr->SetFloat("uSpecularOcclusionStrength",lit.specularOcclusionStrength);
    m_pbr->SetFloat("uLocalProbeInfluence",lit.localProbeInfluence);
    m_pbr->SetInt("uLightingDebugMode",lit.lightingDebugMode);
    m_pbr->SetInt("uUseSSAO",lit.ssao?1:0);
    if(lit.ssao){lit.ssao->BindAO(8);GLint textureUnits=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&textureUnits);
        if(textureUnits>26)lit.ssao->BindBentNormal(26);if(textureUnits>27)lit.ssao->BindRawAO(27);m_pbr->SetInt("uSsaoMap",8);
        m_pbr->SetInt("uUseBentNormal",textureUnits>26?1:0);
        m_pbr->SetMat4("uViewToWorld",glm::inverse(camera.ViewMatrix()));m_pbr->SetVec2("uScreenSize",lit.screenSize);}
    else m_pbr->SetInt("uUseBentNormal",0);
    if(lit.reflectionProbes)lit.reflectionProbes->BindBest(*m_pbr,camera.Position(),22);
    else m_pbr->SetInt("uReflectionProbeCount",0);
    m_pbr->SetInt("uCloudShadows", lit.cloudShadows ? 1 : 0);
    m_pbr->SetFloat("uCloudShadowStrength", lit.cloudShadowStrength);
    m_pbr->SetFloat("uCloudShadowScale", lit.cloudShadowScale);
    m_pbr->SetFloat("uCloudCoverage", lit.cloudCoverage);
    m_pbr->SetFloat("uCloudDensity", lit.cloudDensity);
    m_pbr->SetFloat("uCloudSoftness", lit.cloudSoftness);
    const float cloudAngle = glm::radians(lit.cloudWindDirectionDegrees);
    const float cloudSeconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pbr->SetVec2("uCloudWindOffset",
        glm::vec2(std::cos(cloudAngle), std::sin(cloudAngle))
        * (cloudSeconds * lit.cloudWindSpeed));

    struct PointData{glm::vec3 position,color;float radius;};
    struct SpotData{glm::vec3 position,direction,color;float inner,outer,range;};
    struct AreaData{glm::vec3 position,color,right,up;glm::vec2 halfSize;float radius;int shape,twoSided;};
    std::vector<PointData> points;std::vector<SpotData> spots;std::vector<AreaData> areas;
    auto lights=reg.view<ecs::Transform,ecs::Light>();
    if(!lights.empty())lights.each([&](ecs::Entity,ecs::Transform&t,ecs::Light&light){
        const glm::vec3 color=light.color*light.intensity;if(glm::dot(color,color)<=1e-8f)return;
        if(light.type==ecs::Light::Type::Point&&points.size()<32){const float radius=std::sqrt(std::max({color.r,color.g,color.b})/0.03f);points.push_back({t.position,color,radius});}
        else if(light.type==ecs::Light::Type::Spot&&spots.size()<4)spots.push_back({t.position,glm::normalize(light.direction),color,glm::cos(glm::radians(light.innerAngle)),glm::cos(glm::radians(light.outerAngle)),std::max(light.range,0.01f)});
        else if(light.type==ecs::Light::Type::Area&&areas.size()<4)areas.push_back({t.position,color,glm::normalize(t.rotation*glm::vec3(1,0,0)),glm::normalize(t.rotation*glm::vec3(0,1,0)),glm::vec2(std::max(light.areaWidth,0.01f),std::max(light.areaHeight,0.01f))*0.5f,std::max(light.sourceRadius,0.01f),light.areaShape==ecs::Light::AreaShape::Rectangle?1:0,light.areaTwoSided?1:0});
    });
    m_pbr->SetInt("uNumPoints",static_cast<int>(points.size()));
    for(std::size_t i=0;i<points.size();++i){const std::string index=std::to_string(i);m_pbr->SetVec4(("uPointPosRadius["+index+"]").c_str(),glm::vec4(points[i].position,points[i].radius));m_pbr->SetVec3(("uPointColor["+index+"]").c_str(),points[i].color);}
    m_pbr->SetInt("uNumSpots",static_cast<int>(spots.size()));
    for(std::size_t i=0;i<spots.size();++i){const std::string index=std::to_string(i);m_pbr->SetVec3(("uSpotPos["+index+"]").c_str(),spots[i].position);m_pbr->SetVec3(("uSpotDir["+index+"]").c_str(),spots[i].direction);m_pbr->SetVec3(("uSpotColor["+index+"]").c_str(),spots[i].color);m_pbr->SetFloat(("uSpotCosInner["+index+"]").c_str(),spots[i].inner);m_pbr->SetFloat(("uSpotCosOuter["+index+"]").c_str(),spots[i].outer);m_pbr->SetFloat(("uSpotRange["+index+"]").c_str(),spots[i].range);}
    GLint fragmentTextureUnits=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragmentTextureUnits);
    const bool ltcAvailable=fragmentTextureUnits>25;
    m_pbr->SetInt("uNumAreas",static_cast<int>(areas.size()));
    for(std::size_t i=0;i<areas.size();++i){const std::string index="["+std::to_string(i)+"]";m_pbr->SetVec3("uAreaPos"+index,areas[i].position);m_pbr->SetVec3("uAreaColor"+index,areas[i].color);m_pbr->SetFloat("uAreaRadius"+index,areas[i].radius);m_pbr->SetVec3("uAreaRight"+index,areas[i].right);m_pbr->SetVec3("uAreaUp"+index,areas[i].up);m_pbr->SetVec2("uAreaHalfSize"+index,areas[i].halfSize);m_pbr->SetInt("uAreaShape"+index,ltcAvailable?areas[i].shape:0);m_pbr->SetInt("uAreaTwoSided"+index,areas[i].twoSided);}
    if(ltcAvailable)m_ltcLut->Bind(24,25);

    // Backface culling: characters are closed meshes, so skip their inside faces.
    // Restored to the default (off) after the pass so other passes are unaffected.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Skinned meshes are considerably more expensive than static meshes. Use
    // one conservative model-space sphere to reject characters that cannot
    // contribute to the current camera view before uploading bones or binding
    // their materials. Shadow rendering has its own pass and is unaffected.
    const Frustum viewFrustum = ExtractFrustum(viewProj);

    auto animatedView = reg.view<ecs::Transform, AnimatedModel>();
    if (!animatedView.empty()) animatedView.each([&](ecs::Entity entity, ecs::Transform& t, AnimatedModel& am) {
        if (!am.model || am.pose.empty()) return;
        const glm::mat4 modelMatrix = t.Model() * am.renderOffset;
        const glm::mat4& boundsModel = modelMatrix;
        const glm::vec3 boundsCenter = glm::vec3(
            boundsModel * glm::vec4(am.model->Center(), 1.0f));
        const glm::vec3 scale = glm::abs(t.scale);
        const float boundsRadius = am.model->BoundingRadius()
            * std::max({scale.x, scale.y, scale.z});
        if (!SphereInFrustum(viewFrustum, boundsCenter, boundsRadius)) return;
        if (const ecs::LoadedMaterialAsset* custom =
                reg.TryGet<ecs::LoadedMaterialAsset>(entity);
            custom && custom->skinnedShader) {
            Shader& shader = *const_cast<Shader*>(custom->skinnedShader);
            shader.Bind();
            shader.SetMat4("uViewProjection", viewProj);
            shader.SetMat4("uModel", modelMatrix);
            shader.SetVec3("uCameraPosition", camera.Position());
            shader.SetFloat("uTime", cloudSeconds);
            shader.SetFloat("uDeltaTime", 1.0f / 60.0f);
            shader.SetVec3("uLightDirection", lit.sunDir);
            shader.SetFloat("uLightIntensity",
                std::max({lit.sunColor.x, lit.sunColor.y, lit.sunColor.z}));
            shader.SetVec4("uObjectColor",
                glm::vec4(custom->material.albedo, custom->material.opacity));
            UploadBones(shader, am.pose);
            UploadMaterialShaderParameters(shader, *custom);
            for (const SubMesh& submesh : am.model->SubMeshes())
                submesh.mesh.Draw();
            return;
        }
        m_pbr->SetMat4("uModel", modelMatrix);
        glFrontFace(glm::determinant(glm::mat3(modelMatrix)) < 0.0f ? GL_CW : GL_CCW);
        UploadBones(*m_pbr, am.pose);

        // An albedo override (a shared palette atlas, say) applies to every submesh.
        const Texture* boundAlbedo = nullptr;
        if (am.albedoOverride) {
            am.albedoOverride->Bind(0);
            boundAlbedo = am.albedoOverride;
            m_pbr->SetInt("uAlbedoMap", 0);
        }

        const auto& mats = am.model->Materials();
        const auto& texs = am.model->Textures();
        for (const SubMesh& sm : am.model->SubMeshes()) {
            glm::vec3 diffuse(0.8f), emissive(0.0f);
            int diffuseMap = -1, metalRoughMap = -1, emissiveMap = -1;
            float metallic = am.metallic, roughness = am.roughness, ao = 1.0f;
            if (sm.material >= 0 && sm.material < static_cast<int>(mats.size())) {
                const Material& m = mats[static_cast<std::size_t>(sm.material)];
                diffuse = m.diffuse; emissive = m.emissive; diffuseMap = m.diffuseMap;
                metallic = m.metallic; roughness = m.roughness; ao = m.ao;
                metalRoughMap = m.metalRoughMap;
                emissiveMap = m.emissiveMap;
            }
            m_pbr->SetVec3("uAlbedo", am.albedoOverride ? am.tint : (diffuse * am.tint));
            m_pbr->SetFloat("uMetallic", metallic);
            m_pbr->SetFloat("uRoughness", roughness);
            m_pbr->SetFloat("uAO", ao);
            m_pbr->SetVec3("uEmissive", emissive);
            if (am.albedoOverride) {
                m_pbr->SetInt("uHasAlbedoMap", 1);              // override bound above
            } else if (diffuseMap >= 0 && diffuseMap < static_cast<int>(texs.size()) && texs[static_cast<std::size_t>(diffuseMap)]) {
                const Texture* diffuseTexture = texs[static_cast<std::size_t>(diffuseMap)].get();
                if (diffuseTexture != boundAlbedo) {
                    diffuseTexture->Bind(0);
                    boundAlbedo = diffuseTexture;
                }
                m_pbr->SetInt("uAlbedoMap", 0);
                m_pbr->SetInt("uHasAlbedoMap", 1);
            } else {
                m_pbr->SetInt("uHasAlbedoMap", 0);
            }
            if (metalRoughMap >= 0
                && metalRoughMap < static_cast<int>(texs.size())
                && texs[static_cast<std::size_t>(metalRoughMap)]) {
                texs[static_cast<std::size_t>(metalRoughMap)]->Bind(2);
                m_pbr->SetInt("uMetalRoughMap", 2);
                m_pbr->SetInt("uHasMetalRoughMap", 1);
            } else {
                m_pbr->SetInt("uHasMetalRoughMap", 0);
            }
            if (emissiveMap >= 0
                && emissiveMap < static_cast<int>(texs.size())
                && texs[static_cast<std::size_t>(emissiveMap)]) {
                texs[static_cast<std::size_t>(emissiveMap)]->Bind(16);
                m_pbr->SetInt("uEmissiveMap", 16);
                m_pbr->SetInt("uHasEmissiveMap", 1);
            } else {
                m_pbr->SetInt("uHasEmissiveMap", 0);
            }
            sm.mesh.Draw();
        }
    });

    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);   // restore the default (off) for subsequent passes
}

void SkinnedRenderer::DrawDepth(const SkinnedModel& model,
                                const std::vector<glm::mat4>& bones,
                                const glm::mat4& modelMatrix, const glm::mat4& lightVP) {
    m_depth->Bind();
    m_depth->SetMat4("uLightVP", lightVP);
    m_depth->SetMat4("uModel", modelMatrix);
    UploadBones(*m_depth, bones);
    for (const SubMesh& sm : model.SubMeshes()) sm.mesh.Draw();
}

void SkinnedRenderer::DrawSceneDepth(ecs::Registry& reg, const glm::mat4& lightVP) {
    m_depth->Bind();
    m_depth->SetMat4("uLightVP", lightVP);
    // Reject animated characters outside the light's orthographic/frustum volume
    // before uploading their bone palette. This is especially important for
    // large scenes where only a fraction of characters can cast into a cascade.
    const Frustum lightFrustum = ExtractFrustum(lightVP);
    auto animatedView = reg.view<ecs::Transform, AnimatedModel>();
    if (!animatedView.empty()) animatedView.each([&](ecs::Entity, ecs::Transform& t, AnimatedModel& am) {
        if (!am.model || am.pose.empty() || !am.castShadow) return;
        const glm::mat4 modelMatrix = t.Model() * am.renderOffset;
        const glm::vec3 boundsCenter = glm::vec3(
            modelMatrix * glm::vec4(am.model->Center(), 1.0f));
        const glm::vec3 scale = glm::abs(t.scale);
        const float boundsRadius = am.model->BoundingRadius()
            * std::max({scale.x, scale.y, scale.z});
        if (!SphereInFrustum(lightFrustum, boundsCenter, boundsRadius)) return;
        m_depth->SetMat4("uModel", modelMatrix);
        UploadBones(*m_depth, am.pose);
        for (const SubMesh& sm : am.model->SubMeshes()) sm.mesh.Draw();
    });
}

} // namespace engine
