#pragma once

#include "engine/assets/AssetIdentity.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
class Shader;
namespace ecs { class Registry; }

// Render-thread cache and bounded selector for authored reflection probes.
// Only the best two candidates are bound, keeping the fragment cost fixed.
class ReflectionProbeSystem {
public:
    ReflectionProbeSystem() = default;
    ~ReflectionProbeSystem();
    ReflectionProbeSystem(const ReflectionProbeSystem&) = delete;
    ReflectionProbeSystem& operator=(const ReflectionProbeSystem&) = delete;

    void Sync(ecs::Registry& registry, const glm::vec3& referencePosition = glm::vec3(0.0f));
    void BindBest(Shader& shader, const glm::vec3& referencePosition,
                  unsigned int firstTextureUnit = 22) const;
    void Clear();
    std::size_t ProbeCount() const { return m_entries.size(); }
    std::uint64_t MemoryBytes() const { return m_memoryBytes; }
    std::uint64_t MemoryBudgetBytes() const { return m_memoryBudgetBytes; }
    std::size_t ResidentProbeCount() const { return m_residentCount; }
    std::size_t CandidateCountLastQuery() const { return m_candidateCountLastQuery; }
    bool OverBudget() const { return m_memoryBytes > m_memoryBudgetBytes; }
    void SetStreaming(float distance, std::uint64_t budgetBytes, int maxSampled = 2);

private:
    struct Entry {
        AssetHandle id;
        glm::vec3 position{0.0f};
        glm::vec3 extents{1.0f};
        float radius=1.0f, blend=0.25f, intensity=1.0f;
        int priority=0, shape=0, resolution=0, mipCount=0;
        unsigned int texture=0;
        std::string path;
    };
    std::unordered_map<std::uint32_t,Entry> m_entries;
    std::unordered_map<std::int64_t,std::vector<std::uint32_t>> m_spatialGrid;
    std::uint64_t m_memoryBytes=0;
    std::uint64_t m_memoryBudgetBytes=256ull*1024ull*1024ull;
    float m_streamingDistance=300.0f;
    float m_cellSize=32.0f;
    int m_maxSampled=2;
    int m_textureUnits=0;
    std::size_t m_residentCount=0;
    mutable std::size_t m_candidateCountLastQuery=0;
};
} // namespace engine
