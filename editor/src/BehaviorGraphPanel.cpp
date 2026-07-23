#include "BehaviorGraphPanel.h"

#include <engine/ai/BtScript.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>

using engine::ai::BtNodeType;
using engine::ai::BtGraphNode;

namespace {

constexpr float kNodeW   = 150.0f;
constexpr float kNodeH   = 56.0f;   // title + param area
constexpr float kAttachH = 16.0f;   // per attached decorator/service row

std::filesystem::path NormalizedAbsolutePath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::filesystem::path CanonicalBehaviorGraphPath(const std::filesystem::path& input) {
    std::filesystem::path result = input;
    constexpr const char* suffix = ".btgraph";
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };

    std::string filename = result.filename().string();
    std::string lowered = lower(filename);
    const std::string doubled = std::string(suffix) + suffix;
    while (lowered.size() >= doubled.size()
           && lowered.compare(lowered.size() - doubled.size(), doubled.size(), doubled) == 0) {
        filename.erase(filename.size() - std::char_traits<char>::length(suffix));
        lowered = lower(filename);
    }
    if (lowered.size() < std::char_traits<char>::length(suffix)
        || lowered.compare(lowered.size() - std::char_traits<char>::length(suffix),
                           std::char_traits<char>::length(suffix), suffix) != 0) {
        filename += suffix;
    }
    result.replace_filename(filename);
    return result;
}

float NodeHeight(const BtGraphNode& n) {
    return kNodeH + static_cast<float>(n.decorators.size() + n.services.size()) * kAttachH;
}

// A combo listing every registered BtScript class name. Edits 'name' in place.
void ScriptPicker(const char* label, std::string& name) {
    const std::vector<std::string> names = engine::ai::BtScriptRegistry::Instance().Names();
    const char* preview = name.empty() ? "(pick a script)" : name.c_str();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo(label, preview)) {
        if (names.empty()) {
            ImGui::TextDisabled("No BtScripts registered");
        }
        for (const std::string& n : names) {
            if (ImGui::Selectable(n.c_str(), n == name)) name = n;
        }
        ImGui::EndCombo();
    }
}

// A combo of the graph's blackboard keys. Edits 'key' in place.
void KeyPicker(const char* id, const std::vector<engine::ai::BlackboardEntry>& schema,
               std::string& key) {
    const char* preview = key.empty() ? "(pick key)" : key.c_str();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo(id, preview)) {
        if (schema.empty()) ImGui::TextDisabled("Add keys in Blackboard above");
        for (const engine::ai::BlackboardEntry& e : schema) {
            if (!e.key.empty() && ImGui::Selectable(e.key.c_str(), e.key == key)) key = e.key;
        }
        ImGui::EndCombo();
    }
}

// Renders the categorized "create node" menu (Composite / Task / Condition /
// Decorator). Returns true and writes *out when the user picks a type.
bool CreationMenu(BtNodeType* out) {
    bool picked = false;
    auto item = [&](BtNodeType t) {
        if (ImGui::MenuItem(engine::ai::BtNodeTypeName(t))) { *out = t; picked = true; }
    };
    if (ImGui::BeginMenu("Composite")) {
        item(BtNodeType::Selector);
        item(BtNodeType::Sequence);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Task")) {
        item(BtNodeType::Chase);
        item(BtNodeType::Patrol);
        item(BtNodeType::MoveToTarget);
        item(BtNodeType::Wait);
        item(BtNodeType::Idle);
        item(BtNodeType::Flee);
        item(BtNodeType::Wander);
        item(BtNodeType::Attack);       // deal damage to the target
        item(BtNodeType::FocusTarget);  // face a visible target persistently
        item(BtNodeType::ClearFocus);   // release target-facing control
        item(BtNodeType::ScriptTask);   // your own C++ task
        item(BtNodeType::BbSetBool);    // blackboard writes (no code)
        item(BtNodeType::BbSetFloat);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Condition")) {
        item(BtNodeType::SeesTarget);
        item(BtNodeType::TargetWithin);
        item(BtNodeType::HealthBelow);   // self HP low
        item(BtNodeType::TargetDead);    // target killed
        item(BtNodeType::BbCheckBool);   // blackboard checks (no code)
        item(BtNodeType::BbCheckFloat);
        item(BtNodeType::BbFloatBelow);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Decorator")) {
        item(BtNodeType::Inverter);
        item(BtNodeType::Succeeder);
        item(BtNodeType::Repeat);
        ImGui::EndMenu();
    }
    item(BtNodeType::Subtree);   // runs another .btgraph
    return picked;
}

