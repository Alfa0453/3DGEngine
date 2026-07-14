#pragma once

#include "engine/ai/NavMesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace engine {
namespace ai {

// An axis-aligned box obstacle (only the XZ footprint is used for navigation).
struct NavObstacle {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.5f};
};

struct NavBuildConfig {
    glm::vec3 boundsMin{0.0f};   // XZ area to cover; polygons sit at boundsMin.y
    glm::vec3 boundsMax{0.0f};
    float     cellSize    = 0.5f;
    float     agentRadius = 0.4f;  // blocked cells are grown by this (erosion)
};

// Auto-generates a NavMesh from level geometry, Recast-style but lightweight:
//   1. VOXELIZE: rasterize the bounds into a grid; a cell is walkable unless it is
//      within agentRadius of an obstacle (or out of bounds).
//   2. REGION-MERGE: greedily merge walkable cells into maximal rectangles.
//   3. Emit each rectangle as a convex polygon and link them (geometric adjacency
//      handles the T-junctions between differently-sized rectangles).
class NavMeshBuilder {
public:
    static NavMesh Build(const NavBuildConfig& cfg, const std::vector<NavObstacle>& obstacles);
};

} // namespace ai
} // namespace engine
