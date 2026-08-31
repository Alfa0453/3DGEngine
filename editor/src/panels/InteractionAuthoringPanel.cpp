#include "InteractionAuthoringPanel.h"
#include "EditorPanels.h"
#include <engine/gameplay/InteractionSystem.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace {
template <class Enum>
bool EnumCombo(const char* label, Enum& value, int count, const char* (*name)(Enum)) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name(value))) {
        for (int i = 0; i < count; ++i) {
            const Enum candidate = static_cast<Enum>(i);
            if (ImGui::Selectable(name(candidate), candidate == value)) { value = candidate; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool StringField(const char* label, std::string& value, std::size_t capacity = 256) {
    std::vector<char> buffer(capacity, '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    value = buffer.data(); return true;
}

std::vector<std::string> ParseTags(const std::string& text) {
    std::vector<std::string> tags;
    std::stringstream stream(text); std::string tag;
    while (std::getline(stream, tag, ',')) {
        const auto first = tag.find_first_not_of(" \t");
        const auto last = tag.find_last_not_of(" \t");
        if (first != std::string::npos) tags.push_back(tag.substr(first, last - first + 1));
    }
    return tags;
}

std::string JoinTags(const std::vector<std::string>& tags) {
    std::string result;
    for (const auto& tag : tags) { if (!result.empty()) result += ", "; result += tag; }
    return result;
}
}

void InteractionAuthoringPanel::New(const std::string& root) {
    m_asset = {};
    m_asset.header.id = engine::AssetHandle::Generate();
    m_asset.name = "NewInteraction";
    m_path = (std::filesystem::path(root) / "GameAssets" / "Interactions" /
              "NewInteraction.3dginteraction").string();
    m_previewAlpha = 0.0f; m_previewPlaying = false; m_previewOpening = true;
    m_status.clear(); m_dirty = true;
}

void InteractionAuthoringPanel::Preset(engine::InteractionMotionType type) {
    m_asset.motion = type;
    m_asset.loop = false; m_asset.autoClose = true; m_asset.openAngleDegrees = 90.0f;
    m_asset.localTranslation = {0.0f, 3.0f, 0.0f}; m_asset.pivotOffset = {-0.5f, 0.0f, 0.0f};
    if (type == engine::InteractionMotionType::SlidingDoor) m_asset.localTranslation = {2.0f, 0.0f, 0.0f};
    else if (type == engine::InteractionMotionType::Gate) m_asset.localTranslation = {0.0f, 2.5f, 0.0f};
    else if (type == engine::InteractionMotionType::Elevator) {
        m_asset.localTranslation = {0.0f, 5.0f, 0.0f}; m_asset.holdOpenTime = 2.0f;
    } else if (type == engine::InteractionMotionType::MovingPlatform) {
        m_asset.localTranslation = {5.0f, 0.0f, 0.0f}; m_asset.loop = true; m_asset.autoClose = false;
    }
    m_dirty = true;
}

bool InteractionAuthoringPanel::SaveForShutdown(const std::string& root, std::string* error) {
    if (m_path.empty()) New(root);
    if (!engine::SaveInteractionAsset(m_path, m_asset, error)) return false;
    m_dirty = false; return true;
}

bool InteractionAuthoringPanel::AssetCombo(const char* label, EditorAssets::Type type,
                                           std::string& path, engine::AssetHandle& id,
                                           EditorAssets& assets, const char* empty) {
    bool changed = false;
    const std::string preview = path.empty() ? empty : std::filesystem::path(path).filename().string();
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(empty, path.empty())) { path.clear(); id = {}; changed = true; }
        for (const auto& candidate : assets.ContentAssetPaths(type)) {
            ImGui::PushID(candidate.c_str());
            const std::string name = std::filesystem::path(candidate).filename().string();
            if (ImGui::Selectable(name.c_str(), candidate == path)) {
                path = candidate; id = assets.AssetIdForPath(candidate); changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void InteractionAuthoringPanel::DrawPreview(float deltaSeconds) {
    if (m_previewPlaying) {
        const float duration = m_previewOpening ? m_asset.openDuration : m_asset.closeDuration;
        m_previewAlpha += (m_previewOpening ? 1.0f : -1.0f) * std::max(deltaSeconds, 0.0f) /
                          std::max(duration, 0.01f);
        if (m_previewAlpha >= 1.0f || m_previewAlpha <= 0.0f) {
            m_previewAlpha = std::clamp(m_previewAlpha, 0.0f, 1.0f);
            if (m_asset.loop) m_previewOpening = !m_previewOpening;
            else m_previewPlaying = false;
        }
    }
    if (ImGui::Button(m_previewPlaying ? "Pause Preview" : "Play Preview")) m_previewPlaying = !m_previewPlaying;
    ImGui::SameLine(); if (ImGui::Button("Toggle Direction")) { m_previewOpening = !m_previewOpening; m_previewPlaying = true; }
    ImGui::SameLine(); if (ImGui::Button("Reset Preview")) { m_previewAlpha = 0.0f; m_previewOpening = true; m_previewPlaying = false; }
    ImGui::SliderFloat("Preview Position", &m_previewAlpha, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    const ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(230.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##InteractionMotionPreview", size);
    auto* draw = ImGui::GetWindowDrawList(); const ImVec2 o = ImGui::GetItemRectMin();
    draw->AddRectFilled(o, {o.x + size.x, o.y + size.y}, IM_COL32(18, 20, 24, 255));
    const ImVec2 center{o.x + size.x * 0.5f, o.y + size.y * 0.56f};
    const float t = engine::EvaluateInteractionEasing(m_asset.easing, m_previewAlpha);
    const ImU32 closedColor = IM_COL32(80, 105, 125, 150), liveColor = IM_COL32(75, 200, 235, 255);
    draw->AddLine({o.x + 20, center.y + 65}, {o.x + size.x - 20, center.y + 65}, IM_COL32(75, 80, 90, 255), 2.0f);
    if (m_asset.motion == engine::InteractionMotionType::HingedDoor) {
        const float length = std::min(150.0f, size.x * 0.28f);
        const ImVec2 pivot{center.x - length * 0.5f, center.y};
        draw->AddLine(pivot, {pivot.x + length, pivot.y}, closedColor, 9.0f);
        const float angle = glm::radians(m_asset.openAngleDegrees * t);
        draw->AddLine(pivot, {pivot.x + std::cos(angle) * length, pivot.y - std::sin(angle) * length}, liveColor, 9.0f);
        draw->AddCircleFilled(pivot, 6.0f, IM_COL32(245, 180, 70, 255));
    } else {
        const float travel = std::clamp(glm::length(m_asset.localTranslation) * 22.0f, 30.0f, 150.0f);
        const ImVec2 closed{center.x - travel * 0.5f, center.y};
        const ImVec2 open{closed.x + travel, center.y - m_asset.localTranslation.y * 12.0f};
        const ImVec2 live{closed.x + (open.x - closed.x) * t, closed.y + (open.y - closed.y) * t};
        draw->AddRectFilled({closed.x - 45, closed.y - 12}, {closed.x + 45, closed.y + 12}, closedColor);
        draw->AddLine(closed, open, IM_COL32(245, 180, 70, 210), 2.0f);
        draw->AddRectFilled({live.x - 45, live.y - 12}, {live.x + 45, live.y + 12}, liveColor);
    }
    const std::string caption = std::string(engine::InteractionMotionTypeName(m_asset.motion)) +
        " | " + engine::InteractionEasingName(m_asset.easing) + " | " + std::to_string(static_cast<int>(m_previewAlpha * 100.0f)) + "%";
    draw->AddText({o.x + 10, o.y + 10}, IM_COL32(220, 225, 235, 255), caption.c_str());
}

InteractionAuthoringPanel::Result InteractionAuthoringPanel::Draw(
    EditorAssets& assets, const std::string& root, float deltaSeconds, bool* open) {
    Result result;
    if (!m_asset.header.id.Valid()) New(root);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (engine::LoadInteractionAsset(m_pendingOpen, &m_asset, &error)) {
            m_path = m_pendingOpen; m_dirty = false; m_status = "Loaded " + m_path;
            m_previewAlpha = m_asset.startsOpen ? 1.0f : 0.0f;
        } else m_status = error;
        m_pendingOpen.clear();
    }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::InteractionAuthoring), open)) { ImGui::End(); return result; }
    if (ImGui::Button("New")) New(root);
    ImGui::SameLine(); if (ImGui::Button("Save")) {
        std::string error;
        if (engine::SaveInteractionAsset(m_path, m_asset, &error)) {
            m_dirty = false; result.saved = true; result.message = "Saved interaction asset: " + m_path;
        } else result.message = error;
    }
    ImGui::SameLine(); if (ImGui::Button("Apply to Selected")) result.applySelected = true;
    ImGui::SameLine(); ImGui::TextUnformatted(m_path.c_str());
    char name[128]{}; std::snprintf(name, sizeof(name), "%s", m_asset.name.c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) { m_asset.name = name; m_dirty = true; }
    ImGui::TextUnformatted("Presets:"); ImGui::SameLine();
    const engine::InteractionMotionType presets[] = {engine::InteractionMotionType::HingedDoor, engine::InteractionMotionType::SlidingDoor, engine::InteractionMotionType::Gate, engine::InteractionMotionType::Elevator, engine::InteractionMotionType::MovingPlatform};
    for (int i = 0; i < 5; ++i) { ImGui::PushID(i); if (ImGui::Button(engine::InteractionMotionTypeName(presets[i]))) Preset(presets[i]); ImGui::PopID(); if (i < 4) ImGui::SameLine(); }
    ImGui::SeparatorText("Motion");
    m_dirty |= EnumCombo("Motion Type", m_asset.motion, 5, engine::InteractionMotionTypeName);
    m_dirty |= EnumCombo("Easing", m_asset.easing, 3, engine::InteractionEasingName);
    if (m_asset.motion == engine::InteractionMotionType::HingedDoor) {
        m_dirty |= ImGui::DragFloat3("Hinge Axis", &m_asset.hingeAxis.x, 0.01f, -1.0f, 1.0f);
        m_dirty |= ImGui::DragFloat3("Local Pivot", &m_asset.pivotOffset.x, 0.02f, -10000.0f, 10000.0f, "%.2f m");
        m_dirty |= ImGui::SliderFloat("Open Angle", &m_asset.openAngleDegrees, -360.0f, 360.0f, "%.1f deg");
    } else m_dirty |= ImGui::DragFloat3("Local Travel", &m_asset.localTranslation.x, 0.05f, -10000.0f, 10000.0f, "%.2f m");
    m_dirty |= ImGui::DragFloat("Open Time", &m_asset.openDuration, 0.02f, 0.01f, 3600.0f, "%.2f s");
    m_dirty |= ImGui::DragFloat("Close Time", &m_asset.closeDuration, 0.02f, 0.01f, 3600.0f, "%.2f s");
    m_dirty |= ImGui::DragFloat("Open Hold", &m_asset.holdOpenTime, 0.05f, 0.0f, 3600.0f, "%.2f s");
    m_dirty |= ImGui::Checkbox("Starts Open", &m_asset.startsOpen); ImGui::SameLine();
    m_dirty |= ImGui::Checkbox("Auto Close", &m_asset.autoClose); ImGui::SameLine();
    m_dirty |= ImGui::Checkbox("Loop / Ping Pong", &m_asset.loop); ImGui::SameLine();
    m_dirty |= ImGui::Checkbox("One Shot", &m_asset.oneShot);
    ImGui::SeparatorText("Prompt and Input");
    m_dirty |= ImGui::DragFloat("Interaction Range", &m_asset.interactionRange, 0.05f, 0.01f, 10000.0f, "%.2f m");
    m_dirty |= EnumCombo("Input Mode", m_asset.inputMode, 2, engine::InteractionInputModeName);
    if (m_asset.inputMode == engine::InteractionInputMode::Hold)
        m_dirty |= ImGui::DragFloat("Hold Duration", &m_asset.holdDuration, 0.02f, 0.05f, 60.0f, "%.2f s");
    m_dirty |= StringField("Input Action", m_asset.inputAction);
    m_dirty |= StringField("Available Prompt", m_asset.prompt);
    m_dirty |= StringField("Unavailable Prompt", m_asset.unavailablePrompt);

    ImGui::SeparatorText("Availability Conditions");
    m_dirty |= ImGui::Checkbox("Locked", &m_asset.locked);
    m_dirty |= StringField("Required Access Tag", m_asset.accessTag);
    std::string conditionTags = JoinTags(m_asset.requiredConditionTags);
    if (StringField("Required Condition Tags", conditionTags, 512)) {
        m_asset.requiredConditionTags = ParseTags(conditionTags); m_dirty = true;
    }
    ImGui::TextDisabled("Comma-separated gameplay tags; every tag must be present.");
    m_dirty |= ImGui::Checkbox("Require Facing", &m_asset.requireFacing);
    if (m_asset.requireFacing)
        m_dirty |= ImGui::SliderFloat("Facing Half-Angle", &m_asset.facingAngleDegrees, 1.0f, 180.0f, "%.0f deg");
    m_dirty |= ImGui::Checkbox("Require Line of Sight", &m_asset.requireLineOfSight);

    ImGui::SeparatorText("Animation Requirements");
    m_dirty |= AssetCombo("Interactor Action Clip", EditorAssets::Type::AnimationClip,
                          m_asset.interactorAnimationPath, m_asset.interactorAnimationId, assets, "None");
    m_dirty |= ImGui::Checkbox("Lock Interactor Movement", &m_asset.lockInteractorMovement);
    m_dirty |= ImGui::Checkbox("Wait for Animation Event", &m_asset.waitForAnimationEvent);
    if (m_asset.waitForAnimationEvent)
        m_dirty |= StringField("Commit Event", m_asset.animationCommitEvent);

    ImGui::SeparatorText("Interaction Events");
    m_dirty |= StringField("Started Event", m_asset.startedEvent);
    m_dirty |= StringField("Completed Event", m_asset.completedEvent);
    m_dirty |= StringField("Failed Event", m_asset.failedEvent);

    ImGui::SeparatorText("Motion Sounds and Animations");
    m_dirty |= AssetCombo("Open Sound", EditorAssets::Type::Audio, m_asset.openAudioPath, m_asset.openAudioId, assets, "None");
    m_dirty |= AssetCombo("Close Sound", EditorAssets::Type::Audio, m_asset.closeAudioPath, m_asset.closeAudioId, assets, "None");
    m_dirty |= AssetCombo("Locked Sound", EditorAssets::Type::Audio, m_asset.lockedAudioPath, m_asset.lockedAudioId, assets, "None");
    m_dirty |= AssetCombo("Open Action Clip", EditorAssets::Type::AnimationClip, m_asset.openAnimationPath, m_asset.openAnimationId, assets, "None");
    m_dirty |= AssetCombo("Close Action Clip", EditorAssets::Type::AnimationClip, m_asset.closeAnimationPath, m_asset.closeAnimationId, assets, "None");
    engine::NormalizeInteractionAsset(m_asset);
    ImGui::SeparatorText("Availability Preview");
    ImGui::DragFloat("Debug Distance", &m_debugDistance, 0.05f, 0.0f, 10000.0f, "%.2f m");
    ImGui::SliderFloat("Debug Facing Angle", &m_debugFacingAngle, 0.0f, 180.0f, "%.0f deg");
    ImGui::Checkbox("Debug Has Line of Sight", &m_debugLineOfSight);
    StringField("Debug Access Tag", m_debugAccessTag);
    StringField("Debug Condition Tags", m_debugConditionTags, 512);
    engine::InteractionQuery debugQuery;
    debugQuery.interactorPosition = {0.0f, 0.0f, 0.0f};
    debugQuery.targetPosition = {0.0f, 0.0f, std::max(0.0f, m_debugDistance)};
    const float debugRadians = glm::radians(m_debugFacingAngle);
    debugQuery.interactorForward = {std::sin(debugRadians), 0.0f, std::cos(debugRadians)};
    debugQuery.accessTag = m_debugAccessTag;
    debugQuery.conditionTags = ParseTags(m_debugConditionTags);
    debugQuery.hasLineOfSight = m_debugLineOfSight;
    const auto availability = engine::EvaluateInteraction(m_asset, m_asset.locked, debugQuery);
    ImGui::TextColored(availability.available ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                               : ImVec4(1.0f, 0.45f, 0.3f, 1.0f),
                       "%s: %s", availability.available ? "AVAILABLE" : "BLOCKED",
                       availability.prompt.c_str());
    if (!availability.reason.empty()) ImGui::TextWrapped("Reason: %s", availability.reason.c_str());
    ImGui::SeparatorText("Motion Preview"); DrawPreview(deltaSeconds);
    if (m_dirty) ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "Unsaved changes");
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End(); return result;
}
