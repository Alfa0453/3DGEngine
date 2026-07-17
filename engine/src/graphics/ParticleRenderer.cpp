#include "engine/graphics/ParticleRenderer.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/ParticleSystem.h"
#include "engine/graphics/GpuParticleSystem.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Primitives.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <algorithm>
#include <vector>
#include <sstream>

namespace engine {
namespace {

const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aCorner;   // unit quad corner in [-0.5,0.5]
layout (location = 1) in vec3 iCenter;   // per-particle world centre
layout (location = 2) in float iSize;
layout (location = 3) in vec4 iColor;
layout (location = 4) in float iRotation;
layout (location = 5) in float iFrame;
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
out vec2 vUV;
out vec4 vColor;
flat out float vFrame;
void main() {
    float c = cos(iRotation), s = sin(iRotation);
    vec2 corner = mat2(c, -s, s, c) * aCorner;
    vec3 world = iCenter + (corner.x * uCamRight + corner.y * uCamUp) * iSize;
    gl_Position = uViewProj * vec4(world, 1.0);
    vUV = aCorner + 0.5;
    vColor = iColor;
    vFrame = iFrame;
}
)GLSL";

const char* kFrag = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
flat in float vFrame;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform int uUseTexture;
uniform int uColumns;
uniform int uRows;
uniform int uLoopFrames;
void main() {
    if (uUseTexture != 0) {
        int count = max(uColumns * uRows, 1);
        int frame = int(floor(vFrame));
        frame = uLoopFrames != 0 ? frame % count : min(frame, count - 1);
        int column = frame % max(uColumns, 1);
        int row = frame / max(uColumns, 1);
        vec2 cell = vec2(1.0 / float(max(uColumns, 1)), 1.0 / float(max(uRows, 1)));
        vec2 uv = (vec2(float(column), float(max(uRows, 1) - 1 - row)) + vUV) * cell;
        vec4 texel = texture(uTexture, uv);
        FragColor = vec4(texel.rgb * vColor.rgb, texel.a * vColor.a);
        return;
    }
    // Soft round sprite: radial falloff, squared for a smoother core.
    float d = length(vUV - vec2(0.5)) * 2.0;
    float a = clamp(1.0 - d, 0.0, 1.0);
    a *= a;
    FragColor = vec4(vColor.rgb, vColor.a * a);
}
)GLSL";

const char* kTrailVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
    gl_Position = uViewProj * vec4(aPosition, 1.0);
    vColor = aColor;
}
)GLSL";

const char* kTrailFrag = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)GLSL";

const char* kMeshVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
}
)GLSL";

const char* kMeshFrag = R"GLSL(
#version 330 core
in vec3 vNormal;
out vec4 FragColor;
uniform vec4 uColor;
uniform vec3 uLightDir;
void main() {
    float light = 0.28 + 0.72 * max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    FragColor = vec4(uColor.rgb * light, uColor.a);
}
)GLSL";

std::vector<float> ShaderNumbers(std::string value) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::replace(value.begin(), value.end(), '(', ' ');
    std::replace(value.begin(), value.end(), ')', ' ');
    std::istringstream input(value);
    std::vector<float> result;
    float number = 0.0f;
    while (input >> number) result.push_back(number);
    return result;
}

void UploadParticleShaderParameters(Shader& shader, const EmitterConfig& config) {
    int textureUnit = 4;
    for (const ParticleShaderParameter& parameter : config.shaderParameters) {
        const std::string uniform = "u_" + parameter.name;
        if (parameter.type == 7) {
            const auto texture = config.shaderTextures.find(parameter.name);
            if (texture != config.shaderTextures.end() && texture->second) {
                texture->second->Bind(static_cast<unsigned int>(textureUnit));
                shader.SetInt(uniform, textureUnit++);
            }
            continue;
        }
        const auto values = ShaderNumbers(parameter.value);
        if (parameter.type == 1 || parameter.type == 2)
            shader.SetInt(uniform, parameter.value == "true" ? 1
                : values.empty() ? 0 : static_cast<int>(values[0]));
        else if (parameter.type == 3 && values.size() >= 2)
            shader.SetVec2(uniform, {values[0], values[1]});
        else if (parameter.type == 4 && values.size() >= 3)
            shader.SetVec3(uniform, {values[0], values[1], values[2]});
        else if ((parameter.type == 5 || parameter.type == 6)
                 && values.size() >= 4)
            shader.SetVec4(
                uniform, {values[0], values[1], values[2], values[3]});
        else shader.SetFloat(
            uniform, values.empty() ? 0.0f : values[0]);
    }
}

} // namespace

