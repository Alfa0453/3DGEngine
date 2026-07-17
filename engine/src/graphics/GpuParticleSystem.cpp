#include "engine/graphics/GpuParticleSystem.h"

#include "engine/graphics/Camera.h"
#include "engine/graphics/ParticleSystem.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Primitives.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
namespace {

constexpr std::size_t kParticleStride = sizeof(float) * 28;

const char* kCompute = R"GLSL(
#version 430 core
layout(local_size_x = 128) in;
struct Particle {
    vec4 posAge;
    vec4 velLife;
    vec4 color;
    vec4 data;
    vec4 startColor;
    vec4 endColor;
    vec4 sizeRotation;
};
layout(std430, binding=0) buffer Particles { Particle particles[]; };
layout(std430, binding=1) buffer Counters { uint deadSeen; uint aliveCount; uint collisionCount; };
layout(std430, binding=2) buffer TrailHistory { vec4 trailPositions[]; };
uniform uint uCapacity;
uniform uint uSpawnCount;
uniform uint uSeed;
uniform float uDt;
uniform float uDrag;
uniform vec3 uGravity;
uniform vec3 uOrigin;
uniform vec3 uTranslation;
uniform int uLocalSpace;
uniform int uShape;
uniform float uRadius;
uniform vec3 uDirection;
uniform float uConeAngle;
uniform vec2 uSpeed;
uniform vec2 uLife;
uniform vec4 uStartColor;
uniform vec4 uEndColor;
uniform vec2 uSize;
uniform vec2 uRotation;
uniform vec2 uAngular;
uniform int uUseSizeCurve;
uniform int uUseColorCurve;
uniform vec4 uSizeCurve;
uniform vec4 uColorCurve;
uniform int uCollisionEnabled;
uniform int uCollisionCount;
uniform int uCollisionResponse;
uniform float uCollisionRadius;
uniform float uCollisionBounce;
uniform float uCollisionFriction;
uniform float uCollisionLifetimeLoss;
uniform int uColliderType[32];
uniform vec4 uColliderPositionRadius[32];
uniform vec4 uColliderData[32];
uniform vec4 uColliderRotation[32];
uniform int uTrailsEnabled;
uniform int uTrailSegments;
uniform float uTrailLength;

uint hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    x *= 0x846ca68bu; x ^= x >> 16; return x;
}
float random(inout uint state) { state = hash(state); return float(state) / 4294967296.0; }
vec3 randomUnit(inout uint state) {
    float z = random(state) * 2.0 - 1.0;
    float a = random(state) * 6.28318530718;
    float r = sqrt(max(1.0 - z*z, 0.0));
    return vec3(r*cos(a), z, r*sin(a));
}
float sampleCurve(vec4 keys, float t) {
    float scaled = clamp(t, 0.0, 1.0) * 3.0;
    int index = min(int(floor(scaled)), 2);
    return mix(keys[index], keys[index + 1], scaled - float(index));
}
vec3 rotateByQuaternion(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= uCapacity) return;
    Particle p = particles[id];
    bool dead = p.posAge.w >= p.velLife.w;
    bool spawned = false;
    if (dead) {
        uint order = atomicAdd(deadSeen, 1u);
        if (order >= uSpawnCount) return;
        uint state = hash(id ^ uSeed);
        vec3 axis = length(uDirection) > 0.00001 ? normalize(uDirection) : vec3(0,1,0);
        vec3 offset = vec3(0);
        if (uShape == 1) {
            offset = randomUnit(state) * (pow(random(state), 0.3333333) * max(uRadius, 0.0));
        } else if (uShape == 2) {
            vec3 helper = abs(axis.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0);
            vec3 right = normalize(cross(helper, axis));
            vec3 forward = normalize(cross(axis, right));
            float radius = sqrt(random(state)) * max(uRadius, 0.0);
            float azimuth = random(state) * 6.28318530718;
            offset = (right*cos(azimuth) + forward*sin(azimuth)) * radius;
        }
        vec3 direction = axis;
        if (uShape == 2 && uConeAngle > 0.0) {
            vec3 helper = abs(axis.y) < 0.98 ? vec3(0,1,0) : vec3(1,0,0);
            vec3 right = normalize(cross(helper, axis));
            vec3 forward = normalize(cross(axis, right));
            float cosMaximum = cos(max(uConeAngle, 0.0));
            float cosAngle = mix(1.0, cosMaximum, random(state));
            float angle = acos(clamp(cosAngle, -1.0, 1.0));
            float azimuth = random(state) * 6.28318530718;
            direction = normalize(axis*cos(angle) + (right*cos(azimuth)+forward*sin(azimuth))*sin(angle));
        }
        float life = mix(uLife.x, uLife.y, random(state));
        float speed = mix(uSpeed.x, uSpeed.y, random(state));
        p.posAge = vec4(uOrigin + offset, 0.0);
        p.velLife = vec4(direction * speed, max(life, 0.001));
        p.startColor = uStartColor;
        p.endColor = uEndColor;
        p.sizeRotation = vec4(uSize, radians(mix(uRotation.x, uRotation.y, random(state))),
                              radians(mix(uAngular.x, uAngular.y, random(state))));
        p.data.z = 1.0;
        p.data.w = 0.0;
        trailPositions[id * 16u] = vec4(p.posAge.xyz, 1.0);
        spawned = true;
    } else if (uLocalSpace != 0) {
        p.posAge.xyz += uTranslation;
        int historyCount = clamp(int(p.data.z), 0, uTrailSegments);
        for (int trailIndex = 0; trailIndex < historyCount; ++trailIndex)
            trailPositions[id * 16u + uint(trailIndex)].xyz += uTranslation;
    }
    vec3 previousPosition = p.posAge.xyz;
    p.velLife.xyz += uGravity * uDt;
    p.velLife.xyz *= exp(-max(uDrag, 0.0) * uDt);
    p.posAge.xyz += p.velLife.xyz * uDt;
    if (uCollisionEnabled != 0) {
        float particleRadius = max(uCollisionRadius, p.data.x * 0.5);
        for (int colliderIndex = 0; colliderIndex < uCollisionCount; ++colliderIndex) {
            int type = uColliderType[colliderIndex];
            vec4 positionRadius = uColliderPositionRadius[colliderIndex];
            vec3 normal = vec3(0);
            float penetration = 0.0;
            if (type == 0) {
                vec3 n = dot(positionRadius.xyz, positionRadius.xyz) > 0.00000001
                    ? normalize(positionRadius.xyz) : vec3(0,1,0);
                float distance = dot(n, p.posAge.xyz) - positionRadius.w;
                if (distance < particleRadius
                    && dot(n, previousPosition) - positionRadius.w >= -particleRadius) {
                    normal = n;
                    penetration = particleRadius - distance;
                }
            } else if (type == 1) {
                vec3 delta = p.posAge.xyz - positionRadius.xyz;
                float combined = max(positionRadius.w, 0.0) + particleRadius;
                float distanceSquared = dot(delta, delta);
                if (distanceSquared < combined * combined) {
                    float distance = sqrt(max(distanceSquared, 0.0));
                    normal = distance > 0.00001 ? delta / distance : vec3(0,1,0);
                    penetration = combined - distance;
                }
            } else {
                vec4 rotation = normalize(uColliderRotation[colliderIndex]);
                vec4 inverseRotation = vec4(-rotation.xyz, rotation.w);
                vec3 local = rotateByQuaternion(inverseRotation,
                    p.posAge.xyz - positionRadius.xyz);
                vec3 expanded = max(uColliderData[colliderIndex].xyz, vec3(0))
                    + vec3(particleRadius);
                if (all(lessThanEqual(abs(local), expanded))) {
                    vec3 depths = expanded - abs(local);
                    vec3 localNormal = vec3(0);
                    if (depths.x <= depths.y && depths.x <= depths.z) {
                        localNormal.x = local.x < 0.0 ? -1.0 : 1.0; penetration = depths.x;
                    } else if (depths.y <= depths.z) {
                        localNormal.y = local.y < 0.0 ? -1.0 : 1.0; penetration = depths.y;
                    } else {
                        localNormal.z = local.z < 0.0 ? -1.0 : 1.0; penetration = depths.z;
                    }
                    normal = rotateByQuaternion(rotation, localNormal);
                }
            }
            if (penetration <= 0.0) continue;
            atomicAdd(collisionCount, 1u);
            if (uCollisionResponse == 1) {
                p.posAge.w = p.velLife.w;
                break;
            }
            p.posAge.xyz += normal * penetration;
            float normalSpeed = dot(p.velLife.xyz, normal);
            if (normalSpeed < 0.0) {
                vec3 normalVelocity = normal * normalSpeed;
                vec3 tangentVelocity = p.velLife.xyz - normalVelocity;
                p.velLife.xyz = tangentVelocity * (1.0-clamp(uCollisionFriction,0.0,1.0))
                    - normalVelocity * max(uCollisionBounce,0.0);
            }
            float loss = clamp(uCollisionLifetimeLoss, 0.0, 1.0);
            p.posAge.w += (p.velLife.w - p.posAge.w) * loss;
        }
    }
    p.posAge.w += uDt;
    if (uTrailsEnabled != 0) {
        int historyCount = clamp(int(p.data.z), 0, uTrailSegments);
        float spacing = max(uTrailLength / float(max(uTrailSegments - 1, 1)), 0.001);
        p.data.w += length(p.posAge.xyz - previousPosition);
        if (historyCount <= 0) {
            trailPositions[id * 16u] = vec4(p.posAge.xyz, 1.0);
            historyCount = 1;
        } else if (!spawned && p.data.w >= spacing) {
            int last = min(historyCount, uTrailSegments - 1);
            for (int trailIndex = last; trailIndex > 0; --trailIndex)
                trailPositions[id * 16u + uint(trailIndex)] =
                    trailPositions[id * 16u + uint(trailIndex - 1)];
            trailPositions[id * 16u] = vec4(p.posAge.xyz, 1.0);
            historyCount = min(historyCount + 1, uTrailSegments);
            p.data.w = mod(p.data.w, spacing);
        }
        p.data.z = float(historyCount);
    } else {
        trailPositions[id * 16u] = vec4(p.posAge.xyz, 1.0);
        p.data.z = 1.0;
        p.data.w = 0.0;
    }
    p.sizeRotation.z += p.sizeRotation.w * uDt;
    float t = clamp(p.posAge.w / max(p.velLife.w, 0.001), 0.0, 1.0);
    float colorT = uUseColorCurve != 0 ? sampleCurve(uColorCurve, t) : t;
    float sizeT = uUseSizeCurve != 0 ? sampleCurve(uSizeCurve, t) : t;
    p.color = mix(p.startColor, p.endColor, colorT);
    p.data.x = mix(p.sizeRotation.x, p.sizeRotation.y, sizeT);
    particles[id] = p;
    if (p.posAge.w < p.velLife.w) atomicAdd(aliveCount, 1u);
}
)GLSL";

