#include "engine/graphics/TextRenderer.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Texture.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <sstream>
#include <vector>

namespace engine {
namespace {

std::vector<float> ParseValues(const std::string& text) {
    std::string normalized = text;
    for (char& c : normalized) if (c == ',' || c == '(' || c == ')') c = ' ';
    std::istringstream input(normalized);
    std::vector<float> values;
    float value = 0.0f;
    while (input >> value) values.push_back(value);
    return values;
}

// Auto-generated 8x8 bitmap font, ASCII 32..126 (LiberationMono-Bold).
// Row-major; bit 7 (0x80) = leftmost pixel.
static const unsigned char kFont8x8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00}, // 33 '!'
    {0x28, 0x28, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00}, // 34 '"'
    {0x14, 0x04, 0x7C, 0x28, 0x7C, 0x28, 0x28, 0x00}, // 35 '#'
    {0x38, 0x7C, 0x30, 0x38, 0x1C, 0x74, 0x3C, 0x10}, // 36 '$'
    {0x64, 0x58, 0x68, 0x10, 0x1C, 0x2C, 0x4C, 0x00}, // 37 '%'
    {0x38, 0x28, 0x38, 0x34, 0x5C, 0x4C, 0x3C, 0x00}, // 38 '&'
    {0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}, // 39 '''
    {0x18, 0x10, 0x10, 0x30, 0x30, 0x30, 0x10, 0x10}, // 40 '('
    {0x10, 0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x10}, // 41 ')'
    {0x10, 0x38, 0x18, 0x28, 0x00, 0x00, 0x00, 0x00}, // 42 '*'
    {0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x00, 0x00}, // 43 '+'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x30}, // 44 ','
    {0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00}, // 45 '-'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00}, // 46 '.'
    {0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x00}, // 47 '/'
    {0x38, 0x2C, 0x6C, 0x7C, 0x6C, 0x2C, 0x38, 0x00}, // 48 '0'
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 49 '1'
    {0x38, 0x2C, 0x0C, 0x18, 0x30, 0x20, 0x7C, 0x00}, // 50 '2'
    {0x38, 0x2C, 0x0C, 0x18, 0x0C, 0x6C, 0x38, 0x00}, // 51 '3'
    {0x18, 0x18, 0x28, 0x28, 0x7C, 0x08, 0x08, 0x00}, // 52 '4'
    {0x3C, 0x20, 0x38, 0x0C, 0x04, 0x2C, 0x38, 0x00}, // 53 '5'
    {0x38, 0x20, 0x38, 0x6C, 0x24, 0x2C, 0x38, 0x00}, // 54 '6'
    {0x7C, 0x0C, 0x08, 0x18, 0x10, 0x10, 0x30, 0x00}, // 55 '7'
    {0x38, 0x2C, 0x2C, 0x38, 0x2C, 0x6C, 0x38, 0x00}, // 56 '8'
    {0x38, 0x2C, 0x6C, 0x2C, 0x3C, 0x0C, 0x38, 0x00}, // 57 '9'
    {0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00}, // 58 ':'
    {0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10}, // 59 ';'
    {0x00, 0x0C, 0x38, 0x60, 0x38, 0x0C, 0x00, 0x00}, // 60 '<'
    {0x00, 0x00, 0x7C, 0x00, 0x7C, 0x00, 0x00, 0x00}, // 61 '='
    {0x00, 0x60, 0x38, 0x0C, 0x38, 0x60, 0x00, 0x00}, // 62 '>'
    {0x38, 0x6C, 0x0C, 0x18, 0x10, 0x00, 0x10, 0x00}, // 63 '?'
    {0x18, 0x24, 0x44, 0x5C, 0x6C, 0x7C, 0x40, 0x24}, // 64 '@'
    {0x18, 0x38, 0x38, 0x2C, 0x3C, 0x64, 0x64, 0x00}, // 65 'A'
    {0x78, 0x6C, 0x6C, 0x78, 0x64, 0x64, 0x7C, 0x00}, // 66 'B'
    {0x38, 0x2C, 0x60, 0x60, 0x60, 0x24, 0x38, 0x00}, // 67 'C'
    {0x78, 0x6C, 0x64, 0x64, 0x64, 0x6C, 0x78, 0x00}, // 68 'D'
    {0x7C, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7C, 0x00}, // 69 'E'
    {0x3C, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20, 0x00}, // 70 'F'
    {0x38, 0x2C, 0x60, 0x7C, 0x64, 0x24, 0x38, 0x00}, // 71 'G'
    {0x6C, 0x6C, 0x6C, 0x7C, 0x6C, 0x6C, 0x6C, 0x00}, // 72 'H'
    {0x3C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x3C, 0x00}, // 73 'I'
    {0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x28, 0x38, 0x00}, // 74 'J'
    {0x6C, 0x68, 0x78, 0x78, 0x78, 0x6C, 0x64, 0x00}, // 75 'K'
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3C, 0x00}, // 76 'L'
    {0x6C, 0x6C, 0x7C, 0x7C, 0x74, 0x64, 0x64, 0x00}, // 77 'M'
    {0x64, 0x74, 0x74, 0x7C, 0x7C, 0x6C, 0x6C, 0x00}, // 78 'N'
    {0x38, 0x2C, 0x64, 0x64, 0x64, 0x2C, 0x38, 0x00}, // 79 'O'
    {0x78, 0x6C, 0x6C, 0x78, 0x60, 0x60, 0x60, 0x00}, // 80 'P'
    {0x38, 0x2C, 0x64, 0x64, 0x64, 0x2C, 0x38, 0x18}, // 81 'Q'
    {0x7C, 0x6C, 0x6C, 0x78, 0x68, 0x6C, 0x64, 0x00}, // 82 'R'
    {0x38, 0x6C, 0x60, 0x3C, 0x0C, 0x64, 0x38, 0x00}, // 83 'S'
    {0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00}, // 84 'T'
    {0x6C, 0x6C, 0x6C, 0x6C, 0x6C, 0x2C, 0x38, 0x00}, // 85 'U'
    {0x64, 0x64, 0x2C, 0x28, 0x38, 0x38, 0x18, 0x00}, // 86 'V'
    {0x44, 0x44, 0x74, 0x7C, 0x7C, 0x2C, 0x2C, 0x00}, // 87 'W'
    {0x6C, 0x28, 0x38, 0x18, 0x38, 0x2C, 0x64, 0x00}, // 88 'X'
    {0x64, 0x2C, 0x38, 0x18, 0x10, 0x10, 0x10, 0x00}, // 89 'Y'
    {0x3C, 0x0C, 0x18, 0x10, 0x30, 0x20, 0x7C, 0x00}, // 90 'Z'
    {0x18, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // 91 '['
    {0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x00}, // 92 '\'
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}, // 93 ']'
    {0x18, 0x28, 0x2C, 0x64, 0x00, 0x00, 0x00, 0x00}, // 94 '^'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 95 '_'
    {0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 96 '`'
    {0x00, 0x00, 0x38, 0x0C, 0x3C, 0x6C, 0x3C, 0x00}, // 97 'a'
    {0x20, 0x20, 0x38, 0x2C, 0x24, 0x2C, 0x38, 0x00}, // 98 'b'
    {0x00, 0x00, 0x38, 0x2C, 0x60, 0x2C, 0x38, 0x00}, // 99 'c'
    {0x0C, 0x0C, 0x3C, 0x6C, 0x6C, 0x6C, 0x3C, 0x00}, // 100 'd'
    {0x00, 0x00, 0x38, 0x2C, 0x7C, 0x20, 0x3C, 0x00}, // 101 'e'
    {0x1C, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x00}, // 102 'f'
    {0x00, 0x00, 0x3C, 0x2C, 0x64, 0x2C, 0x3C, 0x0C}, // 103 'g'
    {0x20, 0x20, 0x3C, 0x2C, 0x24, 0x24, 0x24, 0x00}, // 104 'h'
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x7C, 0x00}, // 105 'i'
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18}, // 106 'j'
    {0x20, 0x20, 0x2C, 0x38, 0x38, 0x28, 0x2C, 0x00}, // 107 'k'
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7C, 0x00}, // 108 'l'
    {0x00, 0x00, 0x7C, 0x74, 0x54, 0x54, 0x54, 0x00}, // 109 'm'
    {0x00, 0x00, 0x3C, 0x2C, 0x24, 0x24, 0x24, 0x00}, // 110 'n'
    {0x00, 0x00, 0x38, 0x6C, 0x64, 0x6C, 0x38, 0x00}, // 111 'o'
    {0x00, 0x00, 0x38, 0x2C, 0x24, 0x2C, 0x38, 0x20}, // 112 'p'
    {0x00, 0x00, 0x3C, 0x6C, 0x6C, 0x6C, 0x3C, 0x0C}, // 113 'q'
    {0x00, 0x00, 0x3C, 0x30, 0x30, 0x20, 0x20, 0x00}, // 114 'r'
    {0x00, 0x00, 0x38, 0x20, 0x38, 0x0C, 0x38, 0x00}, // 115 's'
    {0x00, 0x10, 0x38, 0x30, 0x30, 0x30, 0x1C, 0x00}, // 116 't'
    {0x00, 0x00, 0x6C, 0x6C, 0x6C, 0x2C, 0x3C, 0x00}, // 117 'u'
    {0x00, 0x00, 0x64, 0x2C, 0x28, 0x38, 0x18, 0x00}, // 118 'v'
    {0x00, 0x00, 0x44, 0x54, 0x7C, 0x3C, 0x2C, 0x00}, // 119 'w'
    {0x00, 0x00, 0x2C, 0x38, 0x18, 0x38, 0x6C, 0x00}, // 120 'x'
    {0x00, 0x00, 0x64, 0x2C, 0x28, 0x38, 0x18, 0x10}, // 121 'y'
    {0x00, 0x00, 0x3C, 0x08, 0x10, 0x30, 0x3C, 0x00}, // 122 'z'
    {0x1C, 0x10, 0x10, 0x10, 0x30, 0x10, 0x10, 0x10}, // 123 '{'
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // 124 '|'
    {0x30, 0x18, 0x18, 0x18, 0x0C, 0x18, 0x18, 0x18}, // 125 '}'
    {0x00, 0x00, 0x70, 0x1C, 0x00, 0x00, 0x00, 0x00}, // 126 '~'
};

