#pragma once

#include <engine/ui/Hud.h>

#include <functional>
#include <string>
#include <vector>

// UMG-style HUD editor: a WYSIWYG canvas, a widget hierarchy, and a property
// inspector. It edits a HudDocument in place and reports file intents (new /
// load / save / set-as-scene-HUD) back to EditorApp, which owns the actual IO
// and the scene reference.
class HudEditorPanel {
public:
    struct Result {
        bool newRequested            = false;
        bool loadRequested           = false;
        bool saveRequested           = false;
        bool setAsSceneHud           = false;   // make 'path' the scene's active HUD
        bool refreshImagesRequested  = false;   // rescan the content folder for images
        std::string path;                       // current path text (for load/save/set)
    };

    // Draws the panel window. 'open' is the ImGui close flag. 'imageChoices' are
    // content-folder image paths for the Image dropdown; 'texLookup' resolves a
    // path to a GL texture id so the canvas can preview images.
    Result Draw(engine::HudDocument& doc, bool* open,
                const std::vector<std::string>& imageChoices,
                const std::function<unsigned int(const std::string&)>& texLookup,
                const engine::HudContext* previewContext = nullptr);

    int  SelectedIndex() const { return m_selected; }
    void SetSelected(int index) { m_selected = index; }
    void SetPath(const std::string& path);

private:
    int  m_selected = -1;
    char m_pathBuf[512] = "assets/ui/hud.hud";
    char m_imageFilter[128] = "";   // substring filter for the image dropdown
};
