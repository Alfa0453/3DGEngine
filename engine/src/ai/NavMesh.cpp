#include "engine/ai/NavMesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace engine {
namespace ai {
namespace {

// Twice the signed area of (a,b,c) in XZ. >0 = c is left of a->b.
float TriArea2(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
}
float DistXZ(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}
bool ApproxEq(const glm::vec3& a, const glm::vec3& b) { return DistXZ(a, b) < 1e-5f; }

} // namespace

int NavMesh::AddPolygon(const std::vector<int>& v) {
    polys.push_back(Poly{ v });
    return static_cast<int>(polys.size()) - 1;
}

glm::vec3 NavMesh::Centroid(int poly) const {
    glm::vec3 c(0.0f);
    const Poly& p = polys[static_cast<std::size_t>(poly)];
    for (int vi : p.verts) c += vertices[static_cast<std::size_t>(vi)];
    if (!p.verts.empty()) c /= static_cast<float>(p.verts.size());
    return c;
}

void NavMesh::Build() {
    const std::size_t n = polys.size();
    m_links.assign(n, {});
    const float kEps = 1e-3f;

    for (std::size_t ai = 0; ai < n; ++ai) {
        const Poly& A = polys[ai];
        const int na = static_cast<int>(A.verts.size());
        for (int ea = 0; ea < na; ++ea) {
            const glm::vec3& a0 = vertices[static_cast<std::size_t>(A.verts[ea])];
            const glm::vec3& a1 = vertices[static_cast<std::size_t>(A.verts[(ea + 1) % na])];
            const glm::vec3 da = a1 - a0;
            const float lenA2 = da.x * da.x + da.z * da.z;
            if (lenA2 < 1e-9f) continue;

            for (std::size_t bi = 0; bi < n; ++bi) {
                if (bi == ai) continue;
                const Poly& B = polys[bi];
                const int nb = static_cast<int>(B.verts.size());
                for (int eb = 0; eb < nb; ++eb) {
                    const glm::vec3& b0 = vertices[static_cast<std::size_t>(B.verts[eb])];
                    const glm::vec3& b1 = vertices[static_cast<std::size_t>(B.verts[(eb + 1) % nb])];
                    const glm::vec3 db = b1 - b0;

                    // Opposite direction (shared boundary of two CCW polys).
                    if (da.x * db.x + da.z * db.z >= 0.0f) continue;
                    // Collinear: B's endpoints lie on A's line.
                    if (std::fabs(TriArea2(a0, a1, b0)) > kEps * std::sqrt(lenA2)) continue;
                    if (std::fabs(TriArea2(a0, a1, b1)) > kEps * std::sqrt(lenA2)) continue;

                    // Overlap interval along A (params 0..1 for a0..a1).
                    const float tb0 = ((b0.x - a0.x) * da.x + (b0.z - a0.z) * da.z) / lenA2;
                    const float tb1 = ((b1.x - a0.x) * da.x + (b1.z - a0.z) * da.z) / lenA2;
                    const float lo = std::max(0.0f, std::min(tb0, tb1));
                    const float hi = std::min(1.0f, std::max(tb0, tb1));
                    if (hi - lo <= kEps) continue;

                    Link link;
                    link.to    = static_cast<int>(bi);
                    link.left  = a0 + da * lo;   // nearer a0 (= verts[ea]) -> left
                    link.right = a0 + da * hi;
                    m_links[ai].push_back(link);
                }
            }
        }
    }
}

bool NavMesh::Adjacent(int a, int b) const {
    if (a < 0 || a >= static_cast<int>(m_links.size())) return false;
    for (const Link& l : m_links[static_cast<std::size_t>(a)]) if (l.to == b) return true;
    return false;
}

int NavMesh::PolyAt(const glm::vec3& pt) const {
    for (std::size_t pi = 0; pi < polys.size(); ++pi) {
        const Poly& p = polys[pi];
        const int nn = static_cast<int>(p.verts.size());
        bool inside = true;
        for (int e = 0; e < nn; ++e) {
            const glm::vec3& a = vertices[static_cast<std::size_t>(p.verts[e])];
            const glm::vec3& b = vertices[static_cast<std::size_t>(p.verts[(e + 1) % nn])];
            if (TriArea2(a, b, pt) < -1e-4f) { inside = false; break; }
        }
        if (inside) return static_cast<int>(pi);
    }
    return -1;
}

int NavMesh::NearestPoly(const glm::vec3& pt) const {
    int best = -1; float bestD = std::numeric_limits<float>::max();
    for (std::size_t pi = 0; pi < polys.size(); ++pi) {
        const float d = DistXZ(Centroid(static_cast<int>(pi)), pt);
        if (d < bestD) { bestD = d; best = static_cast<int>(pi); }
    }
    return best;
}

bool NavMesh::FindPath(const glm::vec3& start, const glm::vec3& goal,
                       std::vector<glm::vec3>& outPath) const {
    outPath.clear();
    if (polys.empty()) return false;

    int sp = PolyAt(start); if (sp < 0) sp = NearestPoly(start);
    int gp = PolyAt(goal);  if (gp < 0) gp = NearestPoly(goal);
    if (sp < 0 || gp < 0) return false;

    // A* over the polygon graph.
    const std::size_t n = polys.size();
    std::vector<float> g(n, std::numeric_limits<float>::max());
    std::vector<int>   came(n, -1);
    std::vector<char>  closed(n, 0);
    using QN = std::pair<float, int>;
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    g[static_cast<std::size_t>(sp)] = 0.0f;
    open.push({ DistXZ(Centroid(sp), goal), sp });
    while (!open.empty()) {
        const int cur = open.top().second; open.pop();
        if (cur == gp) break;
        if (closed[static_cast<std::size_t>(cur)]) continue;
        closed[static_cast<std::size_t>(cur)] = 1;
        for (const Link& l : m_links[static_cast<std::size_t>(cur)]) {
            if (closed[static_cast<std::size_t>(l.to)]) continue;
            const float ng = g[static_cast<std::size_t>(cur)] + DistXZ(Centroid(cur), Centroid(l.to));
            if (ng < g[static_cast<std::size_t>(l.to)]) {
                g[static_cast<std::size_t>(l.to)] = ng;
                came[static_cast<std::size_t>(l.to)] = cur;
                open.push({ ng + DistXZ(Centroid(l.to), goal), l.to });
            }
        }
    }
    if (sp != gp && came[static_cast<std::size_t>(gp)] < 0) return false;

    // Reconstruct the polygon corridor.
    std::vector<int> corridor;
    for (int c = gp; c != -1; c = (c == sp) ? -1 : came[static_cast<std::size_t>(c)])
        corridor.push_back(c);
    std::reverse(corridor.begin(), corridor.end());

    // Portals from the stored links between consecutive corridor polys.
    std::vector<glm::vec3> portalL, portalR;
    portalL.push_back(start); portalR.push_back(start);
    for (std::size_t i = 0; i + 1 < corridor.size(); ++i) {
        for (const Link& l : m_links[static_cast<std::size_t>(corridor[i])]) {
            if (l.to == corridor[i + 1]) { portalL.push_back(l.left); portalR.push_back(l.right); break; }
        }
    }
    portalL.push_back(goal); portalR.push_back(goal);

    // Simple Stupid Funnel Algorithm (Mononen).
    std::vector<glm::vec3> path;
    path.push_back(start);
    glm::vec3 apex = start, left = portalL[0], right = portalR[0];
    std::size_t apexI = 0, leftI = 0, rightI = 0;
    for (std::size_t i = 1; i < portalL.size(); ++i) {
        const glm::vec3 l = portalL[i], r = portalR[i];
        if (TriArea2(apex, right, r) <= 0.0f) {
            if (ApproxEq(apex, right) || TriArea2(apex, left, r) > 0.0f) {
                right = r; rightI = i;
            } else {
                path.push_back(left);
                apex = left; apexI = leftI;
                left = right = apex; leftI = rightI = apexI;
                i = apexI; continue;
            }
        }
        if (TriArea2(apex, left, l) >= 0.0f) {
            if (ApproxEq(apex, left) || TriArea2(apex, right, l) < 0.0f) {
                left = l; leftI = i;
            } else {
                path.push_back(right);
                apex = right; apexI = rightI;
                left = right = apex; leftI = rightI = apexI;
                i = apexI; continue;
            }
        }
    }
    if (path.empty() || !ApproxEq(path.back(), goal)) path.push_back(goal);
    outPath = std::move(path);
    return true;
}

} // namespace ai
} // namespace engine
