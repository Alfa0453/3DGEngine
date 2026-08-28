#pragma once

#include "engine/assets/AssetIdentity.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>

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

    void Sync(ecs::Registry& registry);
    void BindBest(Shader& shader, const glm::vec3& referencePosition,
                  unsigned int firstTextureUnit = 22) const;
    void Clear();
    std::size_t ProbeCount() const { return m_entries.size(); }
    std::uint64_t MemoryBytes() const { return m_memoryBytes; }

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
    std::uint64_t m_memoryBytes=0;
};
} // namespace engine
