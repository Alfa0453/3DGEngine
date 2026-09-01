#include "EditorPanels.h"

#include <cassert>
#include <string>

int main() {
    using Panel = EditorPanels::Panel;
    EditorPanels panels;
    assert(EditorPanels::GroupOf(Panel::RenderDebugger) == EditorPanels::Group::Debug);
    assert(std::string(EditorPanels::Name(Panel::RenderDebugger)).find("Render Debugger")
        != std::string::npos);
    assert(!panels.IsOpen(Panel::RenderDebugger));
    panels.SetOpen(Panel::RenderDebugger, true);
    assert(panels.IsOpen(Panel::RenderDebugger));
    panels.ResetDefaults();
    assert(!panels.IsOpen(Panel::RenderDebugger));
    return 0;
}
