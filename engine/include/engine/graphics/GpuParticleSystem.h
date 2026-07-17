#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace engine {

class Camera;
class Texture;
class Mesh;
class Model;
struct EmitterConfig;
struct ParticleCollisionShape;

// OpenGL 4.3 compute-backed billboard simulation. Unsupported feature sets
// remain on ParticleEmitter, so choosing Auto is always safe.
class GpuParticleEmitter {
public:
    GpuParticleEmitter();
    ~GpuParticleEmitter();
    GpuParticleEmitter(const GpuParticleEmitter&) = delete;
    GpuParticleEmitter& operator=(const GpuParticleEmitter&) = delete;

    static bool Supported();
    static bool Supports(const EmitterConfig& config);

    // Loads optional render resources. False means the caller must use CPU
    // fallback so a bad texture never turns an effect invisible.
    bool Prepare(const EmitterConfig& config);

    void Reset(const EmitterConfig& config, const glm::vec3& position);
    void Update(const EmitterConfig& config, const glm::vec3& position,
                const glm::vec3& translationDelta, bool localSpace,
                float dt, int spawnCount,
                const std::vector<ParticleCollisionShape>& collisionShapes = {},
                bool synchronize = true);
    void Prewarm(const EmitterConfig& config, const glm::vec3& position,
                 float seconds, int burstCount, float burstInterval,
                 const std::vector<ParticleCollisionShape>& collisionShapes = {});
    void Clear();
    void Draw(const EmitterConfig& config, const Camera& camera, float aspect);

    std::size_t Alive() const { return m_alive; }
    int Capacity() const { return m_capacity; }
    bool TextureLoadFailed() const { return m_textureFailed; }
    bool MeshLoadFailed() const { return m_meshFailed; }
    int LastCollisionCount() const { return m_lastCollisionCount; }
    int LastDrawCalls() const { return m_lastDrawCalls; }
    double LastGpuMilliseconds() const { return m_lastGpuMilliseconds; }

private:
    void EnsurePrograms();
    void EnsureCapacity(int capacity);

    unsigned int m_computeProgram = 0;
    unsigned int m_renderProgram = 0;
    unsigned int m_trailProgram = 0;
    unsigned int m_meshProgram = 0;
    unsigned int m_ssbo = 0;
    unsigned int m_trailSsbo = 0;
    unsigned int m_counterBuffer = 0;
    unsigned int m_vao = 0;
    unsigned int m_quadVbo = 0;
    unsigned int m_timerQuery = 0;
    int m_capacity = 0;
    std::size_t m_alive = 0;
    unsigned int m_seed = 1;
    std::unique_ptr<Texture> m_texture;
    std::string m_texturePath;
    bool m_textureFailed = false;
    std::unique_ptr<Mesh> m_particleCube;
    std::unique_ptr<Mesh> m_particleSphere;
    std::unique_ptr<Mesh> m_particleCone;
    std::unique_ptr<Mesh> m_particleCylinder;
    std::unique_ptr<Model> m_model;
    std::string m_meshPath;
    bool m_meshFailed = false;
    int m_lastCollisionCount = 0;
    int m_lastDrawCalls = 0;
    double m_lastGpuMilliseconds = 0.0;
    int m_statsReadbackCountdown = 0;
};

bool IsGpuParticleSimulationSupported();
bool IsGpuParticleConfigurationSupported(const EmitterConfig& config);

} // namespace engine