const char* kTrailVertex = R"GLSL(
#version 430 core
struct Particle { vec4 posAge; vec4 velLife; vec4 color; vec4 data; vec4 startColor; vec4 endColor; vec4 sizeRotation; };
layout(std430, binding=0) readonly buffer Particles { Particle particles[]; };
layout(std430, binding=2) readonly buffer TrailHistory { vec4 trailPositions[]; };
uniform mat4 uViewProj;
uniform vec3 uCameraPosition;
uniform vec3 uCameraRight;
uniform int uTrailSegments;
uniform float uTrailWidth;
uniform float uTrailOpacity;
out vec4 vColor;
vec3 historyPosition(uint particleId, int pointIndex, Particle p) {
    return pointIndex == 0 ? p.posAge.xyz
        : trailPositions[particleId * 16u + uint(pointIndex - 1)].xyz;
}
void main() {
    uint particleId = uint(gl_InstanceID);
    Particle p = particles[particleId];
    int pointIndex = gl_VertexID / 2;
    int sideIndex = gl_VertexID & 1;
    int historyCount = clamp(int(p.data.z), 0, uTrailSegments);
    bool alive = p.posAge.w < p.velLife.w;
    if (!alive) {
        gl_Position = vec4(2,2,2,1);
        vColor = vec4(0);
        return;
    }
    bool visiblePoint = pointIndex <= historyCount;
    int sampledIndex = min(pointIndex, historyCount);
    vec3 position = historyPosition(particleId, sampledIndex, p);
    int previousIndex = max(sampledIndex - 1, 0);
    int nextIndex = min(sampledIndex + 1, historyCount);
    vec3 tangent = historyPosition(particleId, previousIndex, p)
        - historyPosition(particleId, nextIndex, p);
    if (dot(tangent,tangent) < 0.00000001) tangent = p.velLife.xyz;
    tangent = dot(tangent,tangent) > 0.00000001 ? normalize(tangent) : vec3(0,1,0);
    vec3 toCamera = uCameraPosition - position;
    vec3 side = cross(tangent, normalize(dot(toCamera,toCamera) > 0.00000001 ? toCamera : vec3(0,0,1)));
    side = dot(side,side) > 0.00000001 ? normalize(side) : uCameraRight;
    float t = float(sampledIndex) / float(max(historyCount, 1));
    float halfWidth = visiblePoint ? uTrailWidth * (1.0 - t) * 0.5 : 0.0;
    position += side * (sideIndex == 0 ? halfWidth : -halfWidth);
    gl_Position = uViewProj * vec4(position, 1.0);
    vColor = p.color;
    vColor.a *= visiblePoint ? uTrailOpacity * (1.0 - t) : 0.0;
}
)GLSL";

