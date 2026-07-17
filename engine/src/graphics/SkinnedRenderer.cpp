#include "engine/graphics/SkinnedRenderer.h"

#include "engine/graphics/SkinnedModel.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/IBL.h"
#include "engine/animation/AnimatedModel.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <algorithm>
#include <sstream>

namespace engine {
namespace {

std::vector<float> ShaderParameterNumbers(std::string value) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::replace(value.begin(), value.end(), '(', ' ');
    std::replace(value.begin(), value.end(), ')', ' ');
    std::istringstream input(value);
    std::vector<float> values;
    float number = 0.0f;
    while (input >> number) values.push_back(number);
    return values;
}

void UploadMaterialShaderParameters(Shader& shader, const ecs::LoadedMaterialAsset& material) {
    int textureUnit = 18;
    for (const auto& entry : material.shaderParameters) {
        const std::string uniform = "u_" + entry.first;
        const auto type = material.shaderParameterTypes.find(entry.first);
        const int valueType = type == material.shaderParameterTypes.end() ? 0 : type->second;
        if (valueType == 7) {
            const auto texture = material.shaderTextures.find(entry.first);
            if (texture != material.shaderTextures.end() && texture->second) {
                texture->second->Bind(static_cast<unsigned int>(textureUnit));
                shader.SetInt(uniform, textureUnit++);
            }
            continue;
        }
        const auto values = ShaderParameterNumbers(entry.second);
        if (valueType == 1 || valueType == 2)
            shader.SetInt(uniform, entry.second == "true" ? 1
                : values.empty() ? 0 : static_cast<int>(values[0]));
        else if (valueType == 3 && values.size() >= 2)
            shader.SetVec2(uniform, glm::vec2(values[0], values[1]));
        else if (valueType == 4 && values.size() >= 3)
            shader.SetVec3(uniform, glm::vec3(values[0], values[1], values[2]));
        else if ((valueType == 5 || valueType == 6) && values.size() >= 4)
            shader.SetVec4(uniform, glm::vec4(values[0], values[1], values[2], values[3]));
        else shader.SetFloat(uniform, values.empty() ? 0.0f : values[0]);
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

// Cook-Torrance fragment matching PbrRenderer's sun + cascade shadows +
// ambient/IBL + fog (no clustered/spot/area lights -- characters use the sun).
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
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uAmbient;
uniform sampler2DArray uCascadeMaps;
uniform mat4  uCascadeVP[4];
uniform float uCascadeSplits[4];
uniform mat4  uView;
uniform float uShadowSoftness;
uniform int   uHasShadow;
uniform int   uUseIBL;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D   uBrdfLUT;
uniform float uMaxReflectionLod;
uniform int   uApplyTonemap;
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
    float blockerSum = 0.0; int blockers = 0;
    for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) {
        float d = texture(uCascadeMaps, vec3(p.xy + vec2(x, y) * texel * uShadowSoftness, float(layer))).r;
        if (d < cur - bias) { blockerSum += d; ++blockers; }
    }
    if (blockers == 0) return 0.0;
    float avgBlocker = blockerSum / float(blockers);
    float penumbra = (cur - avgBlocker) * uShadowSoftness * 300.0;
    float radius = clamp(penumbra, 1.0, 16.0);
    float s = 0.0;
    for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) {
        float d = texture(uCascadeMaps, vec3(p.xy + vec2(x, y) * texel * radius, float(layer))).r;
        s += (cur - bias > d) ? 1.0 : 0.0;
    }
    return s / 25.0;
}
vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
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
void main() {
    vec3 albedo = uAlbedo;
    if (uHasAlbedoMap == 1) albedo *= pow(texture(uAlbedoMap, vUV).rgb, vec3(2.2));  // sRGB -> linear
    float metallic = uMetallic, roughness = uRoughness, ao = uAO;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Ls = normalize(-uSunDir);
    float sunNdotL = max(dot(N, Ls), 0.0);
    float shadow = (uHasShadow == 1) ? ShadowFactor(sunNdotL) : 0.0;
    vec3 Lo = (1.0 - shadow) * Lighting(N, V, Ls, uSunColor, albedo, F0, metallic, roughness);
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
    vec3 color = ambient + Lo + uEmissive;
    if (uFogEnabled == 1) {
        float dist = length(uViewPos - vWorldPos);
        float distFog = 1.0 - exp(-dist * uFogDensity);
        float heightF = clamp(exp(-(vWorldPos.y - uFogHeight) * uFogHeightFalloff), 0.0, 1.0);
        float fog = clamp(distFog * heightF, 0.0, 1.0);
        color = mix(color, uFogColor, fog);
    }
    if (uApplyTonemap == 1) {
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
    for (std::size_t i = 0; i < n; ++i)
        sh.SetMat4("uBones[" + std::to_string(i) + "]", bones[i]);
}

} // namespace