ParticleRenderer::ParticleRenderer()
    : m_shader(std::make_unique<Shader>(kVert, kFrag)),
      m_trailShader(std::make_unique<Shader>(kTrailVert, kTrailFrag)) {
    m_meshShader = std::make_unique<Shader>(kMeshVert, kMeshFrag);
    m_particleCube = std::make_unique<Mesh>(primitives::Cube());
    m_particleSphere = std::make_unique<Mesh>(primitives::Sphere(12));
    m_particleCone = std::make_unique<Mesh>(primitives::Cone(16));
    m_particleCylinder = std::make_unique<Mesh>(primitives::Cylinder(16));
    const float quad[] = {
        -0.5f, -0.5f,   0.5f, -0.5f,   0.5f, 0.5f,
        -0.5f, -0.5f,   0.5f,  0.5f,  -0.5f, 0.5f,
    };
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVBO);
    glGenBuffers(1, &m_instanceVBO);
    glGenQueries(1, &m_timerQuery);
    glGenVertexArrays(1, &m_trailVao);
    glGenBuffers(1, &m_trailVbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

ParticleRenderer::~ParticleRenderer() {
    if (m_trailVbo)   glDeleteBuffers(1, &m_trailVbo);
    if (m_trailVao)   glDeleteVertexArrays(1, &m_trailVao);
    if (m_timerQuery)  glDeleteQueries(1, &m_timerQuery);
    if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);
    if (m_quadVBO)     glDeleteBuffers(1, &m_quadVBO);
    if (m_vao)         glDeleteVertexArrays(1, &m_vao);
}

