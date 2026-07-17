#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class TextRenderer;
class Shader;
class Texture;

// ---------------------------------------------------------------------------
// Data-driven HUD / game UI (UMG-style). A HudDocument is a flat list of
// widgets positioned by a 9-point anchor + pixel offset, authored in the
// editor's HUD panel and drawn every frame during play by DrawHud(). It is a
// reusable asset saved as a ".hud" text file and referenced by a scene.
// ---------------------------------------------------------------------------

enum class HudWidgetType {
    Panel,   // solid coloured rectangle (backgrounds, frames)
    Text,    // static or data-bound label
    Bar,     // background + fill rectangle (health / mana / progress)
    Button,  // clickable rectangle with a centred label
    Image    // tinted rectangle (textured images come later; drawn as a panel for now)
};

// Nine anchor points. The widget is positioned relative to this point of the
// screen, then nudged by 'offset' (in design pixels).
enum class HudAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight
};

// Where a widget pulls a live value from at runtime.
enum class HudBinding {
    None,            // use the static 'text' / colours as authored
    HealthFraction,  // 0..1 from the player's Health component (bars)
    HealthText,      // "hp/maxHp" from the player's Health component (text)
    NamedFloat,      // ctx.floats[bindKey]  (text or bar, normalised by min/max)
    NamedString      // ctx.strings[bindKey] (text)
};

// What a Button does when clicked (only meaningful while the cursor is free).
enum class HudButtonAction {
    None,
    ExitPlay,     // stop play mode
    RestartPlay,  // restart play mode
    EmitEvent     // set ctx event flag 'bindKey' (game/script can read it)
};

struct HudWidget {
    int           id       = 0;
    int           parent   = -1;         // reserved for nesting; -1 = root
    std::string   name     = "Widget";
    HudWidgetType type     = HudWidgetType::Panel;

    HudAnchor  anchor  = HudAnchor::TopLeft;
    glm::vec2  offset  {24.0f, 24.0f};   // design-pixel offset from the anchor
    glm::vec2  size    {220.0f, 48.0f};  // design-pixel size

    glm::vec4  color    {0.90f, 0.90f, 0.95f, 1.0f};  // text colour / panel colour / button bg
    glm::vec4  bgColor  {0.10f, 0.10f, 0.14f, 0.65f}; // bar background
    glm::vec4  fillColor{0.85f, 0.25f, 0.30f, 1.0f};  // bar fill
    float      textScale = 1.5f;

    std::string text = "Label";          // static text / button label ("{}" = bound value)

    HudBinding  binding  = HudBinding::None;
    std::string bindKey  = "";           // key for NamedFloat / NamedString / EmitEvent
    float       minValue = 0.0f;         // NamedFloat -> bar normalisation
    float       maxValue = 1.0f;

    HudButtonAction action = HudButtonAction::None;

    std::string imageAsset;   // Image widget: path (relative to content root) of the picture
    std::string shaderPath;    // optional Unlit/UI shader graph
    std::unordered_map<std::string, std::string> shaderParameters;
    std::unordered_map<std::string, int> shaderParameterTypes;
    const Shader* customShader = nullptr; // transient runtime resolution
    std::unordered_map<std::string, const Texture*> shaderTextures;

    bool visible = true;
};

struct HudDocument {
    glm::vec2              designSize{1920.0f, 1080.0f};  // authoring resolution
    std::vector<HudWidget> widgets;
    int                    nextId = 1;

    HudWidget& Add(HudWidgetType type);
    void       Remove(int index);
    void       Clear();

    bool Save(const std::string& path, std::string* error = nullptr) const;
    bool Load(const std::string& path, std::string* error = nullptr);
};

// Live values the HUD binds to, rebuilt each frame from the play registry.
struct HudContext {
    bool  hasHealth      = false;
    float health         = 0.0f;
    float maxHealth      = 1.0f;
    float healthFraction = 0.0f;   // 0..1
    bool  alive          = true;

    std::unordered_map<std::string, float>       floats;   // named numeric values
    std::unordered_map<std::string, std::string> strings;  // named text values

    // Mouse (screen pixels, top-left origin) + click edge for button hit-testing.
    // cursorActive is false when the cursor is captured for FPS look.
    bool  cursorActive  = false;
    float cursorX       = 0.0f;
    float cursorY       = 0.0f;
    bool  mousePressed  = false;   // true on the frame the button goes down

    // Resolves an Image widget's asset path to a GL texture id (0 = not found).
    // Supplied by the editor/game; keeps the HUD renderer free of asset code.
    std::function<unsigned int(const std::string&)> textureLookup;
};

// Result of drawing the HUD: which button (if any) was clicked this frame.
struct HudDrawResult {
    int             clickedWidget = -1;
    HudButtonAction clickedAction = HudButtonAction::None;
    std::string     clickedKey    = "";
};

// Draw the whole HUD for one frame. Handles its own text.Begin()/End().
// screenW/screenH are the framebuffer size in pixels.
HudDrawResult DrawHud(TextRenderer& text, const HudDocument& doc, const HudContext& ctx,
                      int screenW, int screenH);

// Compute a widget's on-screen rect (pixels) for a given screen size. Exposed
// so the editor canvas and hit-testing share identical layout math.
void HudWidgetRect(const HudWidget& w, const HudDocument& doc, int screenW, int screenH,
                   float& outX, float& outY, float& outW, float& outH);

const char* HudWidgetTypeName(HudWidgetType type);
const char* HudAnchorName(HudAnchor anchor);
const char* HudBindingName(HudBinding binding);
const char* HudButtonActionName(HudButtonAction action);

} // namespace engine
