#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace engine {

class Shader;
class TextRenderer;

// Per-frame pointer input for the UI.
struct UiInput {
    float mouseX = 0.0f, mouseY = 0.0f;
    bool  down = false;         // left button held this frame
    bool  pressed = false;      // went down this frame
    bool  released = false;     // went up this frame
};

struct UiResult { bool hovered = false; bool held = false; bool clicked = false; };

// --- Pure interaction helpers (no GL; unit-testable) -------------------------
bool     UiPointInRect(float px, float py, float x, float y, float w, float h);
UiResult UiButtonBehavior(int id, float x, float y, float w, float h, const UiInput& in, int& hot, int& active);
// Slider handle: value01 (0..1) is updated in place while dragging; returns true if
// it changed this frame.
bool     UiSliderBehavior(int id, float x, float y, float w, float h, const UiInput& in, int& hot, int& active, float& value01);

// Immediate-mode UI: call widgets each frame between Begin()/End(); they draw and
// return interaction results. No retained widget tree, no per-widget objects.
class UI {
public:
    UI();
    ~UI();
    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;

    void Begin(int screenWidth, int screenHeight, const UiInput& input);

    void Panel(float x, float y, float w, float h, const glm::vec3& color, float alpha = 0.88f);
    void Bar(float x, float y, float w, float h, float frac, const glm::vec3& fg, const glm::vec3& bg = glm::vec3(0.10f));
    void Label(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    bool Button(int id, const std::string& label, float x, float y, float w, float h);
    // Draws a labelled slider; edits `value` in [min,max]; returns true if changed.
    bool Slider(int id, const std::string& label, float x, float y, float w, float h, float& value, float min, float max);

    void End();

private:
    void Rect(float x, float y, float w, float h, const glm::vec3& color, float alpha);

    std::unique_ptr<Shader>       m_shader;
    std::unique_ptr<TextRenderer> m_text;
    unsigned int m_vao = 0, m_vbo = 0;

    int  m_w = 0, m_h = 0;
    UiInput m_in;
    int  m_hot = -1, m_active = -1;

    struct TextCmd { std::string s; float x, y, scale; glm::vec3 color; };
    std::vector<TextCmd> m_texts;   // queued, flushed on top in End()
};

} // namespace engine