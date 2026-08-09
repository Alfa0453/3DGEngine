#include "LevelValidationPanel.h"

#include "EditorPanels.h"
#include "EditorScene.h"

#include <engine/physics/PhysicsComponents.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace {

bool Finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool Finite(const glm::quat& value) {
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z);
}

const char* SeverityName(LevelValidationPanel::Severity severity) {
    switch (severity) {
    case LevelValidationPanel::Severity::Info: return "Info";
    case LevelValidationPanel::Severity::Warning: return "Warning";
    case LevelValidationPanel::Severity::Error: return "Error";
    }
    return "Issue";
}

ImVec4 SeverityColor(LevelValidationPanel::Severity severity) {
    switch (severity) {
    case LevelValidationPanel::Severity::Info: return ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
    case LevelValidationPanel::Severity::Warning: return ImVec4(1.0f, 0.72f, 0.2f, 1.0f);
    case LevelValidationPanel::Severity::Error: return ImVec4(1.0f, 0.3f, 0.25f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

} // namespace

bool LevelValidationPanel::AssetExists(const std::string& path,
                                       const std::string& assetRoot) const {
    if (path.empty()) return true;
    if (path.rfind("/Game/", 0) == 0 || path.rfind("Game/", 0) == 0)
        return true; // Virtual engine path; the asset registry validates it at load time.
    std::error_code ec;
    const std::filesystem::path input(path);
    if (input.is_absolute()) return std::filesystem::is_regular_file(input, ec);
    const std::filesystem::path root(assetRoot);
    if (std::filesystem::is_regular_file(root / input, ec)) return true;
    ec.clear();
    return std::filesystem::is_regular_file(root.parent_path() / input, ec);
}

void LevelValidationPanel::Scan(const EditorScene& scene, const std::string& assetRoot) {
    m_issues.clear();
    m_assetRoot = assetRoot;
    m_lastObjectCount = static_cast<int>(scene.Objects().size());
    std::unordered_map<std::string, int> firstName;
    std::unordered_set<std::string> names;
    int playerControllers = 0;
    int navAgents = 0;
    int navVolumes = 0;
    int directionalLights = 0;

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.name.empty()) names.insert(object.name);
        if (object.playerControllerEnabled) ++playerControllers;
        if (object.navAgentEnabled) ++navAgents;
        if (object.navMeshBoundsVolume) ++navVolumes;
        if (object.light && object.lightData.type == engine::ecs::Light::Type::Directional)
            ++directionalLights;
    }

    auto add = [&](Severity severity, Kind kind, int index, const std::string& message,
                   bool fixable = false, bool safeFix = false) {
        Issue issue;
        issue.severity = severity;
        issue.kind = kind;
        issue.objectIndex = index;
        issue.objectName = index >= 0 && index < static_cast<int>(scene.Objects().size())
            ? scene.Objects()[static_cast<std::size_t>(index)].name : "Scene";
        issue.message = message;
        issue.fixable = fixable;
        issue.safeFix = safeFix;
        m_issues.push_back(std::move(issue));
    };

    for (int i = 0; i < static_cast<int>(scene.Objects().size()); ++i) {
        const EditorScene::Object& object = scene.Objects()[static_cast<std::size_t>(i)];
        if (object.name.empty()) {
            add(Severity::Error, Kind::EmptyName, i, "Object has no name.", true, true);
        } else {
            const auto [it, inserted] = firstName.emplace(object.name, i);
            if (!inserted) add(Severity::Error, Kind::DuplicateName, i,
                "Duplicate object name; name-based gameplay references are ambiguous.", true, true);
        }

        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform || !Finite(transform->position) || !Finite(transform->scale)
            || !Finite(transform->rotation)) {
            add(Severity::Error, Kind::InvalidTransform, i,
                "Transform contains NaN/infinite values or is missing.", transform != nullptr, true);
        } else if (std::abs(transform->scale.x) < 0.0001f
            || std::abs(transform->scale.y) < 0.0001f
            || std::abs(transform->scale.z) < 0.0001f) {
            add(Severity::Warning, Kind::TinyScale, i,
                "One or more scale axes are effectively zero.", true, true);
        }

        if (!object.modelAssetPath.empty() && !AssetExists(object.modelAssetPath, assetRoot))
            add(Severity::Error, Kind::MissingModel, i,
                "Model asset is missing: " + object.modelAssetPath, true, false);
        if (!object.materialAssetPath.empty() && !AssetExists(object.materialAssetPath, assetRoot))
            add(Severity::Error, Kind::MissingMaterial, i,
                "Material asset is missing: " + object.materialAssetPath, true, false);
        if (object.scriptEnabled && !object.scriptPath.empty()
            && !AssetExists(object.scriptPath, assetRoot))
            add(Severity::Error, Kind::MissingScript, i,
                "Script source is missing: " + object.scriptPath, true, false);
        if (object.audioSourceEnabled && !object.audioAssetPath.empty()
            && !AssetExists(object.audioAssetPath, assetRoot))
            add(Severity::Error, Kind::MissingAudio, i,
                "Audio asset is missing: " + object.audioAssetPath);
        if (object.particleSystemEnabled && !object.particleAssetPath.empty()
            && !AssetExists(object.particleAssetPath, assetRoot))
            add(Severity::Error, Kind::MissingParticle, i,
                "Particle asset is missing: " + object.particleAssetPath);
        if (object.navAgentEnabled && !object.navAgentBrainAsset.empty()
            && !AssetExists(object.navAgentBrainAsset, assetRoot))
            add(Severity::Error, Kind::MissingBehaviorTree, i,
                "Behavior tree is missing: " + object.navAgentBrainAsset, true, false);
        if (!object.triggerTargetName.empty() && names.count(object.triggerTargetName) == 0)
            add(Severity::Warning, Kind::MissingTarget, i,
                "Trigger target does not exist: " + object.triggerTargetName);
        if (object.navAgentEnabled && !object.navAgentTargetName.empty()
            && names.count(object.navAgentTargetName) == 0)
            add(Severity::Warning, Kind::MissingTarget, i,
                "AI chase target does not exist: " + object.navAgentTargetName);

        if (object.colliderEnabled) {
            const engine::ecs::Collider& c = object.collider;
            bool invalid = !std::isfinite(c.radius) || c.radius <= 0.0f
                || !Finite(c.halfExtents) || glm::any(glm::lessThanEqual(c.halfExtents, glm::vec3(0.0f)))
                || !std::isfinite(c.halfHeight) || c.halfHeight < 0.0f
                || !std::isfinite(c.restitution) || !std::isfinite(c.friction);
            if (c.shape == engine::ecs::ColliderShape::Plane
                && glm::dot(c.planeNormal, c.planeNormal) < 1.0e-8f) invalid = true;
            if (c.shape == engine::ecs::ColliderShape::Torus
                && (c.majorRadius <= 0.0f || c.minorRadius <= 0.0f)) invalid = true;
            if (c.shape == engine::ecs::ColliderShape::Staircase && c.steps < 1) invalid = true;
            if (invalid) add(Severity::Error, Kind::InvalidCollider, i,
                "Collider contains invalid dimensions or material values.", true, true);
        }

        const bool unusedEmpty = object.primitive == EditorScene::Primitive::Empty
            && !object.light && !object.colliderEnabled && !object.rigidBodyEnabled
            && !object.scriptEnabled && object.additionalScripts.empty()
            && !object.audioSourceEnabled && !object.particleSystemEnabled
            && !object.navAgentEnabled && !object.playerControllerEnabled
            && !object.moverEnabled && !object.rotatorEnabled && !object.healthEnabled
            && !object.cameraZoneEnabled && !object.isSpline && !object.isTerrain;
        if (unusedEmpty) add(Severity::Info, Kind::OrphanEmpty, i,
            "Empty object has no authored components; verify that it is still needed.");
    }

    if (playerControllers == 0)
        add(Severity::Warning, Kind::SceneConfiguration, -1,
            "Scene has no enabled Player Controller.");
    else if (playerControllers > 1)
        add(Severity::Warning, Kind::SceneConfiguration, -1,
            "Scene has multiple enabled Player Controllers.");
    if (navAgents > 0 && navVolumes == 0)
        add(Severity::Warning, Kind::SceneConfiguration, -1,
            "Navigation agents exist but the scene has no Nav Mesh Bounds Volume.");
    if (directionalLights > 1)
        add(Severity::Info, Kind::SceneConfiguration, -1,
            "Scene contains multiple directional lights; verify intentional sun lighting.");

    std::stable_sort(m_issues.begin(), m_issues.end(), [](const Issue& a, const Issue& b) {
        return static_cast<int>(a.severity) > static_cast<int>(b.severity);
    });
    m_selectedIssue = -1;
}

