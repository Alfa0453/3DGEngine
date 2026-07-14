#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace engine {
namespace ai {

// A polygon navigation mesh: convex walkable polygons over a shared vertex list,
// linked by shared edges. Navigation is done in the XZ plane (Y is height). Paths
// come out as a taut list of waypoints via the funnel algorithm, so an agent walks
// straight lines and only turns at corners -- smoother and more general than a grid.
//
// Build a mesh by adding convex, counter-clockwise (in XZ) polygons, then Build().
class NavMesh {
public:
    struct Poly {
        std::vector<int> verts;         // CCW vertex indices (in XZ)
        std::vector<int> neighbors;     // per edge i (verts[i]->verts[i+1]): neighbour poly, or -1
    };

    std::vector<glm::vec3> vertices;
    std::vector<Poly>      polys;

    // Add a convex polygon by vertex indices (CCW in XZ). Call Build() after all.
    int AddPolygon(const std::vector<int>& vertexIndices);
    // Compute edge adjacency (which polygons share an edge). Call once after adding.
    void Build();

    int      PolyAt(const glm::vec3& p) const;   // polygon containing p (XZ), or -1
    int      NearestPoly(const glm::vec3& p) const;  // fallback if p is off-mesh
    glm::vec3 Centroid(int poly) const;
    bool      Adjacent(int a, int b) const;        // do polys a,b share a portal?

    // Find a path from start to goal. Returns the taut waypoint list (start..goal)
    // through the funnel, or false if unreachable. Off-mesh endpoints snap to the
    // nearest polygon.
    bool FindPath(const glm::vec3& start, const glm::vec3& goal,
                  std::vector<glm::vec3>& outPath) const;
            
    std::size_t PolyCount() const { return polys.size(); }

private:
    // One portal out of a polygon: the neighbour and the shared segment, oriented
    // (left,right) for travel out of this polygon.
    struct Link { int to; glm::vec3 left, right; };
    std::vector<std::vector<Link>> m_links;        // parallel to polys
};

} // namespace ai
} // namespace engine