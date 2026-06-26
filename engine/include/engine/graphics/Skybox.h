#pragma once

#include "engine/graphics/Cubemap.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/Shader.h"

#include <glm/glm.hpp>

#include <array>
#include <string>

namespace engine {

// A drop-in skybox: owns a cubemap, a cube mesh and an embedded shader, so any
// app can add a sky in one line. Draw it after the opaque scene:
//
//     Skybox sky = Skybox::Gradient(...);   // once, after GL init
//     ...
//     sky.Draw(view, projection);           // after the scene, before the HUD
//
class Skybox {
public:
    // Take ownership of an existing cubemap.
    explicit Skybox(Cubemap cubemap);

    // Procedural sky: a horizon->zenith gradient with a soft sun glow.
    // `sunDir` points TOWARD the sun in the sky (i.e. -lightDirection).
    static Skybox Gradient(const glm::vec3& horizon, const glm::vec3& zenith,
                           const glm::vec3& sunDir,  const glm::vec3& sunColor, int faceSize = 256);

    // Load six face images (+X,-X,+Y,-Y,+Z,-Z). PNG or JPEG.
    static Skybox FromFiles(const std::array<std::string, 6>& facePaths);

    Skybox(const Skybox&)            = delete;
    Skybox& operator=(const Skybox&) = delete;
    Skybox(Skybox&&) noexcept            = default;
    Skybox& operator=(Skybox&&) noexcept = default;

    // Render the sky behind everything (depth test temporarily set to LEQUAL).
    void Draw(const glm::mat4& view, const glm::mat4& projection, bool tonemap = true);

private:
    Cubemap m_cubemap;
    Mesh    m_cube;
    Shader  m_shader;
};

} // namespace engine