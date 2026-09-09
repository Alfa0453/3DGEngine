#pragma once

// Visual Scripting — Pass 3 : editor panel (a VIEW over the engine document model).
//
// This panel owns NO runtime graph logic. It edits a vs::VisualScriptEditorDocument (engine-side)
// and renders it with the same hand-rolled ImGui canvas idioms the Animation/Behavior graph editors
// use (BeginChild + InvisibleButton background, GetWindowDrawList grid/bezier, a screen() transform,
// middle-drag pan, wheel zoom, right-click search-add). Runtime execution stays under engine/.

#include <engine/visualscript/VisualScriptEditorDocument.h>
#include <engine/visualscript/VisualScriptNodeCatalog.h>

#include "VisualScriptValueWidgets.h"

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
    // `valueCtx` supplies scene objects + content-browser assets for the shared typed value editors
    // (Milestone 1). It is borrowed for the duration of the call only.
    Result Draw(bool* open, const std::string& assetRoot,
                const vswidgets::ValueWidgetContext& valueCtx = {});

private:
    bool Load(const std::string& path, std::string* error);
    bool Save(std::string* error);

    void DrawToolbar(Result& result, const std::string& assetRoot);
    void DrawCanvas();
    void DrawSidePanel();
    void DrawContextSearch(const glm::vec2& graphPos);
    void AutoConnectDraggedPin(engine::vs::NodeId newNode);
    // Milestone 2: fit the view to all nodes (or the current selection when `selectionOnly`).
    void FrameView(bool selectionOnly);
    // Milestone 3: function authoring UI (breadcrumb + function/param/local editors).
    void DrawFunctionsSection();
    // A rename text field that keeps in-progress text in one shared buffer (no per-frame reseed
    // wiping). `token` uniquely identifies the active field; commits on deactivate. Returns true
    // (with the new text in *out) on commit.
    bool EditText(const char* label, const std::string& token, const std::string& current, std::string* out);

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

    // Rebuilt each Draw(): the borrowed scene/asset context plus this graph's struct/enum defs so
    // the shared value editors can render array/struct/enum values (Milestone 4).
    vswidgets::ValueWidgetContext m_localCtx;
    const vswidgets::ValueWidgetContext& ValueCtx() const { return m_localCtx; }
    void DrawTypesSection();    // struct/enum authoring UI
    void DrawEventsSection();          // event + interface authoring UI (Milestone 6)
    void DrawStateMachinesSection();   // state-machine authoring UI (Milestone 7)

    // ---- Milestone 2: navigation & layout state ---------------------------
    bool  m_snapToGrid = false;              // snap nodes to the grid while dragging
    float m_gridSize   = 28.0f;              // graph-space grid pitch (matches the drawn grid)
    bool  m_showMinimap = true;
    bool  m_commentsCarryNodes = true;       // dragging a comment also moves enclosed nodes
    static constexpr float kZoomMin = 0.20f;
    static constexpr float kZoomMax = 3.00f;

    // box (rubber-band) selection
    bool      m_boxSelecting = false;
    bool      m_boxArmed = false;            // mouse pressed on empty canvas; may become a box
    glm::vec2 m_boxStartScreen{0.0f};

    // comment interaction
    bool m_movingComment = false;
    engine::vs::CommentId m_dragComment = engine::vs::kInvalidCommentId;
    engine::vs::CommentId m_selectedComment = engine::vs::kInvalidCommentId;

    // jump-to-node search (side panel)
    std::array<char, 64> m_jumpBuffer{};

    // shared rename-field state (function names, params, locals) — Milestone 3
    std::string m_editToken;
    std::array<char, 96> m_editBuf{};
    int m_newParamType = 3;   // Float — type combo for new function params/locals
    // frame request deferred until the canvas knows its size
    int m_frameRequest = 0;                  // 0=none 1=all 2=selection
    ImVec2 m_lastCanvasSize{800.0f, 600.0f}; // updated each frame for framing math
};