const char* kTrailFragment = R"GLSL(
#version 430 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)GLSL";

const char* kMeshVertex = R"GLSL(
#version 430 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
struct Particle { vec4 posAge; vec4 velLife; vec4 color; vec4 data; vec4 startColor; vec4 endColor; vec4 sizeRotation; };
layout(std430, binding=0) readonly buffer Particles { Particle particles[]; };
uniform mat4 uViewProj;
uniform float uMeshScale;
uniform int uAlignToVelocity;
out vec3 vNormal;
out vec4 vColor;
void main() {
    Particle p = particles[gl_InstanceID];
    bool alive = p.posAge.w < p.velLife.w;
    float angle = p.sizeRotation.z;
    float c = cos(angle), s = sin(angle);
    float scale = max(p.data.x * uMeshScale, 0.0001);
    vec3 local = aPosition * scale;
    local = vec3(c*local.x + s*local.z, local.y, -s*local.x + c*local.z);
    vec3 normal = vec3(c*aNormal.x + s*aNormal.z, aNormal.y, -s*aNormal.x + c*aNormal.z);
    if (uAlignToVelocity != 0 && dot(p.velLife.xyz,p.velLife.xyz) > 0.00000001) {
        vec3 up = normalize(p.velLife.xyz);
        vec3 reference = abs(up.y) < 0.98 ? vec3(0,1,0) : vec3(1,0,0);
        vec3 right = normalize(cross(reference, up));
        vec3 forward = normalize(cross(up, right));
        local = right*local.x + up*local.y + forward*local.z;
        normal = right*normal.x + up*normal.y + forward*normal.z;
    }
    gl_Position = alive ? uViewProj * vec4(p.posAge.xyz + local, 1.0) : vec4(2,2,2,1);
    vNormal = normalize(normal);
    vColor = alive ? p.color : vec4(0);
}
)GLSL";

