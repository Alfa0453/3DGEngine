#include "engine/graphics/Primitives.h"
#include "engine/graphics/VertexLayout.h"

#include <glm/gtc/constants.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>


namespace engine {
namespace primitives{
namespace {
// Shared layout for every primitive: position, normal, texcoord.
VertexLayout PNT() { return VertexLayout{ {3}, {3}, {2} }; }
} // anonymous namespace

Mesh Cube() {
    const std::vector<float> v = {
        // front (+Z)
        -0.5f,-0.5f, 0.5f,   0.0f, 0.0f, 1.0f,   0.0f,0.0f,
         0.5f,-0.5f, 0.5f,   0.0f, 0.0f, 1.0f,   1.0f,0.0f,
         0.5f, 0.5f, 0.5f,   0.0f, 0.0f, 1.0f,   1.0f,1.0f,
        -0.5f, 0.5f, 0.5f,   0.0f, 0.0f, 1.0f,   0.0f,1.0f,
        // back (-Z)
         0.5f,-0.5f,-0.5f,   0.0f, 0.0f,-1.0f,   0.0f,0.0f,
        -0.5f,-0.5f,-0.5f,   0.0f, 0.0f,-1.0f,   1.0f,0.0f,
        -0.5f, 0.5f,-0.5f,   0.0f, 0.0f,-1.0f,   1.0f,1.0f,
         0.5f, 0.5f,-0.5f,   0.0f, 0.0f,-1.0f,   0.0f,1.0f,
        // left (-X)
        -0.5f,-0.5f,-0.5f,  -1.0f, 0.0f, 0.0f,   0.0f,0.0f,
        -0.5f,-0.5f, 0.5f,  -1.0f, 0.0f, 0.0f,   1.0f,0.0f,
        -0.5f, 0.5f, 0.5f,  -1.0f, 0.0f, 0.0f,   1.0f,1.0f,
        -0.5f, 0.5f,-0.5f,  -1.0f, 0.0f, 0.0f,   0.0f,1.0f,
        // right (+X)
         0.5f,-0.5f, 0.5f,   1.0f, 0.0f, 0.0f,   0.0f,0.0f,
         0.5f,-0.5f,-0.5f,   1.0f, 0.0f, 0.0f,   1.0f,0.0f,
         0.5f, 0.5f,-0.5f,   1.0f, 0.0f, 0.0f,   1.0f,1.0f,
         0.5f, 0.5f, 0.5f,   1.0f, 0.0f, 0.0f,   0.0f,1.0f,
        // bottom (-Y)
        -0.5f,-0.5f,-0.5f,   0.0f,-1.0f, 0.0f,   0.0f,0.0f,
         0.5f,-0.5f,-0.5f,   0.0f,-1.0f, 0.0f,   1.0f,0.0f,
         0.5f,-0.5f, 0.5f,   0.0f,-1.0f, 0.0f,   1.0f,1.0f,
        -0.5f,-0.5f, 0.5f,   0.0f,-1.0f, 0.0f,   0.0f,1.0f,
        // top (+Y)
        -0.5f, 0.5f, 0.5f,   0.0f, 1.0f, 0.0f,   0.0f,0.0f,
         0.5f, 0.5f, 0.5f,   0.0f, 1.0f, 0.0f,   1.0f,0.0f,
         0.5f, 0.5f,-0.5f,   0.0f, 1.0f, 0.0f,   1.0f,1.0f,
        -0.5f, 0.5f,-0.5f,   0.0f, 1.0f, 0.0f,   0.0f,1.0f,
    };
    std::vector<std::uint32_t> idx;
    idx.reserve(36);
    for (std::uint32_t f = 0; f < 6; ++f) {
        const std::uint32_t o = f * 4;
        idx.insert(idx.end(), { o + 0, o + 1, o + 2, o + 2, o + 3, o + 0 });
    }
    return Mesh(v, idx, PNT());
}

Mesh Quad() {
    const std::vector<float> v = {
        //  position           normal           uv
        -0.5f,-0.5f, 0.0f,   0.0f,0.0f,1.0f,   0.0f,0.0f,
         0.5f,-0.5f, 0.0f,   0.0f,0.0f,1.0f,   1.0f,0.0f,
         0.5f, 0.5f, 0.0f,   0.0f,0.0f,1.0f,   1.0f,1.0f,
        -0.5f, 0.5f, 0.0f,   0.0f,0.0f,1.0f,   0.0f,1.0f,
    };
    const std::vector<std::uint32_t> idx = { 0, 1, 2, 2, 3, 0 };
    return Mesh(v, idx, PNT());
}

Mesh Plane(float size, float uvTiling) {
    const float h = size * 0.5f;
    const float t = uvTiling;
    const std::vector<float> v = {
        //  position          normal           uv
        -h, 0.0f, -h,   0.0f,1.0f,0.0f,   0.0f, 0.0f,
         h, 0.0f, -h,   0.0f,1.0f,0.0f,   t,    0.0f,
         h, 0.0f,  h,   0.0f,1.0f,0.0f,   t,    t,
        -h, 0.0f,  h,   0.0f,1.0f,0.0f,   0.0f, t,
    };
    const std::vector<std::uint32_t> idx = { 0, 1, 2, 2, 3, 0 };
    return Mesh(v, idx, PNT());
}

Mesh Cone(int segments)
{
        if (segments < 3) segments = 3;
    const float R = 0.5f;
    const float H = 1.0f;
    const float PI = glm::pi<float>();

    std::vector<float> v;
    std::vector<std::uint32_t> idx;
    v.reserve(static_cast<std::size_t>(segments * 2 + 2) * 8);

    const std::uint32_t tip = 0;
    v.insert(v.end(), {
        0.0f, H * 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.5f, 1.0f
    });

    const std::uint32_t baseCenter = 1;
    v.insert(v.end(), {
        0.0f, -H * 0.5f, 0.0f,
        0.0f, -1.0f, 0.0f,
        0.5f, 0.5f
    });

    const std::uint32_t sideStart = 2;
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
        const float x = std::cos(t) * R;
        const float z = std::sin(t) * R;
        const glm::vec3 normal = glm::normalize(glm::vec3(x, R, z));
        v.insert(v.end(), {
            x, -H * 0.5f, z,
            normal.x, normal.y, normal.z,
            static_cast<float>(i) / static_cast<float>(segments), 0.0f
        });
    }

