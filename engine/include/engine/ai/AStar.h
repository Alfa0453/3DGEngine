#pragma once

#include "engine/ai/NavGrid.h"

#include <glm/glm.hpp>

#include <vector>

namespace engine {
namespace ai {

// A* on a NavGrid. 8-directional by default with an octile heuristic; diagonal
// moves never cut corners through blocked cells. Returns an empty path when no
// route exists (or start/goal is blocked).
class AStar {
public:
    static std::vector<glm::ivec2> FindPath(const NavGrid& grid, glm::ivec2 start, glm::ivec2 goal,
                                            bool allowDiagonal = true);

    // Same, but converts world start/goal to cells and returns world-space waypoints.
    static std::vector<glm::vec3> FindPathWorld(const NavGrid& grid,
                                                const glm::vec3& start, const glm::vec3& goal,
                                                bool allowDiagonal = true);
};

} // namespace ai
} // namespace engine