const char* kMeshFragment = R"GLSL(
#version 430 core
in vec3 vNormal;
in vec4 vColor;
out vec4 FragColor;
uniform vec3 uLightDirection;
void main() {
    float light = 0.28 + 0.72 * max(dot(normalize(vNormal), normalize(-uLightDirection)), 0.0);
    FragColor = vec4(vColor.rgb * light, vColor.a);
}
)GLSL";

const char* kVertex = R"GLSL(
#version 430 core
layout(location=0) in vec2 aCorner;
struct Particle { vec4 posAge; vec4 velLife; vec4 color; vec4 data; vec4 startColor; vec4 endColor; vec4 sizeRotation; };
layout(std430, binding=0) readonly buffer Particles { Particle particles[]; };
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
out vec2 vUV;
out vec4 vColor;
flat out float vFrame;
uniform float uTextureFps;
void main() {
    Particle p = particles[gl_InstanceID];
    bool alive = p.posAge.w < p.velLife.w;
    float c = cos(p.sizeRotation.z), s = sin(p.sizeRotation.z);
    vec2 corner = mat2(c,-s,s,c) * aCorner;
    vec3 world = p.posAge.xyz + (corner.x*uCamRight + corner.y*uCamUp) * p.data.x;
    gl_Position = alive ? uViewProj * vec4(world,1) : vec4(2,2,2,1);
    vUV = aCorner + 0.5;
    vColor = alive ? p.color : vec4(0);
    vFrame = p.posAge.w * uTextureFps;
}
)GLSL";

const char* kFragment = R"GLSL(
#version 430 core
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
        int frame = int(floor(max(vFrame, 0.0)));
        frame = uLoopFrames != 0 ? frame % count : min(frame, count - 1);
        int column = frame % max(uColumns, 1);
        int row = frame / max(uColumns, 1);
        vec2 cell = vec2(1.0 / float(max(uColumns, 1)), 1.0 / float(max(uRows, 1)));
        vec2 uv = (vec2(float(column), float(max(uRows, 1)-1-row)) + vUV) * cell;
        vec4 texel = texture(uTexture, uv);
        FragColor = vec4(texel.rgb * vColor.rgb, texel.a * vColor.a);
        return;
    }
    float d = length(vUV - vec2(0.5)) * 2.0;
    float a = clamp(1.0-d, 0.0, 1.0); a *= a;
    FragColor = vec4(vColor.rgb, vColor.a*a);
}
)GLSL";

unsigned int Compile(unsigned int type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        int length = 0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("GPU particle shader compile failed: ") + log.data());
    }
    return shader;
}

unsigned int Link(std::initializer_list<unsigned int> shaders) {
    const unsigned int program = glCreateProgram();
    for (unsigned int shader : shaders) glAttachShader(program, shader);
    glLinkProgram(program);
    int ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    for (unsigned int shader : shaders) { glDetachShader(program, shader); glDeleteShader(shader); }
    if (!ok) {
        int length = 0; glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error(std::string("GPU particle program link failed: ") + log.data());
    }
    return program;
}

int U(unsigned int program, const char* name) { return glGetUniformLocation(program, name); }

} // namespace

GpuParticleEmitter::GpuParticleEmitter() = default;

GpuParticleEmitter::~GpuParticleEmitter() {
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_timerQuery) glDeleteQueries(1, &m_timerQuery);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_counterBuffer) glDeleteBuffers(1, &m_counterBuffer);
    if (m_trailSsbo) glDeleteBuffers(1, &m_trailSsbo);
    if (m_ssbo) glDeleteBuffers(1, &m_ssbo);
    if (m_renderProgram) glDeleteProgram(m_renderProgram);
    if (m_trailProgram) glDeleteProgram(m_trailProgram);
    if (m_meshProgram) glDeleteProgram(m_meshProgram);
    if (m_computeProgram) glDeleteProgram(m_computeProgram);
}

bool GpuParticleEmitter::Supported() {
    const bool version43 = GLVersion.major > 4
        || (GLVersion.major == 4 && GLVersion.minor >= 3);
    return version43 && glDispatchCompute && glMemoryBarrier && glBindBufferBase;
}

