#pragma once

#include <engine/ai/BehaviorGraph.h>

#include <string>
#include <utility>
#include <vector>

// A hand-rolled ImGui node-canvas editor for a data-driven behaviour tree
// (engine::ai::BehaviorGraph). Nodes are draggable boxes; drag from a node's output
// pin onto another node to make it a child. Graphs are saved as `.btgraph` files the
// scene references by path, so a NavAgent can run an authored tree instead of the
// built-in patrol/chase/search brain.
class BehaviorGraphPanel {
public:
    void SetOutputDirectory(const std::string& dir) { m_outputDir = dir; }

    // Draws the whole panel body (toolbar + canvas + inspector). Returns true on the
    // frame the graph was saved (so the host can react, e.g. assign it to a selection).
    bool DrawContent();

    const engine::ai::BehaviorGraph& Graph() const { return m_graph; }
    const std::string& LastSavedPath() const { return m_currentPath; }
    const std::string& StatusMessage() const { return m_status; }

    void NewGraph();
    bool SaveToFile(const std::string& path);
    bool LoadFromFile(const std::string& path);

    // Live debugger feed (from EditorApp during Play). 'status' is per-node
    // (0 idle / 1 running / 2 success / 3 failure) indexed like the graph's nodes.
    void SetDebugSnapshot(const std::string& agent, const std::vector<int>& status,
                          const std::vector<std::pair<std::string, std::string>>& blackboard);
    void ClearDebug();

private:
    void DrawToolbar(bool* savedThisFrame);
    void DrawBlackboard();
    void DrawCanvas();
    void DrawInspector();

    bool CanLink(int parent, int child) const;   // rejects self/cycles/arity violations
    void AddLink(int parent, int child);
    bool CanAcceptChild(int node) const;          // composite: always; decorator: if empty

    engine::ai::BehaviorGraph m_graph;
    std::string m_outputDir;
    std::string m_currentPath;
    std::string m_status;
    char        m_nameBuffer[128] = "behavior";

    int   m_selected   = -1;
    int   m_hovered    = -1;   // node under the cursor this frame (for link targeting)
    int   m_linkSource = -1;   // node whose output pin is being dragged from
    int   m_addType    = 0;    // BtNodeType index in the "Add" combo
    float m_scrollX    = 0.0f; // canvas pan
    float m_scrollY    = 0.0f;

    // Pending "create node here" request from a pin-drop or right-click on empty space.
    int       m_pendingParent  = -1;   // node to attach the new child to (-1 = unlinked)
    glm::vec2 m_pendingDropPos{0.0f};  // canvas-space position for the new node

    // Live debugger snapshot (valid while m_debugActive).
    bool                     m_debugActive = false;
    std::string              m_debugAgent;
    std::vector<int>         m_debugStatus;
    std::vector<std::pair<std::string, std::string>> m_debugBlackboard;
};
