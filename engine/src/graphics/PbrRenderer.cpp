#include "engine/graphics/PbrRenderer.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/Camera.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

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
uniform mat4 uLightSpace;
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vLightSpacePos;
void main() {
    vec4 world     = uModel * vec4(aPos, 1.0);
    vWorldPos      = world.xyz;
    vNormal        = normalize(uNormalMat * aNormal);
    vLightSpacePos = uLightSpace * world;
    gl_Position    = uViewProj * world;
}
)GLSL";

const char* kPbrFrag = R"GLSL(
#version 330 core
const float PI = 3.14159265359;
const int   MAX_POINTS = 16;
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vLightSpacePos;
out vec4 FragColor;
uniform vec3  uViewPos;
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3  uEmissive;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uAmbient;
uniform int   uNumPoints;
uniform vec3  uPointPos[MAX_POINTS];
uniform vec3  uPointColor[MAX_POINTS];
uniform sampler2D uShadowMap;
uniform int uApplyTonemap;
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
float ShadowFactor(vec4 lp, float NdotL) {
    vec3 p = lp.xyz/lp.w*0.5+0.5;
    if (p.z > 1.0) return 0.0;
    float bias = max(0.0025*(1.0-NdotL),0.0008);
    float s = 0.0; vec2 texel = 1.0/vec2(textureSize(uShadowMap,0));
    for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) {
        float c = texture(uShadowMap, p.xy+vec2(x,y)*texel).r;
        s += (p.z-bias > c) ? 1.0 : 0.0;
    }
    return s/9.0;
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
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos-vWorldPos);
    vec3 F0 = mix(vec3(0.04), uAlbedo, uMetallic);
    vec3 Lo = vec3(0.0);
    vec3 Ls = normalize(-uSunDir);
    float sunNdotL = max(dot(N,Ls),0.0);
    float shadow = ShadowFactor(vLightSpacePos, sunNdotL);
    Lo += (1.0-shadow)*Lighting(N,V,Ls,uSunColor,uAlbedo,F0,uMetallic,uRoughness);
    for (int i=0;i<uNumPoints && i<MAX_POINTS;++i) {
        vec3 d = uPointPos[i]-vWorldPos;
        float dist2 = dot(d,d);
        vec3 L = d/sqrt(dist2);
        vec3 radiance = uPointColor[i]/max(dist2,0.0001);
        Lo += Lighting(N,V,L,radiance,uAlbedo,F0,uMetallic,uRoughness);
    }
    vec3 color = uAmbient*uAlbedo*uAO + Lo + uEmissive;
    if (uApplyTonemap == 1) {
        color = color/(color+vec3(1.0));
        color = pow(color, vec3(1.0/2.2));
    }
    FragColor = vec4(color,1.0);
}
)GLSL";

const char* kDepthVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightSpace;
void main() { gl_Position = uLightSpace*uModel*vec4(aPos,1.0); }
)GLSL";

const char* kDepthFrag = R"GLSL(
#version 330 core
void main() { }
)GLSL";

} // namespace

PbrRenderer::PbrRenderer(int shadowSize)
    : m_shadow(shadowSize),
      m_pbr(std::make_unique<Shader>(kPbrVert, kPbrFrag)),
      m_depth(std::make_unique<Shader>(kDepthVert, kDepthFrag)) {}

PbrRenderer::~PbrRenderer() = default;

void PbrRenderer::Render(ecs::Registry& reg, const Camera& camera, float aspect,
                         int screenWidth, int screenHeight) {
    Render(reg, camera, aspect, screenWidth, screenHeight, Options());                      
}

void PbrRenderer::Render(ecs::Registry& reg, const Camera& camera, float aspect,
                         int screenWidth, int screenHeight, const Options& opt) {
    // Gather lights.
    glm::vec3 sunDir(0.0f, -1.0f, 0.0f), sunColor(0.0f);
    std::vector<glm::vec3> ppos, pcol;
    reg.view<Transform, Light>().each([&](Entity, Transform& t, Light& l) {
        const glm::vec3 c = l.color * l.intensity;
        if (l.type == Light::Type::Directional) { sunDir = glm::normalize(l.direction); sunColor = c; }
        else { ppos.push_back(t.position); pcol.push_back(c); }
    });

    // Shadow frustum: explicit, or auto-fitted to the MeshPBR entities.
    glm::vec3 center = opt.shadowCenter;
    float radius = opt.shadowRadius;
    if (radius <= 0.0f) {
        glm::vec3 lo(1e30f), hi(-1e30f);
        bool any = false;
        reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR&) {
            lo = glm::min(lo, t.position); hi = glm::max(hi, t.position); any = true;
        });
        if (any) { center = (lo + hi) * 0.5f; radius = glm::length(hi - lo) * 0.5f + 3.0f; }
        else { center = glm::vec3(0.0f); radius = 16.0f; }
    }
    const glm::mat4 lightSpace = ShadowMap::LightSpaceMatrix(sunDir, center, radius);

    // Shadow depth pass.
    m_depth->Bind();
    m_depth->SetMat4("uLightSpace", lightSpace);
    m_shadow.BeginDepthPass();
    reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR& m) {
        if (!m.mesh) return;
        m_depth->SetMat4("uModel", t.Model());
        m.mesh->Draw();
    });
    m_shadow.EndDepthPass(screenWidth, screenHeight);

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
    m_pbr->SetMat4("uLightSpace", lightSpace);
    m_pbr->SetInt("uNumPoints", static_cast<int>(ppos.size()));
    for (std::size_t i = 0; i < ppos.size(); ++i) {
        const std::string idx = "[" + std::to_string(i) + "]";
        m_pbr->SetVec3("uPointPos" + idx, ppos[i]);
        m_pbr->SetVec3("uPointColor" + idx, pcol[i]);
    }
    m_shadow.BindDepthTexture(4);
    m_pbr->SetInt("uShadowMap", 4);

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
        m.mesh->Draw();
    });
}

} // namespace engine