void ParticleRenderer::Draw(const ParticleEmitter& emitter, const Camera& camera, float aspect) {
    const auto cpuStart = std::chrono::steady_clock::now();
    const auto& ps = emitter.Particles();
    if (ps.empty()) return;

    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    if (emitter.cfg.cullingEnabled) {
        const glm::vec4 clip = proj * view * glm::vec4(emitter.position, 1.0f);
        const float radius = std::max(emitter.cfg.boundsRadius, 0.01f)
            * std::max(std::abs(proj[0][0]), std::abs(proj[1][1]));
        if (clip.w <= 0.0f || clip.x + radius < -clip.w || clip.x - radius > clip.w
            || clip.y + radius < -clip.w || clip.y - radius > clip.w
            || clip.z + radius < -clip.w || clip.z - radius > clip.w) {
            ++m_stats.culledEmitters;
            m_stats.cpuMilliseconds += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - cpuStart).count();
            return;
        }
    }
    std::vector<const Particle*> ordered;
    ordered.reserve(ps.size());
    for (const Particle& particle : ps) ordered.push_back(&particle);
    if (emitter.cfg.blend == ParticleBlend::Alpha) {
        const glm::vec3 cameraPosition = camera.Position();
        std::stable_sort(ordered.begin(), ordered.end(),
            [&cameraPosition](const Particle* a, const Particle* b) {
                const glm::vec3 da = a->pos - cameraPosition;
                const glm::vec3 db = b->pos - cameraPosition;
                return glm::dot(da, da) > glm::dot(db, db);
            });
    }

    if (emitter.cfg.renderMode == ParticleRenderMode::Mesh) {
        const Mesh* primitive = m_particleCube.get();
        if (emitter.cfg.meshShape == ParticleMeshShape::Sphere) primitive = m_particleSphere.get();
        else if (emitter.cfg.meshShape == ParticleMeshShape::Cone) primitive = m_particleCone.get();
        else if (emitter.cfg.meshShape == ParticleMeshShape::Cylinder) primitive = m_particleCylinder.get();
        const Model* model = nullptr;
        if (emitter.cfg.meshShape == ParticleMeshShape::Model && !emitter.cfg.meshPath.empty()) {
            auto found = m_models.find(emitter.cfg.meshPath);
            if (found != m_models.end()) model = found->second.get();
            else if (m_failedTextures.find("mesh:" + emitter.cfg.meshPath) == m_failedTextures.end()) {
                try {
                    auto loaded = std::make_unique<Model>(Model::FromFile(emitter.cfg.meshPath));
                    model = loaded.get();
                    m_models.emplace(emitter.cfg.meshPath, std::move(loaded));
                } catch (...) {
                    m_failedTextures.insert("mesh:" + emitter.cfg.meshPath);
                }
            }
        }
        m_meshShader->Bind();
        m_meshShader->SetMat4("uViewProj", proj * view);
        m_meshShader->SetVec3("uLightDir", glm::vec3(-0.4f, -1.0f, -0.3f));
        glEnable(GL_BLEND);
        if (emitter.cfg.blend == ParticleBlend::Additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        if (m_timerQuery) glBeginQuery(GL_TIME_ELAPSED, m_timerQuery);
        for (const Particle* particle : ordered) {
            const Particle& p = *particle;
            glm::mat4 orientation(1.0f);
            if (emitter.cfg.meshAlignToVelocity && glm::dot(p.vel, p.vel) > 1.0e-8f) {
                const glm::vec3 up = glm::normalize(p.vel);
                const glm::vec3 reference = std::abs(up.y) < 0.98f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 right = glm::normalize(glm::cross(reference, up));
                const glm::vec3 forward = glm::normalize(glm::cross(up, right));
                orientation = glm::mat4(glm::mat3(right, up, forward));
            }
            glm::mat4 world = glm::translate(glm::mat4(1.0f), p.pos) * orientation;
            world = glm::rotate(world, p.rotation, glm::vec3(0, 1, 0));
            world = glm::scale(world, glm::vec3(std::max(p.size * emitter.cfg.meshScale, 0.0001f)));
            m_meshShader->SetMat4("uModel", world);
            m_meshShader->SetVec4("uColor", p.color);
            if (model) {
                for (const SubMesh& subMesh : model->SubMeshes()) {
                    subMesh.mesh.Draw();
                    ++m_stats.drawCalls;
                }
            } else if (primitive) {
                primitive->Draw();
                ++m_stats.drawCalls;
            }
        }
        if (m_timerQuery) {
            glEndQuery(GL_TIME_ELAPSED);
            GLint available = GL_FALSE;
            glGetQueryObjectiv(m_timerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE) {
                GLuint64 elapsedNanoseconds = 0;
                glGetQueryObjectui64v(m_timerQuery, GL_QUERY_RESULT, &elapsedNanoseconds);
                m_stats.gpuMilliseconds += static_cast<double>(elapsedNanoseconds) / 1000000.0;
            }
        }
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
        m_stats.particles += ps.size();
        m_stats.cpuMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cpuStart).count();
        return;
    }

    // Pack per-instance data: center, size, color, rotation, flipbook frame.
    std::vector<float> inst;
    inst.reserve(ps.size() * 14);
    for (const Particle* particle : ordered) {
        const Particle& p = *particle;
        inst.push_back(p.pos.x); inst.push_back(p.pos.y); inst.push_back(p.pos.z);
        inst.push_back(p.size);
        inst.push_back(p.color.r); inst.push_back(p.color.g); inst.push_back(p.color.b); inst.push_back(p.color.a);
        inst.push_back(p.rotation); inst.push_back(p.frame);
        inst.push_back(p.vel.x); inst.push_back(p.vel.y); inst.push_back(p.vel.z);
        inst.push_back(p.life > 0.0f
            ? std::clamp(p.age / p.life, 0.0f, 1.0f) : 1.0f);
    }

    // Camera right/up from the view matrix (rows) for billboarding.
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);

    std::vector<float> trailVertices;
    if (emitter.cfg.trailsEnabled && emitter.cfg.trailWidth > 0.0f) {
        for (const Particle* particle : ordered) {
            const Particle& p = *particle;
            const int count = std::clamp(p.trailCount, 0, std::clamp(emitter.cfg.trailSegments, 2, 16));
            for (int i = 0; i < count; ++i) {
                const glm::vec3 a = i == 0 ? p.pos : p.trailPositions[static_cast<std::size_t>(i - 1)];
                const glm::vec3 b = p.trailPositions[static_cast<std::size_t>(i)];
                const glm::vec3 segment = b - a;
                if (glm::dot(segment, segment) < 1.0e-8f) continue;
                const glm::vec3 direction = glm::normalize(segment);
                glm::vec3 toCamera = camera.Position() - (a + b) * 0.5f;
                if (glm::dot(toCamera, toCamera) < 1.0e-8f) toCamera = -camera.Front();
                glm::vec3 side = glm::cross(direction, glm::normalize(toCamera));
                if (glm::dot(side, side) < 1.0e-8f) side = camRight;
                else side = glm::normalize(side);
                const float t0 = static_cast<float>(i) / static_cast<float>(std::max(count, 1));
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(std::max(count, 1));
                const float w0 = emitter.cfg.trailWidth * (1.0f - t0) * 0.5f;
                const float w1 = emitter.cfg.trailWidth * (1.0f - t1) * 0.5f;
                glm::vec4 c0 = p.color;
                glm::vec4 c1 = p.color;
                c0.a *= emitter.cfg.trailOpacity * (1.0f - t0);
                c1.a *= emitter.cfg.trailOpacity * (1.0f - t1);
                const glm::vec3 vertices[] = {a + side * w0, a - side * w0, b - side * w1,
                                              a + side * w0, b - side * w1, b + side * w1};
                const glm::vec4 colors[] = {c0, c0, c1, c0, c1, c1};
                for (int vertex = 0; vertex < 6; ++vertex) {
                    trailVertices.push_back(vertices[vertex].x);
                    trailVertices.push_back(vertices[vertex].y);
                    trailVertices.push_back(vertices[vertex].z);
                    trailVertices.push_back(colors[vertex].r);
                    trailVertices.push_back(colors[vertex].g);
                    trailVertices.push_back(colors[vertex].b);
                    trailVertices.push_back(colors[vertex].a);
                }
            }
        }
    }

    Shader* billboardShader = emitter.cfg.customShader
        ? const_cast<Shader*>(emitter.cfg.customShader) : m_shader.get();
    billboardShader->Bind();
    if (emitter.cfg.customShader) {
        billboardShader->SetMat4("uViewProjection", proj * view);
        billboardShader->SetVec3("uCameraRight", camRight);
        billboardShader->SetVec3("uCameraUp", camUp);
        UploadParticleShaderParameters(*billboardShader, emitter.cfg);
    } else {
        billboardShader->SetMat4("uViewProj", proj * view);
        billboardShader->SetVec3("uCamRight", camRight);
        billboardShader->SetVec3("uCamUp", camUp);
    }

    const Texture* texture = nullptr;
    if (!emitter.cfg.texturePath.empty() && emitter.cfg.texturePath != "-") {
        auto found = m_textures.find(emitter.cfg.texturePath);
        if (found != m_textures.end()) texture = found->second.get();
        else if (m_failedTextures.find(emitter.cfg.texturePath) == m_failedTextures.end()) {
            try {
                auto loaded = std::make_unique<Texture>(emitter.cfg.texturePath);
                texture = loaded.get();
                m_textures.emplace(emitter.cfg.texturePath, std::move(loaded));
            } catch (...) {
                m_failedTextures.insert(emitter.cfg.texturePath);
            }
        }
    }
    if (!emitter.cfg.customShader) {
        billboardShader->SetInt("uUseTexture", texture ? 1 : 0);
        billboardShader->SetInt("uColumns", std::max(emitter.cfg.textureColumns, 1));
        billboardShader->SetInt("uRows", std::max(emitter.cfg.textureRows, 1));
        billboardShader->SetInt("uLoopFrames", emitter.cfg.textureLoop ? 1 : 0);
        if (texture) { texture->Bind(0); billboardShader->SetInt("uTexture", 0); }
    }

    // Blend + depth state: test against the scene, don't write depth.
    glEnable(GL_BLEND);
    if (emitter.cfg.blend == ParticleBlend::Additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else                                              glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);

    if (m_timerQuery) glBeginQuery(GL_TIME_ELAPSED, m_timerQuery);
    if (!trailVertices.empty()) {
        m_trailShader->Bind();
        m_trailShader->SetMat4("uViewProj", proj * view);
        glBindVertexArray(m_trailVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_trailVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(trailVertices.size() * sizeof(float)),
                     trailVertices.data(), GL_DYNAMIC_DRAW);
        constexpr GLsizei trailStride = 7 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, trailStride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, trailStride,
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(trailVertices.size() / 7));
        ++m_stats.drawCalls;
    }

    billboardShader->Bind();
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(inst.size() * sizeof(float)), inst.data(), GL_DYNAMIC_DRAW);
    const GLsizei stride = 14 * sizeof(float);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(10 * sizeof(float)));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(13 * sizeof(float)));
    glVertexAttribDivisor(7, 1);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(ps.size()));
    if (m_timerQuery) {
        glEndQuery(GL_TIME_ELAPSED);
        GLint available = GL_FALSE;
        glGetQueryObjectiv(m_timerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 elapsedNanoseconds = 0;
            glGetQueryObjectui64v(m_timerQuery, GL_QUERY_RESULT, &elapsedNanoseconds);
            m_stats.gpuMilliseconds += static_cast<double>(elapsedNanoseconds) / 1000000.0;
        }
    }

    glBindVertexArray(0);
    // Restore sane defaults for following passes.
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    ++m_stats.drawCalls;
    m_stats.particles += ps.size();
    m_stats.cpuMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cpuStart).count();
}

