#include "engine/ui/UI.h"

#include <algorithm>

namespace engine {

// Pure immediate-mode interaction logic (no GL) -- unit-testable.

bool UiPointInRect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

UiResult UiButtonBehavior(int id, float x, float y, float w, float h,
                          const UiInput& in, int& hot, int& active) {
    UiResult r;
    const bool inside = UiPointInRect(in.mouseX, in.mouseY, x, y, w, h);
    if (inside) { hot = id; r.hovered = true; if (in.pressed) active = id; }
    r.held = (active == id);
    if (active == id && in.released) {
        if (inside) r.clicked = true;
        active = -1;
    }
    return r;
}

bool UiSliderBehavior(int id, float x, float y, float w, float h,
                      const UiInput& in, int& hot, int& active, float& value01) {
    const bool inside = UiPointInRect(in.mouseX, in.mouseY, x, y, w, h);
    if (inside) hot = id;
    if (inside && in.pressed) active = id;
    bool changed = false;
    if (active == id) {
        const float t = std::clamp((in.mouseX - x) / std::max(w, 1e-3f), 0.0f, 1.0f);
        if (t != value01) { value01 = t; changed = true; }
        if (in.released) active = -1;
    }
    return changed;
}

} // namespace engine