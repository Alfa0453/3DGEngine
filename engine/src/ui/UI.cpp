#include "engine/ui/UI.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/TextRenderer.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace engine {

// ---- GL immediate-mode UI ---------------------------------------------------

namespace {
const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;   // unit quad [0,1]^2
uniform vec4 uRect;                    // x, y, w, h (pixels, top-left origin)
uniform mat4 uProj;
void main() {
    vec2 p = uRect.xy + aPos * uRect.zw;
    gl_Position = uProj * vec4(p, 0.0, 1.0);
}
)GLSL";
const char* kFrag = R"GLSL(
#version 330 core
uniform vec3 uColor;
uniform float uAlpha;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, uAlpha); }
)GLSL";
} // namespace

UI::UI()
    : m_shader(std::make_unique<Shader>(kVert, kFrag)),
      m_text(std::make_unique<TextRenderer>()) {
    const float quad[] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

UI::~UI() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void UI::Begin(int screenWidth, int screenHeight, const UiInput& input) {
    m_w = screenWidth; m_h = screenHeight; m_in = input;
    m_hot = -1;
    m_texts.clear();
    // Shape pass state: alpha blend, no depth.
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_shader->Bind();
    m_shader->SetMat4("uProj", glm::ortho(0.0f, static_cast<float>(m_w),
                                          static_cast<float>(m_h), 0.0f));
    glBindVertexArray(m_vao);
}

void UI::Rect(float x, float y, float w, float h, const glm::vec3& color, float alpha) {
    m_shader->Bind();
    m_shader->SetVec3("uColor", color);
    m_shader->SetFloat("uAlpha", alpha);
    glUniform4f(glGetUniformLocation(m_shader->ID(), "uRect"), x, y, w, h);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void UI::Panel(float x, float y, float w, float h, const glm::vec3& color, float alpha) {
    Rect(x, y, w, h, color, alpha);
}

void UI::Bar(float x, float y, float w, float h, float frac, const glm::vec3& fg, const glm::vec3& bg) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    Rect(x, y, w, h, bg, 0.9f);
    Rect(x, y, w * frac, h, fg, 0.95f);
}

void UI::Label(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_texts.push_back({ text, x, y, scale, color });
}

bool UI::Button(int id, const std::string& label, float x, float y, float w, float h) {
    const UiResult r = UiButtonBehavior(id, x, y, w, h, m_in, m_hot, m_active);
    glm::vec3 c(0.22f, 0.26f, 0.32f);
    if (r.held)         c = glm::vec3(0.14f, 0.34f, 0.5f);
    else if (r.hovered) c = glm::vec3(0.30f, 0.38f, 0.48f);
    Rect(x, y, w, h, c, 0.95f);
    const float tw = m_text->Measure(label, 1.5f);
    m_texts.push_back({ label, x + (w - tw) * 0.5f, y + h * 0.5f - 7.0f, 1.5f, glm::vec3(0.92f) });
    return r.clicked;
}

bool UI::Slider(int id, const std::string& label, float x, float y, float w, float h,
                float& value, float min, float max) {
    float v01 = (max > min) ? (value - min) / (max - min) : 0.0f;
    v01 = std::clamp(v01, 0.0f, 1.0f);
    const bool changed = UiSliderBehavior(id, x, y, w, h, m_in, m_hot, m_active, v01);
    if (changed) value = min + v01 * (max - min);

    Rect(x, y + h * 0.5f - 3.0f, w, 6.0f, glm::vec3(0.12f), 0.9f);         // track
    Rect(x, y + h * 0.5f - 3.0f, w * v01, 6.0f, glm::vec3(0.3f, 0.55f, 0.9f), 0.95f);  // fill
    const float hx = x + w * v01 - 6.0f;                                   // handle
    const bool hot = (m_hot == id) || (m_active == id);
    Rect(hx, y, 12.0f, h, hot ? glm::vec3(0.6f, 0.8f, 1.0f) : glm::vec3(0.5f, 0.6f, 0.75f), 1.0f);
    m_texts.push_back({ label, x, y - 16.0f, 1.3f, glm::vec3(0.85f) });
    return changed;
}

void UI::End() {
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    // Text on top of the shapes.
    m_text->Begin(m_w, m_h);
    for (const TextCmd& t : m_texts) m_text->Text(t.s, t.x, t.y, t.scale, t.color);
    m_text->End();
    glEnable(GL_DEPTH_TEST);
}

} // namespace engine