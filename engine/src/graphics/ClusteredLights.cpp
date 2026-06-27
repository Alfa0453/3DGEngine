#include "engine/graphics/ClusteredLight.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

namespace engine {

ClusteredLights::ClusteredLights() {
    // Light UBO: vec4 posRadius[kMaxLights] then vec4 color[kMaxLights] (std140).
    glGenBuffers(1, &m_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    glBufferData(GL_UNIFORM_BUFFER, kMaxLights * 2 * static_cast<GLsizeiptr>(sizeof(glm::vec4)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Tile buffer: int per slot, exposed to the shader as an isamplerBuffer.
    glGenBuffers(1, &m_tileBuf);
    glBindBuffer(GL_TEXTURE_BUFFER, m_tileBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(kTilesX * kTilesY * kTileStride * sizeof(int)),
                 nullptr, GL_DYNAMIC_DRAW);
    glGenTextures(1, &m_tileTex);
    glBindTexture(GL_TEXTURE_BUFFER, m_tileTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32I, m_tileBuf);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

ClusteredLights::~ClusteredLights() {
    glDeleteBuffers(1, &m_ubo);
    glDeleteTextures(1, &m_tileTex);
    glDeleteBuffers(1, &m_tileBuf);
}

void ClusteredLights::Build(const Camera& camera, float aspect, int screenWidth, int screenHeight,
                            const std::vector<PointLight>& lights) {
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    const int count = std::min(static_cast<int>(lights.size()), kMaxLights);

    // Light data: store WORLD-space position (the lit pass works in world space).
    std::vector<glm::vec4> posR(kMaxLights, glm::vec4(0.0f));
    std::vector<glm::vec4> col(kMaxLights, glm::vec4(0.0f));
    std::vector<glm::vec4> viewPos(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        posR[static_cast<std::size_t>(i)] = glm::vec4(lights[static_cast<std::size_t>(i)].position,
                                                      lights[static_cast<std::size_t>(i)].radius);
        col[static_cast<std::size_t>(i)] = glm::vec4(lights[static_cast<std::size_t>(i)].color, 0.0f);
        viewPos[static_cast<std::size_t>(i)] = view * glm::vec4(lights[static_cast<std::size_t>(i)].position, 1.0f);
    }

    // Cull: scatter each light into the screen tiles its sphere covers.
    std::vector<int> tiles(static_cast<std::size_t>(kTilesX * kTilesY * kTileStride), 0);
    const float tileW = static_cast<float>(screenWidth)  / kTilesX;
    const float tileH = static_cast<float>(screenHeight) / kTilesY;
    for (int i = 0; i < count; ++i) {
        const glm::vec4 vp = viewPos[static_cast<std::size_t>(i)];
        const glm::vec4 clip = proj * vp;
        if (clip.w <= 0.0f) continue;                       // wholly behind the camera
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float sx = (ndc.x * 0.5f + 0.5f) * screenWidth;
        const float sy = (ndc.y * 0.5f + 0.5f) * screenHeight;
        const float dist = std::max(-vp.z, 0.01f);
        const float srad = lights[static_cast<std::size_t>(i)].radius * proj[1][1] * screenHeight * 0.5f / dist;

        const int minTX = std::clamp(static_cast<int>((sx - srad) / tileW), 0, kTilesX - 1);
        const int maxTX = std::clamp(static_cast<int>((sx + srad) / tileW), 0, kTilesX - 1);
        const int minTY = std::clamp(static_cast<int>((sy - srad) / tileH), 0, kTilesY - 1);
        const int maxTY = std::clamp(static_cast<int>((sy + srad) / tileH), 0, kTilesY - 1);
        for (int ty = minTY; ty <= maxTY; ++ty)
            for (int tx = minTX; tx <= maxTX; ++tx) {
                const int base = (ty * kTilesX + tx) * kTileStride;
                const int c = tiles[static_cast<std::size_t>(base)];
                if (c < kTileStride - 1) {
                    tiles[static_cast<std::size_t>(base + 1 + c)] = i;
                    tiles[static_cast<std::size_t>(base)] = c + 1;
                }
            }
    }

    glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    const GLsizeiptr half = kMaxLights * static_cast<GLsizeiptr>(sizeof(glm::vec4));
    glBufferSubData(GL_UNIFORM_BUFFER, 0, half, posR.data());
    glBufferSubData(GL_UNIFORM_BUFFER, half, half, col.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBuffer(GL_TEXTURE_BUFFER, m_tileBuf);
    glBufferSubData(GL_TEXTURE_BUFFER, 0,
                    static_cast<GLsizeiptr>(tiles.size() * sizeof(int)), tiles.data());
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void ClusteredLights::BindLightUBO(unsigned int bindingPoint) const {
    glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, m_ubo);
}

void ClusteredLights::BindTileBuffer(unsigned int textureUnit) const {
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_BUFFER, m_tileTex);
}

} // namespace engine