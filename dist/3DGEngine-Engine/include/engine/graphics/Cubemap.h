#pragma once

#include <array>
#include <vector>

namespace engine {

// An OpenGL cubemap texture (six square RGBA faces), used for the skybox. Face
// order matches GL: +X, -X, +Y, -Y, +Z, -Z. Move-only (owns a GL texture).
class Cubemap {
public:
    Cubemap(const std::array<std::vector<unsigned char>, 6>& faces, int faceSize);
    ~Cubemap();

    Cubemap(const Cubemap&)            = delete;
    Cubemap& operator=(const Cubemap&) = delete;
    Cubemap(Cubemap&& other) noexcept;
    Cubemap& operator=(Cubemap&& other) noexcept;

    void Bind(unsigned int unit = 0) const;

private:
    void Release();
    unsigned int m_id = 0;
};

} // namespace engine