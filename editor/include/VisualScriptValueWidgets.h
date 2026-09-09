#pragma once

// =============================================================================
// Visual Scripting — Milestone 1 : one shared, type-aware value editor.
//
// A SINGLE implementation reused everywhere a VisualValue is authored:
//   * inline pin-default editors on the canvas                (graph panel)
//   * the selected-node inspector on the side panel           (graph panel)
//   * graph variable default-value editors                    (graph panel)
//   * per-object exposed-variable overrides in the Inspector  (EditorDockspace)
//
// It covers EVERY engine::vs::ValueType — including the ones the old ad-hoc
// switches skipped (Entity, Asset, Quaternion, Color, ScriptHandle) — with:
//   * a scene-object picker for Entity/ScriptHandle (None / Self / objects)
//   * a Content-Browser picker for Asset (no handle typing)
//   * reflected enum names for Int-backed enums (optional)
//   * optional numeric limits / drag speed / unit suffix / tooltip
//   * a reset-to-default affordance and stable, collision-free ImGui ids
//
// EDITOR-ONLY: depends on ImGui and lives under editor/. It is header-only, so
// it adds no translation unit / CMake reconfigure. Runtime code never sees it.
// =============================================================================

#include <engine/assets/AssetIdentity.h>
#include <engine/ecs/Entity.h>
#include <engine/visualscript/VisualScriptTypes.h>
#include <engine/visualscript/VisualScriptAsset.h>   // struct/enum type defs (Milestone 4)

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vswidgets {

// A scene object the Entity picker can offer (name + stable entity id).
struct SceneObjectRef {
    engine::ecs::Entity entity = engine::ecs::kNull;
    std::string         name;
};

// A Content-Browser asset the Asset picker can offer (resolved handle + label).
struct AssetChoice {
    engine::AssetHandle handle;
    std::string         label;
};

// Per-frame context supplying the project-wide data the pickers need. Every list
// is OPTIONAL: a null list degrades to a manual entry so the widget still works
// even when a caller has not wired the editor data yet.
struct ValueWidgetContext {
    const std::vector<SceneObjectRef>* sceneObjects = nullptr;   // Entity / ScriptHandle
    engine::ecs::Entity                selfEntity   = engine::ecs::kNull; // "Self"
    const std::vector<AssetChoice>*    assets       = nullptr;   // Asset picker
    // Optional authoring hints.
    bool  hasFloatRange = false; float floatMin = 0.0f; float floatMax = 0.0f;
    float floatSpeed = 0.05f;
    bool  hasIntRange = false;   int   intMin = 0;      int   intMax = 0;
    const char* tooltip = nullptr;                       // shown on hover
    const char* unit    = nullptr;                       // e.g. "m", "s", "deg"
    const std::vector<std::string>* enumNames = nullptr; // reflected enum, Int-backed
    // Struct/enum type definitions for container editors (Milestone 4).
    const std::vector<engine::vs::VisualStructType>* structs = nullptr;
    const std::vector<engine::vs::VisualEnumType>*   enums   = nullptr;
};

// ---- small helpers ---------------------------------------------------------

inline std::string EntityLabel(engine::ecs::Entity e, const ValueWidgetContext& ctx) {
    if (e == engine::ecs::kNull) return "None";
    if (ctx.selfEntity != engine::ecs::kNull && e == ctx.selfEntity) return "Self";
    if (ctx.sceneObjects) {
        for (const SceneObjectRef& o : *ctx.sceneObjects)
            if (o.entity == e)
                return o.name.empty() ? ("Entity " + std::to_string(static_cast<unsigned>(e))) : o.name;
    }
    return "Entity " + std::to_string(static_cast<unsigned>(e));
}

inline void MaybeTooltip(const ValueWidgetContext& ctx) {
    if (ctx.tooltip && *ctx.tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", ctx.tooltip);
}

// ---- pickers ---------------------------------------------------------------

inline bool DrawEntityPicker(const char* label, engine::vs::VisualValue& value,
                             const ValueWidgetContext& ctx) {
    using namespace engine::vs;
    bool changed = false;
    const engine::ecs::Entity current = value.AsEntity();
    if (ImGui::BeginCombo(label, EntityLabel(current, ctx).c_str())) {
        if (ImGui::Selectable("None", current == engine::ecs::kNull)) {
            value = VisualValue::Ent(engine::ecs::kNull); changed = true;
        }
        if (ctx.selfEntity != engine::ecs::kNull &&
            ImGui::Selectable("Self", current == ctx.selfEntity)) {
            value = VisualValue::Ent(ctx.selfEntity); changed = true;
        }
        if (ctx.sceneObjects) {
            for (const SceneObjectRef& o : *ctx.sceneObjects) {
                if (o.entity == engine::ecs::kNull) continue;
                ImGui::PushID(static_cast<int>(o.entity));
                const std::string nm = o.name.empty()
                    ? ("Entity " + std::to_string(static_cast<unsigned>(o.entity))) : o.name;
                if (ImGui::Selectable(nm.c_str(), o.entity == current)) {
                    value = VisualValue::Ent(o.entity); changed = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

inline bool DrawAssetPicker(const char* label, engine::vs::VisualValue& value,
                            const ValueWidgetContext& ctx) {
    using namespace engine::vs;
    bool changed = false;
    const engine::AssetHandle current = value.AsAsset();
    std::string preview = "None";
    if (current.Valid()) {
        preview = current.ToString();
        if (ctx.assets)
            for (const AssetChoice& a : *ctx.assets)
                if (a.handle == current) { preview = a.label; break; }
    }
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("None", !current.Valid())) {
            value = VisualValue::Asset(engine::AssetHandle{}); changed = true;
        }
        if (ctx.assets) {
            for (std::size_t i = 0; i < ctx.assets->size(); ++i) {
                const AssetChoice& a = (*ctx.assets)[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(a.label.c_str(), a.handle == current)) {
                    value = VisualValue::Asset(a.handle); changed = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A correctly-typed default value, resolving struct fields recursively (Milestone 4).
inline engine::vs::VisualValue MakeTypedDefault(engine::vs::ValueType t, std::uint32_t typeId,
                                                const ValueWidgetContext& ctx) {
    using namespace engine::vs;
    if (t == ValueType::Struct) {
        VisualValue s = VisualValue::MakeStruct(typeId);
        if (ctx.structs)
            for (const VisualStructType& sd : *ctx.structs)
                if (sd.id == typeId) {
                    for (const VisualStructField& f : sd.fields)
                        s.Elements().push_back(MakeTypedDefault(f.type, f.typeId, ctx));
                    break;
                }
        return s;
    }
    if (t == ValueType::Enum)  return VisualValue::MakeEnum(typeId, 0);
    if (t == ValueType::Array) return VisualValue::MakeArray(ValueType::Float);
    if (t == ValueType::Map)   return VisualValue::MakeMap(ValueType::String, ValueType::Float);
    return VisualValue::MakeDefault(t);
}

// ---- the one shared editor -------------------------------------------------
// Returns true when `value` was edited. `idLabel` must be unique in the current
// ImGui id stack; callers still push a per-(node,pin)/per-variable id around it.
inline bool DrawVisualValueEditor(const char* idLabel, engine::vs::ValueType type,
                                  engine::vs::VisualValue& value,
                                  const ValueWidgetContext& ctx = {}) {
    using namespace engine::vs;
    bool changed = false;

    switch (type) {
        case ValueType::Bool: {
            bool b = value.AsBool();
            if (ImGui::Checkbox(idLabel, &b)) { value = VisualValue::Bool(b); changed = true; }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Int: {
            int v = value.AsInt();
            if (ctx.enumNames && !ctx.enumNames->empty()) {
                const int count = static_cast<int>(ctx.enumNames->size());
                const int clamped = (v >= 0 && v < count) ? v : 0;
                const char* preview = (*ctx.enumNames)[clamped].c_str();
                if (ImGui::BeginCombo(idLabel, preview)) {
                    for (int i = 0; i < count; ++i) {
                        if (ImGui::Selectable((*ctx.enumNames)[i].c_str(), i == v)) {
                            value = VisualValue::Int(i); changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (ctx.hasIntRange) {
                if (ImGui::SliderInt(idLabel, &v, ctx.intMin, ctx.intMax)) {
                    value = VisualValue::Int(v); changed = true;
                }
            } else {
                if (ImGui::DragInt(idLabel, &v)) { value = VisualValue::Int(v); changed = true; }
            }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Float: {
            float v = value.AsFloat();
            const char* fmt = (ctx.unit && *ctx.unit) ? nullptr : "%.3f";
            std::array<char, 32> fmtBuf{};
            if (!fmt) { std::snprintf(fmtBuf.data(), fmtBuf.size(), "%%.3f %s", ctx.unit); fmt = fmtBuf.data(); }
            if (ctx.hasFloatRange) {
                if (ImGui::SliderFloat(idLabel, &v, ctx.floatMin, ctx.floatMax, fmt)) {
                    value = VisualValue::Float(v); changed = true;
                }
            } else {
                if (ImGui::DragFloat(idLabel, &v, ctx.floatSpeed, 0.0f, 0.0f, fmt)) {
                    value = VisualValue::Float(v); changed = true;
                }
            }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::String: {
            std::array<char, 256> buf{};
            std::snprintf(buf.data(), buf.size(), "%s", value.AsString().c_str());
            if (ImGui::InputText(idLabel, buf.data(), buf.size())) {
                value = VisualValue::Str(buf.data()); changed = true;
            }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Vector2: {
            glm::vec2 v = value.AsVec2();
            if (ImGui::DragFloat2(idLabel, &v.x, ctx.floatSpeed)) { value = VisualValue::Vec2(v); changed = true; }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Vector3: {
            glm::vec3 v = value.AsVec3();
            if (ImGui::DragFloat3(idLabel, &v.x, ctx.floatSpeed)) { value = VisualValue::Vec3(v); changed = true; }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Color: {
            glm::vec4 v = value.AsColor();
            if (ImGui::ColorEdit4(idLabel, &v.x)) { value = VisualValue::Col(v); changed = true; }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Quaternion: {
            // Author as intuitive Euler degrees; store as a quaternion.
            glm::vec3 deg = glm::degrees(glm::eulerAngles(value.AsQuat()));
            if (ImGui::DragFloat3(idLabel, &deg.x, 0.5f)) {
                value = VisualValue::Quat(glm::quat(glm::radians(deg))); changed = true;
            }
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Entity:
        case ValueType::ScriptHandle: {
            changed = DrawEntityPicker(idLabel, value, ctx);
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Asset: {
            changed = DrawAssetPicker(idLabel, value, ctx);
            MaybeTooltip(ctx);
            break;
        }
        case ValueType::Enum: {
            int v = value.AsInt();
            const VisualEnumType* def = nullptr;
            if (ctx.enums) for (const VisualEnumType& e : *ctx.enums) if (e.id == value.typeId) { def = &e; break; }
            if (def && !def->entries.empty()) {
                const int count = static_cast<int>(def->entries.size());
                const int clamped = (v >= 0 && v < count) ? v : 0;
                if (ImGui::BeginCombo(idLabel, def->entries[clamped].c_str())) {
                    for (int i = 0; i < count; ++i)
                        if (ImGui::Selectable(def->entries[i].c_str(), i == v)) {
                            value = VisualValue::MakeEnum(value.typeId, i); changed = true;
                        }
                    ImGui::EndCombo();
                }
            } else if (ImGui::DragInt(idLabel, &v)) {
                value = VisualValue::MakeEnum(value.typeId, v); changed = true;
            }
            break;
        }
        case ValueType::Array: {
            ImGui::PushID(idLabel);
            std::vector<VisualValue>& elems = value.Elements();
            if (ImGui::TreeNodeEx(idLabel, ImGuiTreeNodeFlags_DefaultOpen, "%s [%d]",
                                  idLabel, static_cast<int>(elems.size()))) {
                int removeIdx = -1;
                for (std::size_t i = 0; i < elems.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    // ensure the element carries the array's element struct/enum id
                    elems[i].typeId = value.typeId;
                    const std::string el = "##e" + std::to_string(i);
                    if (DrawVisualValueEditor(el.c_str(), value.elementType, elems[i], ctx)) changed = true;
                    ImGui::SameLine(); if (ImGui::SmallButton("x")) removeIdx = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (removeIdx >= 0) { elems.erase(elems.begin() + removeIdx); changed = true; }
                if (ImGui::SmallButton("+ Add")) {
                    elems.push_back(MakeTypedDefault(value.elementType, value.typeId, ctx));
                    changed = true;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        case ValueType::Struct: {
            ImGui::PushID(idLabel);
            const VisualStructType* def = nullptr;
            if (ctx.structs) for (const VisualStructType& s : *ctx.structs) if (s.id == value.typeId) { def = &s; break; }
            if (def) {
                std::vector<VisualValue>& fields = value.Elements();
                if (fields.size() != def->fields.size()) fields.resize(def->fields.size());
                for (std::size_t i = 0; i < def->fields.size(); ++i) {
                    const VisualStructField& fd = def->fields[i];
                    if (fields[i].type != fd.type) fields[i] = MakeTypedDefault(fd.type, fd.typeId, ctx);
                    fields[i].typeId = fd.typeId; fields[i].elementType = fd.elementType;
                    ImGui::PushID(static_cast<int>(i));
                    const std::string fl = (fd.name.empty() ? std::string("field") : fd.name)
                                         + "##sf" + std::to_string(i);
                    if (DrawVisualValueEditor(fl.c_str(), fd.type, fields[i], ctx)) changed = true;
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("(unknown struct type)");
            }
            ImGui::PopID();
            break;
        }
        case ValueType::Map: {
            ImGui::PushID(idLabel);
            std::vector<VisualValue>& keys = value.MapKeys();
            std::vector<VisualValue>& vals = value.MapValues();
            if (vals.size() < keys.size()) vals.resize(keys.size());
            if (ImGui::TreeNodeEx(idLabel, ImGuiTreeNodeFlags_DefaultOpen, "%s {%d}",
                                  idLabel, static_cast<int>(keys.size()))) {
                int removeIdx = -1;
                for (std::size_t i = 0; i < keys.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::SetNextItemWidth(120.0f);
                    if (DrawVisualValueEditor("##k", value.keyType, keys[i], ctx)) changed = true;
                    ImGui::SameLine(); ImGui::TextUnformatted("=>"); ImGui::SameLine();
                    ImGui::SetNextItemWidth(140.0f);
                    if (DrawVisualValueEditor("##v", value.elementType, vals[i], ctx)) changed = true;
                    ImGui::SameLine(); if (ImGui::SmallButton("x")) removeIdx = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (removeIdx >= 0) { keys.erase(keys.begin() + removeIdx); vals.erase(vals.begin() + removeIdx); changed = true; }
                if (ImGui::SmallButton("+ Pair")) {
                    keys.push_back(MakeTypedDefault(value.keyType, 0, ctx));
                    vals.push_back(MakeTypedDefault(value.elementType, value.typeId, ctx));
                    changed = true;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        case ValueType::Exec:
        default:
            ImGui::TextDisabled("%s", ValueTypeName(type));
            break;
    }
    return changed;
}

// Convenience: draw the editor with a trailing "reset to type default" button.
// Returns true if the value changed (edit OR reset).
inline bool DrawVisualValueEditorWithReset(const char* idLabel, engine::vs::ValueType type,
                                           engine::vs::VisualValue& value,
                                           const ValueWidgetContext& ctx = {}) {
    bool changed = DrawVisualValueEditor(idLabel, type, value, ctx);
    if (type != engine::vs::ValueType::Exec) {
        ImGui::SameLine();
        ImGui::PushID(idLabel);
        if (ImGui::SmallButton("x")) {
            value = engine::vs::VisualValue::MakeDefault(type);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to default");
        ImGui::PopID();
    }
    return changed;
}

} // namespace vswidgets