SkinnedRenderer::SkinnedRenderer()
    : m_shader(std::make_unique<Shader>(kVert, kFrag)),
      m_pbr(std::make_unique<Shader>(kVert, kPbrFrag)),
      m_depth(std::make_unique<Shader>(kDepthVert, kDepthFrag)) {}

SkinnedRenderer::~SkinnedRenderer() = default;

void SkinnedRenderer::Draw(const SkinnedModel& model,
                           const std::vector<glm::mat4>& bones,
                           const glm::mat4& modelMatrix,
                           const Camera& camera, float aspect,
                           const glm::vec3& sunDir, const glm::vec3& sunColor,
                           const glm::vec3& ambient) {
    m_shader->Bind();
    m_shader->SetMat4("uViewProj", camera.ProjectionMatrix(aspect) * camera.ViewMatrix());
    m_shader->SetMat4("uModel", modelMatrix);
    m_shader->SetVec3("uViewPos", camera.Position());
    m_shader->SetVec3("uSunDir", sunDir);
    m_shader->SetVec3("uSunColor", sunColor);
    m_shader->SetVec3("uAmbient", ambient);
    UploadBones(*m_shader, bones);

    const auto& mats = model.Materials();
    const auto& texs = model.Textures();
    for (const SubMesh& sm : model.SubMeshes()) {
        glm::vec3 color(0.8f), specular(0.2f), emissive(0.0f);
        float shininess = 32.0f;
        int diffuseMap = -1;
        if (sm.material >= 0 && sm.material < static_cast<int>(mats.size())) {
            const Material& m = mats[static_cast<std::size_t>(sm.material)];
            color = m.diffuse; specular = m.specular; emissive = m.emissive;
            shininess = m.shininess; diffuseMap = m.diffuseMap;
        }
        m_shader->SetVec3("uColor", color);
        m_shader->SetVec3("uSpecular", specular);
        m_shader->SetVec3("uEmissive", emissive);
        m_shader->SetFloat("uShininess", shininess);
        if (diffuseMap >= 0 && diffuseMap < static_cast<int>(texs.size()) && texs[static_cast<std::size_t>(diffuseMap)]) {
            texs[static_cast<std::size_t>(diffuseMap)]->Bind(0);
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

    m_pbr->Bind();
    m_pbr->SetMat4("uViewProj", camera.ProjectionMatrix(aspect) * camera.ViewMatrix());
    m_pbr->SetMat4("uView", camera.ViewMatrix());
    m_pbr->SetVec3("uViewPos", camera.Position());
    m_pbr->SetVec3("uSunDir", lit.sunDir);
    m_pbr->SetVec3("uSunColor", lit.sunColor);
    m_pbr->SetVec3("uAmbient", lit.ambient);
    m_pbr->SetInt("uApplyTonemap", lit.tonemap ? 1 : 0);
    m_pbr->SetFloat("uShadowSoftness", lit.shadowSoftness);

    // Sun (cascade) shadows -- same texture array + matrices the static PBR pass built.
    lit.cascade->BindArray(4);
    m_pbr->SetInt("uCascadeMaps", 4);
    m_pbr->SetInt("uHasShadow", 1);
    for (int i = 0; i < CascadedShadow::kCascades; ++i) {
        const std::string ix = "[" + std::to_string(i) + "]";
        m_pbr->SetMat4("uCascadeVP" + ix, lit.cascade->CascadeVP(i));
        m_pbr->SetFloat("uCascadeSplits" + ix, lit.cascade->SplitDepth(i));
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
    }
    // Fog.
    m_pbr->SetInt("uFogEnabled", lit.fog ? 1 : 0);
    m_pbr->SetVec3("uFogColor", lit.fogColor);
    m_pbr->SetFloat("uFogDensity", lit.fogDensity);
    m_pbr->SetFloat("uFogHeight", lit.fogHeight);
    m_pbr->SetFloat("uFogHeightFalloff", lit.fogHeightFalloff);

    reg.view<ecs::Transform, AnimatedModel>().each([&](ecs::Entity entity, ecs::Transform& t, AnimatedModel& am) {
        if (!am.model || am.pose.empty()) return;
        if (const ecs::LoadedMaterialAsset* custom =
                reg.TryGet<ecs::LoadedMaterialAsset>(entity);
            custom && custom->skinnedShader) {
            Shader& shader = *const_cast<Shader*>(custom->skinnedShader);
            shader.Bind();
            shader.SetMat4("uViewProjection",
                camera.ProjectionMatrix(aspect) * camera.ViewMatrix());
            shader.SetMat4("uModel", t.Model());
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
        m_pbr->SetMat4("uModel", t.Model());
        UploadBones(*m_pbr, am.pose);

        // An albedo override (a shared palette atlas, say) applies to every submesh.
        if (am.albedoOverride) { am.albedoOverride->Bind(0); m_pbr->SetInt("uAlbedoMap", 0); }

        const auto& mats = am.model->Materials();
        const auto& texs = am.model->Textures();
        for (const SubMesh& sm : am.model->SubMeshes()) {
            glm::vec3 diffuse(0.8f), emissive(0.0f);
            int diffuseMap = -1;
            if (sm.material >= 0 && sm.material < static_cast<int>(mats.size())) {
                const Material& m = mats[static_cast<std::size_t>(sm.material)];
                diffuse = m.diffuse; emissive = m.emissive; diffuseMap = m.diffuseMap;
            }
            m_pbr->SetVec3("uAlbedo", am.albedoOverride ? am.tint : (diffuse * am.tint));
            m_pbr->SetFloat("uMetallic", am.metallic);
            m_pbr->SetFloat("uRoughness", am.roughness);
            m_pbr->SetFloat("uAO", 1.0f);
            m_pbr->SetVec3("uEmissive", emissive);
            if (am.albedoOverride) {
                m_pbr->SetInt("uHasAlbedoMap", 1);              // override bound above
            } else if (diffuseMap >= 0 && diffuseMap < static_cast<int>(texs.size()) && texs[static_cast<std::size_t>(diffuseMap)]) {
                texs[static_cast<std::size_t>(diffuseMap)]->Bind(0);
                m_pbr->SetInt("uAlbedoMap", 0);
                m_pbr->SetInt("uHasAlbedoMap", 1);
            } else {
                m_pbr->SetInt("uHasAlbedoMap", 0);
            }
            sm.mesh.Draw();
        }
    });
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
    reg.view<ecs::Transform, AnimatedModel>().each([&](ecs::Entity, ecs::Transform& t, AnimatedModel& am) {
        if (!am.model || am.pose.empty() || !am.castShadow) return;
        m_depth->SetMat4("uModel", t.Model());
        UploadBones(*m_depth, am.pose);
        for (const SubMesh& sm : am.model->SubMeshes()) sm.mesh.Draw();
    });
}

} // namespace engine
