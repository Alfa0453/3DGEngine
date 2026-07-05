#include "engine/graphics/SkinnedRenderer.h"

#include "engine/graphics/SkinnedModel.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Texture.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace engine {
namespace {

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
    color = color / (color + vec3(1.0));      // Reinhard tone map
    color = pow(color, vec3(1.0 / 2.2));       // gamma
    FragColor = vec4(color, 1.0);
}
)GLSL";

} // namespace

SkinnedRenderer::SkinnedRenderer()
    : m_shader(std::make_unique<Shader>(kVert, kFrag)) {}

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

    const std::size_t n = (bones.size() < static_cast<std::size_t>(kMaxBones))
                          ? bones.size() : static_cast<std::size_t>(kMaxBones);
    for (std::size_t i = 0; i < n; ++i)
        m_shader->SetMat4("uBones[" + std::to_string(i) + "]", bones[i]);

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

} // namespace engine