bool GpuParticleEmitter::Supports(const EmitterConfig& config) {
    if (!config.shaderPath.empty()) return false;
    // Correct alpha compositing requires back-to-front ordering. The CPU path
    // performs that sort; keep alpha effects there until GPU sorting/OIT exists.
    return Supported() && config.blend == ParticleBlend::Additive;
}

bool GpuParticleEmitter::Prepare(const EmitterConfig& config) {
    if (!Supports(config)) return false;
    if (config.renderMode == ParticleRenderMode::Mesh) {
        EnsurePrograms();
        if (!m_particleCube) {
            m_particleCube = std::make_unique<Mesh>(primitives::Cube());
            m_particleSphere = std::make_unique<Mesh>(primitives::Sphere(12));
            m_particleCone = std::make_unique<Mesh>(primitives::Cone(16));
            m_particleCylinder = std::make_unique<Mesh>(primitives::Cylinder(16));
        }
        m_texture.reset();
        m_texturePath.clear();
        m_textureFailed = false;
        if (config.meshShape != ParticleMeshShape::Model || config.meshPath.empty()) {
            m_model.reset();
            m_meshPath.clear();
            m_meshFailed = false;
            return true;
        }
        if (config.meshPath == m_meshPath) return true;
        m_model.reset();
        m_meshPath = config.meshPath;
        m_meshFailed = false;
        try {
            m_model = std::make_unique<Model>(Model::FromFile(config.meshPath));
        } catch (...) {
            m_meshFailed = true;
        }
        return true; // failed imported models deliberately render as a cube
    }
    m_model.reset();
    m_meshPath.clear();
    m_meshFailed = false;
    if (config.texturePath.empty()) {
        m_texture.reset();
        m_texturePath.clear();
        m_textureFailed = false;
        return true;
    }
    if (config.texturePath == m_texturePath) return m_texture && !m_textureFailed;
    m_texture.reset();
    m_texturePath = config.texturePath;
    m_textureFailed = false;
    try {
        m_texture = std::make_unique<Texture>(config.texturePath, true);
    } catch (...) {
        m_textureFailed = true;
    }
    return m_texture && !m_textureFailed;
}

void GpuParticleEmitter::EnsurePrograms() {
    if (m_computeProgram) return;
    if (!Supported()) throw std::runtime_error("OpenGL 4.3 compute shaders are unavailable");
    m_computeProgram = Link({Compile(GL_COMPUTE_SHADER, kCompute)});
    m_renderProgram = Link({Compile(GL_VERTEX_SHADER, kVertex), Compile(GL_FRAGMENT_SHADER, kFragment)});
    m_trailProgram = Link({Compile(GL_VERTEX_SHADER, kTrailVertex), Compile(GL_FRAGMENT_SHADER, kTrailFragment)});
    m_meshProgram = Link({Compile(GL_VERTEX_SHADER, kMeshVertex), Compile(GL_FRAGMENT_SHADER, kMeshFragment)});
    const float quad[] = {-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f};
    glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_quadVbo);
    glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    glBindVertexArray(0);
    glGenBuffers(1, &m_ssbo); glGenBuffers(1, &m_counterBuffer); glGenBuffers(1, &m_trailSsbo);
    glGenQueries(1, &m_timerQuery);
}

void GpuParticleEmitter::EnsureCapacity(int capacity) {
    EnsurePrograms();
    capacity = std::clamp(capacity, 1, 1000000);
    if (capacity == m_capacity) return;
    m_capacity = capacity;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(kParticleStride * capacity), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_trailSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(sizeof(float) * 4 * 16 * capacity), nullptr, GL_DYNAMIC_DRAW);
    Clear();
}

void GpuParticleEmitter::Reset(const EmitterConfig& config, const glm::vec3&) {
    EnsureCapacity(config.maxParticles);
    ++m_seed;
    m_statsReadbackCountdown = 0;
    Clear();
}

void GpuParticleEmitter::Clear() {
    if (!m_ssbo || m_capacity <= 0) return;
    const std::array<float, 28> dead{};
    std::vector<std::array<float, 28>> particles(static_cast<std::size_t>(m_capacity), dead);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(particles.size() * sizeof(particles[0])), particles.data());
    const std::array<float, 4> emptyTrail{};
    std::vector<std::array<float, 4>> trails(static_cast<std::size_t>(m_capacity) * 16, emptyTrail);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_trailSsbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(trails.size() * sizeof(trails[0])), trails.data());
    m_alive = 0;
    m_lastCollisionCount = 0;
    m_statsReadbackCountdown = 0;
}

