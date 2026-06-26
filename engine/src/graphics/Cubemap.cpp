#include "engine/graphics/Cubemap.h"

#include <glad/glad.h>

namespace engine {

Cubemap::Cubemap(const std::array<std::vector<unsigned char>, 6>& faces, int faceSize) {
    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA,
                     faceSize, faceSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, faces[i].data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

Cubemap::~Cubemap() { Release(); }
void Cubemap::Release() { if (m_id) glDeleteTextures(1, &m_id); m_id = 0; }

Cubemap::Cubemap(Cubemap&& o) noexcept : m_id(o.m_id) { o.m_id = 0; }
Cubemap& Cubemap::operator=(Cubemap&& o) noexcept {
    if (this != &o) { Release(); m_id = o.m_id; o.m_id = 0;; }
    return *this;
}

void Cubemap::Bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
}

} // namespace engine