#include "LevelLayersPanel.h"

#include "EditorScene.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct LayerInfo {
    std::string name;
    std::vector<int> objects;
    bool allVisible = true;
    bool allLocked = true;
};

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

bool ContainsInsensitive(const std::string& text, const char* filter) {
    if (!filter || !*filter) return true;
    std::string a = text;
    std::string b = filter;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(b) != std::string::npos;
}

} // namespace

void LevelLayersPanel::Draw(EditorScene& scene, bool* open) {
    if (!ImGui::Begin("Level Layers & Visibility", open)) {
        ImGui::End();
        return;
    }

    std::map<std::string, LayerInfo> layerMap;
    for (int i = 0; i < static_cast<int>(scene.Objects().size()); ++i) {
        const EditorScene::Object& object = scene.Objects()[static_cast<std::size_t>(i)];
        const std::string layerName = object.editorLayer.empty() ? "Default" : object.editorLayer;
        LayerInfo& layer = layerMap[layerName];
        layer.name = layerName;
        layer.objects.push_back(i);
        layer.allVisible = layer.allVisible && object.visible;
        layer.allLocked = layer.allLocked && object.locked;
    }
    if (layerMap.empty()) layerMap["Default"].name = "Default";
    if (layerMap.find(m_activeLayer) == layerMap.end()) m_activeLayer = layerMap.begin()->first;

    ImGui::TextDisabled("Organize scene objects without changing the runtime hierarchy.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##LayerFilter", "Filter layers and objects...", m_filter, sizeof(m_filter));

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 112.0f);
    ImGui::InputTextWithHint("##NewLayer", "New layer name", m_newLayer, sizeof(m_newLayer));
    ImGui::SameLine();
    if (ImGui::Button("Create + Assign", ImVec2(108.0f, 0.0f))) {
        const std::string name = Trim(m_newLayer);
        if (!name.empty() && !scene.SelectedIndices().empty()) {
            scene.AssignObjectsToLayer(scene.SelectedIndices(), name);
            m_activeLayer = name;
            m_newLayer[0] = '\0';
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create the layer by assigning the current selection to it.");

    if (ImGui::Button("Show All")) scene.ShowAllLayers();
    ImGui::SameLine();
    if (ImGui::Button("Unlock All")) {
        for (const auto& [name, layer] : layerMap) scene.SetLayerLocked(name, false);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show objects", &m_showObjects);

    ImGui::Text("Active: %s", m_activeLayer.c_str());
    if (ImGui::Button("Assign Selection"))
        scene.AssignObjectsToLayer(scene.SelectedIndices(), m_activeLayer);
    ImGui::SameLine();
    if (ImGui::Button("Move Layer to Default") && m_activeLayer != "Default") {
        const auto found = layerMap.find(m_activeLayer);
        if (found != layerMap.end()) scene.AssignObjectsToLayer(found->second.objects, "Default");
        m_activeLayer = "Default";
        m_renameInitialized = false;
    }
    if (!m_renameInitialized) {
        std::strncpy(m_renameLayer, m_activeLayer.c_str(), sizeof(m_renameLayer) - 1);
        m_renameLayer[sizeof(m_renameLayer) - 1] = '\0';
        m_renameInitialized = true;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 72.0f);
    ImGui::InputText("##RenameLayer", m_renameLayer, sizeof(m_renameLayer));
    ImGui::SameLine();
    if (ImGui::Button("Rename", ImVec2(68.0f, 0.0f))) {
        const std::string renamed = Trim(m_renameLayer);
        if (!renamed.empty() && scene.RenameLayer(m_activeLayer, renamed)) {
            m_activeLayer = renamed;
            m_renameInitialized = false;
        }
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##LayerList", ImVec2(0.0f, 0.0f), true)) {
        for (const auto& [name, layer] : layerMap) {
            bool objectMatch = false;
            for (int index : layer.objects)
                objectMatch = objectMatch || ContainsInsensitive(scene.Objects()[static_cast<std::size_t>(index)].name, m_filter);
            if (!ContainsInsensitive(name, m_filter) && !objectMatch) continue;

            ImGui::PushID(name.c_str());
            const bool active = name == m_activeLayer;
            bool visible = layer.allVisible;
            if (ImGui::Checkbox("V", &visible)) scene.SetLayerVisible(name, visible);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle layer visibility");
            ImGui::SameLine();
            bool locked = layer.allLocked;
            if (ImGui::Checkbox("L", &locked)) scene.SetLayerLocked(name, locked);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle layer editing lock");
            ImGui::SameLine();
            const std::string label = name + "  (" + std::to_string(layer.objects.size()) + ")";
            const float labelWidth = std::max(90.0f, ImGui::GetContentRegionAvail().x - 124.0f);
            if (ImGui::Selectable(label.c_str(), active, ImGuiSelectableFlags_None,
                                  ImVec2(labelWidth, 0.0f))) {
                m_activeLayer = name;
                std::strncpy(m_renameLayer, name.c_str(), sizeof(m_renameLayer) - 1);
                m_renameLayer[sizeof(m_renameLayer) - 1] = '\0';
                m_renameInitialized = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Select")) scene.SelectIndices(layer.objects);
            ImGui::SameLine();
            if (ImGui::SmallButton("Isolate")) {
                for (const auto& [otherName, other] : layerMap)
                    scene.SetLayerVisible(otherName, otherName == name);
            }

            if (m_showObjects && (active || objectMatch)) {
                for (int index : layer.objects) {
                    const EditorScene::Object& object = scene.Objects()[static_cast<std::size_t>(index)];
                    if (!ContainsInsensitive(object.name, m_filter)) continue;
                    ImGui::Indent(30.0f);
                    const bool selected = std::find(scene.SelectedIndices().begin(), scene.SelectedIndices().end(), index)
                        != scene.SelectedIndices().end();
                    if (ImGui::Selectable(object.name.c_str(), selected)) scene.SelectIndex(index);
                    ImGui::Unindent(30.0f);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