void GpuParticleEmitter::Update(const EmitterConfig& c, const glm::vec3& position,
                                const glm::vec3& translationDelta, bool localSpace,
                                float dt, int spawnCount,
                                const std::vector<ParticleCollisionShape>& collisionShapes,
                                bool synchronize) {
    EnsureCapacity(c.maxParticles);
    const std::array<unsigned int, 3> counters{{0u, 0u, 0u}};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(counters), counters.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_counterBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_trailSsbo);
    glUseProgram(m_computeProgram);
    glUniform1ui(U(m_computeProgram,"uCapacity"), static_cast<unsigned int>(m_capacity));
    glUniform1ui(U(m_computeProgram,"uSpawnCount"), static_cast<unsigned int>(std::max(spawnCount,0)));
    glUniform1ui(U(m_computeProgram,"uSeed"), ++m_seed);
    glUniform1f(U(m_computeProgram,"uDt"), std::max(dt,0.0f));
    glUniform1f(U(m_computeProgram,"uDrag"), c.drag);
    glUniform3fv(U(m_computeProgram,"uGravity"),1,glm::value_ptr(c.gravity));
    glUniform3fv(U(m_computeProgram,"uOrigin"),1,glm::value_ptr(position));
    glUniform3fv(U(m_computeProgram,"uTranslation"),1,glm::value_ptr(translationDelta));
    glUniform1i(U(m_computeProgram,"uLocalSpace"), localSpace ? 1 : 0);
    glUniform1i(U(m_computeProgram,"uShape"), static_cast<int>(c.shape));
    glUniform1f(U(m_computeProgram,"uRadius"), c.shapeRadius);
    glUniform3fv(U(m_computeProgram,"uDirection"),1,glm::value_ptr(c.direction));
    glUniform1f(U(m_computeProgram,"uConeAngle"), glm::radians(c.coneAngleDeg));
    glUniform2f(U(m_computeProgram,"uSpeed"), c.speedMin, c.speedMax);
    glUniform2f(U(m_computeProgram,"uLife"), c.lifeMin, c.lifeMax);
    glUniform4fv(U(m_computeProgram,"uStartColor"),1,glm::value_ptr(c.startColor));
    glUniform4fv(U(m_computeProgram,"uEndColor"),1,glm::value_ptr(c.endColor));
    glUniform2f(U(m_computeProgram,"uSize"), c.startSize, c.endSize);
    glUniform2f(U(m_computeProgram,"uRotation"), c.rotationMinDeg, c.rotationMaxDeg);
    glUniform2f(U(m_computeProgram,"uAngular"), c.angularVelocityMinDeg, c.angularVelocityMaxDeg);
    glUniform1i(U(m_computeProgram,"uUseSizeCurve"), c.useSizeCurve ? 1 : 0);
    glUniform1i(U(m_computeProgram,"uUseColorCurve"), c.useColorCurve ? 1 : 0);
    glUniform4fv(U(m_computeProgram,"uSizeCurve"), 1, c.sizeCurve.data());
    glUniform4fv(U(m_computeProgram,"uColorCurve"), 1, c.colorCurve.data());
    const int collisionCount = c.collisionEnabled
        ? static_cast<int>(std::min<std::size_t>(collisionShapes.size(), 32)) : 0;
    glUniform1i(U(m_computeProgram,"uCollisionEnabled"), collisionCount > 0 ? 1 : 0);
    glUniform1i(U(m_computeProgram,"uCollisionCount"), collisionCount);
    glUniform1i(U(m_computeProgram,"uCollisionResponse"), static_cast<int>(c.collisionResponse));
    glUniform1f(U(m_computeProgram,"uCollisionRadius"), c.collisionRadius);
    glUniform1f(U(m_computeProgram,"uCollisionBounce"), c.collisionBounce);
    glUniform1f(U(m_computeProgram,"uCollisionFriction"), c.collisionFriction);
    glUniform1f(U(m_computeProgram,"uCollisionLifetimeLoss"), c.collisionLifetimeLoss);
    glUniform1i(U(m_computeProgram,"uTrailsEnabled"), c.trailsEnabled ? 1 : 0);
    glUniform1i(U(m_computeProgram,"uTrailSegments"), std::clamp(c.trailSegments, 2, 16));
    glUniform1f(U(m_computeProgram,"uTrailLength"), std::max(c.trailLength, 0.001f));
    for (int i = 0; i < collisionCount; ++i) {
        const ParticleCollisionShape& shape = collisionShapes[static_cast<std::size_t>(i)];
        const std::string suffix = "[" + std::to_string(i) + "]";
        glUniform1i(U(m_computeProgram, ("uColliderType" + suffix).c_str()), static_cast<int>(shape.type));
        glm::vec4 positionRadius(0.0f);
        glm::vec4 data(0.0f);
        if (shape.type == ParticleCollisionShape::Type::Plane)
            positionRadius = glm::vec4(shape.normal, shape.offset);
        else {
            positionRadius = glm::vec4(shape.center,
                shape.type == ParticleCollisionShape::Type::Sphere ? shape.radius : 0.0f);
            data = glm::vec4(shape.halfExtents, 0.0f);
        }
        const glm::quat q = glm::normalize(shape.rotation);
        const glm::vec4 rotation(q.x, q.y, q.z, q.w);
        glUniform4fv(U(m_computeProgram, ("uColliderPositionRadius" + suffix).c_str()), 1,
                     glm::value_ptr(positionRadius));
        glUniform4fv(U(m_computeProgram, ("uColliderData" + suffix).c_str()), 1,
                     glm::value_ptr(data));
        glUniform4fv(U(m_computeProgram, ("uColliderRotation" + suffix).c_str()), 1,
                     glm::value_ptr(rotation));
    }
    if (synchronize && m_timerQuery) glBeginQuery(GL_TIME_ELAPSED, m_timerQuery);
    glDispatchCompute(static_cast<unsigned int>((m_capacity + 127) / 128), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    if (synchronize && m_timerQuery) {
        glEndQuery(GL_TIME_ELAPSED);
        GLint available = GL_FALSE;
        glGetQueryObjectiv(m_timerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(m_timerQuery, GL_QUERY_RESULT, &elapsed);
            m_lastGpuMilliseconds = static_cast<double>(elapsed) / 1000000.0;
        }
    }
    if (synchronize && m_statsReadbackCountdown <= 0) {
        std::array<unsigned int, 3> result{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), result.data());
        m_alive = result[1];
        m_lastCollisionCount = static_cast<int>(result[2]);
        m_statsReadbackCountdown = 3;
    } else if (synchronize) {
        --m_statsReadbackCountdown;
    }
}