void ParticleRenderer::Draw(const ParticleSystemComponent& system, const Camera& camera, float aspect) {
    if (system.gpuBackendActive && system.gpuEmitter) {
        const auto cpuStart = std::chrono::steady_clock::now();
        if (system.config.cullingEnabled) {
            const glm::mat4 view = camera.ViewMatrix();
            const glm::mat4 proj = camera.ProjectionMatrix(aspect);
            const glm::vec4 clip = proj * view * glm::vec4(system.lastPosition, 1.0f);
            const float radius = std::max(system.config.boundsRadius, 0.01f)
                * std::max(std::abs(proj[0][0]), std::abs(proj[1][1]));
            if (clip.w <= 0.0f || clip.x + radius < -clip.w || clip.x - radius > clip.w
                || clip.y + radius < -clip.w || clip.y - radius > clip.w
                || clip.z + radius < -clip.w || clip.z - radius > clip.w) {
                ++m_stats.culledEmitters;
                return;
            }
        }
        system.gpuEmitter->Draw(system.config, camera, aspect);
        m_stats.drawCalls += system.gpuEmitter->LastDrawCalls();
        m_stats.particles += system.gpuEmitter->Alive();
        m_stats.gpuMilliseconds += system.gpuEmitter->LastGpuMilliseconds();
        m_stats.cpuMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cpuStart).count();
        return;
    }
    Draw(system.emitter, camera, aspect);
}

} // namespace engine