constexpr int kAtlasCols = 16;
constexpr int kAtlasRows = 6;       // 16*6 = 96 cells, covers ASCII 32..126
constexpr int kAtlasW    = kAtlasCols * 8;  // 128
constexpr int kAtlasH    = kAtlasRows * 8;  // 48

// 2D text shaders, kept inline so the engine needs no font shader files.
const char* kTextVert = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
uniform mat4 uProj;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";

const char* kTextFrag = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uText;
uniform vec3 uColor;
uniform float uSolid;   // 1 = solid fill (ignore the atlas), 0 = sampled glyph
uniform float uAlpha;   // alpha used by the solid-fill path
uniform float uImage;   // 1 = sample the bound texture as an RGBA image (tinted by uColor*uAlpha)
void main() {
    if (uImage > 0.5) {
        vec4 t = texture(uText, vUV);
        FragColor = vec4(t.rgb * uColor, t.a * uAlpha);
        return;
    }
    if (uSolid > 0.5) { FragColor = vec4(uColor, uAlpha); return; }
    float a = texture(uText, vUV).a;    // glyph coverage is in the alpha channel
    if (a < 0.01) discard;
    FragColor = vec4(uColor, a);
}
)";

// Expand the 1-bit 8x8 font into an RGBA atlas: white glyphs, alpha = coverage.
std::vector<unsigned char> BuildAtlas() {
    std::vector<unsigned char> px(static_cast<size_t>(kAtlasW) * kAtlasH * 4, 0);
    for (int i = 0; i < 95; ++i) {
        const int col = i % kAtlasCols;
        const int row = i / kAtlasCols;
        for (int gy = 0; gy < 8; ++gy) {
            for (int gx = 0; gx < 8; ++gx) {
                const bool on = (kFont8x8[i][gy] >> (7 - gx)) & 1;
                const int ax = col * 8 + gx;
                const int ay = row * 8 + gy;
                const size_t p = (static_cast<size_t>(ay) * kAtlasW + ax) * 4;
                px[p + 0] = 255;
                px[p + 1] = 255;
                px[p + 2] = 255;
                px[p + 3] = on ? 255 : 0;
            }
        }
    }
    return px;
}

} // anonymous namespace

