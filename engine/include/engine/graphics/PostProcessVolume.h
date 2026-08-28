#pragma once

#include <glm/glm.hpp>

namespace engine {
class PostProcess;
namespace ecs { class Registry; }

// Resolves all enabled PostProcessVolume components at the camera. Volumes are
// ordered by priority and stable entity handle, then blended over their boundary
// distance. Only explicit override flags affect the global settings.
void ApplyPostProcessVolumes(ecs::Registry& registry,
                             const glm::vec3& cameraPosition,
                             PostProcess& postProcess);
}
