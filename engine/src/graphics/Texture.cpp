#include "engine/graphics/Texture.h"

#include <glad/glad.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba; // tightly packed RGBA, bottom-up for GL
};

// Minimal loader for uncompressed, true-colour (24- or 32-bit) TGA files.
// TGA stores pixels bottom-to-top by default and in BGR(A) order, so we convert
// to RGBA and respect the origin bit.
Image LoadTGA(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Texture: cannot open '" + path + "'");
    
    unsigned char header[18];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file)
        throw std::runtime_error("Texture: bad TGA header in '" + path + "'");

    const int idLength   = header[0];
    const int imageType  = header[2];                        // 2 = uncompressed RGB
    const int width      = header[12] | (header[13] << 8);
    const int height     = header[14] | (header[15] << 8);
    const int depth      = header[16];                       // bits per pixel
    const int descriptor = header[17];

    if (imageType != 2 || (depth != 24 && depth != 32))
        throw std::runtime_error("Texture: only uncompressed 24/32-bit TGA supported: " + path);

    file.seekg(idLength, std::ios::cur);                    // skip image id field

    const int channels = depth / 8;
    std::vector<unsigned char> raw(static_cast<size_t>(width) * height * channels);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!file)
        throw std::runtime_error("Texture: truncated TGA pixel data: " + path);

    const bool topOrigin = (descriptor & 0x20) != 0;        // bit 5 = top-left origin

    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        // Store bottom-up so OpenGL's (0,0)=bottom-left convention is satisfied.
        const int srcRow = topOrigin ? (height - 1 - y) : y;
        for (int x = 0; x < width; ++x) {
            const size_t s = (static_cast<size_t>(srcRow) * width + x) * channels;
            const size_t d = (static_cast<size_t>(y) * width + x) * 4;
            img.rgba[d + 0] = raw[s + 2];                   // R <- B
            img.rgba[d + 1] = raw[s + 1];                   // G
            img.rgba[d + 2] = raw[s + 0];                   // B <- R
            img.rgba[d + 3] = (channels == 4) ? raw[s + 3] : 255;
        }
    }
    return img;
}
} // anonymous namespace

void Texture::Create(const unsigned char *rgba, int width, int height, bool smooth) {
    m_width = width;
    m_height = height;

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    if (smooth) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        // Crisp pixels (font atlas): no smoothing, clamp to the cell edges.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (smooth)
        glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::Texture(const std::string& path, bool smooth) {
    const Image img = LoadTGA(path);
    Create(img.rgba.data(), img.width, img.height, smooth);

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture() { Release(); }

Texture::Texture(const unsigned char *rgbaPixels, int width, int height, bool smooth) {
    Create(rgbaPixels, width, height, smooth);
}
void Texture::Release() {
    if (m_id) glDeleteTextures(1, &m_id);
    m_id = 0;
}

Texture::Texture(Texture&& other) noexcept
    : m_id(other.m_id), m_width(other.m_width), m_height(other.m_height) {
    other.m_id = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        Release();
        m_id    = other.m_id;
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_id = 0;
    }
    return *this;
}

void Texture::Bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

} // namespace engine