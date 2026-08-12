#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace engine {

class Camera;
class Shader;
namespace ecs { class Registry; }

// Draws FoliageComponent instances in one GPU call per foliage type/submesh.
// Instance transforms remain CPU-side scene data; only visible transforms are
// streamed to one reusable GPU buffer each frame.
class FoliageRenderer {
public:
    FoliageRenderer();
    ~FoliageRenderer();

    FoliageRenderer(const FoliageRenderer&) = delete;
    FoliageRenderer& operator=(const FoliageRenderer&) = delete;

    void Draw(ecs::Registry& registry, const Camera& camera, float aspect,
              const glm::vec3& sunDirection, const glm::vec3& sunColor,
              const glm::vec3& ambient, float time = 0.0f);

    int VisibleInstances() const { return m_visibleInstances; }
    int DrawCalls() const { return m_drawCalls; }

private:
    std::unique_ptr<Shader> m_shader;
    unsigned int m_instanceVbo = 0;
    std::size_t m_instanceCapacity = 0;
    std::vector<glm::mat4> m_lodMatrices[3];
    int m_visibleInstances = 0;
    int m_drawCalls = 0;
};

} // namespace engine
