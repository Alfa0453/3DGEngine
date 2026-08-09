#include "EditorBranding.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace editor::branding {
namespace {

struct Point { float x; float y; };

float SegmentDistance(Point p, Point a, Point b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float lengthSquared = vx * vx + vy * vy;
    const float t = lengthSquared > 0.0f
        ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / lengthSquared,
                     0.0f, 1.0f)
        : 0.0f;
    const float dx = p.x - (a.x + vx * t);
    const float dy = p.y - (a.y + vy * t);
    return std::sqrt(dx * dx + dy * dy);
}

void Blend(std::array<float, 4>& destination,
           const std::array<float, 4>& source, float coverage) {
    coverage = std::clamp(coverage, 0.0f, 1.0f) * source[3];
    destination[0] = destination[0] * (1.0f - coverage) + source[0] * coverage;
    destination[1] = destination[1] * (1.0f - coverage) + source[1] * coverage;
    destination[2] = destination[2] * (1.0f - coverage) + source[2] * coverage;
    destination[3] = std::max(destination[3], coverage);
}

WindowIcon BuildIcon() {
    constexpr int size = 64;
    WindowIcon icon;
    icon.width = size;
    icon.height = size;
    icon.rgba.resize(size * size * 4);

    constexpr std::array<float, 4> cyan{0.12f, 0.82f, 0.96f, 1.0f};
    constexpr std::array<float, 4> blue{0.18f, 0.45f, 0.94f, 1.0f};
    constexpr std::array<float, 4> gold{1.00f, 0.67f, 0.18f, 1.0f};
    constexpr std::array<Point, 8> vertices{{
        {32.0f, 10.0f}, {12.0f, 21.5f}, {32.0f, 33.0f}, {52.0f, 21.5f},
        {12.0f, 42.5f}, {32.0f, 54.0f}, {52.0f, 42.5f}, {32.0f, 31.0f}
    }};
    struct Edge { int a; int b; const std::array<float, 4>* color; };
    const std::array<Edge, 12> edges{{
        {0, 1, &cyan}, {0, 3, &blue}, {1, 2, &cyan}, {3, 2, &blue},
        {1, 4, &cyan}, {3, 6, &blue}, {4, 5, &cyan}, {6, 5, &blue},
        {2, 5, &gold}, {0, 7, &gold}, {4, 2, &cyan}, {6, 2, &blue}
    }};

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float dx = std::max(std::abs(px - 32.0f) - 25.0f, 0.0f);
            const float dy = std::max(std::abs(py - 32.0f) - 25.0f, 0.0f);
            const float roundedDistance = std::sqrt(dx * dx + dy * dy);
            const float backgroundCoverage = std::clamp(4.5f - roundedDistance, 0.0f, 1.0f);
            std::array<float, 4> color{0.025f, 0.055f, 0.105f, backgroundCoverage};

            const Point p{px, py};
            for (const Edge& edge : edges) {
                const float distance = SegmentDistance(p, vertices[edge.a], vertices[edge.b]);
                Blend(color, *edge.color, std::clamp(2.15f - distance, 0.0f, 1.0f));
            }
            const float centerDistance = std::hypot(px - 32.0f, py - 33.0f);
            Blend(color, gold, std::clamp(3.8f - centerDistance, 0.0f, 1.0f));

            const std::size_t index = static_cast<std::size_t>((y * size + x) * 4);
            for (int channel = 0; channel < 4; ++channel) {
                icon.rgba[index + channel] = static_cast<unsigned char>(
                    std::clamp(color[channel], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
    return icon;
}

} // namespace

const WindowIcon& Icon() {
    static const WindowIcon icon = BuildIcon();
    return icon;
}

} // namespace editor::branding
