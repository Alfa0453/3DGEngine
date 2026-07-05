#pragma once

#include <glm/glm.hpp>

#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace ecs { class Registry; }
class Mesh;
class Shader;

// Collects the scene's shadow casters once (skipping emissive light gizmos) and
// draws them into whatever light view is currently set on a depth shader. Same-
// mesh untextured casters are batched into one instanced draw; the caster set is
// gathered a single time per Generate and reused across every cascade, cube face
// and light, so a shadowed point light no longer re-submits every mesh 6 times
// one-by-one. The depth shader must expose a `uInstanced` int and per-instance
// model-matrix attributes at locations 3..6.
class ShadowCasterBatch {
public:
    ShadowCasterBatch();
    ~ShadowCasterBatch();
    ShadowCasterBatch(const ShadowCasterBatch&)            = delete;
    ShadowCasterBatch& operator=(const ShadowCasterBatch&) = delete;

    void Build(ecs::Registry& registry);  // gather + upload; once per Generate
    void Draw(Shader& depthShader);        // draw all casters; once per light view
    bool Empty() const { return m_records.empty() && m_textured.empty(); }

private:
    struct Record { const Mesh* mesh; int offsetFloats; int count; };
    unsigned int                                   m_vbo = 0;
    std::vector<float>                             m_data;      // 16 floats (model) per instance
    std::vector<Record>                           m_records;   // instanced groups
    std::vector<std::pair<const Mesh*, glm::mat4>> m_textured;  // per-object fallback
};

} // namespace engine