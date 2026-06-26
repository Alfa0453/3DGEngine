#include "engine/graphics/Skybox.h"

#include "engine/graphics/Primitives.h"
#include "engine/graphics/ImageDecode.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace engine {
namespace {

const char* kSkyVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vDir;
void main() {
    vDir = aPos;
    vec4 p = uViewProj * vec4(aPos, 1.0);
    gl_Position = p.xyww;            // depth = 1, behind everything
}
)GLSL";

const char* kSkyFrag = R"GLSL(
#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSky;
uniform int uApplyTonemap;
void main() {
    vec3 c = texture(uSky, normalize(vDir)).rgb;
    if (uApplyTonemap == 1) c = pow(c, vec3(1.0/2.2));        // linear -> sRGB, matching the scene
    FragColor = vec4(c, 1.0);
}
)GLSL";

// Direction for face `f`, pixel (col i, row j) of an SxS face.
glm::vec3 FaceDir(int f, int i, int j, int S) {
    const float u = (i + 0.5f) / S * 2.0f - 1.0f;
    const float v = (j + 0.5f) / S * 2.0f - 1.0f;
    glm::vec3 d;
    switch (f) {
        case 0: d = {  1.0f, -v, -u }; break;   // +X
        case 1: d = { -1.0f, -v,  u }; break;   // -X
        case 2: d = {  u,  1.0f,  v }; break;   // +Y
        case 3: d = {  u, -1.0f, -v }; break;   // -Y
        case 4: d = {  u, -v,  1.0f }; break;   // +Z
        default:d = { -u, -v, -1.0f }; break;   // -Z
    }
    return glm::normalize(d);
}

std::string LowerExt(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    std::string e = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

} // namespace

Skybox::Skybox(Cubemap cubemap)
    : m_cubemap(std::move(cubemap)),
      m_cube(primitives::Cube()),
      m_shader(kSkyVert, kSkyFrag) {}

Skybox Skybox::Gradient(const glm::vec3 &horizon, const glm::vec3 &zenith, const glm::vec3 &sunDir, const glm::vec3 &sunColor, int faceSize)
{
    const glm::vec3 sun = glm::normalize(sunDir);
    const glm::vec3 ground = horizon * 0.4f;
    std::array<std::vector<unsigned char>, 6> faces;
    for (int f = 0; f < 6; ++f) {
        std::vector<unsigned char>& px = faces[static_cast<std::size_t>(f)];
        px.resize(static_cast<std::size_t>(faceSize) * faceSize * 4);
        for (int j = 0; j < faceSize; ++j)
            for (int i = 0; i < faceSize; ++i) {
                const glm::vec3 d = FaceDir(f, i, j, faceSize);
                const float t = glm::clamp(d.y, 0.0f, 1.0f);
                glm::vec3 col = (d.y < 0.0f) ? ground * (1.0f + d.y * 0.3f)
                                            : horizon * (1.0f - t) + zenith * t;
                const float sd = glm::clamp(glm::dot(d, sun), 0.0f, 1.0f);
                col += sunColor * std::pow(sd, 220.0f) * 1.4f;  // tight sun disc
                col += sunColor * std::pow(sd, 8.0f) * 0.12f;   // soft halo
                col = glm::clamp(col, glm::vec3(0.0f), glm::vec3(1.0f));
                const std::size_t o = (static_cast<std::size_t>(j) * faceSize + i) * 4;
                px[o + 0] = static_cast<unsigned char>(col.r * 255.0f + 0.5f);
                px[o + 1] = static_cast<unsigned char>(col.g * 255.0f + 0.5f);
                px[o + 2] = static_cast<unsigned char>(col.b * 255.0f + 0.5f);
                px[o + 3] = 255;
            }
    }
    return Skybox(Cubemap(faces, faceSize));
}

Skybox Skybox::FromFiles(const std::array<std::string, 6>& facePaths) {
    std::array<std::vector<unsigned char>, 6> faces;
    int size = 0;
    for (int f = 0; f < 6; ++f) {
        const std::string& p = facePaths[static_cast<std::size_t>(f)];
        const std::string ext = LowerExt(p);
        image::Image im = (ext == "png") ? image::DecodePNG(p)
                        : (ext == "jpg" || ext == "jpeg") ? image::DecodeJPEG(p)
                        : throw std::runtime_error("Skybox: face must be PNG ot JPEG: " + p);
        if (size == 0) size = im.width;
        faces[static_cast<std::size_t>(f)] = std::move(im.rgba);
    }
    return Skybox(Cubemap(faces, size));
}

void Skybox::Draw(const glm::mat4& view, const glm::mat4& projection, bool tonemap) {
    glDepthFunc(GL_LEQUAL);
    m_shader.Bind();
    m_shader.SetMat4("uViewProj", projection * glm::mat4(glm::mat3(view)));
    m_cubemap.Bind(0);
    m_shader.SetInt("uSky", 0);
    m_shader.SetInt("uApplyTonemap", tonemap ? 1 : 0);
    m_cube.Draw();
    glDepthFunc(GL_LESS);
}

} // namespace engine