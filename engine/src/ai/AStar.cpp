#include "engine/ai/AStar.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace engine {
namespace ai {
namespace {

constexpr float kOrtho = 1.0f;
constexpr float kDiag  = 1.41421356f;

float Heuristic(int dx, int dy, bool diag) {
    dx = std::abs(dx); dy = std::abs(dy);
    if (diag) {                            // octile distance
        const int mn = std::min(dx, dy), mx = std::max(dx, dy);
        return static_cast<float>(mx - mn) * kOrtho + static_cast<float>(mn) * kDiag;
    }
    return static_cast<float>(dx + dy) * kOrtho;    // manhattan
}

// True if a straight line from cell a to cell b crosses only walkable cells.
// Walks the grid with integer Bresenham; on a diagonal step it forbids clipping a
// blocked orthogonal corner, matching the pathfinder's own corner rule.
bool LineOfSight(const NavGrid& grid, glm::ivec2 a, glm::ivec2 b) {
    int x = a.x, y = a.y;
    const int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
    const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
    if (!grid.Walkable(x, y)) return false;
    int err = dx - dy;
    while (x != b.x || y != b.y) {
        const int e2 = 2 * err;
        bool stepX = false, stepY = false;
        if (e2 > -dy) { err -= dy; x += sx; stepX = true; }
        if (e2 <  dx) { err += dx; y += sy; stepY = true; }
        if (stepX && stepY) {   // diagonal: don't cut through a blocked corner
            if (!grid.Walkable(x - sx, y) || !grid.Walkable(x, y - sy)) return false;
        }
        if (!grid.Walkable(x, y)) return false;
    }
    return true;
}

} // namespace

std::vector<glm::ivec2> AStar::FindPath(const NavGrid& grid, glm::ivec2 start, glm::ivec2 goal, bool allowDiagonal) {
    std::vector<glm::ivec2> path;
    if (!grid.Walkable(start.x, start.y) || !grid.Walkable(goal.x, goal.y)) return path;
    if (start == goal) { path.push_back(start); return path; }

    const int W = grid.width, H = grid.height;
    const auto idx = [&](int x, int y) { return static_cast<std::size_t>(y) * W + x; };
    const std::size_t N = static_cast<std::size_t>(W) * H;

    std::vector<float> g(N, std::numeric_limits<float>::max());
    std::vector<int>   from(N, -1);
    std::vector<char>  closed(N, 0);

    using Node = std::pair<float, int>;     // (fScore, cellIndex)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    g[idx(start.x, start.y)] = 0.0f;
    open.emplace(Heuristic(start.x - goal.x, start.y - goal.y, allowDiagonal), static_cast<int>(idx(start.x, start.y)));

    // 8 neighbour offsets (last four are diagonal).
    const int dirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    const int nDirs = allowDiagonal ? 8 : 4;

    while (!open.empty()) {
        const int cur = open.top().second; open.pop();
        if (closed[static_cast<std::size_t>(cur)]) continue;
        closed[static_cast<std::size_t>(cur)] = 1;
        const int cx = cur % W, cy = cur / W;
        if (cx == goal.x && cy == goal.y) break;

        for (int d = 0; d < nDirs; ++d) {
            const int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
            if (!grid.Walkable(nx, ny)) continue;
            if (d >= 4) {    // diagonal: don't cut through a blocked orthogonal corner
                if (!grid.Walkable(cx + dirs[d][0], cy) || !grid.Walkable(cx, cy + dirs[d][1])) continue;
            }
            const std::size_t ni = idx(nx, ny);
            if (closed[ni]) continue;
            const float step = (d >= 4) ? kDiag : kOrtho;
            const float tentative = g[static_cast<std::size_t>(cur)] + step;
            if (tentative < g[ni]) {
                g[ni] = tentative;
                from[ni] = cur;
                const float f = tentative + Heuristic(nx - goal.x, ny - goal.y, allowDiagonal);
                open.emplace(f, static_cast<int>(ni));
            }
        }
    }

    const std::size_t gi = idx(goal.x, goal.y);
    if (from[gi] == -1 && !(start == goal)) return path;    // unreachable
    for (int c = static_cast<int>(gi); c != -1; c = from[static_cast<std::size_t>(c)])
        path.emplace_back(c % W, c / W);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<glm::ivec2> AStar::SmoothPath(const NavGrid& grid, const std::vector<glm::ivec2>& cells) {
    if (cells.size() <= 2) return cells;
    std::vector<glm::ivec2> out;
    out.reserve(cells.size());
    out.push_back(cells.front());
    std::size_t anchor = 0;                         // last kept vertex
    for (std::size_t i = 1; i + 1 < cells.size(); ++i) {
        // If the anchor can still see the *next* cell, cells[i] is a redundant
        // interior point on a straight run — skip it. Otherwise the path bends
        // here, so keep cells[i] and re-anchor to it.
        if (!LineOfSight(grid, cells[anchor], cells[i + 1])) {
            out.push_back(cells[i]);
            anchor = i;
        }
    }
    out.push_back(cells.back());
    return out;
}

std::vector<glm::vec3> AStar::FindPathWorld(const NavGrid& grid, const glm::vec3& start, const glm::vec3& goal, bool allowDiagonal) {
    const glm::ivec2 s = grid.WorldToCell(start), gg = grid.WorldToCell(goal);
    const std::vector<glm::ivec2> cells = SmoothPath(grid, FindPath(grid, s, gg, allowDiagonal));
    std::vector<glm::vec3> world;
    world.reserve(cells.size());
    for (const glm::ivec2& c : cells) world.push_back(grid.CellToWorld(c.x, c.y));
    return world;
}

} // namespace ai
} // namespace engine