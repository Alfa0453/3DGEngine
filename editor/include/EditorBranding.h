#pragma once

#include <vector>

namespace editor::branding {

struct WindowIcon {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

// Engine-owned 3DG cube mark used by the editor window and taskbar.
const WindowIcon& Icon();

} // namespace editor::branding
