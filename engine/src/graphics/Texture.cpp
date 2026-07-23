#include "engine/graphics/Texture.h"
#include "engine/graphics/ImageDecode.h"

#include <glad/glad.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine {

// TEMPORARY: pinpoint a crash during terrain albedo texture creation. Own file
// (truncated each run), flushed per line. Remove once resolved.
static void TexTrace(const char* fmt, ...) {
    static std::FILE* file = [] {
        std::FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, "D:/C++_Projects/3DGEngine/texture_trace.log", "w");
#else
        f = std::fopen("D:/C++_Projects/3DGEngine/texture_trace.log", "w");
#endif
        return f;
    }();
    if (!file) return;
    std::va_list args; va_start(args, fmt);
    std::vfprintf(file, fmt, args); va_end(args);
    std::fputc('\n', file); std::fflush(file);
}

namespace {

struct Image {
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> rgba;   // tightly packed RGBA, bottom-up for GL
};

// Minimal loader for uncompressed, true-colour (24- or 32-bit) TGA files.
Image LoadTGA(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Texture: cannot open '" + path + "'");

    unsigned char header[18];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file)
        throw std::runtime_error("Texture: bad TGA header in '" + path + "'");

    const int idLength   = header[0];
    const int imageType  = header[2];
    const int width      = header[12] | (header[13] << 8);
    const int height     = header[14] | (header[15] << 8);
    const int depth      = header[16];
    const int descriptor = header[17];

    if (imageType != 2 || (depth != 24 && depth != 32))
        throw std::runtime_error("Texture: only uncompressed 24/32-bit TGA supported: " + path);

    file.seekg(idLength, std::ios::cur);

    const int channels = depth / 8;
    std::vector<unsigned char> raw(static_cast<size_t>(width) * height * channels);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!file)
        throw std::runtime_error("Texture: truncated TGA pixel data: " + path);

    const bool topOrigin = (descriptor & 0x20) != 0;

    Image img;
    img.width  = width;
    img.height = height;
    img.rgba.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const int srcRow = topOrigin ? (height - 1 - y) : y;
        for (int x = 0; x < width; ++x) {
            const size_t s = (static_cast<size_t>(srcRow) * width + x) * channels;
            const size_t d = (static_cast<size_t>(y) * width + x) * 4;
            img.rgba[d + 0] = raw[s + 2];
            img.rgba[d + 1] = raw[s + 1];
            img.rgba[d + 2] = raw[s + 0];
            img.rgba[d + 3] = (channels == 4) ? raw[s + 3] : 255;
        }
    }
    return img;
}

// File extension, lower-cased, without the dot.
std::string LowerExt(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    std::string e = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// PNG/JPEG decoders return the top row first; GL's texture origin is bottom-left,
// so flip rows to match the TGA loader (which already emits bottom-up).
void FlipRowsRGBA(std::vector<unsigned char>& px, int w, int h) {
    const std::size_t row = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h / 2; ++y)
        std::swap_ranges(px.begin() + static_cast<std::ptrdiff_t>(y * row),
                         px.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         px.begin() + static_cast<std::ptrdiff_t>((h - 1 - y) * row));
}

} // anonymous namespace

void Texture::Create(const unsigned char* rgba, int width, int height, bool smooth) {
    TexTrace("Create: begin w=%d h=%d smooth=%d rgba=%p thisId=%u", width, height,
             smooth ? 1 : 0, static_cast<const void*>(rgba), m_id);
    m_width  = width;
    m_height = height;

    glGenTextures(1, &m_id);
    TexTrace("Create: glGenTextures -> id=%u", m_id);
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
    // Reset pixel-unpack state to safe defaults. Other code (ImGui, image loaders, font
    // atlases) can leave GL_UNPACK_ROW_LENGTH / alignment / skips set; if so, glTexImage2D
    // would read past our tightly-packed buffer -- a segfault that grows with texture width
    // (which is exactly why the 256-wide terrain albedo crashed while a 128-wide one didn't).
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);   // ensure we read client memory, not a PBO
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    TexTrace("Create: params set, glTexImage2D...");

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    TexTrace("Create: glTexImage2D done");
    if (smooth) {
        glGenerateMipmap(GL_TEXTURE_2D);
        TexTrace("Create: glGenerateMipmap done");
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    TexTrace("Create: end");
}

Texture::Texture(const std::string& path, bool smooth) {
    // Detect the format by content first (magic bytes), then fall back to the file
    // extension. This is robust to odd extensions/casing/whitespace that would
    // otherwise mis-route a PNG/JPEG into the TGA loader.
    std::string kind = LowerExt(path);
    if (kind != "tga") {
        unsigned char sig[3] = {0, 0, 0};
        std::ifstream f(path, std::ios::binary);
        if (f) f.read(reinterpret_cast<char*>(sig), 3);
        if      (sig[0] == 0x89 && sig[1] == 0x50 && sig[2] == 0x4E) kind = "png";   // \x89PNG
        else if (sig[0] == 0xFF && sig[1] == 0xD8)                   kind = "jpg";   // JPEG SOI
    }
    if (kind == "png" || kind == "jpg" || kind == "jpeg") {
        engine::image::Image im = (kind == "png") ? engine::image::DecodePNG(path)
                                                  : engine::image::DecodeJPEG(path);
        FlipRowsRGBA(im.rgba, im.width, im.height);
        Create(im.rgba.data(), im.width, im.height, smooth);
    } else {
        const Image img = LoadTGA(path);   // already bottom-up for GL
        Create(img.rgba.data(), img.width, img.height, smooth);
    }
}

Texture::Texture(const unsigned char* rgbaPixels, int width, int height, bool smooth) {
    Create(rgbaPixels, width, height, smooth);
}

Texture::~Texture() { Release(); }

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
        m_id     = other.m_id;
        m_width  = other.m_width;
        m_height = other.m_height;
        other.m_id = 0;
    }
    return *this;
}

void Texture::Bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

void Texture::Update(const unsigned char* rgbaPixels, int width, int height) {
    if (!m_id || width != m_width || height != m_height) {
        return;   // size must match the existing texture
    }
    glBindTexture(GL_TEXTURE_2D, m_id);
    // Same safe pixel-unpack state as Create() -- avoid reading past the buffer if some
    // other code left GL_UNPACK_ROW_LENGTH / alignment set.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
    glGenerateMipmap(GL_TEXTURE_2D);   // keep mip chain consistent
}

} // namespace engine
