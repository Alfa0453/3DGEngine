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
    // The cell path is string-pulled (line-of-sight smoothed) before conversion, so
    // the agent follows straight diagonals instead of a cell-by-cell staircase.
    static std::vector<glm::vec3> FindPathWorld(const NavGrid& grid,
                                                const glm::vec3& start, const glm::vec3& goal,
                                                bool allowDiagonal = true);
    static void FindPathWorld(const NavGrid& grid,
                              const glm::vec3& start, const glm::vec3& goal,
                              std::vector<glm::vec3>& outPath,
                              bool allowDiagonal = true);

    // Line-of-sight string pull: drops interior waypoints whose corner can be cut
    // by a straight walkable line, keeping only the vertices where the path must
    // bend around an obstacle. Never shortcuts through a blocked (or corner-clipped)
    // cell, so the smoothed route stays collision-free on the grid.
    static std::vector<glm::ivec2> SmoothPath(const NavGrid& grid,
                                              const std::vector<glm::ivec2>& cells);
};

} // namespace ai
} // namespace engine
