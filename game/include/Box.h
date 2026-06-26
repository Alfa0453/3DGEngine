#pragma once

#include <glm/glm.hpp>

#include <cmath>

// An axis-aligned box: a centre, half-extents, and a render colour. One
// description used two ways — physics (the AABB overlap test below) and
// rendering (the scale applied to the unit cube).
struct Box {
    glm::vec3 center{0.0f};
    glm::vec3 half{0.5f};
    glm::vec3 color{1.0f};
};

// Axis-Aligned Bounding Box overlap test: two boxes intersect only when they
// overlap on every axis at once. The cheapest, most common collision check.
inline bool Overlap(const Box& a, const Box& b) {
    return std::abs(a.center.x - b.center.x) <= (a.half.x + b.half.x) &&
           std::abs(a.center.y - b.center.y) <= (a.half.y + b.half.y) &&
           std::abs(a.center.z - b.center.z) <= (a.half.z + b.half.z);
}