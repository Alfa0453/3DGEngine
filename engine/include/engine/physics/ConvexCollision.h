#pragma once

// -----------------------------------------------------------------------------
// Pass-2 narrow phase: exact convex/convex collision by support mapping.
//
// GJK decides overlap using only a support function (the farthest point of a
// shape along a direction); EPA then expands the Minkowski-difference polytope
// to recover the penetration normal + depth and a contact point. This gives the
// engine exact collision for shapes that had no dedicated pair test -- most
// importantly ConvexHull colliders (which previously only collided with spheres
// and capsules via the triangle-mesh path) -- without touching the tuned
// sphere/box/capsule solvers.
//
// Header-only (glm only) so it can be pulled into PhysicsWorld.cpp without a
// CMake reconfigure. Everything lives in engine::physics::convex.
// -----------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace engine::physics::convex {

enum class Kind { Sphere, Box, Capsule, Cylinder, Cone, Pyramid, Hull };

// A convex collider prepared for support mapping. All quantities are WORLD space.
// `basis` columns are the shape's local axes (unit, right-handed). Sphere and
// Capsule are represented as a point/segment "core" plus `roundRadius`; the other
// shapes are polytopes/quadrics with roundRadius 0. This core+radius split gives
// smooth, well-defined contacts for the rounded shapes.
struct Shape {
    Kind      kind = Kind::Box;
    glm::vec3 center{0.0f};        // world position of the local origin
    glm::mat3 basis{1.0f};         // world orientation (columns = local axes)
    glm::vec3 halfExtents{0.5f};   // Box/Pyramid; Cone/Cylinder use x,z = radius
    float     radius = 0.5f;       // Sphere/Capsule/Cylinder/Cone cross radius
    float     halfHeight = 0.5f;   // Capsule segment / Cylinder & Cone axis half-length
    float     roundRadius = 0.0f;  // Minkowski inflation (Sphere/Capsule shell)
    const glm::vec3* hullPoints = nullptr;  // Hull: world-space vertices
    std::size_t      hullCount = 0;
};

// Farthest point of the shape's *core* (roundRadius excluded) along world dir `d`.
inline glm::vec3 CoreSupport(const Shape& s, const glm::vec3& d) {
    switch (s.kind) {
        case Kind::Sphere:
            return s.center;   // point core
        case Kind::Capsule: {
            const glm::vec3 axis = s.basis[1];                 // local +Y
            const float sign = glm::dot(d, axis) >= 0.0f ? 1.0f : -1.0f;
            return s.center + axis * (sign * s.halfHeight);
        }
        case Kind::Box: {
            glm::vec3 p = s.center;
            for (int i = 0; i < 3; ++i) {
                const float sign = glm::dot(d, s.basis[i]) >= 0.0f ? 1.0f : -1.0f;
                p += s.basis[i] * (sign * s.halfExtents[i]);
            }
            return p;
        }
        case Kind::Cylinder: {
            const glm::vec3 ax = s.basis[1];
            const float ySign = glm::dot(d, ax) >= 0.0f ? 1.0f : -1.0f;
            glm::vec3 p = s.center + ax * (ySign * s.halfHeight);
            // Radial component: farthest rim point along d projected into the XZ disc.
            glm::vec3 radial = d - ax * glm::dot(d, ax);
            const float len = glm::length(radial);
            if (len > 1e-6f) p += (radial / len) * s.radius;
            return p;
        }
        case Kind::Cone: {
            // Apex at +halfHeight, base disc of `radius` at -halfHeight.
            const glm::vec3 ax = s.basis[1];
            const glm::vec3 apex = s.center + ax * s.halfHeight;
            glm::vec3 base = s.center - ax * s.halfHeight;
            glm::vec3 radial = d - ax * glm::dot(d, ax);
            const float len = glm::length(radial);
            if (len > 1e-6f) base += (radial / len) * s.radius;
            return glm::dot(d, apex) >= glm::dot(d, base) ? apex : base;
        }
        case Kind::Pyramid: {
            // Apex at +Y; square base (halfExtents.x by .z) at -Y. Support = farthest
            // of the apex and the four base corners.
            const glm::vec3 ax = s.basis[1];
            const glm::vec3 ex = s.basis[0] * s.halfExtents.x;
            const glm::vec3 ez = s.basis[2] * s.halfExtents.z;
            const glm::vec3 apex = s.center + ax * s.halfExtents.y;
            glm::vec3 best = apex; float bestDot = glm::dot(d, apex);
            const glm::vec3 baseC = s.center - ax * s.halfExtents.y;
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sz = -1; sz <= 1; sz += 2) {
                    const glm::vec3 corner = baseC + ex * float(sx) + ez * float(sz);
                    const float dt = glm::dot(d, corner);
                    if (dt > bestDot) { bestDot = dt; best = corner; }
                }
            return best;
        }
        case Kind::Hull: {
            if (!s.hullPoints || s.hullCount == 0) return s.center;
            glm::vec3 best = s.hullPoints[0]; float bestDot = glm::dot(d, best);
            for (std::size_t i = 1; i < s.hullCount; ++i) {
                const float dt = glm::dot(d, s.hullPoints[i]);
                if (dt > bestDot) { bestDot = dt; best = s.hullPoints[i]; }
            }
            return best;
        }
    }
    return s.center;
}