bool LevelValidationPanel::FixIssue(EditorScene& scene, const Issue& issue) {
    if (!issue.fixable || issue.objectIndex < 0
        || issue.objectIndex >= static_cast<int>(scene.Objects().size())) return false;
    scene.SelectIndex(issue.objectIndex);
    const EditorScene::Object* object = scene.SelectedObject();
    if (!object || object->locked) return false;
    switch (issue.kind) {
    case Kind::EmptyName:
        return scene.SetSelectedName("Object_" + std::to_string(issue.objectIndex + 1));
    case Kind::DuplicateName: {
        const std::string base = object->name.empty() ? "Object" : object->name;
        std::string unique = base;
        int suffix = 2;
        auto exists = [&](const std::string& candidate) {
            for (int i = 0; i < static_cast<int>(scene.Objects().size()); ++i)
                if (i != issue.objectIndex
                    && scene.Objects()[static_cast<std::size_t>(i)].name == candidate) return true;
            return false;
        };
        while (exists(unique)) unique = base + "_" + std::to_string(suffix++);
        return scene.SetSelectedName(unique);
    }
    case Kind::InvalidTransform: {
        const engine::ecs::Transform* current = scene.SelectedTransform();
        if (!current) return false;
        engine::ecs::Transform fixed = *current;
        if (!Finite(fixed.position)) fixed.position = glm::vec3(0.0f);
        if (!Finite(fixed.scale)) fixed.scale = glm::vec3(1.0f);
        if (!Finite(fixed.rotation) || glm::dot(fixed.rotation, fixed.rotation) < 1.0e-8f)
            fixed.rotation = glm::quat(1, 0, 0, 0);
        else fixed.rotation = glm::normalize(fixed.rotation);
        return scene.SetSelectedTransform(fixed);
    }
    case Kind::TinyScale: {
        const engine::ecs::Transform* current = scene.SelectedTransform();
        if (!current) return false;
        engine::ecs::Transform fixed = *current;
        for (int axis = 0; axis < 3; ++axis)
            if (std::abs(fixed.scale[axis]) < 0.0001f)
                fixed.scale[axis] = fixed.scale[axis] < 0.0f ? -0.001f : 0.001f;
        return scene.SetSelectedTransform(fixed);
    }
    case Kind::MissingModel: return scene.SetSelectedModelAsset("");
    case Kind::MissingMaterial: return scene.SetSelectedMaterialAsset("");
    case Kind::MissingScript:
        return scene.SetSelectedScript(object->scriptClassName, "", false);
    case Kind::MissingBehaviorTree: return scene.SetSelectedNavAgentBrain("");
    case Kind::InvalidCollider: {
        engine::ecs::Collider collider = object->collider;
        collider.radius = std::isfinite(collider.radius) ? std::max(collider.radius, 0.001f) : 0.5f;
        for (int axis = 0; axis < 3; ++axis)
            collider.halfExtents[axis] = std::isfinite(collider.halfExtents[axis])
                ? std::max(collider.halfExtents[axis], 0.001f) : 0.5f;
        collider.halfHeight = std::isfinite(collider.halfHeight)
            ? std::max(collider.halfHeight, 0.0f) : 0.5f;
        collider.majorRadius = std::isfinite(collider.majorRadius)
            ? std::max(collider.majorRadius, 0.001f) : 0.35f;
        collider.minorRadius = std::isfinite(collider.minorRadius)
            ? std::max(collider.minorRadius, 0.001f) : 0.15f;
        collider.steps = std::max(collider.steps, 1);
        collider.restitution = std::isfinite(collider.restitution)
            ? std::clamp(collider.restitution, 0.0f, 1.0f) : 0.4f;
        collider.friction = std::isfinite(collider.friction)
            ? std::max(collider.friction, 0.0f) : 0.5f;
        if (glm::dot(collider.planeNormal, collider.planeNormal) < 1.0e-8f)
            collider.planeNormal = glm::vec3(0, 1, 0);
        else collider.planeNormal = glm::normalize(collider.planeNormal);
        return scene.SetSelectedCollider(collider);
    }
    default: return false;
    }
}

