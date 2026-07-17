#include "HudEditorPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>

using engine::HudAnchor;
using engine::HudBinding;
using engine::HudButtonAction;
using engine::HudDocument;
using engine::HudWidget;
using engine::HudWidgetType;

namespace {

ImU32 ToU32(const glm::vec4& c) {
    return IM_COL32(static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f));
}

// Generic combo over an int-backed enum using a name callback.
template <typename EnumT>
bool EnumCombo(const char* label, EnumT& value, int count, const char* (*name)(EnumT)) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name(value))) {
        for (int i = 0; i < count; ++i) {
            const EnumT candidate = static_cast<EnumT>(i);
            const bool selected = static_cast<int>(value) == i;
            if (ImGui::Selectable(name(candidate), selected)) {
                value = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

void HudEditorPanel::SetPath(const std::string& path) {
    std::snprintf(m_pathBuf, sizeof(m_pathBuf), "%s", path.c_str());
}

HudEditorPanel::Result HudEditorPanel::Draw(HudDocument& doc, bool* open,
                                            const std::vector<std::string>& imageChoices,
                                            const std::function<unsigned int(const std::string&)>& texLookup) {
    Result result;

    ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HUD Editor", open)) {
        ImGui::End();
        return result;
    }

    // ---- Toolbar: file + add widgets -------------------------------------
    ImGui::InputText("##hudpath", m_pathBuf, sizeof(m_pathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Save")) { result.saveRequested = true; result.path = m_pathBuf; }
    ImGui::SameLine();
    if (ImGui::Button("Load")) { result.loadRequested = true; result.path = m_pathBuf; }
    ImGui::SameLine();
    if (ImGui::Button("New"))  { result.newRequested = true; }
    ImGui::SameLine();
    if (ImGui::Button("Use in Scene")) { result.setAsSceneHud = true; result.path = m_pathBuf; }

    ImGui::TextUnformatted("Add:");
    ImGui::SameLine();
    const struct { const char* label; HudWidgetType type; } kAdd[] = {
        {"Panel", HudWidgetType::Panel}, {"Text", HudWidgetType::Text},
        {"Bar", HudWidgetType::Bar}, {"Button", HudWidgetType::Button},
        {"Image", HudWidgetType::Image},
    };
    for (const auto& a : kAdd) {
        if (ImGui::Button(a.label)) {
            doc.Add(a.type);
            m_selected = static_cast<int>(doc.widgets.size()) - 1;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();

    if (m_selected >= static_cast<int>(doc.widgets.size())) m_selected = -1;

    // ---- Left: hierarchy --------------------------------------------------
    const float leftWidth = 200.0f;
    ImGui::BeginChild("hud_hierarchy", ImVec2(leftWidth, 0.0f), true);
    ImGui::TextUnformatted("Widgets");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(doc.widgets.size()); ++i) {
        const HudWidget& w = doc.widgets[i];
        char label[128];
        std::snprintf(label, sizeof(label), "%s  [%s]##%d",
                      w.name.c_str(), engine::HudWidgetTypeName(w.type), w.id);
        if (ImGui::Selectable(label, m_selected == i)) {
            m_selected = i;
        }
    }
    if (doc.widgets.empty()) {
        ImGui::TextDisabled("(empty - use Add)");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- Middle: WYSIWYG canvas ------------------------------------------
    const float rightWidth = 320.0f;
    const float canvasWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x - rightWidth - 8.0f);
    ImGui::BeginChild("hud_canvas", ImVec2(canvasWidth, 0.0f), true);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        // Keep the canvas at the document's aspect ratio.
        const float aspect = doc.designSize.x / std::max(doc.designSize.y, 1.0f);
        float cw = avail.x;
        float ch = cw / aspect;
        if (ch > avail.y) { ch = avail.y; cw = ch * aspect; }
        cw = std::max(cw, 32.0f);
        ch = std::max(ch, 32.0f);

        dl->AddRectFilled(origin, ImVec2(origin.x + cw, origin.y + ch), IM_COL32(24, 26, 32, 255));
        dl->AddRect(origin, ImVec2(origin.x + cw, origin.y + ch), IM_COL32(70, 74, 84, 255));

        const float s = ch / std::max(doc.designSize.y, 1.0f);

        // Interaction layer (click to select, drag to move).
        ImGui::InvisibleButton("hud_canvas_hit", ImVec2(cw, ch));
        const bool canvasActive = ImGui::IsItemActive();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemClicked()) {
            m_selected = -1;
            for (int i = static_cast<int>(doc.widgets.size()) - 1; i >= 0; --i) {
                float x, y, ww, hh;
                engine::HudWidgetRect(doc.widgets[i], doc, static_cast<int>(cw), static_cast<int>(ch), x, y, ww, hh);
                if (mouse.x >= origin.x + x && mouse.x <= origin.x + x + ww &&
                    mouse.y >= origin.y + y && mouse.y <= origin.y + y + hh) {
                    m_selected = i;
                    break;
                }
            }
        }
        if (canvasActive && m_selected >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && s > 0.0001f) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            doc.widgets[m_selected].offset.x += d.x / s;
            doc.widgets[m_selected].offset.y += d.y / s;
        }

        // Draw widgets.
        for (int i = 0; i < static_cast<int>(doc.widgets.size()); ++i) {
            const HudWidget& w = doc.widgets[i];
            if (!w.visible) continue;
            float x, y, ww, hh;
            engine::HudWidgetRect(w, doc, static_cast<int>(cw), static_cast<int>(ch), x, y, ww, hh);
            const ImVec2 p0(origin.x + x, origin.y + y);
            const ImVec2 p1(origin.x + x + ww, origin.y + y + hh);

            switch (w.type) {
                case HudWidgetType::Bar:
                    dl->AddRectFilled(p0, p1, ToU32(w.bgColor));
                    dl->AddRectFilled(p0, ImVec2(p0.x + ww * 0.65f, p1.y), ToU32(w.fillColor));
                    break;
                case HudWidgetType::Text:
                    dl->AddText(p0, ToU32(w.color), w.text.c_str());
                    break;
                case HudWidgetType::Image: {
                    unsigned int tex = 0;
                    if (!w.imageAsset.empty() && texLookup) tex = texLookup(w.imageAsset);
                    if (tex != 0) {
                        // GL textures are bottom-up; flip V so the preview is upright.
                        dl->AddImage((ImTextureID)(intptr_t)tex,
                                     p0, p1, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), ToU32(w.color));
                    } else {
                        dl->AddRectFilled(p0, p1, IM_COL32(60, 62, 72, 200));
                        dl->AddRect(p0, p1, IM_COL32(120, 124, 138, 255));
                        dl->AddText(ImVec2(p0.x + 4.0f, p0.y + 4.0f), IM_COL32(180, 182, 190, 255), "image");
                    }
                    break;
                }
                default: // Panel / Button
                    dl->AddRectFilled(p0, p1, ToU32(w.color));
                    if (w.type == HudWidgetType::Button && !w.text.empty()) {
                        const ImVec2 ts = ImGui::CalcTextSize(w.text.c_str());
                        dl->AddText(ImVec2(p0.x + (ww - ts.x) * 0.5f, p0.y + (hh - ts.y) * 0.5f),
                                    IM_COL32(245, 245, 245, 255), w.text.c_str());
                    }
                    break;
            }

            if (i == m_selected) {
                dl->AddRect(ImVec2(p0.x - 1.0f, p0.y - 1.0f), ImVec2(p1.x + 1.0f, p1.y + 1.0f),
                            IM_COL32(255, 190, 60, 255), 0.0f, 0, 2.0f);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- Right: inspector -------------------------------------------------
    ImGui::BeginChild("hud_inspector", ImVec2(0.0f, 0.0f), true);
    if (m_selected >= 0 && m_selected < static_cast<int>(doc.widgets.size())) {
        HudWidget& w = doc.widgets[m_selected];

        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", w.name.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) w.name = nameBuf;

        EnumCombo<HudWidgetType>("Type", w.type, 5, engine::HudWidgetTypeName);
        EnumCombo<HudAnchor>("Anchor", w.anchor, 9, engine::HudAnchorName);
        ImGui::Checkbox("Visible", &w.visible);

        if (w.type != HudWidgetType::Text) {
            ImGui::SeparatorText("Unlit Shader");
            char shaderBuf[512];
            std::snprintf(shaderBuf, sizeof(shaderBuf), "%s", w.shaderPath.c_str());
            if (ImGui::InputTextWithHint("Shader Graph", "Content/Shaders/UI.3dgshader",
                                         shaderBuf, sizeof(shaderBuf))) {
                w.shaderPath = shaderBuf;
                w.shaderParameters.clear();
                w.shaderParameterTypes.clear();
            }
            ImGui::TextDisabled("Use an Unlit-domain shader graph. Parameters are resolved at runtime.");
        }

        ImGui::Separator();
        ImGui::DragFloat2("Offset (px)", &w.offset.x, 1.0f);
        ImGui::DragFloat2("Size (px)", &w.size.x, 1.0f, 1.0f, 8192.0f);

        ImGui::Separator();
        if (w.type == HudWidgetType::Text || w.type == HudWidgetType::Button) {
            char textBuf[256];
            std::snprintf(textBuf, sizeof(textBuf), "%s", w.text.c_str());
            if (ImGui::InputText("Text", textBuf, sizeof(textBuf))) w.text = textBuf;
            ImGui::DragFloat("Text Scale", &w.textScale, 0.02f, 0.2f, 8.0f);
        }

        // Colours per type.
        if (w.type == HudWidgetType::Bar) {
            ImGui::ColorEdit4("Background", &w.bgColor.x);
            ImGui::ColorEdit4("Fill", &w.fillColor.x);
        } else {
            const char* colLabel = (w.type == HudWidgetType::Text) ? "Text Color"
                                 : (w.type == HudWidgetType::Button) ? "Button Color"
                                 : (w.type == HudWidgetType::Image) ? "Tint" : "Color";
            ImGui::ColorEdit4(colLabel, &w.color.x);
        }

        // Image picker: searchable list of content-folder images.
        if (w.type == HudWidgetType::Image) {
            ImGui::Separator();
            ImGui::TextUnformatted("Image");
            ImGui::Text("Current: %s", w.imageAsset.empty() ? "(none)" : w.imageAsset.c_str());

            if (!w.imageAsset.empty() && texLookup) {
                const unsigned int tex = texLookup(w.imageAsset);
                if (tex != 0) {
                    ImGui::Image((ImTextureID)(intptr_t)tex,
                                 ImVec2(96.0f, 96.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                }
            }

            ImGui::SetNextItemWidth(-70.0f);
            ImGui::InputTextWithHint("##imgfilter", "search images", m_imageFilter, sizeof(m_imageFilter));
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) result.refreshImagesRequested = true;

            std::string filter = m_imageFilter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (ImGui::BeginListBox("##hudimages", ImVec2(-FLT_MIN, 150.0f))) {
                if (ImGui::Selectable("(none)", w.imageAsset.empty())) w.imageAsset.clear();
                for (const std::string& img : imageChoices) {
                    if (!filter.empty()) {
                        std::string low = img;
                        std::transform(low.begin(), low.end(), low.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (low.find(filter) == std::string::npos) continue;
                    }
                    if (ImGui::Selectable(img.c_str(), img == w.imageAsset)) w.imageAsset = img;
                }
                ImGui::EndListBox();
            }
            if (imageChoices.empty()) {
                ImGui::TextDisabled("No images found under the content folder. Click Refresh.");
            }
        }

        // Data binding.
        if (w.type == HudWidgetType::Text || w.type == HudWidgetType::Bar) {
            ImGui::Separator();
            ImGui::TextUnformatted("Binding");
            EnumCombo<HudBinding>("Source", w.binding, 5, engine::HudBindingName);
            if (w.binding == HudBinding::NamedFloat || w.binding == HudBinding::NamedString) {
                char keyBuf[128];
                std::snprintf(keyBuf, sizeof(keyBuf), "%s", w.bindKey.c_str());
                if (ImGui::InputText("Key", keyBuf, sizeof(keyBuf))) w.bindKey = keyBuf;
            }
            if (w.type == HudWidgetType::Bar && w.binding == HudBinding::NamedFloat) {
                ImGui::DragFloat("Min", &w.minValue, 0.1f);
                ImGui::DragFloat("Max", &w.maxValue, 0.1f);
            }
            if (w.type == HudWidgetType::Text) {
                ImGui::TextDisabled("Tip: put {} in Text for the bound value.");
            }
        }

        // Button action.
        if (w.type == HudWidgetType::Button) {
            ImGui::Separator();
            ImGui::TextUnformatted("On Click");
            EnumCombo<HudButtonAction>("Action", w.action, 4, engine::HudButtonActionName);
            if (w.action == HudButtonAction::EmitEvent) {
                char keyBuf[128];
                std::snprintf(keyBuf, sizeof(keyBuf), "%s", w.bindKey.c_str());
                if (ImGui::InputText("Event Key", keyBuf, sizeof(keyBuf))) w.bindKey = keyBuf;
            }
            ImGui::TextDisabled("Buttons work when the cursor is free (ESC in play).");
        }

        ImGui::Separator();
        if (ImGui::Button("Duplicate")) {
            HudWidget copy = w;
            copy.id = doc.nextId++;
            copy.offset += glm::vec2(16.0f, 16.0f);
            doc.widgets.push_back(copy);
            m_selected = static_cast<int>(doc.widgets.size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            doc.Remove(m_selected);
            m_selected = -1;
        }
    } else {
        ImGui::TextDisabled("Select a widget to edit its properties.");
        ImGui::Separator();
        ImGui::DragFloat2("Design Size", &doc.designSize.x, 1.0f, 16.0f, 8192.0f);
        ImGui::TextDisabled("Authoring resolution. The HUD scales to fit the play window height.");
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}