// Full support (core inflated by roundRadius along d).
inline glm::vec3 Support(const Shape& s, const glm::vec3& d) {
    glm::vec3 p = CoreSupport(s, d);
    if (s.roundRadius > 0.0f) {
        const float len = glm::length(d);
        if (len > 1e-9f) p += (d / len) * s.roundRadius;
    }
    return p;
}

// One vertex of the Minkowski difference A (-) B, remembering the A-side support
// so EPA can recover a world contact point on A's surface.
struct SupportVert {
    glm::vec3 v;   // supportA(d) - supportB(-d)
    glm::vec3 a;   // supportA(d)
};

inline SupportVert MinkowskiSupport(const Shape& A, const Shape& B, const glm::vec3& d) {
    SupportVert sv;
    sv.a = Support(A, d);
    sv.v = sv.a - Support(B, -d);
    return sv;
}

struct GjkResult {
    bool intersecting = false;
    std::array<SupportVert, 4> simplex{};
    int count = 0;
};

// Canonical GJK (Muratori / "Winter" form): the simplex is stored newest-first in
// `s[0..n)`, and each do-simplex step either declares the origin enclosed or picks
// the sub-feature (edge/face) closest to the origin and the next search direction.
namespace detail {
inline bool SameDir(const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b) > 0.0f; }

// Returns true when the origin is enclosed by the tetrahedron; otherwise reduces
// `s`/`n` to the closest sub-simplex and writes the next search direction.
inline bool DoSimplex(std::array<SupportVert, 4>& s, int& n, glm::vec3& dir) {
    const glm::vec3 a = s[0].v;        // newest
    const glm::vec3 ao = -a;
    if (n == 2) {
        const glm::vec3 b = s[1].v, ab = b - a;
        if (SameDir(ab, ao)) dir = glm::cross(glm::cross(ab, ao), ab);
        else { n = 1; dir = ao; }   // keep just [a]
        return false;
    }
    if (n == 3) {
        const glm::vec3 b = s[1].v, c = s[2].v;
        const glm::vec3 ab = b - a, ac = c - a, abc = glm::cross(ab, ac);
        if (SameDir(glm::cross(abc, ac), ao)) {
            if (SameDir(ac, ao)) { s[1] = s[2]; n = 2; dir = glm::cross(glm::cross(ac, ao), ac); }
            else { n = 2; dir = SameDir(ab, ao) ? glm::cross(glm::cross(ab, ao), ab) : ao;
                   if (!SameDir(ab, ao)) n = 1; }
        } else if (SameDir(glm::cross(ab, abc), ao)) {
            n = 2; dir = SameDir(ab, ao) ? glm::cross(glm::cross(ab, ao), ab) : ao;
            if (!SameDir(ab, ao)) n = 1;
        } else {
            if (SameDir(abc, ao)) dir = abc;
            else { std::swap(s[1], s[2]); dir = -abc; }   // flip winding
        }
        return false;
    }
    // n == 4: origin is enclosed unless it lies outside one of the three new faces.
    const glm::vec3 b = s[1].v, c = s[2].v, d = s[3].v;
    const glm::vec3 ab = b - a, ac = c - a, ad = d - a;
    const glm::vec3 abc = glm::cross(ab, ac);
    const glm::vec3 acd = glm::cross(ac, ad);
    const glm::vec3 adb = glm::cross(ad, ab);
    if (SameDir(abc, ao)) { n = 3; dir = abc; return false; }                          // keep [a,b,c]
    if (SameDir(acd, ao)) { s[1] = s[2]; s[2] = s[3]; n = 3; dir = acd; return false; } // [a,c,d]
    if (SameDir(adb, ao)) { s[2] = s[1]; s[1] = s[3]; n = 3; dir = adb; return false; } // [a,d,b]
    return true;   // enclosed
}
} // namespace detail

// Boolean GJK over the Minkowski difference; on intersection returns the
// terminating tetrahedron simplex for EPA to expand.
inline GjkResult Gjk(const Shape& A, const Shape& B) {
    GjkResult r;
    glm::vec3 dir(1.0f, 0.0f, 0.0f);
    const glm::vec3 delta = A.center - B.center;   // center seed: faster + more robust
    if (glm::dot(delta, delta) > 1e-12f) dir = delta;

    std::array<SupportVert, 4> s{};
    int n = 0;
    s[0] = MinkowskiSupport(A, B, dir);
    dir = -s[0].v;
    n = 1;

    for (int iter = 0; iter < 64; ++iter) {
        if (glm::dot(dir, dir) < 1e-12f) { r.intersecting = true; break; }
        const SupportVert sv = MinkowskiSupport(A, B, dir);
        if (glm::dot(sv.v, dir) < 0.0f) return r;   // origin unreachable -> no overlap
        // Prepend the new vertex (newest-first).
        for (int i = n; i > 0; --i) s[i] = s[i - 1];
        s[0] = sv; ++n;
        if (detail::DoSimplex(s, n, dir)) { r.intersecting = true; break; }
    }

    r.count = n;
    r.simplex = s;
    return r;
}