int LevelValidationPanel::FixAllSafe(EditorScene& scene) {
    int fixed = 0;
    bool first = true;
    for (const Issue& issue : m_issues) {
        if (!issue.safeFix || !issue.fixable) continue;
        scene.SuppressUndo(!first);
        if (FixIssue(scene, issue)) {
            ++fixed;
            first = false;
        }
    }
    scene.SuppressUndo(false);
    return fixed;
}

void LevelValidationPanel::Draw(EditorScene& scene, const std::string& assetRoot,
                                bool* open) {
    if (m_assetRoot != assetRoot
        || m_lastObjectCount != static_cast<int>(scene.Objects().size())) Scan(scene, assetRoot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::LevelValidation), open)) { ImGui::End(); return; }

    if (ImGui::Button("Scan Level")) Scan(scene, assetRoot);
    ImGui::SameLine();
    if (ImGui::Button("Fix All Safe")) {
        FixAllSafe(scene);
        Scan(scene, assetRoot);
    }
    int errors = 0, warnings = 0, info = 0;
    for (const Issue& issue : m_issues) {
        if (issue.severity == Severity::Error) ++errors;
        else if (issue.severity == Severity::Warning) ++warnings;
        else ++info;
    }
    ImGui::TextColored(errors ? SeverityColor(Severity::Error) : ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
                       "%d errors", errors);
    ImGui::SameLine();
    ImGui::TextColored(SeverityColor(Severity::Warning), "%d warnings", warnings);
    ImGui::SameLine();
    ImGui::TextColored(SeverityColor(Severity::Info), "%d info", info);
    ImGui::Checkbox("Errors", &m_showErrors); ImGui::SameLine();
    ImGui::Checkbox("Warnings", &m_showWarnings); ImGui::SameLine();
    ImGui::Checkbox("Info", &m_showInfo);

    if (ImGui::BeginChild("##ValidationIssues", ImVec2(0, -110.0f), true)) {
        for (int i = 0; i < static_cast<int>(m_issues.size()); ++i) {
            const Issue& issue = m_issues[static_cast<std::size_t>(i)];
            if ((issue.severity == Severity::Error && !m_showErrors)
                || (issue.severity == Severity::Warning && !m_showWarnings)
                || (issue.severity == Severity::Info && !m_showInfo)) continue;
            ImGui::PushID(i);
            ImGui::TextColored(SeverityColor(issue.severity), "[%s]", SeverityName(issue.severity));
            ImGui::SameLine();
            const std::string label = issue.objectName + ": " + issue.message;
            if (ImGui::Selectable(label.c_str(), m_selectedIssue == i)) m_selectedIssue = i;
            ImGui::PopID();
        }
        if (m_issues.empty())
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.5f, 1.0f), "No validation issues found.");
    }
    ImGui::EndChild();

    if (m_selectedIssue >= 0 && m_selectedIssue < static_cast<int>(m_issues.size())) {
        const Issue issue = m_issues[static_cast<std::size_t>(m_selectedIssue)];
        ImGui::TextWrapped("%s", issue.message.c_str());
        if (issue.objectIndex >= 0) {
            if (ImGui::Button("Select Object")) scene.SelectIndex(issue.objectIndex);
            if (issue.fixable) ImGui::SameLine();
        }
        if (issue.fixable && ImGui::Button(issue.safeFix ? "Apply Safe Fix" : "Clear Broken Reference")) {
            FixIssue(scene, issue);
            Scan(scene, assetRoot);
        }
    } else {
        ImGui::TextDisabled("Select an issue to inspect it.");
    }
    ImGui::TextDisabled("Safe cleanup never deletes objects or clears asset references.");
    ImGui::End();
}
