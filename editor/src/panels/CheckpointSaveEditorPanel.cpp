#include "CheckpointSaveEditorPanel.h"
#include "EditorPanels.h"

#include <engine/gameplay/SaveGame.h>
#include <engine/gameplay/SaveProfileSystem.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <ctime>
#include <filesystem>

namespace {
std::string SlotSummary(const engine::SaveSlotInfo& slot) {
    if (!slot.exists) return "Empty";
    return slot.displayName + "  |  " + std::to_string(static_cast<int>(slot.playtimeSeconds)) + " s";
}
}

void CheckpointSaveEditorPanel::New(const std::string& root) {
    m_asset = {};
    m_asset.header.id = engine::AssetHandle::Generate();
    m_asset.name = "SaveProfile";
    m_path = (std::filesystem::path(root) / "GameAssets" / "Save" /
              "SaveProfile.3dgsaveprofile").string();
    m_selectedCheckpoint = -1;
    m_dirty = true;
    m_status.clear();
}

bool CheckpointSaveEditorPanel::SaveForShutdown(const std::string& root, std::string* error) {
    if (m_path.empty()) New(root);
    if (!engine::SaveSaveProfileAsset(m_path, m_asset, error)) return false;
    m_dirty = false;
    return true;
}

CheckpointSaveEditorPanel::Result CheckpointSaveEditorPanel::Draw(
    EditorAssets&, const std::string& root, engine::ecs::Registry* runtimeRegistry,
    const std::string& scenePath, float playtimeSeconds, const std::string& selectedName,
    const engine::ecs::Transform* selectedTransform, bool* open) {
    Result result;
    if (!m_asset.header.id.Valid()) New(root);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (engine::LoadSaveProfileAsset(m_pendingOpen, &m_asset, &error)) {
            m_path = m_pendingOpen; m_dirty = false; m_selectedCheckpoint = -1; m_status.clear();
        } else m_status = error;
        m_pendingOpen.clear();
    }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::CheckpointSave), open)) {
        ImGui::End(); return result;
    }
    if (ImGui::Button("New")) New(root);
    ImGui::SameLine();
    if (ImGui::Button("Save Profile")) {
        std::string error;
        if (engine::SaveSaveProfileAsset(m_path, m_asset, &error)) {
            m_dirty = false; result.saved = true; result.message = "Saved save profile: " + m_path;
        } else result.message = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Use in Play")) {
        std::string error;
        if (m_dirty && !engine::SaveSaveProfileAsset(m_path, m_asset, &error)) result.message = error;
        else if (engine::ConfigureSaveProfile(m_path, &error)) {
            m_dirty = false; result.message = "Active save profile: " + m_asset.name;
        } else result.message = error;
    }
    ImGui::TextDisabled("%s", m_path.c_str());

    if (ImGui::BeginTabBar("SaveEditorTabs")) {
        if (ImGui::BeginTabItem("Profile")) {
            m_dirty |= ImGui::InputText("Name##SaveProfileName", &m_asset.name);
            m_dirty |= ImGui::SliderInt("Maximum Slots", &m_asset.maximumSlots, 1, 64);
            m_asset.defaultSlot = std::clamp(m_asset.defaultSlot, 0, std::max(0, m_asset.maximumSlots - 1));
            m_dirty |= ImGui::SliderInt("Default Slot", &m_asset.defaultSlot, 0,
                                       std::max(0, m_asset.maximumSlots - 1));
            m_dirty |= ImGui::Checkbox("Load Latest on Start", &m_asset.loadLatestOnStart);
            m_dirty |= ImGui::Checkbox("Timed Autosave", &m_asset.autosaveEnabled);
            if (m_asset.autosaveEnabled)
                m_dirty |= ImGui::DragFloat("Autosave Interval (s)", &m_asset.autosaveIntervalSeconds, 1.0f, 5.0f, 86400.0f);
            if (ImGui::CollapsingHeader("Persisted State", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& p = m_asset.persistence;
                m_dirty |= ImGui::Checkbox("Transforms", &p.transforms);
                m_dirty |= ImGui::Checkbox("Health", &p.health);
                m_dirty |= ImGui::Checkbox("Velocity", &p.velocity);
                m_dirty |= ImGui::Checkbox("Abilities", &p.abilities);
                m_dirty |= ImGui::Checkbox("Inventory", &p.inventory);
                m_dirty |= ImGui::Checkbox("Quest State", &p.quests);
                m_dirty |= ImGui::Checkbox("Script Values", &p.scriptValues);
                m_dirty |= ImGui::Checkbox("Streamed-Level State", &p.streamedLevels);
            }
            if (ImGui::CollapsingHeader("Respawn Rules", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& r = m_asset.respawn;
                m_dirty |= ImGui::Checkbox("Respawn at Last Checkpoint", &r.restoreAtCheckpoint);
                m_dirty |= ImGui::Checkbox("Restore Health", &r.restoreHealth);
                if (r.restoreHealth) m_dirty |= ImGui::SliderFloat("Health Fraction", &r.healthFraction, 0.01f, 1.0f);
                m_dirty |= ImGui::Checkbox("Clear Movement", &r.clearVelocity);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Checkpoints")) {
            if (ImGui::Button("Add at Selected Object")) {
                if (!selectedTransform) m_status = "Select a scene object to place a checkpoint.";
                else {
                    engine::CheckpointDefinition checkpoint;
                    checkpoint.name = selectedName.empty() ? "Checkpoint" : selectedName;
                    checkpoint.anchorObject = selectedName;
                    checkpoint.position = selectedTransform->position;
                    checkpoint.slot = m_asset.defaultSlot;
                    m_asset.checkpoints.push_back(std::move(checkpoint));
                    m_selectedCheckpoint = static_cast<int>(m_asset.checkpoints.size()) - 1;
                    m_dirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Manual")) {
                m_asset.checkpoints.push_back({});
                m_selectedCheckpoint = static_cast<int>(m_asset.checkpoints.size()) - 1;
                m_dirty = true;
            }
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(m_asset.checkpoints.size()); ++i) {
                ImGui::PushID(i);
                const auto& checkpoint = m_asset.checkpoints[static_cast<std::size_t>(i)];
                if (ImGui::Selectable(checkpoint.name.c_str(), i == m_selectedCheckpoint)) m_selectedCheckpoint = i;
                ImGui::PopID();
            }
            if (m_selectedCheckpoint >= 0 && m_selectedCheckpoint < static_cast<int>(m_asset.checkpoints.size())) {
                ImGui::Separator();
                auto& c = m_asset.checkpoints[static_cast<std::size_t>(m_selectedCheckpoint)];
                ImGui::PushID("CheckpointDetails");
                m_dirty |= ImGui::InputText("Name", &c.name);
                m_dirty |= ImGui::InputText("Anchor Object", &c.anchorObject);
                m_dirty |= ImGui::DragFloat3("Position", &c.position.x, 0.05f);
                m_dirty |= ImGui::DragFloat3("Facing", &c.rotationDegrees.x, 0.5f);
                m_dirty |= ImGui::DragFloat("Activation Radius", &c.activationRadius, 0.05f, 0.1f, 10000.0f);
                m_dirty |= ImGui::SliderInt("Save Slot", &c.slot, 0, std::max(0, m_asset.maximumSlots - 1));
                m_dirty |= ImGui::Checkbox("Enabled", &c.enabled);
                m_dirty |= ImGui::Checkbox("Save on Enter", &c.saveOnEnter);
                m_dirty |= ImGui::Checkbox("One Shot", &c.oneShot);
                if (ImGui::Button("Move to Selected") && selectedTransform) {
                    c.position = selectedTransform->position; c.anchorObject = selectedName; m_dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Checkpoint")) {
                    m_asset.checkpoints.erase(m_asset.checkpoints.begin() + m_selectedCheckpoint);
                    m_selectedCheckpoint = std::min(m_selectedCheckpoint,
                        static_cast<int>(m_asset.checkpoints.size()) - 1); m_dirty = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Slots & Live Debug")) {
            ImGui::Text("Runtime: %s", runtimeRegistry ? "Play session connected" : "Start Play to capture or load world state");
            const auto slots = engine::ListSaveSlots(m_asset.maximumSlots);
            for (const auto& slot : slots) {
                ImGui::PushID(slot.slot);
                ImGui::Text("Slot %d", slot.slot);
                ImGui::SameLine(75.0f); ImGui::TextDisabled("%s", SlotSummary(slot).c_str());
                ImGui::SameLine();
                if (runtimeRegistry && ImGui::SmallButton("Capture")) {
                    auto save = engine::CaptureSaveGame(*runtimeRegistry, scenePath,
                        "Manual Slot " + std::to_string(slot.slot), playtimeSeconds,
                        engine::SavePolicyFromProfile(m_asset));
                    std::string error;
                    if (save.SaveToFile(engine::SaveSlotPath(slot.slot), &error))
                        m_status = "Captured slot " + std::to_string(slot.slot);
                    else m_status = error;
                }
                if (slot.exists) {
                    ImGui::SameLine();
                    if (runtimeRegistry && ImGui::SmallButton("Load")) {
                        engine::SaveGame save; std::string error;
                        if (engine::SaveGame::LoadFromFile(engine::SaveSlotPath(slot.slot), save, &error)) {
                            engine::ApplySaveGame(*runtimeRegistry, save);
                            m_status = "Loaded slot " + std::to_string(slot.slot);
                        } else m_status = error;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete")) {
                        engine::DeleteSaveSlot(slot.slot);
                        m_status = "Deleted slot " + std::to_string(slot.slot);
                    }
                }
                ImGui::PopID();
            }
            if (const auto* active = engine::ActiveSaveProfile()) {
                ImGui::Separator();
                ImGui::Text("Active: %s", active->profile.name.c_str());
                ImGui::Text("Last checkpoint: %s", active->lastCheckpoint.empty() ? "None" : active->lastCheckpoint.c_str());
                ImGui::Text("Last slot: %d", active->lastSlot);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    engine::NormalizeSaveProfile(m_asset);
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    if (m_dirty) ImGui::TextColored({1.0f, 0.72f, 0.15f, 1.0f}, "Unsaved changes");
    ImGui::End();
    return result;
}
