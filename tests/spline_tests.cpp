#include <engine/math/Spline.h>

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <vector>

namespace {
bool Near(float a, float b, float tolerance = 0.02f) {
    return std::abs(a - b) <= tolerance;
}
}

int main() {
    engine::Spline line({glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f),
                         glm::vec3(10.0f, 0.0f, 0.0f)});
    if (!Near(line.Length(), 10.0f) ||
        !Near(line.PositionAtDistance(2.5f).x, 2.5f) ||
        glm::dot(line.TangentAtDistance(7.0f), glm::vec3(1.0f, 0.0f, 0.0f)) < 0.999f) {
        std::cerr << "Straight spline arc-length sampling failed\n";
        return 1;
    }

    float distance = 0.0f;
    const glm::vec3 closest = line.ClosestPoint(glm::vec3(3.37f, 2.0f, 0.0f), &distance);
    if (!Near(closest.x, 3.37f, 0.03f) || !Near(closest.y, 0.0f) ||
        !Near(distance, 3.37f, 0.03f)) {
        std::cerr << "Closest-point chord projection failed\n";
        return 1;
    }

    engine::Spline curve({glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 4.0f),
                          glm::vec3(2.0f, 0.0f, 4.5f), glm::vec3(8.0f, 0.0f, 0.0f)});
    std::vector<glm::vec3> uniform;
    curve.SampleUniform(9, uniform);
    float minStep = 1000.0f;
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < uniform.size(); ++i) {
        const float step = glm::length(uniform[i] - uniform[i - 1]);
        minStep = std::min(minStep, step);
        maxStep = std::max(maxStep, step);
    }
    if (minStep <= 0.0f || maxStep / minStep > 1.15f) {
        std::cerr << "Uniform spline sampling is uneven\n";
        return 1;
    }

    std::cout << "Spline tests passed\n";
    return 0;
}
