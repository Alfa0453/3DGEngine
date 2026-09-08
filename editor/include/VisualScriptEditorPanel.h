#pragma once

// Visual Scripting — Pass 3 : editor panel (a VIEW over the engine document model).
//
// This panel owns NO runtime graph logic. It edits a vs::VisualScriptEditorDocument (engine-side)
// and renders it with the same hand-rolled ImGui canvas idioms the Animation/Behavior graph editors
// use (BeginChild + InvisibleButton background, GetWindowDrawList grid/bezier, a screen() transform,
// middle-drag pan, wheel zoom, right-click search-add). Runtime execution stays under engine/.

#include <engine/visualscript/VisualScriptEditorDocument.h>
#include <engine/visualscript/VisualScriptNodeCatalog.h>

#include <array>
#include <string>

class VisualScriptEditorPanel {
public:
    struct Result {
        bool assetsChanged = false;   // a .3dgvs was created/saved (refresh the content browser)
    };

    // Queue a .3dgvs to open on the next Draw (from the asset browser double-click).
    void QueueOpen(const std::string& path) { m_pendingOpen = path; }
    bool IsDirty() const { return m_document.Dirty(); }
    const std::string& Path() const { return m_path; }
    // Save-for-shutdown hook, mirroring the other authoring panels.
    bool SaveForShutdown(std::string* error) { return Save(error); }

    // `open` toggles the window; `assetRoot` is the project content root (for new-asset placement).
    Result Draw(bool* open, const std::string& assetRoot);

private:
    bool Load(const std::string& path, std::string* error);
    bool Save(std::string* error);

    void DrawToolbar(Result& result, const std::string& assetRoot);
    void DrawCanvas();
    void DrawSidePanel();
    void DrawContextSearch(const glm::vec2& graphPos);
    void AutoConnectDraggedPin(engine::vs::NodeId newNode);

    engine::vs::VisualScriptEditorDocument m_document;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_status;
    bool m_hasAsset = false;

    // canvas view state
    bool  m_contextOpen = false;
    bool  m_contextFromPin = false;   // the search was opened by dragging a wire off a pin (Phase 6)
    glm::vec2 m_contextGraphPos{0.0f};
    std::array<char, 96> m_searchBuffer{};

    // pin-drag link authoring
    bool m_dragging = false;
    engine::vs::NodeId m_dragNode = 0;
    engine::vs::PinId  m_dragPin = 0;
    bool m_dragFromOutput = false;
    engine::vs::ValueType m_dragPinType = engine::vs::ValueType::Float;
    engine::vs::PinKind   m_dragPinKind = engine::vs::PinKind::Data;
    std::string m_linkError;   // transient "why this link was rejected" (Phase 7)

    // node-move undo bracketing
    bool m_movingNodes = false;

    // side-panel variable editing
    std::array<char, 64> m_newVariableName{{'N','e','w','V','a','r','\0'}};
    int m_newVariableType = 2;   // Float
    engine::vs::VariableId m_editingVarId = engine::vs::kInvalidVariableId;   // which rename field is active
    std::array<char, 64> m_varEditBuffer{};   // preserves in-progress rename text across frames
};
