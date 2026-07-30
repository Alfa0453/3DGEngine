// EditorApp — the Script Debug panel. In Play mode it lists every scripted entity and
// its live field values, and lets you tweak them without recompiling: edits write the
// running script's field storage, which its next GetField* call reads.

#include "EditorApp.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <string>

namespace {

// Render one script slot's fields as live-editable widgets. Mutates slot.fields[i].value
// directly (the running script reads these on its next update).
void DrawScriptDebugSlot(engine::NativeScriptSlot& slot, int idSalt) {
    if (!slot.enabled) {
        ImGui::TextDisabled("(disabled)");
    }
    if (slot.fields.empty()) {
        ImGui::TextDisabled("(no fields)");
        return;
    }
    using FieldType = engine::ScriptField::Type;
    for (std::size_t i = 0; i < slot.fields.size(); ++i) {
        engine::ScriptField& field = slot.fields[i];
        ImGui::PushID(static_cast<int>(i) + idSalt * 1000);
        switch (field.type) {
        case FieldType::Bool: {
            bool v = field.value == "1" || field.value == "true" || field.value == "True";
            if (ImGui::Checkbox(field.name.c_str(), &v)) field.value = v ? "1" : "0";
            break;
        }
        case FieldType::Float: {
            float v = 0.0f;
            std::sscanf(field.value.c_str(), "%f", &v);
            bool edited = false;
            if (field.maxValue > field.minValue) {
                edited = ImGui::SliderFloat(field.name.c_str(), &v, field.minValue, field.maxValue);
            } else {
                edited = ImGui::DragFloat(field.name.c_str(), &v, 0.05f);
            }
            if (edited) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%g", v);
                field.value = buffer;
            }
            break;
        }
        case FieldType::Int: {
            int v = 0;
            std::sscanf(field.value.c_str(), "%d", &v);
            if (ImGui::DragInt(field.name.c_str(), &v)) field.value = std::to_string(v);
            break;
        }
        case FieldType::Vec3:
        case FieldType::Color: {
            float v[3] = {0.0f, 0.0f, 0.0f};
            std::sscanf(field.value.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
            const bool edited = field.type == FieldType::Color
                ? ImGui::ColorEdit3(field.name.c_str(), v)
                : ImGui::DragFloat3(field.name.c_str(), v, 0.05f);
            if (edited) {
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer), "%g %g %g", v[0], v[1], v[2]);
                field.value = buffer;
            }
            break;
        }
        default: {   // String / Entity / Asset — plain text
            std::array<char, 128> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", field.value.c_str());
            if (ImGui::InputText(field.name.c_str(), buffer.data(), buffer.size())) {
                field.value = buffer.data();
            }
            break;
        }
        }
        ImGui::PopID();
    }
}

}  // namespace

void EditorApp::DrawScriptDebugPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ScriptDebug)) return;

    bool open = true;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ScriptDebug), &open)) {
        ImGui::End();
        m_panels.SetOpen(EditorPanels::Panel::ScriptDebug, open);
        return;
    }

    if (m_mode != EditorMode::Play || !m_playRegistry) {
        ImGui::TextDisabled("Enter Play mode to inspect and tweak live script fields.");
        ImGui::End();
        m_panels.SetOpen(EditorPanels::Panel::ScriptDebug, open);
        return;
    }

    ImGui::TextDisabled("Live values - edits apply on the script's next update.");
    ImGui::Separator();
    ImGui::BeginChild("##scriptdebuglist");

    int shown = 0;
    for (const auto& entry : m_playEntityNames) {
        const engine::ecs::Entity entity = entry.first;
        if (!m_playRegistry->Valid(entity)) continue;
        engine::NativeScriptComponent* script =
            m_playRegistry->TryGet<engine::NativeScriptComponent>(entity);
        if (!script) continue;

        ++shown;
        ImGui::PushID(shown);
        std::string header = entry.second + "  ["
            + (script->className.empty() ? std::string("script") : script->className) + "]";
        if (!script->enabled) header += " (disabled)";
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawScriptDebugSlot(*script, 0);
            for (std::size_t i = 0; i < script->additional.size(); ++i) {
                ImGui::SeparatorText(script->additional[i].className.empty()
                    ? "additional script"
                    : script->additional[i].className.c_str());
                DrawScriptDebugSlot(script->additional[i], static_cast<int>(i) + 1);
            }
        }
        ImGui::PopID();
    }
    if (shown == 0) {
        ImGui::TextDisabled("No scripted objects in the running scene.");
    }

    ImGui::EndChild();
    ImGui::End();
    m_panels.SetOpen(EditorPanels::Panel::ScriptDebug, open);
}