TextRenderer::TextRenderer() {
    // Font atlas (nearest-filtered, no mipmaps -> crisp pixels).
    const std::vector<unsigned char> atlas = BuildAtlas();
    m_atlas = std::make_unique<Texture>(atlas.data(), kAtlasW, kAtlasH, false);

    m_shader = std::make_unique<Shader>(kTextVert, kTextFrag);

    // A dynamic buffer we refill every Text() call. Vertex = (x, y, u, v).
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

TextRenderer::~TextRenderer() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

float TextRenderer::Measure(const std::string& text, float scale) const {
    return static_cast<float>(text.size()) * kGlyphPx * scale;
}

void TextRenderer::Begin(int screenWidth, int screenHeight) {
    // Pixel-space projection: (0,0) top-left, y increasing downward.
    m_projection = glm::ortho(0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight), 0.0f);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->Bind();
    m_shader->SetMat4("uProj", m_projection);
    m_shader->SetInt("uText", 0);
    m_shader->SetFloat("uSolid", 0.0f);
    m_shader->SetFloat("uImage", 0.0f);
    m_atlas->Bind(0);
}

void TextRenderer::End() {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void TextRenderer::Text(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    const float cell = kGlyphPx * scale;
    const float au = 1.0f / kAtlasW;
    const float av = 1.0f / kAtlasH;

    std::vector<float> verts;
    verts.reserve(text.size() * 6 * 4);

    float penX = x;
    float penY = y;
    for (char c : text) {
        if (c == '\n') { penX = x; penY += cell; continue; }
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc > 126) { penX += cell; continue; }

        const int idx = uc - 32;
        const int col = idx % kAtlasCols;
        const int row = idx / kAtlasCols;

        // Half-texel inset keeps nearest sampling inside the glyph's cell.
        const float u0 = (col * 8 + 0.5f) * au;
        const float u1 = (col * 8 + 7.5f) * au;
        const float v0 = (row * 8 + 0.5f) * av;
        const float v1 = (row * 8 + 7.5f) * av;

        const float x0 = penX, x1 = penX + cell;
        const float y0 = penY, y1 = penY + cell;

        const float quad[6][4] = {
            {x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1},
            {x1, y1, u1, v1}, {x0, y1, u0, v1}, {x0, y0, u0, v0},
        };
        for (const auto& vtx : quad) {
            verts.push_back(vtx[0]); verts.push_back(vtx[1]);
            verts.push_back(vtx[2]); verts.push_back(vtx[3]);
        }
        penX += cell;
    }

    if (verts.empty()) return;

    m_shader->SetFloat("uSolid", 0.0f);
    m_shader->SetVec3("uColor", color);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                                  verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 4));
}

