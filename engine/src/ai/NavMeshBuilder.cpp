#include "engine/ai/NavMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {
namespace ai {

NavMesh NavMeshBuilder::Build(const NavBuildConfig& cfg, const std::vector<NavObstacle>& obstacles) {
    NavMesh nav;

    const float cs = (cfg.cellSize > 1e-4f) ? cfg.cellSize : 0.5f;
    const glm::vec3 mn = cfg.boundsMin;
    const int nx = std::max(1, static_cast<int>(std::ceil((cfg.boundsMax.x - mn.x) / cs)));
    const int nz = std::max(1, static_cast<int>(std::ceil((cfg.boundsMax.z - mn.z) / cs)));

    // 1) Voxelize: mark walkable cells (blocked = within agentRadius of an obstacle).
    std::vector<char> walk(static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz), 1);
    auto at = [&](int i, int j) -> char& { return walk[static_cast<std::size_t>(j) * nx + i]; };
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const float cx = mn.x + (i + 0.5f) * cs;
            const float cz = mn.z + (j + 0.5f) * cs;
            bool blocked = false;
            for (const NavObstacle& o : obstacles) {
                if (std::fabs(cx - o.center.x) <= o.halfExtents.x + cfg.agentRadius &&
                    std::fabs(cz - o.center.z) <= o.halfExtents.z + cfg.agentRadius) {
                    blocked = true; break;
                }
            }
            at(i, j) = blocked ? 0 : 1;
        }
    }

    // 2) Greedy rectangle merge of walkable cells.
    std::vector<char> used(walk.size(), 0);
    auto usedAt = [&](int i, int j) -> char& { return used[static_cast<std::size_t>(j) * nx + i]; };
    struct Rect { int i, j, w, h; };
    std::vector<Rect> rects;
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            if (!at(i, j) || usedAt(i, j)) continue;
            // Grow width along the row.
            int w = 0;
            while (i + w < nx && at(i + w, j) && !usedAt(i + w, j)) ++w;
            // Grow height while the whole span stays walkable + unused.
            int h = 1;
            while (j + h < nz) {
                bool ok = true;
                for (int k = 0; k < w; ++k)
                    if (!at(i + k, j + h) || usedAt(i + k, j + h)) { ok = false; break; }
                if (!ok) break;
                ++h;
            }
            for (int jj = j; jj < j + h; ++jj)
                for (int ii = i; ii < i + w; ++ii) usedAt(ii, jj) = 1;
            rects.push_back({ i, j, w, h });
        }
    }

    // 3) Emit each rectangle as a CCW polygon (fresh verts; geometric Build links).
    for (const Rect& r : rects) {
        const float x0 = mn.x + r.i * cs,       x1 = mn.x + (r.i + r.w) * cs;
        const float z0 = mn.z + r.j * cs,       z1 = mn.z + (r.j + r.h) * cs;
        const int base = static_cast<int>(nav.vertices.size());
        nav.vertices.push_back({ x0, mn.y, z0 });
        nav.vertices.push_back({ x1, mn.y, z0 });
        nav.vertices.push_back({ x1, mn.y, z1 });
        nav.vertices.push_back({ x0, mn.y, z1 });
        nav.AddPolygon({ base, base + 1, base + 2, base + 3 });
    }
    nav.Build();
    return nav;
}

} // namespace ai
} // namespace engine