// ---------------------------------------------------------------------------
// EPA: expand the polytope of Minkowski-difference support points until the
// closest face to the origin stops moving; that face normal is the minimum
// translation direction and its distance is the penetration depth.
// ---------------------------------------------------------------------------
struct EpaResult {
    bool      ok = false;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  // from A toward B, unit
    float     depth = 0.0f;
    glm::vec3 contact{0.0f};             // world point on A's surface
};

namespace detail {
struct Face { int a, b, c; glm::vec3 n; float dist; };

inline void AddFace(std::vector<Face>& faces, const std::vector<SupportVert>& verts,
                    int a, int b, int c) {
    Face f; f.a = a; f.b = b; f.c = c;
    const glm::vec3 ab = verts[b].v - verts[a].v;
    const glm::vec3 ac = verts[c].v - verts[a].v;
    glm::vec3 n = glm::cross(ab, ac);
    const float len = glm::length(n);
    if (len < 1e-12f) { f.n = glm::vec3(0.0f); f.dist = 1e30f; faces.push_back(f); return; }
    n /= len;
    float d = glm::dot(n, verts[a].v);
    if (d < 0.0f) { n = -n; d = -d; std::swap(f.b, f.c); }  // outward-facing
    f.n = n; f.dist = d;
    faces.push_back(f);
}
} // namespace detail

inline EpaResult Epa(const Shape& A, const Shape& B, const GjkResult& gjk) {
    using detail::Face;
    EpaResult res;
    if (gjk.count < 4) return res;   // need a tetrahedron seed

    std::vector<SupportVert> verts(gjk.simplex.begin(), gjk.simplex.begin() + 4);
    std::vector<Face> faces;
    faces.reserve(32);
    detail::AddFace(faces, verts, 0, 1, 2);
    detail::AddFace(faces, verts, 0, 2, 3);
    detail::AddFace(faces, verts, 0, 3, 1);
    detail::AddFace(faces, verts, 1, 3, 2);

    Face closest{};
    for (int iter = 0; iter < 64; ++iter) {
        // Closest face to the origin.
        int ci = -1; float cd = 1e30f;
        for (int i = 0; i < (int)faces.size(); ++i)
            if (faces[i].dist < cd) { cd = faces[i].dist; ci = i; }
        if (ci < 0) return res;
        closest = faces[ci];

        // Support in the face normal direction: if no further out, we've converged.
        const SupportVert sv = MinkowskiSupport(A, B, closest.n);
        const float d = glm::dot(closest.n, sv.v);
        if (d - closest.dist < 1e-4f) break;

        // Remove all faces the new point can "see", then re-close the hole. Edges
        // shared by two removed faces are interior; boundary edges form the horizon.
        const int newIdx = (int)verts.size();
        verts.push_back(sv);
        std::vector<std::pair<int,int>> horizon;
        std::vector<Face> kept;
        kept.reserve(faces.size());
        for (const Face& f : faces) {
            if (glm::dot(f.n, sv.v - verts[f.a].v) > 1e-6f) {
                const int e[3][2] = { {f.a,f.b}, {f.b,f.c}, {f.c,f.a} };
                for (auto& ed : e) {
                    // Cancel an edge already on the horizon (shared interior edge).
                    bool cancelled = false;
                    for (std::size_t k = 0; k < horizon.size(); ++k)
                        if (horizon[k].first == ed[1] && horizon[k].second == ed[0]) {
                            horizon.erase(horizon.begin() + k); cancelled = true; break;
                        }
                    if (!cancelled) horizon.emplace_back(ed[0], ed[1]);
                }
            } else {
                kept.push_back(f);
            }
        }
        faces.swap(kept);
        for (const auto& ed : horizon) detail::AddFace(faces, verts, ed.first, ed.second, newIdx);
        if (faces.size() > 256) break;   // safety
    }

    // Barycentric projection of the origin onto the closest face, reused to place
    // the contact point on A's surface (weighted A-side supports).
    const glm::vec3 A0 = verts[closest.a].v, B0 = verts[closest.b].v, C0 = verts[closest.c].v;
    const glm::vec3 p = closest.n * closest.dist;   // origin projected onto face plane
    const glm::vec3 v0 = B0 - A0, v1 = C0 - A0, v2 = p - A0;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    float u = 1.0f, vb = 0.0f, w = 0.0f;
    if (std::fabs(denom) > 1e-12f) {
        vb = (d11 * d20 - d01 * d21) / denom;
        w  = (d00 * d21 - d01 * d20) / denom;
        u  = 1.0f - vb - w;
    }
    const glm::vec3 contactA = verts[closest.a].a * u + verts[closest.b].a * vb + verts[closest.c].a * w;

    res.ok = closest.dist >= 0.0f && glm::dot(closest.n, closest.n) > 0.5f;
    res.normal = closest.n;      // from A toward B
    res.depth = closest.dist;
    res.contact = contactA;      // point on A's surface (normal points into B)
    return res;
}

} // namespace engine::physics::convex