void GpuParticleEmitter::Prewarm(const EmitterConfig& config, const glm::vec3& position,
                                 float seconds, int burstCount, float burstInterval,
                                 const std::vector<ParticleCollisionShape>& collisionShapes) {
    EnsureCapacity(config.maxParticles);
    Clear();
    const float warmTime = std::clamp(seconds, 0.0f, 30.0f);
    float emissionAccumulator = 0.0f;
    float burstClock = 0.0f;
    bool initialBurst = false;
    if (m_timerQuery) glBeginQuery(GL_TIME_ELAPSED, m_timerQuery);
    for (float elapsed = 0.0f; elapsed < warmTime; ) {
        const float step = std::min(1.0f / 60.0f, warmTime - elapsed);
        int spawnCount = 0;
        if (!initialBurst && burstCount > 0) {
            spawnCount += burstCount;
            initialBurst = true;
        }
        if (burstCount > 0 && burstInterval > 0.0f) {
            burstClock += step;
            while (burstClock >= burstInterval) {
                spawnCount += burstCount;
                burstClock -= burstInterval;
            }
        }
        emissionAccumulator += std::max(config.rate, 0.0f) * step;
        const int continuous = static_cast<int>(emissionAccumulator);
        spawnCount += continuous;
        emissionAccumulator -= static_cast<float>(continuous);
        Update(config, position, glm::vec3(0.0f), false, step, spawnCount,
               collisionShapes, false);
        elapsed += step;
    }
    // One zero-time dispatch produces final alive/contact counters without
    // advancing the warmed simulation.
    Update(config, position, glm::vec3(0.0f), false, 0.0f, 0,
           collisionShapes, false);
    if (m_timerQuery) {
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(m_timerQuery, GL_QUERY_RESULT, &elapsed);
        m_lastGpuMilliseconds = static_cast<double>(elapsed) / 1000000.0;
    }
    std::array<unsigned int, 3> result{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), result.data());
    m_alive = result[1];
    m_lastCollisionCount = static_cast<int>(result[2]);
}