ImU32 NodeColor(BtNodeType t, bool selected) {
    ImU32 base;
    if (engine::ai::BtNodeTypeIsComposite(t))      base = IM_COL32(58, 84, 120, 235);
    else if (engine::ai::BtNodeTypeIsDecorator(t)) base = IM_COL32(96, 74, 120, 235);
    else if (t == BtNodeType::SeesTarget || t == BtNodeType::TargetWithin)
                                                   base = IM_COL32(110, 96, 48, 235);
    else                                           base = IM_COL32(52, 104, 72, 235);
    return selected ? IM_COL32(230, 170, 60, 255) : base;
}

} // namespace

void BehaviorGraphPanel::SetOutputDirectory(const std::string& dir) {
    if (m_outputDir == dir) return;
    m_outputDir = dir;
    RefreshSavedGraphs();
}

void BehaviorGraphPanel::RefreshSavedGraphs() {
    m_savedGraphs.clear();
    if (m_outputDir.empty()) return;

    const std::filesystem::path root = NormalizedAbsolutePath(m_outputDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return;

    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".btgraph") continue;

        SavedGraph asset;
        asset.fullPath = NormalizedAbsolutePath(it->path()).string();
        asset.displayPath = std::filesystem::relative(it->path(), root, ec).generic_string();
        if (ec) {
            ec.clear();
            asset.displayPath = it->path().filename().string();
        }
        m_savedGraphs.push_back(std::move(asset));
    }
    std::sort(m_savedGraphs.begin(), m_savedGraphs.end(),
        [](const SavedGraph& a, const SavedGraph& b) {
            return a.displayPath < b.displayPath;
        });
}

void BehaviorGraphPanel::NewGraph() {
    m_graph = engine::ai::BehaviorGraph{};
    m_selected = -1;
    m_linkSource = -1;
    m_currentPath.clear();
    std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", "behavior");
    m_status = "New graph.";
}

bool BehaviorGraphPanel::SaveToFile(const std::string& path) {
    const std::filesystem::path requestedTarget = NormalizedAbsolutePath(path);
    const std::filesystem::path target = NormalizedAbsolutePath(
        CanonicalBehaviorGraphPath(path));
    std::error_code existenceError;
    const bool replacingExisting = std::filesystem::is_regular_file(target, existenceError);
    std::error_code directoryError;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), directoryError);
    }
    if (directoryError) {
        m_status = "Save failed: could not create the behavior-tree folder.";
        return false;
    }
    std::string err;
    if (engine::ai::SaveBehaviorGraph(target.string(), m_graph, &err)) {
        bool removedDuplicate = false;
        if (requestedTarget != target) {
            std::error_code duplicateError;
            if (std::filesystem::is_regular_file(requestedTarget, duplicateError)) {
                removedDuplicate = std::filesystem::remove(
                    requestedTarget, duplicateError) && !duplicateError;
            }
        }
        m_currentPath = target.string();
        m_status = std::string(replacingExisting ? "Updated " : "Created ")
            + target.filename().string();
        if (removedDuplicate) m_status += " (removed duplicate extension)";
        RefreshSavedGraphs();
        return true;
    }
    m_status = "Save failed: " + err;
    return false;
}

bool BehaviorGraphPanel::LoadFromFile(const std::string& path) {
    const std::filesystem::path target = NormalizedAbsolutePath(path);
    std::string err;
    engine::ai::BehaviorGraph loaded;
    if (engine::ai::LoadBehaviorGraph(target.string(), loaded, &err)) {
        m_graph = std::move(loaded);
        m_currentPath = target.string();
        const std::string stem = target.stem().string();
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", stem.c_str());
        m_selected = -1;
        m_linkSource = -1;
        m_status = "Loaded " + target.filename().string();
        return true;
    }
    m_status = "Load failed: " + err;
    return false;
}