void TextRenderer::FillRect(float x, float y, float w, float h, const glm::vec3& color, float alpha) {
    const float q[6][4] = {
        {x,     y,     0.0f, 0.0f}, {x + w, y,     0.0f, 0.0f}, {x + w, y + h, 0.0f, 0.0f},
        {x + w, y + h, 0.0f, 0.0f}, {x,     y + h, 0.0f, 0.0f}, {x,     y,     0.0f, 0.0f},
    };
    std::vector<float> verts;
    verts.reserve(6 * 4);
    for (const auto& v : q) {
        verts.push_back(v[0]); verts.push_back(v[1]);
        verts.push_back(v[2]); verts.push_back(v[3]);
    }
    m_shader->SetFloat("uSolid", 1.0f);
    m_shader->SetVec3("uColor", color);
    m_shader->SetFloat("uAlpha", alpha);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size()* sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_shader->SetFloat("uSolid", 0.0f);
}

void TextRenderer::Image(unsigned int textureId, float x, float y, float w, float h,
                         const glm::vec3& tint, float alpha) {
    // Screen top-left should map to the image's top. File textures are stored
    // bottom-up for GL, so the top vertices sample v=1 and the bottom v=0.
    const float q[6][4] = {
        {x,     y,     0.0f, 1.0f}, {x + w, y,     1.0f, 1.0f}, {x + w, y + h, 1.0f, 0.0f},
        {x + w, y + h, 1.0f, 0.0f}, {x,     y + h, 0.0f, 0.0f}, {x,     y,     0.0f, 1.0f},
    };
    std::vector<float> verts;
    verts.reserve(6 * 4);
    for (const auto& v : q) {
        verts.push_back(v[0]); verts.push_back(v[1]);
        verts.push_back(v[2]); verts.push_back(v[3]);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    m_shader->SetInt("uText", 0);
    m_shader->SetFloat("uImage", 1.0f);
    m_shader->SetVec3("uColor", tint);
    m_shader->SetFloat("uAlpha", alpha);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Restore state so later Text()/FillRect() use the font atlas again.
    m_shader->SetFloat("uImage", 0.0f);
    m_atlas->Bind(0);
}

void TextRenderer::CustomRect(
    Shader& shader, unsigned int textureId, float x, float y, float w, float h,
    const glm::vec4& color,
    const std::unordered_map<std::string, std::string>& parameters,
    const std::unordered_map<std::string, int>& parameterTypes,
    const std::unordered_map<std::string, const Texture*>& textures) {
    const float q[6][4] = {
        {x, y, 0.0f, 1.0f}, {x + w, y, 1.0f, 1.0f}, {x + w, y + h, 1.0f, 0.0f},
        {x + w, y + h, 1.0f, 0.0f}, {x, y + h, 0.0f, 0.0f}, {x, y, 0.0f, 1.0f},
    };
    shader.Bind();
    shader.SetMat4("uProjection", m_projection);
    shader.SetVec4("uWidgetColor", color);
    shader.SetInt("uUseWidgetTexture", textureId != 0 ? 1 : 0);
    shader.SetInt("uWidgetTexture", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);

    int unit = 1;
    for (const auto& [name, value] : parameters) {
        const auto values = ParseValues(value);
        const auto typeIt = parameterTypes.find(name);
        const int type = typeIt == parameterTypes.end() ? 0 : typeIt->second;
        if (values.empty()) continue;
        if (type == 1 || type == 2) shader.SetInt(name, static_cast<int>(values[0]));
        else if (type == 3 && values.size() >= 2) shader.SetVec2(name, glm::vec2(values[0], values[1]));
        else if (type == 4 && values.size() >= 3) shader.SetVec3(name, glm::vec3(values[0], values[1], values[2]));
        else if ((type == 5 || type == 6) && values.size() >= 4)
            shader.SetVec4(name, glm::vec4(values[0], values[1], values[2], values[3]));
        else shader.SetFloat(name, values[0]);
    }
    for (const auto& [name, texture] : textures) {
        if (!texture) continue;
        texture->Bind(unit);
        shader.SetInt(name, unit++);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    m_shader->Bind();
    m_shader->SetMat4("uProj", m_projection);
    m_shader->SetInt("uText", 0);
    m_shader->SetFloat("uSolid", 0.0f);
    m_shader->SetFloat("uImage", 0.0f);
    m_atlas->Bind(0);
}

} // namespace engine
