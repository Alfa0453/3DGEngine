#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace engine {
namespace ai {

// A 2D grid of walkable cells laid out on the world XZ plane (y is flat). Cell
// (0,0) is centred at 'origin'; neighbouring cells are 'cellSize' apart. Used by
// the A* pathfinder.
struct NavGrid {
    int       width  = 0;
    int       height = 0;
    float     cellSize = 1.0f;
    glm::vec3 origin{0.0f};                 // world centre of cell (0,0); path y = origin.y
    std::vector<std::uint8_t> walkable;     // width*height, row-major (x + y*width), 1 = walkable

    NavGrid() = default;
    NavGrid(int w, int h, float cs = 1.0f, const glm::vec3& o = glm::vec3(0.0f))
        : width(w), height(h), cellSize(cs), origin(o),
          walkable(static_cast<std::size_t>(w) * h, 1) {}
    
    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    bool Walkable(int x, int y) const {
        return InBounds(x, y) && walkable[static_cast<std::size_t>(y) * width + x] != 0;
    }
    void SetWalkable(int x, int y, bool w) {
        if (InBounds(x, y)) walkable[static_cast<std::size_t>(y) * width + x] = w ? 1 : 0;
    }
    void SetObstacle(int x, int y) { SetWalkable(x, y, false); }

    glm::vec3 CellToWorld(int x, int y) const {
        return origin + glm::vec3(x * cellSize, 0.0f, y * cellSize);
    }
    glm::ivec2 WorldToCell(const glm::vec3& p) const {
        return glm::ivec2(static_cast<int>(std::round((p.x - origin.x) / cellSize)),
                          static_cast<int>(std::round((p.z - origin.z) / cellSize)));
    }
};

} // namespace ai
} // namespace engine