bool BehaviorGraphPanel::CanLink(int parent, int child) const {
    if (parent == child || parent < 0 || child < 0) return false;
    if (parent >= static_cast<int>(m_graph.nodes.size()) ||
        child  >= static_cast<int>(m_graph.nodes.size())) return false;

    const BtGraphNode& p = m_graph.nodes[static_cast<std::size_t>(parent)];
    if (engine::ai::BtNodeTypeIsLeaf(p.type)) return false;                 // leaves take no children
    if (engine::ai::BtNodeTypeIsDecorator(p.type) && !p.children.empty()) return false; // one only

    for (int c : p.children) if (c == child) return false;                 // already linked

    // Reject if 'parent' is reachable from 'child' (would create a cycle).
    std::function<bool(int)> reaches = [&](int from) -> bool {
        if (from == parent) return true;
        for (int c : m_graph.nodes[static_cast<std::size_t>(from)].children)
            if (reaches(c)) return true;
        return false;
    };
    return !reaches(child);
}

void BehaviorGraphPanel::AddLink(int parent, int child) {
    if (!CanLink(parent, child)) return;
    m_graph.nodes[static_cast<std::size_t>(parent)].children.push_back(child);
}

bool BehaviorGraphPanel::CanAcceptChild(int node) const {
    if (node < 0 || node >= static_cast<int>(m_graph.nodes.size())) return false;
    const BtGraphNode& n = m_graph.nodes[static_cast<std::size_t>(node)];
    if (engine::ai::BtNodeTypeIsComposite(n.type)) return true;             // unlimited
    if (engine::ai::BtNodeTypeIsDecorator(n.type)) return n.children.empty(); // one only
    return false;                                                          // leaves take none
}

void BehaviorGraphPanel::SetDebugSnapshot(const std::string& agent, const std::vector<int>& status,
                                          const std::vector<std::pair<std::string, std::string>>& blackboard) {
    m_debugActive = true;
    m_debugAgent = agent;
    m_debugStatus = status;
    m_debugBlackboard = blackboard;
}

void BehaviorGraphPanel::ClearDebug() {
    m_debugActive = false;
    m_debugStatus.clear();
    m_debugBlackboard.clear();
}

bool BehaviorGraphPanel::DrawContent() {
    bool savedThisFrame = false;
    DrawToolbar(&savedThisFrame);
    DrawBlackboard();
    ImGui::Separator();
    DrawCanvas();
    DrawInspector();
    return savedThisFrame;
}

void BehaviorGraphPanel::DrawBlackboard() {
    using engine::ai::BlackboardEntry;
    std::vector<BlackboardEntry>& bb = m_graph.blackboard;

    char header[48];
    std::snprintf(header, sizeof(header), "Blackboard (%d keys)###bb", static_cast<int>(bb.size()));
    if (!ImGui::CollapsingHeader(header)) {
        return;
    }
    ImGui::TextDisabled("Shared named values every node/script reads via c.blackboard. "
                        "Keys have no spaces.");

    if (m_debugActive) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Live values (%s):", m_debugAgent.c_str());
        if (m_debugBlackboard.empty()) {
            ImGui::TextDisabled("  (no values set)");
        }
        for (const std::pair<std::string, std::string>& kv : m_debugBlackboard) {
            ImGui::BulletText("%s = %s", kv.first.c_str(), kv.second.c_str());
        }
        ImGui::Separator();
    }

    const char* kTypes[] = {"Bool", "Int", "Float", "Vec3", "String"};
    for (std::size_t k = 0; k < bb.size(); ++k) {
        BlackboardEntry& e = bb[k];
        ImGui::PushID(5000 + static_cast<int>(k));

        char keyBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "%s", e.key.c_str());
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("##key", keyBuf, sizeof(keyBuf))) e.key = keyBuf;

        ImGui::SameLine();
        int ti = static_cast<int>(e.type);
        ImGui::SetNextItemWidth(74.0f);
        if (ImGui::Combo("##type", &ti, kTypes, IM_ARRAYSIZE(kTypes))) {
            e.type = static_cast<BlackboardEntry::Type>(ti);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        switch (e.type) {
            case BlackboardEntry::Type::Bool:   ImGui::Checkbox("##v", &e.b); break;
            case BlackboardEntry::Type::Int:    ImGui::DragInt("##v", &e.i); break;
            case BlackboardEntry::Type::Float:  ImGui::DragFloat("##v", &e.f, 0.05f); break;
            case BlackboardEntry::Type::Vec3:   ImGui::DragFloat3("##v", &e.v.x, 0.05f); break;
            case BlackboardEntry::Type::String: {
                char valBuf[96];
                std::snprintf(valBuf, sizeof(valBuf), "%s", e.s.c_str());
                if (ImGui::InputText("##v", valBuf, sizeof(valBuf))) e.s = valBuf;
                break;
            }
        }

        ImGui::SameLine();
        const bool remove = ImGui::SmallButton("x");
        ImGui::PopID();
        if (remove) {
            bb.erase(bb.begin() + static_cast<long>(k));
            break;
        }
    }

    if (ImGui::Button("+ Add Key")) {
        BlackboardEntry e;
        e.key = "newKey";
        bb.push_back(e);
    }
}