void GpuParticleEmitter::Draw(const EmitterConfig& c, const Camera& camera, float aspect) {
    m_lastDrawCalls = 0;
    if (!m_renderProgram || m_capacity <= 0 || m_alive == 0) return;
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 viewProj = camera.ProjectionMatrix(aspect) * view;
    if (c.renderMode == ParticleRenderMode::Mesh) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_trailSsbo);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, c.blend == ParticleBlend::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        if (m_timerQuery) glBeginQuery(GL_TIME_ELAPSED, m_timerQuery);
        if (c.trailsEnabled && c.trailWidth > 0.0f && c.trailOpacity > 0.0f) {
            glUseProgram(m_trailProgram);
            glUniformMatrix4fv(U(m_trailProgram,"uViewProj"),1,GL_FALSE,glm::value_ptr(viewProj));
            glUniform3fv(U(m_trailProgram,"uCameraPosition"),1,glm::value_ptr(camera.Position()));
            const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
            glUniform3fv(U(m_trailProgram,"uCameraRight"),1,glm::value_ptr(cameraRight));
            const int segments = std::clamp(c.trailSegments, 2, 16);
            glUniform1i(U(m_trailProgram,"uTrailSegments"), segments);
            glUniform1f(U(m_trailProgram,"uTrailWidth"), c.trailWidth);
            glUniform1f(U(m_trailProgram,"uTrailOpacity"), c.trailOpacity);
            glBindVertexArray(m_vao);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, (segments + 1) * 2, m_capacity);
            ++m_lastDrawCalls;
        }
        glUseProgram(m_meshProgram);
        glUniformMatrix4fv(U(m_meshProgram,"uViewProj"),1,GL_FALSE,glm::value_ptr(viewProj));
        glUniform1f(U(m_meshProgram,"uMeshScale"), std::max(c.meshScale, 0.001f));
        glUniform1i(U(m_meshProgram,"uAlignToVelocity"), c.meshAlignToVelocity ? 1 : 0);
        const glm::vec3 lightDirection(-0.4f, -1.0f, -0.3f);
        glUniform3fv(U(m_meshProgram,"uLightDirection"),1,glm::value_ptr(lightDirection));
        auto drawMesh = [&](const Mesh& mesh) {
            glBindVertexArray(mesh.Vao());
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(mesh.IndexCount()),
                                    GL_UNSIGNED_INT, nullptr, m_capacity);
            ++m_lastDrawCalls;
        };
        if (c.meshShape == ParticleMeshShape::Model && m_model) {
            for (const SubMesh& subMesh : m_model->SubMeshes()) drawMesh(subMesh.mesh);
        } else {
            const Mesh* mesh = m_particleCube.get();
            if (c.meshShape == ParticleMeshShape::Sphere) mesh = m_particleSphere.get();
            else if (c.meshShape == ParticleMeshShape::Cone) mesh = m_particleCone.get();
            else if (c.meshShape == ParticleMeshShape::Cylinder) mesh = m_particleCylinder.get();
            if (mesh) drawMesh(*mesh);
        }
        if (m_timerQuery) {
            glEndQuery(GL_TIME_ELAPSED);
            GLint available = GL_FALSE;
            glGetQueryObjectiv(m_timerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE) {
                GLuint64 elapsed = 0;
                glGetQueryObjectui64v(m_timerQuery, GL_QUERY_RESULT, &elapsed);
                m_lastGpuMilliseconds += static_cast<double>(elapsed) / 1000000.0;
            }
        }
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return;
    }
    glUseProgram(m_renderProgram);
    glUniformMatrix4fv(U(m_renderProgram,"uViewProj"),1,GL_FALSE,glm::value_ptr(viewProj));
    glUniform3fv(U(m_renderProgram,"uCamRight"),1,glm::value_ptr(glm::vec3(view[0][0],view[1][0],view[2][0])));
    glUniform3fv(U(m_renderProgram,"uCamUp"),1,glm::value_ptr(glm::vec3(view[0][1],view[1][1],view[2][1])));
    glUniform1f(U(m_renderProgram,"uTextureFps"), std::max(c.textureFps, 0.0f));
    glUniform1i(U(m_renderProgram,"uUseTexture"), m_texture ? 1 : 0);
    glUniform1i(U(m_renderProgram,"uColumns"), std::max(c.textureColumns, 1));
    glUniform1i(U(m_renderProgram,"uRows"), std::max(c.textureRows, 1));
    glUniform1i(U(m_renderProgram,"uLoopFrames"), c.textureLoop ? 1 : 0);
    glUniform1i(U(m_renderProgram,"uTexture"), 0);
    if (m_texture) m_texture->Bind(0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_trailSsbo);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, c.blend == ParticleBlend::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    if (m_timerQuery) glBeginQuery(GL_TIME_ELAPSED, m_timerQuery);
    if (c.trailsEnabled && c.trailWidth > 0.0f && c.trailOpacity > 0.0f) {
        glUseProgram(m_trailProgram);
        glUniformMatrix4fv(U(m_trailProgram,"uViewProj"),1,GL_FALSE,glm::value_ptr(viewProj));
        glUniform3fv(U(m_trailProgram,"uCameraPosition"),1,glm::value_ptr(camera.Position()));
        const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
        glUniform3fv(U(m_trailProgram,"uCameraRight"),1,glm::value_ptr(cameraRight));
        const int segments = std::clamp(c.trailSegments, 2, 16);
        glUniform1i(U(m_trailProgram,"uTrailSegments"), segments);
        glUniform1f(U(m_trailProgram,"uTrailWidth"), c.trailWidth);
        glUniform1f(U(m_trailProgram,"uTrailOpacity"), c.trailOpacity);
        glBindVertexArray(m_vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, (segments + 1) * 2, m_capacity);
        ++m_lastDrawCalls;
    }
    glUseProgram(m_renderProgram);
    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_capacity);
    ++m_lastDrawCalls;
    if (m_timerQuery) {
        glEndQuery(GL_TIME_ELAPSED);
        GLint available = GL_FALSE;
        glGetQueryObjectiv(m_timerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(m_timerQuery, GL_QUERY_RESULT, &elapsed);
            m_lastGpuMilliseconds += static_cast<double>(elapsed) / 1000000.0;
        }
    }
    glBindVertexArray(0); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

bool IsGpuParticleSimulationSupported() { return GpuParticleEmitter::Supported(); }
bool IsGpuParticleConfigurationSupported(const EmitterConfig& config) { return GpuParticleEmitter::Supports(config); }

} // namespace engine