    const std::uint32_t capStart = sideStart + static_cast<std::uint32_t>(segments);
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
        const float x = std::cos(t) * R;
        const float z = std::sin(t) * R;
        v.insert(v.end(), {
            x, -H * 0.5f, z,
            0.0f, -1.0f, 0.0f,
            x + 0.5f, z + 0.5f
        });
    }

    for (int i = 0; i < segments; ++i) {
        const std::uint32_t sideCurrent = sideStart + static_cast<std::uint32_t>(i);
        const std::uint32_t sideNext = sideStart + static_cast<std::uint32_t>((i + 1) % segments);
        const std::uint32_t capCurrent = capStart + static_cast<std::uint32_t>(i);
        const std::uint32_t capNext = capStart + static_cast<std::uint32_t>((i + 1) % segments);
        idx.insert(idx.end(), {tip, sideCurrent, sideNext});
        idx.insert(idx.end(), {baseCenter, capNext, capCurrent});
    }

    return Mesh(v, idx, PNT());
}
Mesh Cylinder(int segments)
{
        if (segments < 3) segments = 3;
    const float R = 0.5f;
    const float H = 1.0f;
    const float PI = glm::pi<float>();

    std::vector<float> v;
    std::vector<std::uint32_t> idx;
    v.reserve(static_cast<std::size_t>(segments * 4 + 2) * 8);

    const std::uint32_t topCenter = 0;
    v.insert(v.end(), {
        0.0f, H * 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.5f, 0.5f
    });

    const std::uint32_t bottomCenter = 1;
    v.insert(v.end(), {
        0.0f, -H * 0.5f, 0.0f,
        0.0f, -1.0f, 0.0f,
        0.5f, 0.5f
    });

    const std::uint32_t sideStart = 2;
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
        const float x = std::cos(t) * R;
        const float z = std::sin(t) * R;
        const glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        v.insert(v.end(), {
            x, H * 0.5f, z,
            normal.x, normal.y, normal.z,
            u, 1.0f,
            x, -H * 0.5f, z,
            normal.x, normal.y, normal.z,
            u, 0.0f
        });
    }

    const std::uint32_t topStart = sideStart + static_cast<std::uint32_t>(segments * 2);
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
        const float x = std::cos(t) * R;
        const float z = std::sin(t) * R;
        v.insert(v.end(), {
            x, H * 0.5f, z,
            0.0f, 1.0f, 0.0f,
            x + 0.5f, z + 0.5f
        });
    }

    const std::uint32_t bottomStart = topStart + static_cast<std::uint32_t>(segments);
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
        const float x = std::cos(t) * R;
        const float z = std::sin(t) * R;
        v.insert(v.end(), {
            x, -H * 0.5f, z,
            0.0f, -1.0f, 0.0f,
            x + 0.5f, z + 0.5f
        });
    }

    for (int i = 0; i < segments; ++i) {
        const std::uint32_t currentTop = sideStart + static_cast<std::uint32_t>(i * 2);
        const std::uint32_t currentBottom = currentTop + 1;
        const std::uint32_t nextTop = sideStart + static_cast<std::uint32_t>(((i + 1) % segments) * 2);
        const std::uint32_t nextBottom = nextTop + 1;
        idx.insert(idx.end(), {currentTop, currentBottom, nextBottom, nextBottom, nextTop, currentTop});

        const std::uint32_t capTopCurrent = topStart + static_cast<std::uint32_t>(i);
        const std::uint32_t capTopNext = topStart + static_cast<std::uint32_t>((i + 1) % segments);
        const std::uint32_t capBottomCurrent = bottomStart + static_cast<std::uint32_t>(i);
        const std::uint32_t capBottomNext = bottomStart + static_cast<std::uint32_t>((i + 1) % segments);
        idx.insert(idx.end(), {topCenter, capTopCurrent, capTopNext});
        idx.insert(idx.end(), {bottomCenter, capBottomNext, capBottomCurrent});
    }

    return Mesh(v, idx, PNT());
}
Mesh Sphere(int segments)
{
    if (segments < 3) segments = 3;
    const int    stacks  = segments;        // latitude bands (pole to pole)
    const int    sectors = segments * 2;    // longitude bands (around)
    const float  R       = 0.5f;            // radius -> unit diameter
    const float  PI      = glm::pi<float>();

    std::vector<float> v;
    v.reserve(static_cast<size_t>((stacks + 1) * (sectors + 1) * 8));
    for (int i = 0; i <= stacks; ++i) {
        const float phi = PI * (0.5f - static_cast<float>(i) / stacks); // +pi/2..-pi/2
        const float y   = std::sin(phi);
        const float r   = std::cos(phi);
        for (int j = 0; j <= sectors; ++j) {
            const float theta = 2.0f * PI * static_cast<float>(j) / sectors;
            const float x = r * std::cos(theta);
            const float z = r * std::sin(theta);
            // position (scaled), normal (already unit length), uv
            v.insert(v.end(), {
                x * R, y * R, z * R,
                x, y, z,
                static_cast<float>(j) / sectors, static_cast<float>(i) / stacks,
            });
        }
    }

    std::vector<std::uint32_t> idx;
    const std::uint32_t cols = static_cast<std::uint32_t>(sectors + 1);
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            const std::uint32_t k1 = static_cast<std::uint32_t>(i) * cols + static_cast<std::uint32_t>(j);
            const std::uint32_t k2 = k1 + cols;
            // Skip the degenerate triangles at the two poles.
            if (i != 0)             idx.insert(idx.end(), { k1, k2, k1 + 1});
            if (i != stacks - 1)    idx.insert(idx.end(), { k1 + 1, k2, k2 + 1});
        }
    }
    return Mesh(v, idx, PNT());
}

} // namespace primitives
} // namespace engine