void BehaviorGraphPanel::DrawToolbar(bool* savedThisFrame) {
    std::string openPreview = "Choose saved behavior tree...";
    for (const SavedGraph& asset : m_savedGraphs) {
        if (std::filesystem::path(asset.fullPath).lexically_normal()
            == std::filesystem::path(m_currentPath).lexically_normal()) {
            openPreview = std::filesystem::path(asset.displayPath).filename().string();
            break;
        }
    }
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Saved Trees", openPreview.c_str())) {
        if (m_savedGraphs.empty()) ImGui::TextDisabled("No .btgraph assets in Content");
        for (const SavedGraph& asset : m_savedGraphs) {
            const bool selected = std::filesystem::path(asset.fullPath).lexically_normal()
                == std::filesystem::path(m_currentPath).lexically_normal();
            const std::string filename =
                std::filesystem::path(asset.displayPath).filename().string();
            const std::string itemLabel = filename + "###" + asset.fullPath;
            if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                LoadFromFile(asset.fullPath);
            }
            if (ImGui::IsItemHovered() && asset.displayPath != filename) {
                ImGui::SetTooltip("Content/%s", asset.displayPath.c_str());
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh Trees")) RefreshSavedGraphs();

    // Node-type picker + Add.
    const char* current = engine::ai::BtNodeTypeName(static_cast<BtNodeType>(m_addType));
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##addtype", current)) {
        for (int i = 0; i < static_cast<int>(BtNodeType::Count); ++i) {
            const BtNodeType t = static_cast<BtNodeType>(i);
            if (t == BtNodeType::Repath || t == BtNodeType::ScriptDecorator ||
                t == BtNodeType::ScriptService || t == BtNodeType::Cooldown ||
                t == BtNodeType::TimeLimit || t == BtNodeType::RandomChance) {
                continue;   // attachment-only; attach to a node instead of placing standalone
            }
            const bool sel = (i == m_addType);
            if (ImGui::Selectable(engine::ai::BtNodeTypeName(static_cast<BtNodeType>(i)), sel)) {
                m_addType = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Node")) {
        const int idx = m_graph.AddNode(static_cast<BtNodeType>(m_addType),
                                        glm::vec2(-m_scrollX + 60.0f, -m_scrollY + 60.0f));
        m_selected = idx;
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        NewGraph();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (!m_currentPath.empty()) ImGui::BeginDisabled();
    ImGui::InputText("##name", m_nameBuffer, sizeof(m_nameBuffer));
    if (!m_currentPath.empty()) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Save updates %s",
                std::filesystem::path(m_currentPath).filename().string().c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string path = m_currentPath;
        if (path.empty()) {
            path = m_outputDir.empty() ? std::string(m_nameBuffer)
                                       : m_outputDir + "/" + m_nameBuffer;
        }
        if (SaveToFile(path) && savedThisFrame) *savedThisFrame = true;
    }

    ImGui::SameLine();
    if (m_graph.IsValid()) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "valid");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1.0f), "no valid root");
    }

    if (!m_status.empty()) {
        ImGui::TextUnformatted(m_status.c_str());
    }
    if (m_debugActive) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                           "Debugging %s - node borders show live status (yellow run / green ok / red fail)",
                           m_debugAgent.c_str());
    }
}

void BehaviorGraphPanel::DrawCanvas() {
    ImGui::BeginChild("bt_canvas", ImVec2(0.0f, -150.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();   // top-left of canvas content
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    // Background grid.
    const ImVec2 canvasMax(origin.x + canvasSize.x, origin.y + canvasSize.y);
    draw->AddRectFilled(origin, canvasMax, IM_COL32(28, 30, 34, 255));
    const float grid = 32.0f;
    for (float x = std::fmod(m_scrollX, grid); x < canvasSize.x; x += grid)
        draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, canvasMax.y),
                      IM_COL32(48, 50, 56, 255));
    for (float y = std::fmod(m_scrollY, grid); y < canvasSize.y; y += grid)
        draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(canvasMax.x, origin.y + y),
                      IM_COL32(48, 50, 56, 255));

    // Background interaction button FIRST (so the nodes submitted after sit on top and
    // win hover); SetItemAllowOverlap lets those later items receive input too.
    ImGui::SetCursorScreenPos(origin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("canvas_bg",
                           (canvasSize.x > 0 && canvasSize.y > 0) ? canvasSize : ImVec2(1.0f, 1.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_linkSource < 0) {
        m_scrollX += ImGui::GetIO().MouseDelta.x;
        m_scrollY += ImGui::GetIO().MouseDelta.y;
    }
    if (ImGui::IsItemClicked()) {
        m_selected = -1;
    }
    // Right-click empty space to add a stand-alone node at the cursor.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        m_pendingParent = -1;
        m_pendingDropPos = glm::vec2(m.x - origin.x - m_scrollX, m.y - origin.y - m_scrollY);
        ImGui::OpenPopup("bt_create");
    }

    auto nodeScreen = [&](int i) {
        const BtGraphNode& n = m_graph.nodes[static_cast<std::size_t>(i)];
        return ImVec2(origin.x + m_scrollX + n.canvasPos.x, origin.y + m_scrollY + n.canvasPos.y);
    };

    // Links (parent bottom-center -> child top-center).
    for (int i = 0; i < static_cast<int>(m_graph.nodes.size()); ++i) {
        const ImVec2 p = nodeScreen(i);
        const ImVec2 from(p.x + kNodeW * 0.5f, p.y + NodeHeight(m_graph.nodes[static_cast<std::size_t>(i)]));
        for (int c : m_graph.nodes[static_cast<std::size_t>(i)].children) {
            if (c < 0 || c >= static_cast<int>(m_graph.nodes.size())) continue;
            const ImVec2 cp = nodeScreen(c);
            const ImVec2 to(cp.x + kNodeW * 0.5f, cp.y);
            draw->AddBezierCubic(from, ImVec2(from.x, from.y + 40.0f),
                                 ImVec2(to.x, to.y - 40.0f), to,
                                 IM_COL32(180, 190, 200, 220), 2.5f);
        }
    }

    m_hovered = -1;

    // Nodes. Each gets an invisible button for select+drag, then a second small one
    // at the bottom pin for starting a link.
    for (int i = 0; i < static_cast<int>(m_graph.nodes.size()); ++i) {
        const BtGraphNode& node = m_graph.nodes[static_cast<std::size_t>(i)];
        const ImVec2 pos = nodeScreen(i);
        ImGui::PushID(i);

        const float h = NodeHeight(node);
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("body", ImVec2(kNodeW, h));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) m_hovered = i;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_linkSource < 0) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            m_graph.nodes[static_cast<std::size_t>(i)].canvasPos += glm::vec2(d.x, d.y);
        }
        if (ImGui::IsItemClicked()) {
            m_selected = i;
        }

        // Node body.
        const ImVec2 pmax(pos.x + kNodeW, pos.y + h);
        draw->AddRectFilled(pos, pmax, NodeColor(node.type, i == m_selected), 5.0f);
        ImU32 border = (i == m_graph.root) ? IM_COL32(240, 210, 90, 255) : IM_COL32(20, 22, 26, 255);
        float borderTh = (i == m_graph.root) ? 2.5f : 1.5f;
        if (m_debugActive && static_cast<std::size_t>(i) < m_debugStatus.size()) {
            switch (m_debugStatus[static_cast<std::size_t>(i)]) {
                case 1: border = IM_COL32(250, 210, 70, 255);  borderTh = 3.0f; break;  // running
                case 2: border = IM_COL32(90, 210, 110, 255);  borderTh = 3.0f; break;  // success
                case 3: border = IM_COL32(220, 90, 80, 255);   borderTh = 3.0f; break;  // failure
                default: break;
            }
        }
        draw->AddRect(pos, pmax, border, 5.0f, 0, borderTh);
        draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 7.0f), IM_COL32(235, 238, 242, 255),
                      engine::ai::BtNodeTypeName(node.type));
        if (i == m_graph.root) {
            draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 30.0f), IM_COL32(240, 210, 90, 255), "ROOT");
        } else if (engine::ai::BtNodeTypeIsSubtree(node.type)) {
            const std::string& p = node.script;
            const std::size_t slash = p.find_last_of("/\\");
            const char* leaf = p.empty() ? "(no asset)"
                                         : p.c_str() + (slash == std::string::npos ? 0 : slash + 1);
            draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 30.0f), IM_COL32(200, 170, 220, 255), leaf);
        } else if (engine::ai::BtNodeTypeUsesScript(node.type)) {
            draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 30.0f), IM_COL32(150, 210, 160, 255),
                          node.script.empty() ? "(no script)" : node.script.c_str());
        } else if (engine::ai::BtNodeTypeUsesKey(node.type)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s = %.2f",
                          node.key.empty() ? "(key)" : node.key.c_str(), node.param);
            draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 30.0f), IM_COL32(170, 200, 230, 255), buf);
        } else if (engine::ai::BtNodeTypeUsesParam(node.type)) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s = %.2f",
                          engine::ai::BtNodeTypeParamLabel(node.type), node.param);
            draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 30.0f), IM_COL32(200, 205, 210, 255), buf);
        }

        // Attached decorators (amber) then services (blue), stacked under the title.
        float rowY = pos.y + kNodeH;
        auto drawAttachRow = [&](const engine::ai::BtAttachment& a, ImU32 stripe, char tag) {
            draw->AddRectFilled(ImVec2(pos.x + 2.0f, rowY), ImVec2(pos.x + kNodeW - 2.0f, rowY + kAttachH - 1.0f),
                                IM_COL32(30, 32, 38, 235));
            draw->AddRectFilled(ImVec2(pos.x + 2.0f, rowY), ImVec2(pos.x + 6.0f, rowY + kAttachH - 1.0f), stripe);
            char buf[64];
            if (engine::ai::BtNodeTypeUsesScript(a.type)) {
                std::snprintf(buf, sizeof(buf), "%c %s", tag,
                              a.script.empty() ? engine::ai::BtNodeTypeName(a.type) : a.script.c_str());
            } else if (engine::ai::BtNodeTypeUsesKey(a.type)) {
                std::snprintf(buf, sizeof(buf), "%c %s %s %.1f", tag, engine::ai::BtNodeTypeName(a.type),
                              a.key.empty() ? "?" : a.key.c_str(), a.param);
            } else if (engine::ai::BtNodeTypeUsesParam(a.type)) {
                std::snprintf(buf, sizeof(buf), "%c %s %.2f", tag, engine::ai::BtNodeTypeName(a.type), a.param);
            } else {
                std::snprintf(buf, sizeof(buf), "%c %s", tag, engine::ai::BtNodeTypeName(a.type));
            }
            draw->AddText(ImVec2(pos.x + 10.0f, rowY + 1.0f), IM_COL32(215, 220, 226, 255), buf);
            rowY += kAttachH;
        };
        for (const engine::ai::BtAttachment& a : node.decorators) drawAttachRow(a, IM_COL32(210, 170, 70, 255), 'D');
        for (const engine::ai::BtAttachment& a : node.services)   drawAttachRow(a, IM_COL32(80, 150, 210, 255), 'S');

        // Output pin (only where the node can take another child).
        if (CanAcceptChild(i)) {
            const ImVec2 pin(pos.x + kNodeW * 0.5f, pos.y + h);
            draw->AddCircleFilled(pin, 5.0f, IM_COL32(210, 215, 220, 255));
            ImGui::SetCursorScreenPos(ImVec2(pin.x - 7.0f, pin.y - 7.0f));
            ImGui::InvisibleButton("pin", ImVec2(14.0f, 14.0f));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_linkSource = i;
            }
        }
        ImGui::PopID();
    }

    // Draw the in-progress link and resolve it on release.
    if (m_linkSource >= 0) {
        const ImVec2 p = nodeScreen(m_linkSource);
        const ImVec2 from(p.x + kNodeW * 0.5f,
                          p.y + NodeHeight(m_graph.nodes[static_cast<std::size_t>(m_linkSource)]));
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        draw->AddBezierCubic(from, ImVec2(from.x, from.y + 40.0f),
                             ImVec2(mouse.x, mouse.y - 40.0f), mouse,
                             IM_COL32(240, 210, 90, 220), 2.5f);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (m_hovered >= 0) {
                AddLink(m_linkSource, m_hovered);      // dropped on a node -> link to it
            } else {
                // Dropped on empty space -> prompt to create a child there.
                const ImVec2 m = ImGui::GetIO().MousePos;
                m_pendingParent = m_linkSource;
                m_pendingDropPos = glm::vec2(m.x - origin.x - m_scrollX, m.y - origin.y - m_scrollY);
                ImGui::OpenPopup("bt_create");
            }
            m_linkSource = -1;
        }
    }

    // Node-creation menu (from a pin-drop on empty space, or a right-click).
    if (ImGui::BeginPopup("bt_create")) {
        ImGui::TextDisabled(m_pendingParent >= 0 ? "Add child node" : "Add node");
        ImGui::Separator();
        BtNodeType picked = BtNodeType::Sequence;
        if (CreationMenu(&picked)) {
            const int idx = m_graph.AddNode(picked, m_pendingDropPos);
            if (m_pendingParent >= 0) {
                AddLink(m_pendingParent, idx);
            }
            m_selected = idx;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void BehaviorGraphPanel::DrawInspector() {
    ImGui::BeginChild("bt_inspector", ImVec2(0.0f, 0.0f), true);
    if (m_selected < 0 || m_selected >= static_cast<int>(m_graph.nodes.size())) {
        ImGui::TextDisabled("Drag a node's bottom pin onto empty space to create + attach a "
                            "child (Composite / Task / Condition / Decorator), or onto another "
                            "node to link it. Right-click empty space to add a loose node. "
                            "Drag empty space to pan.");
        ImGui::EndChild();
        return;
    }

    BtGraphNode& node = m_graph.nodes[static_cast<std::size_t>(m_selected)];
    ImGui::Text("Node %d: %s", m_selected, engine::ai::BtNodeTypeName(node.type));

    if (engine::ai::BtNodeTypeUsesParam(node.type)) {
        ImGui::SetNextItemWidth(160.0f);
        ImGui::DragFloat(engine::ai::BtNodeTypeParamLabel(node.type), &node.param, 0.05f, 0.0f, 999.0f);
    }
    if (engine::ai::BtNodeTypeUsesScript(node.type)) {
        ScriptPicker("Script Class", node.script);
    }
    if (engine::ai::BtNodeTypeUsesKey(node.type)) {
        KeyPicker("Key", m_graph.blackboard, node.key);
    }
    if (engine::ai::BtNodeTypeIsSubtree(node.type)) {
        char pathBuf[256];
        std::snprintf(pathBuf, sizeof(pathBuf), "%s", node.script.c_str());
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText("Subtree .btgraph", pathBuf, sizeof(pathBuf))) node.script = pathBuf;
        ImGui::TextDisabled("Path to another .btgraph; it runs in place of this node.");
    }

    if (m_selected != m_graph.root) {
        if (ImGui::Button("Set as Root")) {
            m_graph.root = m_selected;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Delete Node")) {
        m_graph.RemoveNode(m_selected);
        m_selected = -1;
        ImGui::EndChild();
        return;
    }

    const std::size_t childCount = node.children.size();
    ImGui::Text("Children: %zu", childCount);
    for (std::size_t k = 0; k < node.children.size(); ++k) {
        const int c = node.children[k];
        ImGui::PushID(static_cast<int>(k));
        ImGui::BulletText("%d: %s", c,
                          (c >= 0 && c < static_cast<int>(m_graph.nodes.size()))
                              ? engine::ai::BtNodeTypeName(m_graph.nodes[static_cast<std::size_t>(c)].type)
                              : "?");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            node.children.erase(node.children.begin() + static_cast<long>(k));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    // ---- Attached decorators + services (wrap this node at runtime) ----
    auto defaultParam = [](engine::ai::BtNodeType t) -> float {
        switch (t) {
            case engine::ai::BtNodeType::TargetWithin: return 5.0f;
            case engine::ai::BtNodeType::Repeat:       return 1.0f;
            case engine::ai::BtNodeType::Repath:       return 0.5f;
            default:                                   return 0.0f;
        }
    };
    auto attachmentSection = [&](const char* heading, std::vector<engine::ai::BtAttachment>& list,
                                 const engine::ai::BtNodeType* choices, int choiceCount,
                                 const char* addLabel, const char* popupId, int idBase) {
        ImGui::Separator();
        ImGui::TextUnformatted(heading);
        for (std::size_t k = 0; k < list.size(); ++k) {
            engine::ai::BtAttachment& a = list[k];
            ImGui::PushID(idBase + static_cast<int>(k));
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(engine::ai::BtNodeTypeName(a.type));
            if (engine::ai::BtNodeTypeUsesParam(a.type)) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("##p", &a.param, 0.05f, 0.0f, 999.0f);
            }
            if (engine::ai::BtNodeTypeUsesScript(a.type)) {
                ImGui::SameLine();
                ScriptPicker("##sc", a.script);
            }
            if (engine::ai::BtNodeTypeUsesKey(a.type)) {
                ImGui::SameLine();
                KeyPicker("##ky", m_graph.blackboard, a.key);
            }
            ImGui::SameLine();
            const bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (remove) {
                list.erase(list.begin() + static_cast<long>(k));
                break;
            }
        }
        if (ImGui::Button(addLabel)) {
            ImGui::OpenPopup(popupId);
        }
        if (ImGui::BeginPopup(popupId)) {
            for (int i = 0; i < choiceCount; ++i) {
                if (ImGui::MenuItem(engine::ai::BtNodeTypeName(choices[i]))) {
                    engine::ai::BtAttachment a;
                    a.type = choices[i];
                    a.param = defaultParam(choices[i]);
                    list.push_back(a);
                }
            }
            ImGui::EndPopup();
        }
    };

    static const engine::ai::BtNodeType kDecoratorChoices[] = {
        engine::ai::BtNodeType::SeesTarget,   engine::ai::BtNodeType::TargetWithin,
        engine::ai::BtNodeType::Inverter,     engine::ai::BtNodeType::Succeeder,
        engine::ai::BtNodeType::Repeat,       engine::ai::BtNodeType::ScriptDecorator,
        engine::ai::BtNodeType::BbCheckBool,  engine::ai::BtNodeType::BbCheckFloat,
        engine::ai::BtNodeType::BbFloatBelow, engine::ai::BtNodeType::Cooldown,
        engine::ai::BtNodeType::TimeLimit,    engine::ai::BtNodeType::RandomChance,
        engine::ai::BtNodeType::HealthBelow,  engine::ai::BtNodeType::TargetDead};
    static const engine::ai::BtNodeType kServiceChoices[] = {
        engine::ai::BtNodeType::Repath, engine::ai::BtNodeType::ScriptService};

    attachmentSection("Decorators (gate / modify this node):", node.decorators,
                      kDecoratorChoices, 14, "+ Decorator", "add_dec", 2000);
    attachmentSection("Services (tick while active):", node.services,
                      kServiceChoices, 2, "+ Service", "add_svc", 3000);

    ImGui::EndChild();
}
